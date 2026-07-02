/*
  ==============================================================================
    AudioAnalyzer.h
    FFT + YIN pitch detection – runs on background thread.
    No heap allocation in audio render path (all buffers pre-allocated here).
  ==============================================================================
*/

#pragma once
#include <JuceHeader.h>

//==============================================================================
struct AnalysisResult
{
    float fundamentalFreq   = 440.0f;  // Hz
    float spectralCentroid  = 2000.0f; // Hz
    float harmonicRatio     = 0.5f;    // 0=even harmonics, 1=odd harmonics
    float transientSharpness = 0.5f;   // 0=slow attack, 1=sharp attack
    float decayCurve        = 0.5f;    // normalized 0-1
    float sustainLevel      = 0.5f;    // RMS of steady-state region
    float spectralFlatness  = 0.3f;    // 0=tonal, 1=noise-like
    bool  isValid           = false;
};

//==============================================================================
class AudioAnalyzer : public juce::Thread
{
public:
    AudioAnalyzer();
    ~AudioAnalyzer() override;

    // Call from message thread before analyseAsync
    bool loadFile (const juce::File& file);

    // Starts background analysis; result available via getResult() when isComplete()
    void analyseAsync();

    bool          isComplete()   const { return analysisComplete.load(); }
    float         getProgress()  const { return progress.load(); }
    AnalysisResult getResult()   const { return latestResult; }

private:
    void run() override;

    AnalysisResult doAnalysis();

    float detectFundamental      (const float* samples, int n, double sr) const;
    float computeSpectralCentroid(const float* mag, int bins, float binWidth) const;
    float computeHarmonicRatio   (const float* mag, int bins,
                                  float fundHz, float binWidth) const;
    float computeSpectralFlatness(const float* mag, int bins) const;
    void  detectEnvelope         (const float* samples, int n, double sr,
                                  float& outAttack, float& outDecay, float& outSustain) const;

    static constexpr int FFT_ORDER   = 12;            // 4096 points
    static constexpr int FFT_SIZE    = 1 << FFT_ORDER;
    static constexpr int MAX_SAMPLES = 44100 * 10;    // 10 s @ 44100 Hz

    juce::dsp::FFT                   fft { FFT_ORDER };
    std::array<float, FFT_SIZE * 2>  fftWorkBuf {};   // real+imag interleaved
    std::array<float, MAX_SAMPLES>   audioData  {};
    int    numSamples    = 0;
    double sampleRate    = 44100.0;

    std::atomic<float> progress          { 0.0f };
    std::atomic<bool>  analysisComplete  { false };
    AnalysisResult     latestResult;

    juce::AudioFormatManager formatManager;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AudioAnalyzer)
};
