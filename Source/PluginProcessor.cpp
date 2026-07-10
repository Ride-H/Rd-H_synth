/*
  ==============================================================================
    PluginProcessor.cpp
    Rd-H Synth v2.0

    Fixes applied:
      C-3: voiceCache built in constructor — processBlock uses raw ptr loop,
           no dynamic_cast on the audio thread.
      Q-2: volumeSmoothed reset in prepareToPlay; applyGain replaced with
           LinearSmoothedValue to prevent automation clicks.
      Spectrum FIFO push batched (was sample-by-sample).
  ==============================================================================
*/

#include "PluginProcessor.h"
#include "PluginEditor.h"

//==============================================================================
RdhSynthAudioProcessor::RdhSynthAudioProcessor()
    : AudioProcessor (BusesProperties()
                     #if ! JucePlugin_IsMidiEffect
                      #if ! JucePlugin_IsSynth
                       .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                      #endif
                       .withOutput ("Output", juce::AudioChannelSet::stereo(), true)
                     #endif
                       ),
      apvts (*this, nullptr, "Parameters", createParameterLayout())
{
    synth.addSound (new RdhSynthSound());

    voiceCache.reserve (NUM_VOICES);
    for (int i = 0; i < NUM_VOICES; ++i)
    {
        auto* v = new RdhSynthVoice();
        synth.addVoice (v);
        voiceCache.push_back (v); // C-3: cache raw pointer; synth owns lifetime
    }

    resolveParamPtrs(); // one-time string-ID resolution (never on audio thread)
}

//==============================================================================
void RdhSynthAudioProcessor::resolveParamPtrs()
{
    auto p = [this] (const juce::String& id)
    {
        auto* raw = apvts.getRawParameterValue (id);
        jassert (raw != nullptr); // every ID below must exist in createParameterLayout()
        return raw;
    };

    pp.waveType        = p ("waveType");
    pp.waveType2       = p ("waveType2");
    pp.osc2Enable      = p ("osc2Enable");
    pp.waveType3       = p ("waveType3");
    pp.osc3Enable      = p ("osc3Enable");
    pp.attack          = p ("attack");
    pp.decay           = p ("decay");
    pp.sustain         = p ("sustain");
    pp.release         = p ("release");
    pp.filterCutoff    = p ("filterCutoff");
    pp.filterResonance = p ("filterResonance");
    pp.unisonVoices    = p ("unisonVoices");
    pp.unisonDetune    = p ("unisonDetune");
    pp.pitchEnvAmount  = p ("pitchEnvAmount");
    pp.pitchEnvAttack  = p ("pitchEnvAttack");
    pp.volume          = p ("volume");

    pp.fmEnable        = p ("fm_enable");
    pp.fmOutputLevel   = p ("fm_output_level");
    pp.fmAlgorithm     = p ("fm_algorithm");
    pp.fmFeedback      = p ("fm_feedback");

    static const char* opFields[9] = { "ratio", "detune", "level", "attack", "decay",
                                       "sustain", "release", "vel_sens", "key_scale" };
    for (int o = 0; o < 4; ++o)
        for (int f = 0; f < 9; ++f)
            pp.fmOp[o][f] = p ("fm_op" + juce::String (o + 1) + "_" + opFields[f]);

    pp.noiseEnable     = p ("noise_enable");
    pp.noiseType       = p ("noise_type");
    pp.noiseLevel      = p ("noise_level");
    pp.noiseFilterSend = p ("noise_filter_send");
    pp.noiseDirectOut  = p ("noise_direct_out");
    pp.noiseEgAttack   = p ("noise_eg_attack");
    pp.noiseEgDecay    = p ("noise_eg_decay");
    pp.noiseEgSustain  = p ("noise_eg_sustain");
    pp.noiseEgRelease  = p ("noise_eg_release");

    // Modulation system
    pp.filterEnvAttack  = p ("filter_env_attack");
    pp.filterEnvDecay   = p ("filter_env_decay");
    pp.filterEnvSustain = p ("filter_env_sustain");
    pp.filterEnvRelease = p ("filter_env_release");
    pp.filterEnvAmount  = p ("filter_env_amount");

    static const char* lfoFields[6] = { "rate", "depth", "wave", "phase", "sync", "retrig" };
    for (int i = 0; i < 2; ++i)
    {
        for (int f = 0; f < 6; ++f)
            pp.lfoP[i][f] = p ("lfo" + juce::String (i + 1) + "_" + lfoFields[f]);
        lfoRateParam[i] = apvts.getParameter ("lfo" + juce::String (i + 1) + "_rate");
        jassert (lfoRateParam[i] != nullptr);
    }

    static const char* slotFields[5] = { "source", "dest", "amount", "curve", "enable" };
    for (int s = 0; s < rdh::synth::ModMatrix::NUM_SLOTS; ++s)
        for (int f = 0; f < 5; ++f)
            pp.modSlot[s][f] = p ("mod_slot" + juce::String (s) + "_" + slotFields[f]);
}

