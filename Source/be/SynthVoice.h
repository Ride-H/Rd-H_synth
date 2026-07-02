/*
  ==============================================================================
    SynthVoice.h
    Rd-H Synth – 16-voice polyphonic voice:
      3× Oscillator (OSC 1-3) + 2-OP FM Engine + White Noise Generator
      + OSCEnv(ADSR) + IIR filter + Unison + Pitch Envelope

    Fixes applied:
      B-1: FM output is no longer multiplied by OSC gainScale (HC-4 compliance).
      C-1: std::pow in render loop eliminated — unison detune multipliers are
           cached; pitch env uses std::exp2f + early-exit when amount==0.
      D-2: MAX_UNISON_VOICES / NUM_OSCS moved into class scope.
  ==============================================================================
*/

#pragma once
#include <JuceHeader.h>
#include "dsp/FMEngine.h"
#include "dsp/NoiseGenerator.h"

//==============================================================================
class RdhSynthSound : public juce::SynthesiserSound
{
public:
    bool appliesToNote    (int) override { return true; }
    bool appliesToChannel (int) override { return true; }
};

//==============================================================================
class RdhSynthVoice : public juce::SynthesiserVoice
{
public:
    static constexpr int MAX_UNISON_VOICES = 4;
    static constexpr int NUM_OSCS          = 3;

    bool canPlaySound (juce::SynthesiserSound* s) override
    {
        return dynamic_cast<RdhSynthSound*> (s) != nullptr;
    }

    //==========================================================================
    void setCurrentPlaybackSampleRate (double newRate) override
    {
        juce::SynthesiserVoice::setCurrentPlaybackSampleRate (newRate);

        // Synthesiser::addVoice() propagates the synth's
        // sample rate to each voice at construction — before prepareToPlay —
        // so this is called with 0. Never push an invalid rate into the child
        // DSP (juce::ADSR::setSampleRate asserts > 0); the real rate arrives
        // when prepareToPlay later calls this again.
        if (newRate <= 0.0)
            return;

        oscEnv.setSampleRate        (newRate);
        pitchEnvelope.setSampleRate (newRate);
        for (auto& f : iirFilter) f.reset();
        fmEngine.prepare  (newRate);
        noiseGen.prepare  (newRate);
    }

    //==========================================================================
    void startNote (int midiNote, float velocity,
                    juce::SynthesiserSound*, int /*pitchWheel*/) override
    {
        baseFreqHz      = juce::MidiMessage::getMidiNoteInHertz (midiNote);
        currentVelocity = velocity;

        for (auto& uniPhases : phase)
            for (auto& p : uniPhases) p = 0.0;

        oscEnv.noteOn();
        pitchEnvelope.noteOn();
        fmEngine.noteOn (velocity, static_cast<float> (baseFreqHz));
        noiseGen.noteOn (velocity);
    }

    void stopNote (float /*velocity*/, bool allowTailOff) override
    {
        if (allowTailOff)
        {
            oscEnv.noteOff();
            pitchEnvelope.noteOff();
            fmEngine.noteOff();
            noiseGen.noteOff();
        }
        else
        {
            oscEnv.reset();
            pitchEnvelope.reset();
            fmEngine.reset();
            noiseGen.reset();
            clearCurrentNote();
        }
    }

    void pitchWheelMoved (int) override {}
    void controllerMoved (int, int) override {}

    //==========================================================================
    void renderNextBlock (juce::AudioBuffer<float>& buffer,
                          int startSample, int numSamples) override
    {
        // S1-2: voice stays alive while ANY envelope (OSC / FM ops / Noise) is active,
        // so long FM/Noise releases are not cut off (D-1 multi-EG lifecycle).
        if (! anyEnvelopeActive()) { clearCurrentNote(); return; }

        const double sr    = getSampleRate();
        const int    numCh = buffer.getNumChannels();

        int activeOscs = 0;
        for (int o = 0; o < NUM_OSCS; ++o)
            if (oscEnabled[o]) ++activeOscs;

        // gainScale normalises OSC mix; 0 if no OSC active.
        // FM output is intentionally NOT multiplied by this (HC-4: FM is independent).
        const float gainScale = (activeOscs > 0)
                                ? 0.18f / static_cast<float> (numUnison * activeOscs)
                                : 0.0f;

        for (int i = 0; i < numSamples; ++i)
        {
            // --- Pitch envelope (C-1: exp2f + early-exit when amount == 0) ---
            const float pitchEnvSamp = pitchEnvelope.getNextSample();
            double modFreq = baseFreqHz;
            if (pitchEnvAmountSemitones != 0.0f)
                modFreq *= static_cast<double> (
                    std::exp2f (pitchEnvSamp * pitchEnvAmountSemitones / 12.0f));

            // --- OSC (unison) — cached detuneMultiplier avoids per-sample std::pow ---
            float oscSum = 0.0f;
            for (int u = 0; u < numUnison; ++u)
            {
                const double freq = modFreq * uniDetuneMultiplier[u]; // C-1 fix

                for (int o = 0; o < NUM_OSCS; ++o)
                {
                    if (!oscEnabled[o]) continue;
                    oscSum      += generateSample (phase[u][o], waveTypes[o]);
                    phase[u][o] += freq / sr;
                    if (phase[u][o] >= 1.0) phase[u][o] -= 1.0;
                }
            }

            // --- OSC gain envelope ---
            const float envVal = oscEnv.getNextSample();

            // --- FM (B-1: NOT multiplied by gainScale or oscEnv; has own EG) ---
            const float fmSamp = fmEngine.processSample();

            // --- Noise ---
            const float noiseRaw      = noiseGen.processSample();
            const float noiseToFilter = noiseRaw * noiseGen.getFilterSend();
            const float noiseDirect   = noiseRaw * noiseGen.getDirectOut();

            // --- Signal path ---
            // OSC × oscEnv(gain) → Mixer → Filter
            // FM: independent EG, no oscEnv scaling  ← B-1
            // Noise filterSend: own EG inside NoiseGen, no oscEnv scaling
            const float toFilter = oscSum * gainScale * envVal * currentVelocity
                                   + fmSamp
                                   + noiseToFilter;

            const float filtered = iirFilter[0].processSingleSampleRaw (toFilter);

            // Noise direct-out bypasses filter
            const float finalOut = filtered + noiseDirect;

            for (int ch = 0; ch < numCh; ++ch)
                buffer.addSample (ch, startSample + i, finalOut);
        }

        if (! anyEnvelopeActive())
            clearCurrentNote();
    }

