/*
  ==============================================================================
    WaveTable.h
    Shared read-only sine wavetable for the FM engine (Phase 2 / Sprint 1, S1-1).
    2048-point table + 2^N mask indexing + linear interpolation.

    HC-2: the table is a read-only global (immutable after first init via a
    function-local static), so it introduces NO mutable shared state across
    plugin instances. HC-3: lookup is allocation-free and lock-free.
    Power-of-two mask indexing.
  ==============================================================================
*/

#pragma once
#include <JuceHeader.h>
#include <array>
#include <cmath>

namespace rdh::synth {

inline constexpr int WT_SIZE = 2048;          // 2^11
inline constexpr int WT_MASK = WT_SIZE - 1;

// Single shared sine table. Function-local static => thread-safe one-time init.
inline const std::array<float, WT_SIZE>& sineTable() noexcept
{
    static const std::array<float, WT_SIZE> table = []
    {
        std::array<float, WT_SIZE> t {};
        for (int i = 0; i < WT_SIZE; ++i)
            t[(size_t) i] = (float) std::sin (juce::MathConstants<double>::twoPi * i / WT_SIZE);
        return t;
    }();
    return table;
}

// Wrap any real phase into [0, 1). Handles negative and large arguments.
inline float wrap01 (float x) noexcept { return x - std::floor (x); }

// phase01 must be in [0,1) (caller wraps via wrap01). Linear-interpolated lookup.
// R-3: index masked with WT_MASK so float rounding to WT_SIZE cannot read OOB.
inline float wtSine (float phase01) noexcept
{
    const auto& t    = sineTable();
    const float idx  = phase01 * WT_SIZE;
    const int   i0   = static_cast<int> (idx) & WT_MASK;
    const int   i1   = (i0 + 1) & WT_MASK;            // end point wraps to t[0]
    const float frac = idx - std::floor (idx);
    return t[(size_t) i0] + (t[(size_t) i1] - t[(size_t) i0]) * frac;
}

} // namespace rdh::synth