RdhSynthAudioProcessor::~RdhSynthAudioProcessor() {}

//==============================================================================
juce::AudioProcessorValueTreeState::ParameterLayout
RdhSynthAudioProcessor::createParameterLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;

    // ======== Existing parameters (IDs 0–999, backwards-compatible) ========

    // Oscillators
    layout.add (std::make_unique<juce::AudioParameterChoice> (
        "waveType",  "Wave Type",
        juce::StringArray { "Sine", "Saw", "Square", "Triangle" }, 1));
    layout.add (std::make_unique<juce::AudioParameterChoice> (
        "waveType2", "Wave Type 2",
        juce::StringArray { "Sine", "Saw", "Square", "Triangle" }, 1));
    layout.add (std::make_unique<juce::AudioParameterBool>   ("osc2Enable", "OSC 2 Enable", false));
    layout.add (std::make_unique<juce::AudioParameterChoice> (
        "waveType3", "Wave Type 3",
        juce::StringArray { "Sine", "Saw", "Square", "Triangle" }, 2));
    layout.add (std::make_unique<juce::AudioParameterBool>   ("osc3Enable", "OSC 3 Enable", false));

    // Amp ADSR
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        "attack",  "Attack",  juce::NormalisableRange<float> (0.001f,  3.0f, 0.001f, 0.4f), 0.005f));
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        "decay",   "Decay",   juce::NormalisableRange<float> (0.001f,  3.0f, 0.001f, 0.4f), 0.2f));
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        "sustain", "Sustain", juce::NormalisableRange<float> (0.0f,    1.0f, 0.001f),        0.6f));
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        "release", "Release", juce::NormalisableRange<float> (0.001f,  5.0f, 0.001f, 0.4f), 0.3f));

    // Filter
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        "filterCutoff",    "Filter Cutoff",
        juce::NormalisableRange<float> (20.0f, 20000.0f, 1.0f, 0.25f), 8000.0f));
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        "filterResonance", "Filter Resonance",
        juce::NormalisableRange<float> (0.1f, 10.0f, 0.01f, 0.5f),    1.0f));

    // Unison
    layout.add (std::make_unique<juce::AudioParameterInt> (
        "unisonVoices", "Unison Voices", 1, RdhSynthVoice::MAX_UNISON_VOICES, 1));
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        "unisonDetune", "Unison Detune",
        juce::NormalisableRange<float> (0.0f, 50.0f, 0.1f), 0.0f));

    // Pitch Envelope
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        "pitchEnvAmount", "Pitch Env Amount",
        juce::NormalisableRange<float> (-24.0f, 24.0f, 0.1f), 0.0f));
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        "pitchEnvAttack", "Pitch Env Attack",
        juce::NormalisableRange<float> (0.001f, 2.0f, 0.001f, 0.4f), 0.01f));

    // Master volume
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        "volume", "Volume",
        juce::NormalisableRange<float> (0.0f, 1.0f, 0.001f), 0.7f));

    // ======== FM Section (IDs 1000–1999) ========

    layout.add (std::make_unique<juce::AudioParameterBool>  ("fm_enable",       "FM Enable",       false));
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        "fm_output_level", "FM Output Level",
        juce::NormalisableRange<float> (0.0f, 1.0f, 0.001f), 0.5f));
    // Algorithm 0–3 + OP4 feedback. Defaults reproduce the original 2-OP behaviour.
    layout.add (std::make_unique<juce::AudioParameterInt>   ("fm_algorithm", "FM Algorithm", 0, 3, 0));
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        "fm_feedback",     "FM Feedback",
        juce::NormalisableRange<float> (0.0f, 1.0f, 0.001f), 0.0f));

    // OP1 (carrier)
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        "fm_op1_ratio",    "OP1 Ratio",
        juce::NormalisableRange<float> (0.5f, 32.0f, 0.01f, 0.4f), 1.0f));
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        "fm_op1_detune",   "OP1 Detune",
        juce::NormalisableRange<float> (-100.0f, 100.0f, 0.1f),    0.0f));
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        "fm_op1_level",    "OP1 Level",
        juce::NormalisableRange<float> (0.0f, 1.0f, 0.001f),       0.7f));
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        "fm_op1_attack",   "OP1 Attack",
        juce::NormalisableRange<float> (0.001f, 10.0f, 0.001f, 0.4f), 0.001f));
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        "fm_op1_decay",    "OP1 Decay",
        juce::NormalisableRange<float> (0.001f, 10.0f, 0.001f, 0.4f), 0.2f));
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        "fm_op1_sustain",  "OP1 Sustain",
        juce::NormalisableRange<float> (0.0f, 1.0f, 0.001f),       0.7f));
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        "fm_op1_release",  "OP1 Release",
        juce::NormalisableRange<float> (0.001f, 10.0f, 0.001f, 0.4f), 0.3f));
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        "fm_op1_vel_sens", "OP1 Vel Sens",
        juce::NormalisableRange<float> (0.0f, 1.0f, 0.001f),       0.5f));
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        "fm_op1_key_scale", "OP1 Key Scale",
        juce::NormalisableRange<float> (0.0f, 1.0f, 0.001f),       0.0f));

    // OP2 (modulator)
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        "fm_op2_ratio",    "OP2 Ratio",
        juce::NormalisableRange<float> (0.5f, 32.0f, 0.01f, 0.4f), 2.0f));
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        "fm_op2_detune",   "OP2 Detune",
        juce::NormalisableRange<float> (-100.0f, 100.0f, 0.1f),    0.0f));
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        "fm_op2_level",    "OP2 Level",
        juce::NormalisableRange<float> (0.0f, 1.0f, 0.001f),       0.5f));
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        "fm_op2_attack",   "OP2 Attack",
        juce::NormalisableRange<float> (0.001f, 10.0f, 0.001f, 0.4f), 0.001f));
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        "fm_op2_decay",    "OP2 Decay",
        juce::NormalisableRange<float> (0.001f, 10.0f, 0.001f, 0.4f), 0.3f));
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        "fm_op2_sustain",  "OP2 Sustain",
        juce::NormalisableRange<float> (0.0f, 1.0f, 0.001f),       0.5f));
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        "fm_op2_release",  "OP2 Release",
        juce::NormalisableRange<float> (0.001f, 10.0f, 0.001f, 0.4f), 0.4f));
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        "fm_op2_vel_sens", "OP2 Vel Sens",
        juce::NormalisableRange<float> (0.0f, 1.0f, 0.001f),       0.5f));
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        "fm_op2_key_scale", "OP2 Key Scale",
        juce::NormalisableRange<float> (0.0f, 1.0f, 0.001f),       0.0f));

    // OP3 — default level 0 so existing 2-OP presets are unchanged
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        "fm_op3_ratio",    "OP3 Ratio",
        juce::NormalisableRange<float> (0.5f, 32.0f, 0.01f, 0.4f), 1.0f));
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        "fm_op3_detune",   "OP3 Detune",
        juce::NormalisableRange<float> (-100.0f, 100.0f, 0.1f),    0.0f));
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        "fm_op3_level",    "OP3 Level",
        juce::NormalisableRange<float> (0.0f, 1.0f, 0.001f),       0.0f));
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        "fm_op3_attack",   "OP3 Attack",
        juce::NormalisableRange<float> (0.001f, 10.0f, 0.001f, 0.4f), 0.001f));
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        "fm_op3_decay",    "OP3 Decay",
        juce::NormalisableRange<float> (0.001f, 10.0f, 0.001f, 0.4f), 0.2f));
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        "fm_op3_sustain",  "OP3 Sustain",
        juce::NormalisableRange<float> (0.0f, 1.0f, 0.001f),       0.7f));
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        "fm_op3_release",  "OP3 Release",
        juce::NormalisableRange<float> (0.001f, 10.0f, 0.001f, 0.4f), 0.3f));
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        "fm_op3_vel_sens", "OP3 Vel Sens",
        juce::NormalisableRange<float> (0.0f, 1.0f, 0.001f),       0.5f));
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        "fm_op3_key_scale", "OP3 Key Scale",
        juce::NormalisableRange<float> (0.0f, 1.0f, 0.001f),       0.0f));

    // OP4 — carries the feedback loop; default level 0
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        "fm_op4_ratio",    "OP4 Ratio",
        juce::NormalisableRange<float> (0.5f, 32.0f, 0.01f, 0.4f), 1.0f));
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        "fm_op4_detune",   "OP4 Detune",
        juce::NormalisableRange<float> (-100.0f, 100.0f, 0.1f),    0.0f));
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        "fm_op4_level",    "OP4 Level",
        juce::NormalisableRange<float> (0.0f, 1.0f, 0.001f),       0.0f));
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        "fm_op4_attack",   "OP4 Attack",
        juce::NormalisableRange<float> (0.001f, 10.0f, 0.001f, 0.4f), 0.001f));
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        "fm_op4_decay",    "OP4 Decay",
        juce::NormalisableRange<float> (0.001f, 10.0f, 0.001f, 0.4f), 0.3f));
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        "fm_op4_sustain",  "OP4 Sustain",
        juce::NormalisableRange<float> (0.0f, 1.0f, 0.001f),       0.5f));
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        "fm_op4_release",  "OP4 Release",
        juce::NormalisableRange<float> (0.001f, 10.0f, 0.001f, 0.4f), 0.4f));
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        "fm_op4_vel_sens", "OP4 Vel Sens",
        juce::NormalisableRange<float> (0.0f, 1.0f, 0.001f),       0.5f));
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        "fm_op4_key_scale", "OP4 Key Scale",
        juce::NormalisableRange<float> (0.0f, 1.0f, 0.001f),       0.0f));

    // ======== Noise Section (IDs 2000–2099) ========

    layout.add (std::make_unique<juce::AudioParameterBool>  ("noise_enable",      "Noise Enable",      false));
    layout.add (std::make_unique<juce::AudioParameterChoice> (
        "noise_type", "Noise Type", juce::StringArray { "White", "Pink", "Brown" }, 0));
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        "noise_level",       "Noise Level",
        juce::NormalisableRange<float> (0.0f, 1.0f, 0.001f), 0.5f));
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        "noise_filter_send", "Noise Filter Send",
        juce::NormalisableRange<float> (0.0f, 1.0f, 0.001f), 1.0f));
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        "noise_direct_out",  "Noise Direct Out",
        juce::NormalisableRange<float> (0.0f, 1.0f, 0.001f), 0.0f));
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        "noise_eg_attack",   "Noise EG Attack",
        juce::NormalisableRange<float> (0.0f, 10.0f, 0.001f, 0.4f), 0.001f));
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        "noise_eg_decay",    "Noise EG Decay",
        juce::NormalisableRange<float> (0.0f, 10.0f, 0.001f, 0.4f), 0.1f));
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        "noise_eg_sustain",  "Noise EG Sustain",
        juce::NormalisableRange<float> (0.0f, 1.0f, 0.001f),        0.0f));
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        "noise_eg_release",  "Noise EG Release",
        juce::NormalisableRange<float> (0.0f, 10.0f, 0.001f, 0.4f), 0.2f));

    // ======== Modulation system (LFO x2 / Mod Matrix / Filter Env) ========
    // Defaults keep every path bypassed: amount 0, enable off → v0.1.13 bit-exact.

    // Filter Env (numeric IDs 950–954)
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        "filter_env_attack",  "Filter Env Attack",
        juce::NormalisableRange<float> (0.001f, 3.0f, 0.001f, 0.4f), 0.005f));
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        "filter_env_decay",   "Filter Env Decay",
        juce::NormalisableRange<float> (0.001f, 3.0f, 0.001f, 0.4f), 0.2f));
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        "filter_env_sustain", "Filter Env Sustain",
        juce::NormalisableRange<float> (0.0f, 1.0f, 0.001f),         0.6f));
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        "filter_env_release", "Filter Env Release",
        juce::NormalisableRange<float> (0.001f, 5.0f, 0.001f, 0.4f), 0.3f));
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        "filter_env_amount",  "Filter Env Amount",
        juce::NormalisableRange<float> (-1.0f, 1.0f, 0.001f),        0.0f));

    // LFO 1/2 (numeric IDs 2200–2205 / 2210–2215)
    for (int i = 1; i <= 2; ++i)
    {
        const auto n = juce::String (i);
        layout.add (std::make_unique<juce::AudioParameterFloat> (
            "lfo" + n + "_rate", "LFO" + n + " Rate",
            juce::NormalisableRange<float> (0.01f, 50.0f, 0.001f, 0.35f), 1.0f));
        layout.add (std::make_unique<juce::AudioParameterFloat> (
            "lfo" + n + "_depth", "LFO" + n + " Depth",
            juce::NormalisableRange<float> (0.0f, 1.0f, 0.001f), 1.0f));
        layout.add (std::make_unique<juce::AudioParameterChoice> (
            "lfo" + n + "_wave", "LFO" + n + " Wave",
            juce::StringArray { "Sine", "Tri", "Saw", "Square", "S&H" }, 0));
        layout.add (std::make_unique<juce::AudioParameterFloat> (
            "lfo" + n + "_phase", "LFO" + n + " Phase",
            juce::NormalisableRange<float> (0.0f, 360.0f, 1.0f), 0.0f));
        layout.add (std::make_unique<juce::AudioParameterBool> (
            "lfo" + n + "_sync",   "LFO" + n + " Sync",   false));
        layout.add (std::make_unique<juce::AudioParameterBool> (
            "lfo" + n + "_retrig", "LFO" + n + " Retrig", false));
    }

    // Mod Matrix slots 0–15 (numeric IDs 2100+5s+{0..4}).
    // Host labels use the short "Mod S{n} …" form (80 params in host lists).
    const juce::StringArray modSources { "LFO1", "LFO2", "Velocity", "AfterTouch", "ModWheel" };
    const juce::StringArray modDests   { "Off", "Filter Cutoff", "Filter Reso", "Pitch",
                                         "Master Volume",
                                         "FM OP1 Level", "FM OP2 Level", "FM OP3 Level", "FM OP4 Level",
                                         "FM OP1 Ratio", "FM OP2 Ratio", "FM OP3 Ratio", "FM OP4 Ratio" };
    const juce::StringArray modCurves  { "Linear", "Exp", "Log" };
    for (int s = 0; s < rdh::synth::ModMatrix::NUM_SLOTS; ++s)
    {
        const auto id  = "mod_slot" + juce::String (s);
        const auto lbl = "Mod S" + juce::String (s + 1);
        layout.add (std::make_unique<juce::AudioParameterChoice> (id + "_source", lbl + " Source", modSources, 0));
        layout.add (std::make_unique<juce::AudioParameterChoice> (id + "_dest",   lbl + " Dest",   modDests,   0));
        layout.add (std::make_unique<juce::AudioParameterFloat>  (id + "_amount", lbl + " Amount",
            juce::NormalisableRange<float> (-1.0f, 1.0f, 0.001f), 0.0f));
        layout.add (std::make_unique<juce::AudioParameterChoice> (id + "_curve",  lbl + " Curve",  modCurves,  0));
        layout.add (std::make_unique<juce::AudioParameterBool>   (id + "_enable", lbl + " On",     false));
    }

    return layout;
}

