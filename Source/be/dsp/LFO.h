/*
  ==============================================================================
    LFO.h
    Rd-H Synth — instance-level LFO (modulation system).

    Owned by RdhSynthAudioProcessor (NOT per-voice): evaluation is block-rate
    (block-rate by design) — advanceBlock() returns the value at the block start and
    steps the phase by the whole block. Audible stair-stepping above ~15 Hz is
    a documented trade-off of the block-rate design; audible destinations
    are additionally smoothed at the consumer.

    S&H uses xorshift128+ with a per-instance seed (multi-instance safety,
    same scheme as NoiseGenerator). Sync division mapping is done by the caller (processor),
    which passes the final frequency in Hz.
    
  ==============================================================================
*/

#pragma once
#include <JuceHeader.h>
#include <atomic>
#include <cmath>

namespace rdh::synth {

//==============================================================================
class LFO
{
public:
    enum Wave { Sine = 0, Tri, Saw, Square, SampleHold };

    LFO()
    {
        // Per-instance seed (multi-instance independence): ticks ^ instance counter ^ object address.
        static std::atomic<uint64_t> counter { 0 };
        const auto inst  = counter.fetch_add (1, std::memory_order_relaxed);
        const auto ticks = (uint64_t) juce::Time::getHighResolutionTicks();
        state[0] = ticks ^ (inst << 32) ^ (uint64_t) (uintptr_t) this;
        state[1] = (ticks * 6364136223846793005ULL) ^ ~inst;
        if (state[0] == 0 && state[1] == 0) state[1] = 0x9E3779B97F4A7C15ULL;
        shValue = nextBipolar();
    }

    // Sample-rate change support: phase-increment conversion uses the
    // current sample rate; nothing else to cache.
    void prepare (double sr) { if (sr > 0.0) sampleRate = sr; }

    void setDepth (float d)                 { depth = d; }
    void setWave  (int w)                   { wave = juce::jlimit (0, 4, w); }
    void setPhaseOffsetDegrees (float deg)  { phaseOffset01 = (double) deg / 360.0; }

    // Retrig: called by the processor when a noteOn breaks silence
    // (all voices idle). Chord/legato noteOns while ringing do NOT reset.
    void resetPhase() { phase = 0.0; }

    // Advance by one block; returns the block-start value × depth (−1..+1).
    float advanceBlock (int numSamples, double hz) noexcept
    {
        const float v = valueAt (wrap01 (phase + phaseOffset01));

        phase += hz * (double) numSamples / sampleRate;
        if (phase >= 1.0)
        {
            phase -= std::floor (phase);
            if (wave == SampleHold)          // one hold per wrap (block-rate S&H)
                shValue = nextBipolar();
        }
        return v * depth;
    }

private:
    static double wrap01 (double p) noexcept
    {
        return p - std::floor (p);
    }

    float valueAt (double p) const noexcept
    {
        switch (wave)
        {
            case Sine:       return (float) std::sin (juce::MathConstants<double>::twoPi * p);
            case Tri:        return (float) (p < 0.5 ? 4.0 * p - 1.0 : 3.0 - 4.0 * p);
            case Saw:        return (float) (2.0 * p - 1.0);
            case Square:     return p < 0.5 ? 1.0f : -1.0f;
            case SampleHold: return shValue;
            default:         return 0.0f;
        }
    }

    // xorshift128+ (same generator family as NoiseGenerator, per-instance).
    uint64_t nextRaw() noexcept
    {
        uint64_t x = state[0];
        const uint64_t y = state[1];
        state[0] = y;
        x ^= x << 23;
        state[1] = x ^ y ^ (x >> 17) ^ (y >> 26);
        return state[1] + y;
    }

    float nextBipolar() noexcept
    {
        return (float) ((double) (nextRaw() >> 11) / (double) (1ULL << 53)) * 2.0f - 1.0f;
    }

    double   sampleRate    = 44100.0;
    double   phase         = 0.0;
    double   phaseOffset01 = 0.0;
    float    depth         = 1.0f;
    int      wave          = Sine;
    float    shValue       = 0.0f;
    uint64_t state[2]      = {};
};

} // namespace rdh::synth
