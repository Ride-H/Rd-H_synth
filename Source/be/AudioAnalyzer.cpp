/*
  ==============================================================================
    AudioAnalyzer.cpp
  ==============================================================================
*/

#include "AudioAnalyzer.h"

AudioAnalyzer::AudioAnalyzer() : juce::Thread ("AudioAnalyzer")
{
    formatManager.registerBasicFormats(); // WAV, AIFF, etc.
}

AudioAnalyzer::~AudioAnalyzer()
{
    stopThread (2000);
}

//==============================================================================
bool AudioAnalyzer::loadFile (const juce::File& file)
{
    stopThread (1000);
    analysisComplete.store (false);
    progress.store (0.0f);

    std::unique_ptr<juce::AudioFormatReader> reader (
        formatManager.createReaderFor (file));

    if (reader == nullptr) return false;

    // Resample to mono, cap at MAX_SAMPLES
    const int64_t totalIn = reader->lengthInSamples;
    const int toRead = (int) juce::jmin ((int64_t) MAX_SAMPLES, totalIn);

    juce::AudioBuffer<float> readBuf (1, toRead);
    reader->read (&readBuf, 0, toRead, 0, true, true);

    // Down-mix to mono if needed (reader already does this via true,true)
    const float* src = readBuf.getReadPointer (0);
    for (int i = 0; i < toRead; ++i)
        audioData[(size_t) i] = src[i];

    numSamples = toRead;
    sampleRate = reader->sampleRate;
    return true;
}

void AudioAnalyzer::analyseAsync()
{
    startThread (juce::Thread::Priority::low);
}

//==============================================================================
void AudioAnalyzer::run()
{
    latestResult = doAnalysis();
    analysisComplete.store (true);
    progress.store (1.0f);
}

//==============================================================================
AnalysisResult AudioAnalyzer::doAnalysis()
{
    AnalysisResult result;
    if (numSamples < 512) return result;

    const float* data = audioData.data();
    const int    n    = numSamples;

    // ---------- 1. Fundamental frequency (YIN on first 0.5s) ----------
    progress.store (0.05f);
    const int yinLen = (int) juce::jmin ((double) n, sampleRate * 0.5);
    result.fundamentalFreq = detectFundamental (data, yinLen, sampleRate);

    // ---------- 2. FFT analysis (first FFT_SIZE samples) ----------
    progress.store (0.2f);
    const int fftIn = juce::jmin (n, FFT_SIZE);
    fftWorkBuf.fill (0.0f);

    // Hann window + copy
    for (int i = 0; i < fftIn; ++i)
    {
        const float win = 0.5f - 0.5f * std::cos (
            juce::MathConstants<float>::twoPi * (float) i / (float) (fftIn - 1));
        fftWorkBuf[(size_t) i] = data[i] * win;
    }

    fft.performFrequencyOnlyForwardTransform (fftWorkBuf.data());
    // fftWorkBuf[0..FFT_SIZE/2] now contains magnitudes
    const int   numBins  = FFT_SIZE / 2 + 1;
    const float binWidth = (float) sampleRate / (float) FFT_SIZE;

    progress.store (0.45f);
    result.spectralCentroid = computeSpectralCentroid (fftWorkBuf.data(), numBins, binWidth);

    progress.store (0.55f);
    result.harmonicRatio    = computeHarmonicRatio (fftWorkBuf.data(), numBins,
                                                     result.fundamentalFreq, binWidth);

    progress.store (0.65f);
    result.spectralFlatness = computeSpectralFlatness (fftWorkBuf.data(), numBins);

    // ---------- 3. Envelope analysis (full signal) ----------
    progress.store (0.75f);
    detectEnvelope (data, n, sampleRate,
                    result.transientSharpness,
                    result.decayCurve,
                    result.sustainLevel);

    progress.store (0.95f);
    result.isValid = true;
    return result;
}

//==============================================================================
float AudioAnalyzer::detectFundamental (const float* samples, int n, double sr) const
{
    // YIN algorithm
    const int maxTau = (int) (sr / 50.0);   // min freq 50 Hz
    const int minTau = (int) (sr / 1200.0); // max freq 1200 Hz
    const int yinLen = juce::jmin (maxTau + 1, n / 2);

    if (yinLen <= minTau) return 440.0f;

    // All on heap – background thread only
    std::vector<float> d ((size_t) yinLen, 0.0f);

    // Step 1 + 2: difference + cumulative mean normalized
    d[0] = 1.0f;
    float runSum = 0.0f;
    for (int tau = 1; tau < yinLen; ++tau)
    {
        float diff = 0.0f;
        const int limit = juce::jmin (yinLen, n - tau);
        for (int j = 0; j < limit; ++j)
        {
            const float x = samples[j] - samples[j + tau];
            diff += x * x;
        }
        runSum += diff;
        d[(size_t) tau] = (runSum > 1e-10f) ? (diff * (float) tau / runSum) : 1.0f;
    }

    // Step 3: find first tau below threshold
    const float threshold = 0.1f;
    for (int tau = minTau; tau < yinLen - 1; ++tau)
    {
        if (d[(size_t) tau] < threshold)
        {
            while (tau + 1 < yinLen - 1 && d[(size_t) (tau + 1)] < d[(size_t) tau]) ++tau;
            // Parabolic interpolation
            float better = (float) tau;
            if (tau > 0 && tau < yinLen - 1)
            {
                const float s0 = d[(size_t) (tau - 1)], s1 = d[(size_t) tau], s2 = d[(size_t) (tau + 1)];
                const float denom = 2.0f * s1 - s2 - s0;
                if (std::abs (denom) > 1e-10f)
                    better = (float) tau + (s2 - s0) / (2.0f * denom);
            }
            return (float) (sr / (double) better);
        }
    }

    // No pitch found: use tau with minimum d
    int best = minTau;
    for (int tau = minTau + 1; tau < yinLen; ++tau)
        if (d[(size_t) tau] < d[(size_t) best]) best = tau;

    return (float) (sr / (double) best);
}