//==============================================================================
void RdhSynthAudioProcessor::prepareToPlay (double sampleRate, int /*samplesPerBlock*/)
{
    cachedSampleRate = sampleRate;
    synth.setCurrentPlaybackSampleRate (sampleRate);

    for (auto& l : lfo)   // sample-rate change: phase-increment conversion
        l.prepare (sampleRate);

    // Q-2: reset smoother; 5 ms ramp avoids volume-automation clicks.
    volumeSmoothed.reset (sampleRate, 0.005);
    volumeSmoothed.setCurrentAndTargetValue (
        *apvts.getRawParameterValue ("volume"));

    const auto coeffs = juce::IIRCoefficients::makeLowPass (
                            sampleRate, cachedCutoff, cachedResonance);
    for (auto* v : voiceCache) // C-3: no dynamic_cast
        v->setFilterCoefficients (coeffs);
}

void RdhSynthAudioProcessor::releaseResources() {}

#ifndef JucePlugin_PreferredChannelConfigurations
bool RdhSynthAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
  #if JucePlugin_IsMidiEffect
    juce::ignoreUnused (layouts);
    return true;
  #else
    if (layouts.getMainOutputChannelSet() != juce::AudioChannelSet::mono()
     && layouts.getMainOutputChannelSet() != juce::AudioChannelSet::stereo())
        return false;
   #if ! JucePlugin_IsSynth
    if (layouts.getMainOutputChannelSet() != layouts.getMainInputChannelSet())
        return false;
   #endif
    return true;
  #endif
}
#endif

