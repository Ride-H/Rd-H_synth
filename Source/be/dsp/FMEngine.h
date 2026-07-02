/*
  ==============================================================================
    FMEngine.h
    DX-style FM engine.
      Phase 1: 2-OP, fixed OP2(mod) → OP1(carrier).
      Phase 2 (Sprint 1): 4-OP, 4 selectable algorithms, OP4 feedback,
                          wavetable oscillator, Key Scaling, C-4 ADSR guard.
    Anti-aliasing: 4× oversampling at ≤48 kHz, 2× at ≤96 kHz, 1× above.
    Namespace: rdh::synth (REQ-COMPAT-019).
    Per-operator FM voice engine (S1-1 / S1-3).
  ==============================================================================
*/

#pragma once
#include <JuceHeader.h>
#include "WaveTable.h"
#include <array>
#include <cmath>

namespace rdh::synth {

//==============================================================================
struct OperatorParams
{
    float ratio    = 1.0f;   // 0.50 – 32.00
    float detune   = 0.0f;   // cents, -100 – +100
    float level    = 0.5f;   // 0.0 – 1.0 (output amplitude / modulation index)
    float attack   = 0.001f; // seconds
    float decay    = 0.2f;
    float sustain  = 0.5f;
    float release  = 0.3f;
    float velSens  = 0.5f;   // velocity sensitivity 0.0 – 1.0
    float keyScale = 0.0f;   // S1-3: level key scaling 0.0 (off) – 1.0
};

//==============================================================================
// Single FM Operator: wavetable sine oscillator + independent ADSR.
// Output (scaled by env·velocity·level·keyScale) doubles as the modulation
// signal fed into other operators' phase (in cycles). ADSR runs at the
// oversampled rate so time constants are preserved.
//==============================================================================
// Hot-only per-op state. The cold config (OperatorParams + C-4
// guard) lives in FMEngine::opCold so the per-sample hot footprint is smaller and
// the active operators pack into fewer cache lines (in-host cold-cache mitigation).
// Arithmetic in process() is unchanged → bit-exact (verified by RdhRender PCM MD5).
class FMOperator
{
public:
    void prepare (double osRate)
    {
        adsr.setSampleRate (osRate);
        reset();
    }

    // Cold path (FMEngine, message thread): push ADSR config when it changes.
    void setADSR (const juce::ADSR::Parameters& ap) { adsr.setParameters (ap); }

    // Level/keyScale → precomputed hot gain (bit-exact = level * keyScaleFactor).
    // Called from FMEngine::setOperatorParams so per-block level automation is
    // preserved (levelKey re-synced whenever level changes).
    void setLevel (float level) noexcept { levelVal = level; levelKey = levelVal * keyScaleFactor; }

    void noteOnHot (float vel, double phaseIncrement, float keyScaleFac, float level) noexcept
    {
        velocity       = vel;
        phaseInc       = phaseIncrement;
        keyScaleFactor = keyScaleFac;
        levelVal       = level;
        levelKey       = levelVal * keyScaleFactor;   // bit-exact: level * keyScale
        phase          = 0.0;
        prevOut1 = prevOut2 = 0.0f;
        adsr.noteOn();
    }

    void noteOff() { adsr.noteOff(); }
    void reset()   { adsr.reset(); phase = 0.0; prevOut1 = prevOut2 = 0.0f; }
    bool isActive() const { return adsr.isActive(); }

    float prevAvg() const noexcept { return (prevOut1 + prevOut2) * 0.5f; }

    // Unified process: modCycles = phase modulation input in cycles (incl. feedback).
    float process (float modCycles) noexcept
    {
        const float env = adsr.getNextSample();
        const float s   = wtSine (wrap01 (static_cast<float> (phase) + modCycles));
        advancePhase();
        const float o   = s * env * velocity * levelKey;   // = velocity*(level*keyScale): bit-exact
        prevOut2 = prevOut1;
        prevOut1 = o;
        return o;
    }

private:
    void advancePhase() noexcept
    {
        phase += phaseInc;
        if (phase >= 1.0) phase -= 1.0;
    }

