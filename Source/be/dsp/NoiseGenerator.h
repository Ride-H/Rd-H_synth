/*
  ==============================================================================
    NoiseGenerator.h
    White noise generator using xorshift128+ RNG with independent per-instance
    seeds (HC-2 compliant). ADSR envelope. FilterSend / DirectOut routing.
    Phase 1: White noise only. Pink/Brown added in Phase 2.
  ==============================================================================
*/

#pragma once
#include <JuceHeader.h>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <limits>

#if defined(__APPLE__) || defined(__linux__)
#  include <unistd.h>
#else
#  include <process.h>
#  define getpid _getpid
#endif

namespace rdh::synth {

//==============================================================================
class NoiseGenerator
{
public:
    NoiseGenerator()
    {
        // Seed: time ^ pid ^ instanceCounter
        // HC-2: only g_instanceCounter is static; it is increment-only (no shared mutable state).
        const uint64_t t    = static_cast<uint64_t> (
            std::chrono::high_resolution_clock::now().time_since_epoch().count());
        const uint64_t pid  = static_cast<uint64_t> (getpid());
        const uint64_t inst = g_instanceCounter.fetch_add (1, std::memory_order_relaxed);

        // FNV-style mix to spread the bits
        state[0] = t   ^ (pid  << 17) ^ (inst << 37);
        state[1] = (t  >> 32) ^ (pid * 6364136223846793005ULL)
                              ^ (inst * 1442695040888963407ULL);

        // Warm up to discard weak initial values
        for (int i = 0; i < 64; ++i) nextRaw();
    }

    void prepare (double sr)
    {
        sampleRate = sr;
        adsr.setSampleRate (sr);
        applyADSRParams();
        computeColorCoeffs (sr);   // SR-compensate Pink/Brown coefficients
    }

    void noteOn (float vel)
    {
        velocity = vel;
        adsr.noteOn();
    }

    void noteOff() { adsr.noteOff(); }
    void reset()
    {
        adsr.reset();
        for (auto& b : pink_b) b = 0.0f;   // clear colored-noise state
        brownState = 0.0f;
    }

    bool isActive()  const { return adsr.isActive(); }
    bool isEnabled() const { return enabled; }

    // Returns one audio-rate noise sample (White/Pink/Brown) with noise EG applied.
    float processSample() noexcept
    {
        if (!enabled) return 0.0f;
        return generateColored() * adsr.getNextSample() * velocity * level;
    }

    //--- parameter setters ---------------------------------------------------
    void setEnabled     (bool  e) { enabled = e; }
    void setType        (int   t) { noiseType = juce::jlimit (0, 2, t); } // 0=White,1=Pink,2=Brown
    void setLevel       (float l) { level   = juce::jlimit (0.0f, 1.0f, l); }
    void setFilterSend  (float s) { filterSend = juce::jlimit (0.0f, 1.0f, s); }
    void setDirectOut   (float d) { directOut  = juce::jlimit (0.0f, 1.0f, d); }

    void setEGParams (float a, float d, float s, float r)
    {
        egAttack  = a;  egDecay  = d;
        egSustain = s;  egRelease = r;
        applyADSRParams();
    }

    //--- routing info (read by voice renderer) --------------------------------
    float getFilterSend() const { return filterSend; }
    float getDirectOut()  const { return directOut;  }

private:
    // HC-2: only static is the instance counter (read-only after init).
    inline static std::atomic<uint64_t> g_instanceCounter { 0 };

    uint64_t nextRaw() noexcept
    {
        uint64_t x = state[0], y = state[1];
        state[0] = y;
        x ^= x << 23;
        state[1] = x ^ y ^ (x >> 17) ^ (y >> 26);
        return state[1] + y;
    }

    float nextWhite() noexcept
    {
        const uint64_t r = nextRaw();
        // Map to [-1, +1] via signed reinterpretation
        return static_cast<float> (static_cast<int64_t> (r))
               / static_cast<float> (std::numeric_limits<int64_t>::max());
    }