//==============================================================================
void RdhSynthAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer,
                                            juce::MidiBuffer& midiMessages)
{
    juce::ScopedNoDenormals noDenormals;
    buffer.clear();

    keyboardState.processNextMidiBuffer (midiMessages, 0, buffer.getNumSamples(), true);

    // --- Modulation-source scan (after keyboardState → host + UI keys).
    //     Read-only pass; audio-path MIDI handling stays in synth.renderNextBlock.
    for (const auto meta : midiMessages)
    {
        const auto msg = meta.getMessage();
        if (msg.isNoteOn())
        {
            lastNoteVelocity = msg.getFloatVelocity();

            // Retrig: reset phase only when this noteOn breaks silence.
            if (*pp.lfoP[0][5] > 0.5f || *pp.lfoP[1][5] > 0.5f)
            {
                bool anyActive = false;
                for (auto* v : voiceCache)
                    if (v->isVoiceActive()) { anyActive = true; break; }
                if (! anyActive)
                    for (int i = 0; i < 2; ++i)
                        if (*pp.lfoP[i][5] > 0.5f)
                            lfo[i].resetPhase();
            }
        }
        else if (msg.isController() && msg.getControllerNumber() == 1)
            modWheelVal = (float) msg.getControllerValue() / 127.0f;
        else if (msg.isChannelPressure())
            aftertouchVal = (float) msg.getChannelPressureValue() / 127.0f;
    }

    // --- Read parameters (cached atomic pointers — no string lookups) ---
    const int   waveType  = (int)  *pp.waveType;
    const int   waveType2 = (int)  *pp.waveType2;
    const bool  osc2On    = *pp.osc2Enable > 0.5f;
    const int   waveType3 = (int)  *pp.waveType3;
    const bool  osc3On    = *pp.osc3Enable > 0.5f;
    const int   unisonN   = (int)  *pp.unisonVoices;
    const float unisonDet = *pp.unisonDetune;
    const float pitchAmt  = *pp.pitchEnvAmount;
    const float pitchAtk  = *pp.pitchEnvAttack;
    const float cutoff    = *pp.filterCutoff;
    const float resonance = *pp.filterResonance;
    const float volume    = *pp.volume;

    juce::ADSR::Parameters adsrParams;
    adsrParams.attack  = *pp.attack;
    adsrParams.decay   = *pp.decay;
    adsrParams.sustain = *pp.sustain;
    adsrParams.release = *pp.release;

    // FM parameters
    const bool  fmEnable   = *pp.fmEnable > 0.5f;
    const float fmOutLevel = *pp.fmOutputLevel;
    const int   fmAlgo     = (int) *pp.fmAlgorithm;
    const float fmFeedback = *pp.fmFeedback;

    rdh::synth::OperatorParams opp[4];
    for (int o = 0; o < 4; ++o)
    {
        auto& d = opp[o];
        d.ratio    = *pp.fmOp[o][0];
        d.detune   = *pp.fmOp[o][1];
        d.level    = *pp.fmOp[o][2];
        d.attack   = *pp.fmOp[o][3];
        d.decay    = *pp.fmOp[o][4];
        d.sustain  = *pp.fmOp[o][5];
        d.release  = *pp.fmOp[o][6];
        d.velSens  = *pp.fmOp[o][7];
        d.keyScale = *pp.fmOp[o][8];
    }

    // Noise parameters
    const bool  noiseEnable  = *pp.noiseEnable > 0.5f;
    const int   noiseType    = (int) *pp.noiseType;
    const float noiseLevel   = *pp.noiseLevel;
    const float noiseFltSend = *pp.noiseFilterSend;
    const float noiseDirOut  = *pp.noiseDirectOut;
    const float noiseEGAtk   = *pp.noiseEgAttack;
    const float noiseEGDec   = *pp.noiseEgDecay;
    const float noiseEGSus   = *pp.noiseEgSustain;
    const float noiseEGRel   = *pp.noiseEgRelease;

    // Filter Env parameters
    juce::ADSR::Parameters fenvParams;
    fenvParams.attack  = *pp.filterEnvAttack;
    fenvParams.decay   = *pp.filterEnvDecay;
    fenvParams.sustain = *pp.filterEnvSustain;
    fenvParams.release = *pp.filterEnvRelease;
    const float fenvAmount = *pp.filterEnvAmount;

    // Effective-zero detent: with NormalisableRange(-1..1,
    // interval 0.001) the *raw* default is ≈ 4.7e-8, not 0 (float error in
    // -1 + 0.001f*1000). Anything below half a grid step counts as the 0
    // detent — otherwise the bypass-when-unused rule never engages.
    const bool fenvOn = std::abs (fenvAmount) >= 1.0e-4f;

    // --- Block-rate modulation. Defaults (no slot enabled, amount 0)
    //     skip everything below → legacy-identical path (zero cost when unused).
    bool slotActive = false;
    for (int s = 0; s < rdh::synth::ModMatrix::NUM_SLOTS && ! slotActive; ++s)
        slotActive = *pp.modSlot[s][4] > 0.5f;
    const bool modActive = slotActive || fenvOn;

    float cutoffEff = cutoff, resoEff = resonance, volumeEff = volume;
    float pitchFactor = 1.0f;
    float ratioFactor[4] = { 1.0f, 1.0f, 1.0f, 1.0f };
    bool  voiceFilterPath = fenvOn;

    if (slotActive)
    {
        double bpm = 120.0; // fallback: no host transport
        if (auto* ph = getPlayHead())
            if (auto position = ph->getPosition())
                if (auto b = position->getBpm())
                    bpm = *b;

        rdh::synth::ModSources src;
        for (int i = 0; i < 2; ++i)
        {
            const float rate = *pp.lfoP[i][0];
            lfo[i].setDepth              (*pp.lfoP[i][1]);
            lfo[i].setWave               ((int) *pp.lfoP[i][2]);
            lfo[i].setPhaseOffsetDegrees (*pp.lfoP[i][3]);

            double hz = rate;
            if (*pp.lfoP[i][4] > 0.5f) // sync: normalized rate → division table
            {
                static constexpr double bars[8] = { 4.0, 2.0, 1.0, 0.5, 0.25, 0.125, 0.0625, 0.03125 };
                const int idx = juce::jlimit (0, 7,
                    (int) (lfoRateParam[i]->convertTo0to1 (rate) * 8.0f));
                hz = bpm / (240.0 * bars[idx]); // 1 bar (4/4) = 240/bpm seconds
            }
            const float v = lfo[i].advanceBlock (buffer.getNumSamples(), hz);
            (i == 0 ? src.lfo1 : src.lfo2) = v;
        }
        src.velocity   = lastNoteVelocity;
        src.aftertouch = aftertouchVal;
        src.modWheel   = modWheelVal;

        for (int s = 0; s < rdh::synth::ModMatrix::NUM_SLOTS; ++s)
        {
            auto& sl  = modMatrix.slots[s];
            sl.source = (int) *pp.modSlot[s][0];
            sl.dest   = (int) *pp.modSlot[s][1];
            sl.amount = *pp.modSlot[s][2];
            sl.curve  = (int) *pp.modSlot[s][3];
            sl.enable = *pp.modSlot[s][4] > 0.5f;
        }

        const auto mod = modMatrix.evaluate (src);
        cutoffEff   = cutoff * std::exp2f (mod.cutoffOct);
        resoEff     = juce::jlimit (0.1f, 10.0f, resonance + mod.resoAdd);
        volumeEff   = juce::jlimit (0.0f, 1.0f, volume * mod.volScale); // R-1: saturates at 1.0
        pitchFactor = std::exp2f (mod.pitchSemis / 12.0f);
        for (int o = 0; o < 4; ++o)
        {
            opp[o].level   = juce::jlimit (0.0f, 1.0f, opp[o].level + mod.opLevelAdd[o]);
            ratioFactor[o] = std::exp2f (mod.opRatioOct[o]);
        }
        voiceFilterPath = voiceFilterPath || mod.filterActive;
    }
    cutoffEff = juce::jlimit (20.0f, (float) (cachedSampleRate * 0.49), cutoffEff);

    // Transition guard: when the per-64 voice path releases the filter, force
    // one legacy coefficient resync so voices return to the parameter cutoff.
    if (filterPathWasActive && ! voiceFilterPath)
        cachedCutoff = -1.0e9f;
    filterPathWasActive = voiceFilterPath;

    // Filter coefficient update (guarded to avoid per-block IIR recomputation).
    // Quiet while the per-voice filter path owns the coefficients.
    const bool filterChanged = ! voiceFilterPath
                            && (std::abs (cutoff    - cachedCutoff)    > 1.0f
                             || std::abs (resonance - cachedResonance) > 0.01f);
    juce::IIRCoefficients filterCoeffs;
    if (filterChanged)
    {
        cachedCutoff    = cutoff;
        cachedResonance = resonance;
        filterCoeffs    = juce::IIRCoefficients::makeLowPass (
                              cachedSampleRate,
                              juce::jlimit (20.0, cachedSampleRate * 0.49, (double) cutoff),
                              (double) juce::jlimit (0.1f, 10.0f, resonance));
    }

    // Apply all params to voices — C-3: raw pointer loop, no dynamic_cast.
    for (auto* v : voiceCache)
    {
        v->setOscillators    (waveType, waveType2, osc2On, waveType3, osc3On);
        v->setADSRParameters (adsrParams);
        v->setNumUnison      (unisonN);
        v->setUnisonDetune   (unisonDet);
        v->setPitchEnvAmount (pitchAmt);
        v->setPitchEnvAttack (pitchAtk);
        if (filterChanged)
            v->setFilterCoefficients (filterCoeffs);

        // Modulation distribution. pitchFactor is 1.0 and
        // voiceFilterPath false in the default state → bit-exact legacy path.
        v->setFilterEnvParams (fenvParams);
        v->setFilterEnvAmount (fenvAmount);
        v->setFilterTarget    (cutoffEff, resoEff, voiceFilterPath);
        v->setPitchModFactor  ((double) pitchFactor);
        if (modActive || modWasActive)   // ...WasActive: restore factors to 1 once
            v->setFMPitchRatioFactors (pitchFactor, ratioFactor);

        v->setFMEnabled        (fmEnable);
        v->setFMOutputLevel    (fmOutLevel);
        v->setFMAlgorithm      (fmAlgo);
        v->setFMFeedback       (fmFeedback);
        for (int o = 0; o < 4; ++o)
            v->setFMOperatorParams (o, opp[o]);

        v->setNoiseEnabled    (noiseEnable);
        v->setNoiseType       (noiseType);
        v->setNoiseLevel      (noiseLevel);
        v->setNoiseFilterSend (noiseFltSend);
        v->setNoiseDirectOut  (noiseDirOut);
        v->setNoiseEGParams   (noiseEGAtk, noiseEGDec, noiseEGSus, noiseEGRel);
    }
    modWasActive = modActive;

    synth.renderNextBlock (buffer, midiMessages, 0, buffer.getNumSamples());

    // Q-2: smoothed gain ramp instead of instantaneous applyGain.
    volumeSmoothed.setTargetValue (volumeEff); // == volume when matrix off
    volumeSmoothed.applyGain (buffer, buffer.getNumSamples());

    // Push to spectrum FIFO (lock-free, batched)
    pushSamplesToSpectrumFifo (buffer.getReadPointer (0), buffer.getNumSamples());
}

