/*
  ==============================================================================
    SpectrumAnalyzerComponent.cpp
  ==============================================================================
*/

#include "SpectrumAnalyzerComponent.h"
#include "../PluginProcessor.h"
#include "CoolUiSkin.h"

SpectrumAnalyzerComponent::SpectrumAnalyzerComponent (RdhSynthAudioProcessor& p)
    : processor (p)
{
    barLevels.fill (0.0f);
    startTimerHz (TIMER_HZ);
}

SpectrumAnalyzerComponent::~SpectrumAnalyzerComponent()
{
    stopTimer();
}

//==============================================================================
void SpectrumAnalyzerComponent::timerCallback()
{
    // Pull available samples from the processor FIFO into inputBuf
    auto& fifo   = processor.spectrumFifo;
    auto& srcBuf = processor.spectrumFifoBuffer;

    int start1, size1, start2, size2;
    fifo.prepareToRead (fifo.getNumReady(), start1, size1, start2, size2);

    auto copyChunk = [&] (int start, int size)
    {
        for (int i = 0; i < size; ++i)
        {
            inputBuf[(size_t) inputFillPos] = srcBuf[(size_t) (start + i)];
            if (++inputFillPos >= FFT_SIZE)
            {
                // Buffer full: run FFT
                fftWorkBuf.fill (0.0f);

                // Hann window
                for (int j = 0; j < FFT_SIZE; ++j)
                {
                    const float w = 0.5f - 0.5f * std::cos (
                        juce::MathConstants<float>::twoPi * (float) j / (float) (FFT_SIZE - 1));
                    fftWorkBuf[(size_t) j] = inputBuf[(size_t) j] * w;
                }

                fft.performFrequencyOnlyForwardTransform (fftWorkBuf.data());

                // Map bins to NUM_BARS (logarithmic)
                const int halfBins = FFT_SIZE / 2;
                for (int b = 0; b < NUM_BARS; ++b)
                {
                    const float lo = std::pow (2.0f, (float) b       / NUM_BARS * 10.0f) - 1.0f;
                    const float hi = std::pow (2.0f, (float)(b + 1)  / NUM_BARS * 10.0f) - 1.0f;
                    const int binLo = juce::jlimit (0, halfBins - 1, (int) (lo / 1023.0f * (float) halfBins));
                    const int binHi = juce::jlimit (0, halfBins - 1, (int) (hi / 1023.0f * (float) halfBins));

                    float peak = 0.0f;
                    for (int k = binLo; k <= binHi; ++k)
                        peak = juce::jmax (peak, fftWorkBuf[(size_t) k]);

                    // dB conversion, normalized to 0-1
                    const float dB    = (peak > 1e-6f) ? 20.0f * std::log10 (peak) : -80.0f;
                    const float level = juce::jlimit (0.0f, 1.0f, (dB + 80.0f) / 80.0f);

                    // Smooth decay
                    barLevels[(size_t) b] = juce::jmax (level, barLevels[(size_t) b] * 0.85f);
                }

                inputFillPos = 0;
                dataReady    = true;
            }
        }
    };

    if (size1 > 0) copyChunk (start1, size1);
    if (size2 > 0) copyChunk (start2, size2);
    fifo.finishedRead (size1 + size2);

    if (dataReady) repaint();
}

//==============================================================================
void SpectrumAnalyzerComponent::paint (juce::Graphics& g)
{
    g.setColour (Colors::panel);
    g.fillRoundedRectangle (getLocalBounds().toFloat(), 4.0f);

    g.setColour (Colors::panelBorder);
    g.drawRoundedRectangle (getLocalBounds().toFloat().reduced (0.5f), 4.0f, 1.0f);

    drawSpectrumBars    (g);
    drawFrequencyLabels (g);

    // Title label
    g.setColour (Colors::inactive);
    g.setFont (juce::Font (juce::FontOptions (10.0f)));
    g.drawText ("SPECTRUM", getLocalBounds().removeFromTop (14),
                juce::Justification::centred);
}

void SpectrumAnalyzerComponent::drawSpectrumBars (juce::Graphics& g)
{
    const float w      = (float) getWidth();
    // Reserve 14px top (title) + 14px bottom (freq labels)
    const float h      = (float) getHeight() - 28.0f;
    const float barW   = w / (float) NUM_BARS;
    const float yBase  = (float) getHeight() - 14.0f;

    for (int b = 0; b < NUM_BARS; ++b)
    {
        const float level  = barLevels[(size_t) b];
        const float barH   = level * h;
        const float bx     = (float) b * barW;

        // Color: cyan at low levels, purple at high
        const juce::Colour barColor = Colors::accent1.interpolatedWith (
            Colors::accent2, juce::jmin (1.0f, level * 1.5f));

        g.setColour (barColor.withAlpha (0.8f));
        g.fillRect (bx + 1.0f, yBase - barH, barW - 2.0f, barH);

        // Glow top
        if (level > 0.05f)
        {
            g.setColour (barColor.withAlpha (0.4f));
            g.fillRect (bx + 1.0f, yBase - barH - 2.0f, barW - 2.0f, 3.0f);
        }
    }
}

void SpectrumAnalyzerComponent::drawFrequencyLabels (juce::Graphics& g)
{
    const float w         = (float) getWidth();
    const float yGridTop  = 14.0f;                        // below title
    const float yGridBot  = (float) getHeight() - 14.0f; // above freq labels
    const float yLabel    = yGridBot + 1.0f;
    const float labelH    = 12.0f;

    const double sr       = (processor.getSampleRate() > 100.0) ? processor.getSampleRate() : 44100.0;
    const float  halfBins = (float) (FFT_SIZE / 2);
    const float  barW     = w / (float) NUM_BARS;

    // Convert a frequency (Hz) to the x pixel position on the spectrum.
    auto freqToX = [&] (float freq) -> float
    {
        const float bin  = freq * (float) FFT_SIZE / (float) sr;
        const float loV  = bin / halfBins * 1023.0f;
        const float bIdx = (float) NUM_BARS * std::log2 (juce::jmax (1.0f, loV + 1.0f)) / 10.0f;
        return juce::jlimit (0.0f, w, bIdx * barW);
    };

    struct Marker { float freq; const char* label; };
    const Marker markers[] = {
        { 100.0f,   "100"  },
        { 500.0f,   "500"  },
        { 1000.0f,  "1k"   },
        { 5000.0f,  "5k"   },
        { 10000.0f, "10k"  },
        { 20000.0f, "20k"  },
    };

    g.setFont (juce::Font (juce::FontOptions (8.0f)));

    for (const auto& m : markers)
    {
        const float x = freqToX (m.freq);
        if (x < 6.0f || x > w - 6.0f) continue;

        // Vertical grid line (very subtle)
        g.setColour (Colors::inactive.withAlpha (0.18f));
        g.drawVerticalLine ((int) x, yGridTop, yGridBot);

        // Frequency text
        g.setColour (Colors::textColor.withAlpha (0.45f));
        g.drawText (m.label,
                    juce::Rectangle<float> (x - 14.0f, yLabel, 28.0f, labelH),
                    juce::Justification::centred, false);
    }
}

void SpectrumAnalyzerComponent::resized() {}