    //==========================================================================
    // OSC / ADSR / Filter setters (called from processBlock, RT-safe)
    //==========================================================================
    void setOscillators (int w1, int w2, bool en2, int w3, bool en3)
    {
        waveTypes[0] = w1;
        waveTypes[1] = w2;  oscEnabled[1] = en2;
        waveTypes[2] = w3;  oscEnabled[2] = en3;
    }

    void setNumUnison (int n)
    {
        numUnison = juce::jlimit (1, MAX_UNISON_VOICES, n);
        rebuildDetuneMultipliers();
    }

    void setUnisonDetune (float c)
    {
        unisonDetuneCents = c;
        rebuildDetuneMultipliers();
    }

    void setPitchEnvAmount (float s) { pitchEnvAmountSemitones = s; }

    void setPitchEnvAttack (float attackSec)
    {
        juce::ADSR::Parameters p { attackSec, 0.05f, 0.0f, 0.05f };
        pitchEnvelope.setParameters (p);
    }

    void setADSRParameters (const juce::ADSR::Parameters& p) { oscEnv.setParameters (p); }

    void setFilterCoefficients (const juce::IIRCoefficients& c)
    {
        for (auto& f : iirFilter) f.setCoefficients (c);
    }

    //==========================================================================
    // FM setters
    //==========================================================================
    void setFMEnabled        (bool e)  { fmEngine.setEnabled (e); }
    void setFMOutputLevel    (float l) { fmEngine.setOutputLevel (l); }
    void setFMAlgorithm      (int a)   { fmEngine.setAlgorithm (a); }
    void setFMFeedback       (float f) { fmEngine.setFeedback (f); }
    void setFMOperatorParams (int idx, const rdh::synth::OperatorParams& p)
    {
        fmEngine.setOperatorParams (idx, p);
    }

    //==========================================================================
    // Noise setters
    //==========================================================================
    void setNoiseEnabled    (bool  e) { noiseGen.setEnabled (e); }
    void setNoiseType       (int   t) { noiseGen.setType (t); }
    void setNoiseLevel      (float l) { noiseGen.setLevel (l); }
    void setNoiseFilterSend (float s) { noiseGen.setFilterSend (s); }
    void setNoiseDirectOut  (float d) { noiseGen.setDirectOut (d); }
    void setNoiseEGParams   (float a, float d, float s, float r)
    {
        noiseGen.setEGParams (a, d, s, r);
    }

private:
    //==========================================================================
    // S1-2: voice is active while any sub-envelope (OSC / FM / Noise) still rings.
    // NB: deliberately NOT named isVoiceActive() — that is a juce::SynthesiserVoice
    // virtual and overriding it would change the Synthesiser's voice-allocation logic.
    bool anyEnvelopeActive() const noexcept
    {
        return oscEnv.isActive()
            || fmEngine.isActive()
            || (noiseGen.isEnabled() && noiseGen.isActive());
    }

    //==========================================================================
    // C-1: Rebuild cached per-unison detune multipliers (called only on param change).
    void rebuildDetuneMultipliers()
    {
        for (int u = 0; u < numUnison; ++u)
        {
            const double t = (numUnison > 1)
                             ? 2.0 * u / static_cast<double> (numUnison - 1) - 1.0
                             : 0.0;
            uniDetuneMultiplier[u] = std::pow (2.0, t * unisonDetuneCents / 1200.0);
        }
    }

    //==========================================================================
    float generateSample (double p, int type) const noexcept
    {
        switch (type)
        {
            case 0: return static_cast<float> (std::sin (juce::MathConstants<double>::twoPi * p));
            case 1: return static_cast<float> (2.0 * p - 1.0);
            case 2: return p < 0.5 ? 1.0f : -1.0f;
            case 3: return static_cast<float> (std::abs (2.0 * p - 1.0) * 2.0 - 1.0);
            default: return 0.0f;
        }
    }

    //==========================================================================
    double phase[MAX_UNISON_VOICES][NUM_OSCS] = {};
    double uniDetuneMultiplier[MAX_UNISON_VOICES] = { 1.0, 1.0, 1.0, 1.0 }; // C-1
    double baseFreqHz              = 440.0;
    float  currentVelocity         = 1.0f;
    int    waveTypes[NUM_OSCS]     = { 1, 1, 2 };
    bool   oscEnabled[NUM_OSCS]    = { true, false, false };
    int    numUnison               = 1;
    float  unisonDetuneCents       = 10.0f;
    float  pitchEnvAmountSemitones = 0.0f;

    juce::ADSR      oscEnv;
    juce::ADSR      pitchEnvelope;
    juce::IIRFilter iirFilter[1];

    rdh::synth::FMEngine       fmEngine;
    rdh::synth::NoiseGenerator noiseGen;
};