//==============================================================================
void RdhSynthAudioProcessor::pushSamplesToSpectrumFifo (const float* data,
                                                         int numSamples) noexcept
{
    int start1, size1, start2, size2;
    const int toWrite = juce::jmin (numSamples, spectrumFifo.getFreeSpace());
    if (toWrite <= 0) return;

    spectrumFifo.prepareToWrite (toWrite, start1, size1, start2, size2);
    if (size1 > 0) std::memcpy (spectrumFifoBuffer.data() + start1, data,          (size_t) size1 * sizeof (float));
    if (size2 > 0) std::memcpy (spectrumFifoBuffer.data() + start2, data + size1,  (size_t) size2 * sizeof (float));
    spectrumFifo.finishedWrite (size1 + size2);
}

//==============================================================================
bool RdhSynthAudioProcessor::hasEditor() const { return true; }

juce::AudioProcessorEditor* RdhSynthAudioProcessor::createEditor()
{
    return new RdhSynthAudioProcessorEditor (*this);
}

//==============================================================================
const juce::String RdhSynthAudioProcessor::getName() const { return JucePlugin_Name; }

bool RdhSynthAudioProcessor::acceptsMidi() const
{
   #if JucePlugin_WantsMidiInput
    return true;
   #else
    return false;
   #endif
}
bool RdhSynthAudioProcessor::producesMidi() const
{
   #if JucePlugin_ProducesMidiOutput
    return true;
   #else
    return false;
   #endif
}
bool RdhSynthAudioProcessor::isMidiEffect() const
{
   #if JucePlugin_IsMidiEffect
    return true;
   #else
    return false;
   #endif
}
double RdhSynthAudioProcessor::getTailLengthSeconds() const { return 0.5; }