    juce::ADSR adsr;
    double     phase          = 0.0;
    double     phaseInc       = 0.0;
    float      velocity       = 1.0f;
    float      keyScaleFactor = 1.0f;
    float      levelVal       = 0.5f;   // mirror of OperatorParams::level (per-block automation)
    float      levelKey       = 0.5f;   // = levelVal * keyScaleFactor (per-sample gain)
    float      prevOut1       = 0.0f;
    float      prevOut2       = 0.0f;
};

//==============================================================================
// FMEngine – 4-OP matrix-based engine. Default state reproduces Phase 1
// (2-OP OP2→OP1) so existing v2.0 FM presets sound unchanged.
//==============================================================================
class FMEngine
{
public:
    static constexpr int MAX_OPERATORS = 6;   // Phase 3 headroom
    static constexpr int FB_OP         = 3;   // OP4 carries the single feedback loop

    FMEngine() { initPhase1Default(); }

    void prepare (double baseSR)
    {
        baseSampleRate = baseSR;
        osRatio = (baseSR <= 50000.0) ? 4 : (baseSR <= 100000.0) ? 2 : 1;
        const double osRate = baseSR * osRatio;
        for (auto& op : ops)
            op.prepare (osRate);
    }

    void noteOn (float vel, float baseHz)
    {
        const double osRate = baseSampleRate * osRatio;
        // Per-note hot values are computed here from the cold params,
        // using the same formulas as before → bit-exact.
        const float note     = 69.0f + 12.0f * std::log2 (juce::jmax (1.0e-3f, baseHz) / 440.0f);
        const float ks       = juce::jlimit (-1.0f, 1.0f, (note - 60.0f) / 48.0f);
        for (int i = 0; i < activeOpCount; ++i)
        {
            const OperatorParams& p = opCold[(size_t) i].params;
            const float  velScaled  = 1.0f - p.velSens * (1.0f - vel);
            const double det        = std::pow (2.0, p.detune / 1200.0);
            const double inc        = baseHz * (double) p.ratio * det / osRate;
            const float  keyScale   = juce::jmax (0.0f, 1.0f - p.keyScale * ks);
            ops[(size_t) i].noteOnHot (velScaled, inc, keyScale, p.level);
        }
    }

    void noteOff()
    {
        for (int i = 0; i < activeOpCount; ++i)
            ops[(size_t) i].noteOff();
    }

    void reset()
    {
        for (auto& op : ops) op.reset();
    }

    bool isActive() const
    {
        if (! enabled) return false;
        for (int i = 0; i < activeOpCount; ++i)
            if (ops[(size_t) i].isActive()) return true;
        return false;
    }

    bool  isEnabled()           const { return enabled; }
    void  setEnabled (bool e)         { enabled = e; }
    void  setOutputLevel (float l)    { outputLevel = l; }
    void  setFeedback (float f)       { feedbackAmount = juce::jlimit (0.0f, 1.0f, f); }

    void setOperatorParams (int idx, const OperatorParams& p)
    {
        if (idx < 0 || idx >= MAX_OPERATORS) return;
        OperatorCold& c = opCold[(size_t) idx];
        c.params = p;
        ops[(size_t) idx].setLevel (p.level);   // per-block level automation → hot levelKey

        // C-4 guard (D-03): only re-push ADSR when its fields change.
        if (! juce::exactlyEqual (p.attack,  c.lastA) || ! juce::exactlyEqual (p.decay,   c.lastD)
         || ! juce::exactlyEqual (p.sustain, c.lastS) || ! juce::exactlyEqual (p.release, c.lastR))
        {
            juce::ADSR::Parameters ap;
            ap.attack  = juce::jmax (0.001f, p.attack);
            ap.decay   = juce::jmax (0.001f, p.decay);
            ap.sustain = p.sustain;
            ap.release = juce::jmax (0.001f, p.release);
            ops[(size_t) idx].setADSR (ap);
            c.lastA = p.attack; c.lastD = p.decay; c.lastS = p.sustain; c.lastR = p.release;
        }
    }