    // White → selected colour. Pink = Paul Kellet 7-section IIR
    // (INV-1: Kellet chosen, within REQ-NOISE-010 8 FLOPS). Brown = leaky integrator.
    float generateColored() noexcept
    {
        const float w = nextWhite();
        switch (noiseType)
        {
            case 1: // Pink
            {
                pink_b[0] = pinkPole[0] * pink_b[0] + w * pinkCoef[0];
                pink_b[1] = pinkPole[1] * pink_b[1] + w * pinkCoef[1];
                pink_b[2] = pinkPole[2] * pink_b[2] + w * pinkCoef[2];
                pink_b[3] = pinkPole[3] * pink_b[3] + w * pinkCoef[3];
                pink_b[4] = pinkPole[4] * pink_b[4] + w * pinkCoef[4];
                pink_b[5] = pinkPole[5] * pink_b[5] + w * pinkCoef[5];
                const float pink = (pink_b[0] + pink_b[1] + pink_b[2] + pink_b[3]
                                    + pink_b[4] + pink_b[5] + pink_b[6] + w * 0.5362f) * pinkGain;
                pink_b[6] = w * 0.115926f;
                return pink;
            }
            case 2: // Brown
            {
                brownState = (brownState + brownLeak * w) / (1.0f + brownLeak);
                return brownState * brownGain;
            }
            default: // White
                return w;
        }
    }

    // R-2 / REQ-NOISE-011: recompute SR-dependent coefficients on prepare only.
    // Kellet poles are time-constant based; preserve each section's DC gain by
    // rescaling input coefficients: c_sr = c44 · (1−p_sr)/(1−p44).
    void computeColorCoeffs (double sr) noexcept
    {
        static const float P44[6] = { 0.99886f, 0.99332f, 0.96900f, 0.86650f, 0.55000f, -0.7616f };
        static const float C44[6] = { 0.0555179f, 0.0750759f, 0.1538520f, 0.3104856f, 0.5329522f, -0.0168980f };
        const double ratio = 44100.0 / juce::jmax (1.0, sr);
        for (int i = 0; i < 6; ++i)
        {
            const double pAbs = std::pow (std::abs ((double) P44[i]), ratio);
            const double p    = (P44[i] >= 0.0) ? pAbs : -pAbs;       // preserve sign
            pinkPole[i] = (float) p;
            pinkCoef[i] = (float) ((double) C44[i] * (1.0 - p) / (1.0 - (double) P44[i]));
        }
        // Brown leaky-integrator cutoff ∝ leak·sr → keep cutoff (Hz) constant across SR.
        brownLeak = (float) (0.02 * 44100.0 / juce::jmax (1.0, sr));
    }

    void applyADSRParams()
    {
        juce::ADSR::Parameters p;
        p.attack  = juce::jmax (0.001f, egAttack);
        p.decay   = juce::jmax (0.001f, egDecay);
        p.sustain = egSustain;
        p.release = juce::jmax (0.001f, egRelease);
        adsr.setParameters (p);
    }

    uint64_t   state[2]   {};
    juce::ADSR adsr;
    double     sampleRate  = 44100.0;
    float      velocity    = 1.0f;
    bool       enabled     = false;
    int        noiseType   = 0;        // 0=White, 1=Pink, 2=Brown
    float      level       = 0.5f;
    float      filterSend  = 1.0f;
    float      directOut   = 0.0f;
    float      egAttack    = 0.001f;
    float      egDecay     = 0.1f;
    float      egSustain   = 0.0f;
    float      egRelease   = 0.2f;

    // Colored-noise state / SR-compensated coefficients (computed in prepare)
    float pink_b[7]  = {};
    float pinkPole[6] = { 0.99886f, 0.99332f, 0.96900f, 0.86650f, 0.55000f, -0.7616f };
    float pinkCoef[6] = { 0.0555179f, 0.0750759f, 0.1538520f, 0.3104856f, 0.5329522f, -0.0168980f };
    float pinkGain   = 0.11f;
    float brownState = 0.0f;
    float brownLeak  = 0.02f;
    float brownGain  = 3.5f;
};

} // namespace rdh::synth
