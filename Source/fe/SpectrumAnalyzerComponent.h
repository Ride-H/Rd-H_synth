/*
  ==============================================================================
    SpectrumAnalyzerComponent.h
    Real-time FFT spectrum display. Reads samples from PluginProcessor's FIFO.
  ==============================================================================
*/

#pragma once
#include <JuceHeader.h>

class RdhSynthAudioProcessor; // forward declaration

//==============================================================================
class SpectrumAnalyzerComponent : public juce::Component,
                                   public juce::Timer
{
public:
    explicit SpectrumAnalyzerComponent (RdhSynthAudioProcessor& proc);
    ~SpectrumAnalyzerComponent() override;

    void paint   (juce::Graphics&) override;
    void resized () override;

private:
    void timerCallback() override;
    void drawSpectrumBars    (juce::Graphics&);
    void drawFrequencyLabels (juce::Graphics&);

    static constexpr int FFT_ORDER    = 10;          // 1024 points
    static constexpr int FFT_SIZE     = 1 << FFT_ORDER;
    static constexpr int NUM_BARS     = 64;
    static constexpr int TIMER_HZ     = 30;

    RdhSynthAudioProcessor& processor;

    juce::dsp::FFT fft { FFT_ORDER };

    std::array<float, FFT_SIZE * 2> fftWorkBuf {};
    std::array<float, FFT_SIZE>     inputBuf   {};
    std::array<float, NUM_BARS>     barLevels  {};

    int  inputFillPos = 0;
    bool dataReady    = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SpectrumAnalyzerComponent)
};
