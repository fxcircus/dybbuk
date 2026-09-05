#pragma once

#include <cmath>

#include "ModConstants.h"

// Envelope follower on the post-Strength signal: the player's dynamics as a
// modulation source (CV1 on the hardware), and the onset detector that fires
// Agitation in Gate mode.
//
// It reads the input only, never the loop, so there is no feedback path here.
class InputFollower
{
public:
    void prepare (double sampleRate)
    {
        const float sr = (float) sampleRate;
        aAttack = 1.0f - std::exp (-1.0f / (modk::kFollowerAttackMs * 0.001f * sr));
        aRelease = 1.0f - std::exp (-1.0f / (modk::kFollowerReleaseMs * 0.001f * sr));
        aBaseline = 1.0f - std::exp (-1.0f / (modk::kGateBaselineMs * 0.001f * sr));
        holdSamples = (int) (modk::kGateHoldMs * 0.001f * sr);
        reset();
    }

    void reset() noexcept
    {
        env = 0.0f;
        baseline = 0.0f;
        armed = true;
        holdCounter = 0;
    }

    // Returns true on an onset: above the gate level, above a lagging
    // baseline (so it fires on attacks rather than on sustain), and outside
    // the hold-off that stops one pick counting twice.
    bool processSample (float x) noexcept
    {
        const float r = std::abs (x);
        env += (r - env) * (r > env ? aAttack : aRelease);

        if (holdCounter > 0)
            --holdCounter;

        if (env < modk::kGateOffLevel)
            armed = true;

        bool onset = false;
        if (armed && holdCounter == 0 && env > modk::kGateOnLevel
            && env > baseline * modk::kOnsetRatio)
        {
            onset = true;
            armed = false;
            holdCounter = holdSamples;
        }

        // Baseline updates AFTER the test, or a slow swell would outrun itself.
        baseline += (env - baseline) * aBaseline;
        return onset;
    }

    float getEnv() const noexcept { return env; }
    float value01() const noexcept
    {
        const float v = env * (1.0f / modk::kFollowerFullScale);
        return v > 1.0f ? 1.0f : v;
    }

private:
    float env = 0.0f, baseline = 0.0f;
    float aAttack = 0.0f, aRelease = 0.0f, aBaseline = 0.0f;
    bool armed = true;
    int holdCounter = 0, holdSamples = 0;
};
