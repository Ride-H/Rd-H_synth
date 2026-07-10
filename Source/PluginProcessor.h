/*
  ==============================================================================
    PluginProcessor.h
    Rd-H Synth v2.0 – 16-voice polyphonic synthesizer.
    FM Section (2-OP) + White Noise + AI MATCH + real-time spectrum.

    Fixes applied:
      C-3: voiceCache holds raw RdhSynthVoice* to eliminate dynamic_cast in
           the audio thread's processBlock loop.
      Q-2: volumeSmoothed (LinearSmoothedValue) prevents click on volume automation.
  ==============================================================================
*/

#pragma once
#include <JuceHeader.h>
#include "be/SynthVoice.h"
#include "be/AudioAnalyzer.h"
#include "be/dsp/LFO.h"
#include "be/dsp/ModMatrix.h"

//==============================================================================
class RdhSynthAudioProcessor : public juce::AudioProcessor
{
public:
    //==========================================================================
    RdhSynthAudioProcessor();
    ~RdhSynthAudioProcessor() override;

    //==========================================================================
    void prepareToPlay  (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;

   #ifndef JucePlugin_PreferredChannelConfigurations
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
   #endif

    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    //==========================================================================
    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override;

    //==========================================================================
    const juce::String getName()             const override;
    bool   acceptsMidi()                     const override;
    bool   producesMidi()                    const override;
    bool   isMidiEffect()                    const override;
    double getTailLengthSeconds()            const override;

    //==========================================================================
    int  getNumPrograms()                         override;
    int  getCurrentProgram()                      override;
    void setCurrentProgram (int index)            override;
    const juce::String getProgramName (int index) override;
    void changeProgramName (int, const juce::String&) override;

    //==========================================================================
    void getStateInformation (juce::MemoryBlock& destData)              override;
    void setStateInformation (const void* data, int sizeInBytes)        override;

    // Persist the current preset name in the processor (apvts state)
    // so the editor can restore the displayed name on reopen (the name otherwise
    // lives only in the editor's TopBar and is lost when the editor is recreated).
    // Stored as a non-parameter property → also survives project save/load.
    void setCurrentPresetName (const juce::String& n) { apvts.state.setProperty ("presetName", n, nullptr); }
    juce::String getCurrentPresetName() const { return apvts.state.getProperty ("presetName", {}).toString(); }

    //==========================================================================
    // Public for UI components
    juce::AudioProcessorValueTreeState apvts;
    juce::MidiKeyboardState            keyboardState;
    AudioAnalyzer                      analyzer;

    // Lock-free spectrum FIFO for SpectrumAnalyzerComponent
    static constexpr int SPECTRUM_FIFO_SIZE = 2048;
    juce::AbstractFifo                        spectrumFifo { SPECTRUM_FIFO_SIZE };
    std::array<float, SPECTRUM_FIFO_SIZE>     spectrumFifoBuffer {};

    // CPU usage estimate (updated each processBlock, read by GUI at 30 fps)
    std::atomic<float> cpuUsagePercent { 0.0f };

    //==========================================================================
    // Preset version tagging (for v1.x migration — REQ-COMPAT-001/002)
    static constexpr const char* PRESET_VERSION_KEY   = "rdh_version";
    static constexpr const char* PRESET_VERSION_VALUE = "2.0";

private:
    //==========================================================================
    static constexpr int NUM_VOICES = 16;

    //==========================================================================
    // Every APVTS raw-value pointer is resolved once in the
    // constructor — processBlock then does plain atomic loads, eliminating the
    // per-block string-ID lookups (a measured in-host overhead). Pointers stay
    // valid for the processor's lifetime (APVTS parameters are never removed).
    struct ParamPtrs
    {
        std::atomic<float> *waveType = nullptr, *waveType2 = nullptr, *osc2Enable = nullptr,
                           *waveType3 = nullptr, *osc3Enable = nullptr,
                           *attack = nullptr, *decay = nullptr, *sustain = nullptr, *release = nullptr,
                           *filterCutoff = nullptr, *filterResonance = nullptr,
                           *unisonVoices = nullptr, *unisonDetune = nullptr,
                           *pitchEnvAmount = nullptr, *pitchEnvAttack = nullptr, *volume = nullptr,
                           *fmEnable = nullptr, *fmOutputLevel = nullptr, *fmAlgorithm = nullptr,
                           *fmFeedback = nullptr,
                           *noiseEnable = nullptr, *noiseType = nullptr, *noiseLevel = nullptr,
                           *noiseFilterSend = nullptr, *noiseDirectOut = nullptr,
                           *noiseEgAttack = nullptr, *noiseEgDecay = nullptr,
                           *noiseEgSustain = nullptr, *noiseEgRelease = nullptr;

        // [op][field]: ratio, detune, level, attack, decay, sustain, release,
        //              vel_sens, key_scale — same order as OperatorParams fields.
        std::atomic<float>* fmOp[4][9] = {};

        // Modulation system: [lfo][rate,depth,wave,phase,sync,retrig],
        // [slot][source,dest,amount,curve,enable].
        std::atomic<float>* lfoP[2][6]     = {};
        std::atomic<float>* modSlot[16][5] = {};
        std::atomic<float> *filterEnvAttack = nullptr, *filterEnvDecay = nullptr,
                           *filterEnvSustain = nullptr, *filterEnvRelease = nullptr,
                           *filterEnvAmount = nullptr;
    };
    ParamPtrs pp;
    void resolveParamPtrs();

    //==========================================================================
    // Instance-level modulation, evaluated once per
    // block in processBlock. Fully bypassed (zero cost, bit-exact) while no
    // slot is enabled and filter_env_amount == 0 (zero cost when unused).
    rdh::synth::LFO       lfo[2];
    rdh::synth::ModMatrix modMatrix;
    juce::RangedAudioParameter* lfoRateParam[2] = {}; // normalized rate → sync division
    float lastNoteVelocity = 0.0f;   // matrix source: last-note velocity (documented limitation)
    float aftertouchVal    = 0.0f;   // channel pressure
    float modWheelVal      = 0.0f;   // CC1
    bool  modWasActive     = false;  // transition guard: restore FM factors once
    bool  filterPathWasActive = false; // transition guard: legacy coefficient resync

    juce::Synthesiser synth;

    // C-3: raw pointer cache — built once in constructor, avoids dynamic_cast
    // on the audio thread.
    std::vector<RdhSynthVoice*> voiceCache;

    // Filter coefficient cache (recomputed only when params change)
    double cachedSampleRate    = 44100.0;
    float  cachedCutoff        = 20000.0f;
    float  cachedResonance     = 0.707f;

    // Q-2: smoothed master volume — prevents clicks on automation step changes.
    juce::LinearSmoothedValue<float> volumeSmoothed { 0.7f };

    void pushSamplesToSpectrumFifo (const float* data, int numSamples) noexcept;

    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (RdhSynthAudioProcessor)
};
