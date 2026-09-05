#pragma once

#include <cmath>

#include "ChipConstants.h"

// One-pole lowpass. Used where a filter is a tone control rather than a
// designed response: the DAC pole, the write guard, the Absorb shelf split.
struct OnePole
{
    float s = 0.0f;
    float a = 1.0f;

    inline float lp (float x) noexcept { s += a * (x - s); return s; }
    void reset() noexcept { s = 0.0f; }

    static float coeffFor (float fc, double sampleRate) noexcept
    {
        const float w = pt::kTwoPi * fc / (float) sampleRate;
        return 1.0f - std::exp (-w);
    }
};
