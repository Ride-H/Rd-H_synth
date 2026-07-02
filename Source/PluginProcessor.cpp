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
    // Phase 2 (S1-3): algorithm 0–3 (REQ-FM-007) + OP4 feedback. Defaults reproduce Phase 1.
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

    // OP3 (Phase 2, S1-3) — default level 0 so existing 2-OP presets are unchanged
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

    // OP4 (Phase 2, S1-3) — carries the feedback loop; default level 0
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
        "noise_type", "Noise Type", juce::StringArray { "White", "Pink", "Brown" }, 0)); // S1-4
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

    return layout;
}

//==============================================================================
void RdhSynthAudioProcessor::prepareToPlay (double sampleRate, int /*samplesPerBlock*/)
{
    cachedSampleRate = sampleRate;
    synth.setCurrentPlaybackSampleRate (sampleRate);

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

    // --- Read parameters (atomic raw, no alloc) ---
    const int   waveType  = (int)  *apvts.getRawParameterValue ("waveType");
    const int   waveType2 = (int)  *apvts.getRawParameterValue ("waveType2");
    const bool  osc2On    = *apvts.getRawParameterValue ("osc2Enable")  > 0.5f;
    const int   waveType3 = (int)  *apvts.getRawParameterValue ("waveType3");
    const bool  osc3On    = *apvts.getRawParameterValue ("osc3Enable")  > 0.5f;
    const int   unisonN   = (int)  *apvts.getRawParameterValue ("unisonVoices");
    const float unisonDet = (float)*apvts.getRawParameterValue ("unisonDetune");
    const float pitchAmt  = (float)*apvts.getRawParameterValue ("pitchEnvAmount");
    const float pitchAtk  = (float)*apvts.getRawParameterValue ("pitchEnvAttack");
    const float cutoff    = (float)*apvts.getRawParameterValue ("filterCutoff");
    const float resonance = (float)*apvts.getRawParameterValue ("filterResonance");
    const float volume    = (float)*apvts.getRawParameterValue ("volume");

    juce::ADSR::Parameters adsrParams;
    adsrParams.attack  = *apvts.getRawParameterValue ("attack");
    adsrParams.decay   = *apvts.getRawParameterValue ("decay");
    adsrParams.sustain = *apvts.getRawParameterValue ("sustain");
    adsrParams.release = *apvts.getRawParameterValue ("release");

    // FM parameters
    const bool  fmEnable   = *apvts.getRawParameterValue ("fm_enable")      > 0.5f;
    const float fmOutLevel = *apvts.getRawParameterValue ("fm_output_level");
    const int   fmAlgo     = (int) *apvts.getRawParameterValue ("fm_algorithm");
    const float fmFeedback = *apvts.getRawParameterValue ("fm_feedback");

    rdh::synth::OperatorParams op1p, op2p, op3p, op4p;
    op1p.ratio    = *apvts.getRawParameterValue ("fm_op1_ratio");
    op1p.detune   = *apvts.getRawParameterValue ("fm_op1_detune");
    op1p.level    = *apvts.getRawParameterValue ("fm_op1_level");
    op1p.attack   = *apvts.getRawParameterValue ("fm_op1_attack");
    op1p.decay    = *apvts.getRawParameterValue ("fm_op1_decay");
    op1p.sustain  = *apvts.getRawParameterValue ("fm_op1_sustain");
    op1p.release  = *apvts.getRawParameterValue ("fm_op1_release");
    op1p.velSens  = *apvts.getRawParameterValue ("fm_op1_vel_sens");
    op1p.keyScale = *apvts.getRawParameterValue ("fm_op1_key_scale");

    op2p.ratio    = *apvts.getRawParameterValue ("fm_op2_ratio");
    op2p.detune   = *apvts.getRawParameterValue ("fm_op2_detune");
    op2p.level    = *apvts.getRawParameterValue ("fm_op2_level");
    op2p.attack   = *apvts.getRawParameterValue ("fm_op2_attack");
    op2p.decay    = *apvts.getRawParameterValue ("fm_op2_decay");
    op2p.sustain  = *apvts.getRawParameterValue ("fm_op2_sustain");
    op2p.release  = *apvts.getRawParameterValue ("fm_op2_release");
    op2p.velSens  = *apvts.getRawParameterValue ("fm_op2_vel_sens");
    op2p.keyScale = *apvts.getRawParameterValue ("fm_op2_key_scale");

    op3p.ratio    = *apvts.getRawParameterValue ("fm_op3_ratio");
    op3p.detune   = *apvts.getRawParameterValue ("fm_op3_detune");
    op3p.level    = *apvts.getRawParameterValue ("fm_op3_level");
    op3p.attack   = *apvts.getRawParameterValue ("fm_op3_attack");
    op3p.decay    = *apvts.getRawParameterValue ("fm_op3_decay");
    op3p.sustain  = *apvts.getRawParameterValue ("fm_op3_sustain");
    op3p.release  = *apvts.getRawParameterValue ("fm_op3_release");
    op3p.velSens  = *apvts.getRawParameterValue ("fm_op3_vel_sens");
    op3p.keyScale = *apvts.getRawParameterValue ("fm_op3_key_scale");

    op4p.ratio    = *apvts.getRawParameterValue ("fm_op4_ratio");
    op4p.detune   = *apvts.getRawParameterValue ("fm_op4_detune");
    op4p.level    = *apvts.getRawParameterValue ("fm_op4_level");
    op4p.attack   = *apvts.getRawParameterValue ("fm_op4_attack");
    op4p.decay    = *apvts.getRawParameterValue ("fm_op4_decay");
    op4p.sustain  = *apvts.getRawParameterValue ("fm_op4_sustain");
    op4p.release  = *apvts.getRawParameterValue ("fm_op4_release");
    op4p.velSens  = *apvts.getRawParameterValue ("fm_op4_vel_sens");
    op4p.keyScale = *apvts.getRawParameterValue ("fm_op4_key_scale");

    // Noise parameters
    const bool  noiseEnable  = *apvts.getRawParameterValue ("noise_enable")      > 0.5f;
    const int   noiseType    = (int) *apvts.getRawParameterValue ("noise_type");
    const float noiseLevel   = *apvts.getRawParameterValue ("noise_level");
    const float noiseFltSend = *apvts.getRawParameterValue ("noise_filter_send");
    const float noiseDirOut  = *apvts.getRawParameterValue ("noise_direct_out");
    const float noiseEGAtk   = *apvts.getRawParameterValue ("noise_eg_attack");
    const float noiseEGDec   = *apvts.getRawParameterValue ("noise_eg_decay");
    const float noiseEGSus   = *apvts.getRawParameterValue ("noise_eg_sustain");
    const float noiseEGRel   = *apvts.getRawParameterValue ("noise_eg_release");

    // Filter coefficient update (guarded to avoid per-block IIR recomputation)
    const bool filterChanged = (std::abs (cutoff    - cachedCutoff)    > 1.0f
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

        v->setFMEnabled        (fmEnable);
        v->setFMOutputLevel    (fmOutLevel);
        v->setFMAlgorithm      (fmAlgo);
        v->setFMFeedback       (fmFeedback);
        v->setFMOperatorParams (0, op1p);
        v->setFMOperatorParams (1, op2p);
        v->setFMOperatorParams (2, op3p);
        v->setFMOperatorParams (3, op4p);

        v->setNoiseEnabled    (noiseEnable);
        v->setNoiseType       (noiseType);
        v->setNoiseLevel      (noiseLevel);
        v->setNoiseFilterSend (noiseFltSend);
        v->setNoiseDirectOut  (noiseDirOut);
        v->setNoiseEGParams   (noiseEGAtk, noiseEGDec, noiseEGSus, noiseEGRel);
    }

    synth.renderNextBlock (buffer, midiMessages, 0, buffer.getNumSamples());

    // Q-2: smoothed gain ramp instead of instantaneous applyGain.
    volumeSmoothed.setTargetValue (volume);
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
