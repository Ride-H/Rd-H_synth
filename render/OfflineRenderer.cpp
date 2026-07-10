/*
  ==============================================================================
    OfflineRenderer.cpp
    Rd-H Synth — headless preset → WAV renderer.

    Purpose: render a .rdhs preset to a WAV file with no GUI/host, so the DSP
    can be measured objectively (THD, noise spectral slope, regression and CI
    smoke checks). Reuses the real RdhSynthAudioProcessor signal path.

    Usage:
        RdhRender <preset.rdhs> <out.wav> [--note N] [--secs S] [--sr SR] [--vel V]
                  [--preload <preset>] [--dump-params]

    Defaults: note=60 (C4 / MIDI 60), secs=3.0, sr=44100, vel=100.
    The note is held for 60% of the duration, then released, so the tail
    (release stage of every active envelope) is captured.

    --preload <preset> : load this preset first, then the main one — reproduces a
                         running plugin loading presets in sequence (state-
                         pollution check; missing IDs must resolve to defaults).
    --dump-params      : after loading, print "PARAM <id>=<value>" for the
                         compatibility-relevant IDs and exit (no render / WAV).
  ==============================================================================
*/

#include <JuceHeader.h>
#include "PluginProcessor.h"
#include <chrono>

namespace
{
    int getIntArg (const juce::ArgumentList& args, juce::StringRef flag, int fallback)
    {
        const int i = args.indexOfOption (flag);
        if (i < 0 || i + 1 >= args.size()) return fallback;
        return args[i + 1].text.getIntValue();
    }

    double getDoubleArg (const juce::ArgumentList& args, juce::StringRef flag, double fallback)
    {
        const int i = args.indexOfOption (flag);
        if (i < 0 || i + 1 >= args.size()) return fallback;
        return args[i + 1].text.getDoubleValue();
    }

    // Load a plain-XML .rdhs/.mss2 into the processor's APVTS, faithfully
    // mirroring PresetManager::loadPresetDialog / setStateInformation:
    //   replaceState(tree)  +  (no rdh_version → force FM/Noise OFF, REQ-COMPAT-002).
    // New Phase-2 IDs absent from the tree resolve to whatever the parameter
    // currently holds (JUCE replaceState semantics) — fresh proc = defaults.
    bool loadPreset (RdhSynthAudioProcessor& proc, const juce::File& file)
    {
        std::unique_ptr<juce::XmlElement> xml (juce::XmlDocument::parse (file));
        if (xml == nullptr || ! xml->hasTagName (proc.apvts.state.getType()))
            return false;

        auto tree = juce::ValueTree::fromXml (*xml);
        const bool hasVersionTag = tree.hasProperty ("rdh_version");
        proc.apvts.replaceState (tree);

        if (! hasVersionTag)   // v1.x: FM/Noise forced off (HC-1 / REQ-COMPAT-002)
        {
            for (const char* id : { "fm_enable", "noise_enable" })
                if (auto* p = proc.apvts.getParameter (id))
                    p->setValueNotifyingHost (0.0f);
        }
        return true;
    }

    void dumpParams (RdhSynthAudioProcessor& proc)
    {
        // Compatibility-relevant IDs: Phase-2 additions + the v1.x-migration toggles.
        for (const char* id : { "fm_enable", "fm_algorithm", "fm_feedback",
                                "fm_op3_level", "fm_op4_level",
                                "noise_enable", "noise_type",
                                "waveType", "attack" })
            std::cout << "PARAM " << id << "="
                      << *proc.apvts.getRawParameterValue (id) << "\n";
    }
}

