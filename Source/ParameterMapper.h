/*
  ==============================================================================
    ParameterMapper.h
    Maps AudioAnalyzer results to synthesizer parameter values.
    Pure stateless logic – no audio I/O, no allocation.
  ==============================================================================
*/

#pragma once
#include "be/AudioAnalyzer.h"

//==============================================================================
struct MappedParameters
{
    int   waveType;          // 0=Sine, 1=Saw, 2=Square, 3=Triangle
    float filterCutoff;      // Hz  (20 – 18000)
    float filterResonance;   // Q   (0.1 – 8.0)
    float attack;            // s   (0.001 – 3.0)
    float decay;             // s   (0.001 – 3.0)
    float sustain;           // 0 – 1
    float release;           // s   (0.001 – 5.0)
    int   noteNumber;        // MIDI note for demo playback
};

//==============================================================================
class ParameterMapper
{
public:
    static MappedParameters mapToParameters (const AnalysisResult& r)
    {
        MappedParameters p;

        // --- Wave type ---------------------------------------------------------
        p.waveType = mapWaveform (r.harmonicRatio, r.spectralFlatness);

        // --- Filter cutoff: spectral centroid → cutoff (log scale) -------------
        // centroid ~200 Hz → cutoff 800 Hz, centroid ~8000 Hz → cutoff 18000 Hz
        p.filterCutoff = juce::jlimit (200.0f, 18000.0f,
                                        r.spectralCentroid * 2.5f);

        // --- Filter resonance: flatness → Q ------------------------------------
        // Tonal signal (low flatness) → lower resonance
        // Noisy signal (high flatness) → higher resonance
        p.filterResonance = juce::jlimit (0.1f, 8.0f,
                                           0.5f + r.spectralFlatness * 7.0f);

        // --- ADSR: from transient and decay characteristics --------------------
        // transientSharpness 0→slow(1.0s), 1→fast(0.001s)
        p.attack  = juce::jlimit (0.001f, 1.0f,
                                   1.0f - r.transientSharpness * 0.999f);

        // decayCurve 0→short, 1→long
        p.decay   = juce::jlimit (0.01f, 2.0f, r.decayCurve * 2.0f);
        p.sustain = juce::jlimit (0.0f,  1.0f, r.sustainLevel);
        p.release = juce::jlimit (0.05f, 3.0f, r.decayCurve * 2.5f);

        // --- Demo note: nearest MIDI note to fundamental ----------------------
        if (r.fundamentalFreq > 20.0f)
            p.noteNumber = juce::jlimit (24, 96,
                                          (int) std::round (
                                              69.0 + 12.0 * std::log2 (
                                                  r.fundamentalFreq / 440.0)));
        else
            p.noteNumber = 60; // C4 default

        return p;
    }

private:
    static int mapWaveform (float harmonicRatio, float flatness)
    {
        // harmonicRatio: 0=even, 1=odd
        // flatness: 0=tonal, 1=noisy
        if (flatness > 0.6f)   return 3; // Triangle – mixed/noisy
        if (harmonicRatio > 0.65f) return 2; // Square – dominant odd
        if (harmonicRatio < 0.35f) return 1; // Saw – dominant even
        return 0;                             // Sine – near pure tone
    }
};