//==============================================================================
float AudioAnalyzer::computeSpectralCentroid (const float* mag,
                                               int bins, float binWidth) const
{
    float weightSum = 0.0f, totalMag = 0.0f;
    for (int b = 1; b < bins; ++b)
    {
        const float m = mag[b];
        weightSum += m * (float) b * binWidth;
        totalMag  += m;
    }
    return (totalMag > 1e-10f) ? (weightSum / totalMag) : 2000.0f;
}

//==============================================================================
float AudioAnalyzer::computeHarmonicRatio (const float* mag, int bins,
                                            float fundHz, float binWidth) const
{
    if (fundHz < 20.0f || binWidth < 1.0f) return 0.5f;

    float oddEnergy = 0.0f, evenEnergy = 0.0f;
    for (int harmonic = 1; harmonic <= 10; ++harmonic)
    {
        const int bin = (int) ((float) harmonic * fundHz / binWidth);
        if (bin >= bins) break;
        const float e = mag[bin] * mag[bin];
        if (harmonic % 2 == 0) evenEnergy += e;
        else                    oddEnergy  += e;
    }

    const float total = oddEnergy + evenEnergy;
    return (total > 1e-10f) ? (oddEnergy / total) : 0.5f;
}

//==============================================================================
float AudioAnalyzer::computeSpectralFlatness (const float* mag, int bins) const
{
    // Geometric mean / arithmetic mean (in log space for stability)
    double logSum = 0.0;
    double linSum = 0.0;
    int    count  = 0;
    for (int b = 1; b < bins; ++b)
    {
        const double m = std::max ((double) mag[b], 1e-10);
        logSum += std::log (m);
        linSum += m;
        ++count;
    }
    if (count == 0 || linSum < 1e-10) return 0.0f;
    const double geoMean  = std::exp (logSum / count);
    const double arithMean = linSum / count;
    return (float) juce::jlimit (0.0, 1.0, geoMean / arithMean);
}

//==============================================================================
void AudioAnalyzer::detectEnvelope (const float* samples, int n, double sr,
                                     float& outAttack,
                                     float& outDecay,
                                     float& outSustain) const
{
    // RMS window of ~10ms
    const int winSize = juce::jmax (1, (int) (sr * 0.01));

    std::vector<float> rms;
    rms.reserve ((size_t) (n / winSize + 1));

    for (int i = 0; i + winSize <= n; i += winSize)
    {
        float sum = 0.0f;
        for (int j = i; j < i + winSize; ++j) sum += samples[j] * samples[j];
        rms.push_back (std::sqrt (sum / (float) winSize));
    }

    if (rms.empty()) { outAttack = 0.5f; outDecay = 0.5f; outSustain = 0.3f; return; }

    // Peak location
    const auto peakIt = std::max_element (rms.begin(), rms.end());
    const int  peakIdx = (int) std::distance (rms.begin(), peakIt);
    const float peakVal = *peakIt;

    // Attack: frames from start to peak
    const float attackSec = (float) (peakIdx * winSize) / (float) sr;
    outAttack = juce::jlimit (0.0f, 1.0f, attackSec / 1.0f); // 0=instant, 1=1s

    // Sustain: mean RMS of last 30% of signal
    const int sustainStart = (int) ((float) rms.size() * 0.7f);
    float sustainSum = 0.0f;
    int   sustainCount = 0;
    for (int i = sustainStart; i < (int) rms.size(); ++i)
    {
        sustainSum += rms[(size_t) i];
        ++sustainCount;
    }
    outSustain = (sustainCount > 0) ? juce::jlimit (0.0f, 1.0f, sustainSum / (float) sustainCount / (peakVal + 1e-6f))
                                    : 0.3f;

    // Decay: frames from peak until RMS drops to ~50% of peak
    float halfPeak = peakVal * 0.5f;
    int   decayEndIdx = peakIdx;
    for (int i = peakIdx; i < (int) rms.size(); ++i)
    {
        if (rms[(size_t) i] < halfPeak) { decayEndIdx = i; break; }
    }
    const float decaySec = (float) ((decayEndIdx - peakIdx) * winSize) / (float) sr;
    outDecay = juce::jlimit (0.0f, 1.0f, decaySec / 2.0f); // normalized to 2s
}