int main (int argc, char* argv[])
{
    juce::ScopedJuceInitialiser_GUI juceInit; // message manager for AudioProcessor

    const juce::ArgumentList args (argc, argv);
    if (args.size() < 2)
    {
        std::cerr << "Usage: RdhRender <preset.rdhs> <out.wav> "
                     "[--note N] [--secs S] [--sr SR] [--vel V]\n";
        return 1;
    }

    const juce::File presetFile (args[0].resolveAsExistingFile());
    const juce::File outFile    (args[1].resolveAsFile());

    const int    note  = juce::jlimit (0, 127, getIntArg    (args, "--note", 60));
    const int    vel   = juce::jlimit (1, 127, getIntArg    (args, "--vel",  100));
    const double secs  = juce::jmax   (0.1,    getDoubleArg (args, "--secs", 3.0));
    const double sr    = juce::jmax   (8000.0, getDoubleArg (args, "--sr",   44100.0));
    const int    block = juce::jlimit (16, 8192, getIntArg  (args, "--block", 512));
    const int    poly  = juce::jlimit (1, 16,    getIntArg  (args, "--poly",  1)); // polyphonic chord
    const bool   bench = args.containsOption ("--bench");                          // CPU benchmark timing

    if (! presetFile.existsAsFile())
    {
        std::cerr << "Preset not found: " << presetFile.getFullPathName() << "\n";
        return 1;
    }

    RdhSynthAudioProcessor proc;

    // prepareToPlay first so the sample rate is set before the preset load
    // fires parameter updates (ADSR::setParameters asserts sampleRate > 0).
    proc.setPlayConfigDetails (0, 2, sr, block);
    proc.prepareToPlay (sr, block);

    // --preload: load a preset first (running-plugin sequence, compatibility check).
    const int   plIdx    = args.indexOfOption ("--preload");
    if (plIdx >= 0 && plIdx + 1 < args.size())
    {
        const juce::File pre (args[plIdx + 1].resolveAsExistingFile());
        if (! loadPreset (proc, pre))
        {
            std::cerr << "Failed to parse --preload preset: "
                      << pre.getFullPathName() << "\n";
            return 1;
        }
    }

    if (! loadPreset (proc, presetFile))
    {
        std::cerr << "Failed to parse preset (root tag must be '"
                  << proc.apvts.state.getType().toString() << "'): "
                  << presetFile.getFullPathName() << "\n";
        return 1;
    }

    if (args.containsOption ("--dump-params"))
    {
        dumpParams (proc);
        proc.releaseResources();
        return 0;
    }

    const int totalSamples   = (int) (secs * sr);
    const int noteOffSample  = (int) (totalSamples * 0.6);

    juce::AudioBuffer<float> out (2, totalSamples);
    out.clear();

    // Chord of `poly` distinct notes (spread by 3 semitones), all noteOn at
    // sample 0, all noteOff at 60% — exercises polyphony for the CPU benchmark.
    auto chordNote = [note] (int i) { return juce::jlimit (0, 127, note + i * 3); };

    int  pos = 0;
    bool noteOnSent = false, noteOffSent = false;

    const auto t0 = std::chrono::steady_clock::now();
    while (pos < totalSamples)
    {
        const int n = juce::jmin (block, totalSamples - pos);
        juce::AudioBuffer<float> buf (2, n);
        buf.clear();

        juce::MidiBuffer midi;
        if (! noteOnSent)
        {
            for (int i = 0; i < poly; ++i)
                midi.addEvent (juce::MidiMessage::noteOn (1, chordNote (i), (juce::uint8) vel), 0);
            noteOnSent = true;
        }
        if (! noteOffSent && pos + n > noteOffSample)
        {
            const int off = juce::jlimit (0, n - 1, noteOffSample - pos);
            for (int i = 0; i < poly; ++i)
                midi.addEvent (juce::MidiMessage::noteOff (1, chordNote (i)), off);
            noteOffSent = true;
        }

        proc.processBlock (buf, midi);

        for (int ch = 0; ch < 2; ++ch)
            out.copyFrom (ch, pos, buf, ch, 0, n);

        pos += n;
    }
    const auto t1 = std::chrono::steady_clock::now();

    proc.releaseResources();

    if (bench)
    {
        const double renderS = std::chrono::duration<double> (t1 - t0).count();
        const double rt      = renderS / secs;                 // <1 = faster than realtime
        // BENCH: realtime factor and equiv. single-core CPU% for this voice count.
        std::cout << "BENCH voices=" << poly
                  << " sr=" << (int) sr << " block=" << block
                  << " audio_s=" << secs
                  << " render_s=" << renderS
                  << " rt_factor=" << rt
                  << " cpu1core_pct=" << (rt * 100.0) << "\n";
        return 0;   // skip WAV write in bench mode
    }

    // --- Write 24-bit WAV ---
    outFile.deleteFile();
    if (const auto r = outFile.getParentDirectory().createDirectory(); r.failed())
    {
        std::cerr << "Cannot create output directory: " << r.getErrorMessage() << "\n";
        return 1;
    }

    juce::WavAudioFormat wav;
    std::unique_ptr<juce::OutputStream> stream (outFile.createOutputStream());
    if (stream == nullptr)
    {
        std::cerr << "Cannot open output file: " << outFile.getFullPathName() << "\n";
        return 1;
    }

    const auto wavOpts = juce::AudioFormatWriterOptions{}
                             .withSampleRate    (sr)
                             .withNumChannels   (2)
                             .withBitsPerSample (24);

    std::unique_ptr<juce::AudioFormatWriter> writer (wav.createWriterFor (stream, wavOpts));
    if (writer == nullptr)
    {
        std::cerr << "Cannot create WAV writer.\n";
        return 1;
    }
    writer->writeFromAudioSampleBuffer (out, 0, out.getNumSamples());
    writer.reset();   // flush

    std::cout << "Rendered " << presetFile.getFileName()
              << " -> " << outFile.getFullPathName()
              << "  (note " << note << ", " << secs << "s @ " << (int) sr << "Hz)\n";
    return 0;
}
