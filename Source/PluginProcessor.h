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