//==============================================================================
int  RdhSynthAudioProcessor::getNumPrograms()                    { return 1; }
int  RdhSynthAudioProcessor::getCurrentProgram()                 { return 0; }
void RdhSynthAudioProcessor::setCurrentProgram (int)             {}
const juce::String RdhSynthAudioProcessor::getProgramName (int)  { return {}; }
void RdhSynthAudioProcessor::changeProgramName (int, const juce::String&) {}

//==============================================================================
void RdhSynthAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    state.setProperty (PRESET_VERSION_KEY, PRESET_VERSION_VALUE, nullptr);
    std::unique_ptr<juce::XmlElement> xml (state.createXml());
    copyXmlToBinary (*xml, destData);
}

void RdhSynthAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    std::unique_ptr<juce::XmlElement> xmlState (getXmlFromBinary (data, sizeInBytes));
    if (xmlState == nullptr) return;
    if (!xmlState->hasTagName (apvts.state.getType())) return;

    auto tree = juce::ValueTree::fromXml (*xmlState);

    const bool hasVersionTag = tree.hasProperty (PRESET_VERSION_KEY);
    apvts.replaceState (tree);

    if (!hasVersionTag)
    {
        // REQ-COMPAT-002: disable FM and Noise for v1.x presets
        auto forceDisable = [this] (const juce::String& id)
        {
            if (auto* p = apvts.getParameter (id))
                p->setValueNotifyingHost (0.0f);
        };
        forceDisable ("fm_enable");
        forceDisable ("noise_enable");
    }
}

//==============================================================================
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new RdhSynthAudioProcessor();
}
