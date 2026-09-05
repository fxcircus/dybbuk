#pragma once

#include <cmath>

#include "ChipConstants.h"

// The thing that makes Decay past unity musical instead of digital: a soft
// asymmetric clip that bounds the loop, followed by a DC blocker so the
// asymmetry cannot walk the loop into an offset over thousands of iterations.
class LoopSaturator
{
public:
    void prepare (double sampleRate) noexcept
    {
        r = 1.0f - pt::kTwoPi * pt::kDcBlockHz / (float) sampleRate;
        reset();
    }

    void reset() noexcept { x1 = 0.0f; y1 = 0.0f; }

    void setDrive (float newDrive) noexcept
    {
        drive = newDrive > 0.01f ? newDrive : 0.01f;
        invDrive = 1.0f / drive;
        biasComp = std::tanh (pt::kSatBias);
    }

    inline float process (float x) noexcept
    {
        const float y = (std::tanh (drive * x + pt::kSatBias) - biasComp) * invDrive;
        const float out = y - x1 + r * y1;
        x1 = y;
        y1 = out;
        return out;
    }

private:
    float drive = 1.0f, invDrive = 1.0f, biasComp = 0.0f;
    float r = 0.9987f, x1 = 0.0f, y1 = 0.0f;
};