    // S1-3: select one of the 4 Phase-2 algorithms (REQ-FM-007). Builds the
    // connection matrix / carrier flags / process order. Called on the message
    // thread; no per-sample topological work (REQ-FM-011).
    void setAlgorithm (int id) noexcept
    {
        clearTopology();
        activeOpCount = 4;
        // process order: modulators (OP4..) before carriers — valid for all below
        processOrder[0] = 3; processOrder[1] = 2; processOrder[2] = 1; processOrder[3] = 0;
        orderCount = 4;

        switch (id)
        {
            case 1: // 2並列: OP4→OP2, OP3→OP1
                algorithm[3][1] = true; algorithm[2][0] = true;
                isCarrier[0] = true; isCarrier[1] = true;
                break;
            case 2: // 2-to-1集約: OP4→OP2, OP3→OP2, OP2→OP1
                algorithm[3][1] = true; algorithm[2][1] = true; algorithm[1][0] = true;
                isCarrier[0] = true;
                break;
            case 3: // 加算: 全OPキャリア・変調なし
                isCarrier[0] = isCarrier[1] = isCarrier[2] = isCarrier[3] = true;
                break;
            case 0: // 直列(Chain): OP4→OP3→OP2→OP1
            default:
                algorithm[3][2] = true; algorithm[2][1] = true; algorithm[1][0] = true;
                isCarrier[0] = true;
                break;
        }
    }

    // Returns one audio-rate output sample (oversampled internally).
    float processSample() noexcept
    {
        if (! enabled) return 0.0f;
        float acc = 0.0f;
        for (int k = 0; k < osRatio; ++k)
        {
            float out[MAX_OPERATORS] = {};
            for (int oi = 0; oi < orderCount; ++oi)
            {
                const int op = processOrder[oi];
                float modInput = 0.0f;
                for (int m = 0; m < activeOpCount; ++m)
                    if (algorithm[(size_t) m][(size_t) op]) modInput += out[(size_t) m];

                const float fb = (op == FB_OP) ? feedbackAmount * ops[(size_t) op].prevAvg() : 0.0f;
                out[(size_t) op] = ops[(size_t) op].process (modInput + fb);
            }

            float carrierSum = 0.0f;
            for (int c = 0; c < activeOpCount; ++c)
                if (isCarrier[(size_t) c]) carrierSum += out[(size_t) c];
            acc += carrierSum;
        }
        return (acc / static_cast<float> (osRatio)) * outputLevel;
    }

private:
    void clearTopology() noexcept
    {
        for (auto& row : algorithm) row.fill (false);
        for (auto& c : isCarrier)   c = false;
    }

    void initPhase1Default() noexcept
    {
        clearTopology();
        activeOpCount = 2;
        algorithm[1][0] = true;       // OP2 → OP1
        isCarrier[0]    = true;       // OP1 carrier
        processOrder[0] = 1; processOrder[1] = 0;
        orderCount = 2;
    }

    std::array<FMOperator, MAX_OPERATORS> ops;            // hot per-sample state
    // Cold config, referenced only on the message thread (setOperatorParams / noteOn).
    // Separated from the hot FMOperator so the per-sample working set is smaller.
    struct OperatorCold
    {
        OperatorParams params;
        float lastA = -1.0f, lastD = -1.0f, lastS = -1.0f, lastR = -1.0f; // C-4 guard cache
    };
    std::array<OperatorCold, MAX_OPERATORS> opCold;
    std::array<std::array<bool, MAX_OPERATORS>, MAX_OPERATORS> algorithm {}; // [mod][carrier]
    std::array<bool, MAX_OPERATORS> isCarrier {};
    int    processOrder[MAX_OPERATORS] = { 1, 0, 0, 0, 0, 0 };
    int    orderCount     = 2;
    int    activeOpCount  = 2;
    bool   enabled        = false;
    float  outputLevel    = 1.0f;
    float  feedbackAmount = 0.0f;
    int    osRatio        = 4;
    double baseSampleRate = 44100.0;
};

} // namespace rdh::synth
