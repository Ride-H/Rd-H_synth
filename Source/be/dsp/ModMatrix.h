/*
  ==============================================================================
    ModMatrix.h
    Rd-H Synth — 16-slot modulation matrix.

    Instance-level, evaluated once per block by RdhSynthAudioProcessor
    (block-rate by design). It never touches the audio signal itself — it only
    produces offsets that the processor applies to the *effective* parameter
    values before distributing them to voices. With no slot enabled the
    processor skips evaluate() entirely (zero cost when unused).

    Aggregated modulation is clamped to ±1 per destination BEFORE scaling
    so stacked slots cannot run away. Scales per destination:
    Cutoff ±4 oct, Reso ±5.0, Pitch ±24 st,
    Volume ×(1+mod) (clamped by the processor), FM OP Level ±1 (add, clamp01
    at application), FM OP Ratio ±1 oct.
    
  ==============================================================================
*/

#pragma once
#include <JuceHeader.h>
#include <cmath>

namespace rdh::synth {

//==============================================================================
struct ModSources
{
    float lfo1 = 0.0f, lfo2 = 0.0f;            // bipolar −1..+1 (× LFO depth)
    float velocity = 0.0f;                      // last-note velocity 0..1 (instance-level matrix)
    float aftertouch = 0.0f;                    // channel pressure 0..1
    float modWheel = 0.0f;                      // CC1 0..1
};

struct ModOffsets
{
    float cutoffOct     = 0.0f;                 // ±4 oct
    float resoAdd       = 0.0f;                 // ±5.0
    float pitchSemis    = 0.0f;                 // ±24 st (voice-wide: OSC + FM)
    float volScale      = 1.0f;                 // ×(1+mod); processor clamps 0..1
    float opLevelAdd[4] = {};                   // ±1, clamp01 at application
    float opRatioOct[4] = {};                   // ±1 oct
    bool  filterActive  = false;                // any Cutoff/Reso slot contributed
    bool  any           = false;
};

//==============================================================================
class ModMatrix
{
public:
    static constexpr int NUM_SLOTS = 16;

    enum Source      { SrcLFO1 = 0, SrcLFO2, SrcVelocity, SrcAfterTouch, SrcModWheel };
    enum Destination { DstOff = 0, DstCutoff, DstReso, DstPitch, DstVolume,
                       DstOp1Level, DstOp2Level, DstOp3Level, DstOp4Level,
                       DstOp1Ratio, DstOp2Ratio, DstOp3Ratio, DstOp4Ratio,
                       NUM_DESTS };

    struct Slot
    {
        int   source = SrcLFO1;
        int   dest   = DstOff;
        float amount = 0.0f;                    // −1..+1
        int   curve  = 0;                       // 0=Linear, 1=Exp(γ2), 2=Log(γ0.5)
        bool  enable = false;
    };

    Slot slots[NUM_SLOTS];

    ModOffsets evaluate (const ModSources& src) const noexcept
    {
        float agg[NUM_DESTS] = {};
        bool  any = false, filterTouched = false;

        for (const auto& s : slots)
        {
            // |amount| < 1e-4 is the 0 detent: the parameter grid is 0.001 and
            // the raw default is ≈4.7e-8, not 0 (float error of the quantised range).
            if (! s.enable || s.dest <= DstOff || s.dest >= NUM_DESTS
                || std::abs (s.amount) < 1.0e-4f)
                continue;

            const float v = applyCurve (pick (src, s.source), s.curve);
            agg[s.dest] += v * s.amount;
            any = true;
            if (s.dest == DstCutoff || s.dest == DstReso)
                filterTouched = true;
        }

        ModOffsets o;
        o.any = any;
        if (! any)
            return o;

        auto c1 = [] (float x) { return juce::jlimit (-1.0f, 1.0f, x); }; // per-dest clamp

        o.cutoffOct  = c1 (agg[DstCutoff]) * 4.0f;
        o.resoAdd    = c1 (agg[DstReso])   * 5.0f;
        o.pitchSemis = c1 (agg[DstPitch])  * 24.0f;
        o.volScale   = 1.0f + c1 (agg[DstVolume]);
        for (int i = 0; i < 4; ++i)
        {
            o.opLevelAdd[i] = c1 (agg[DstOp1Level + i]);
            o.opRatioOct[i] = c1 (agg[DstOp1Ratio + i]);
        }
        o.filterActive = filterTouched;
        return o;
    }

private:
    static float pick (const ModSources& s, int source) noexcept
    {
        switch (source)
        {
            case SrcLFO1:       return s.lfo1;
            case SrcLFO2:       return s.lfo2;
            case SrcVelocity:   return s.velocity;
            case SrcAfterTouch: return s.aftertouch;
            case SrcModWheel:   return s.modWheel;
            default:            return 0.0f;
        }
    }

    // Sign-preserving curve: sign(x)·|x|^γ (bipolar sources keep polarity).
    static float applyCurve (float x, int curve) noexcept
    {
        if (curve == 0 || x == 0.0f) return x;                      // Linear
        const float a = std::abs (x);
        const float shaped = (curve == 1) ? a * a                   // Exp (γ=2)
                                          : std::sqrt (a);          // Log (γ=0.5)
        return x > 0.0f ? shaped : -shaped;
    }
};

} // namespace rdh::synth
