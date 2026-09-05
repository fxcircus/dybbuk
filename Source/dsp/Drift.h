#pragma once

#include <cmath>

#include <juce_core/juce_core.h>

#include "ChipConstants.h"
#include "ModConstants.h"
#include "Rng.h"

// Very slow filtered random, 0.05 to 2 Hz. Always on at a tiny depth on Time,
// outside the matrix and outside the Agitate macro: this is what keeps the
// plan's promise that the same settings never land in the same place twice.
class Drift
{
public:
    void prepare (double sampleRate)
    {
        // Runs at control rate, not sample rate.
        const float fc = (float) sampleRate / (float) modk::kControlBlock;
        aLp = 1.0f - std::exp (-pt::kTwoPi * modk::kDriftLpHz / fc);
        aHp = 1.0f - std::exp (-pt::kTwoPi * modk::kDriftHpHz / fc);

        // Uniform white through two one-poles: analytic stationary RMS, so the
        // output lands at a known level instead of "whatever it came out as".
        const float statRms = std::sqrt (aLp) * 0.5f / std::sqrt (3.0f);
        gain = modk::kDriftTargetRms / juce::jmax (statRms, 1.0e-6f);
        reset();
    }

    void seed (unsigned int s) noexcept { rng.seed (s); }

    void reset() noexcept
    {
        lp1 = lp2 = hp = 0.0f;
        cur = inc = 0.0f;
    }

    // Once per control tick.
    float tick() noexcept
    {
        const float w = rng.white();
        lp1 += (w - lp1) * aLp;
        lp2 += (lp1 - lp2) * aLp;
        hp += (lp2 - hp) * aHp;
        const float v = pt::fastTanh ((lp2 - hp) * gain);
        inc = (v - cur) * modk::kInvControlBlock;
        return v;
    }

    float nextSample() noexcept { cur += inc; return cur; }
    float current() const noexcept { return cur; }

private:
    Rng rng;
    float lp1 = 0.0f, lp2 = 0.0f, hp = 0.0f;
    float aLp = 0.0f, aHp = 0.0f, gain = 1.0f;
    float cur = 0.0f, inc = 0.0f;
};
