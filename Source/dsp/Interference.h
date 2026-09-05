#pragma once

#include <cmath>

#include "ModConstants.h"
#include "Rng.h"

// The magic ingredient, and the one the plan says to budget real tuning time
// for: a chaotic source derived from the loop's own state.
//
// A Lorenz system whose rho is driven by the loop's energy, so it rests below
// the chaos threshold when the loop is quiet and thrashes when the loop is
// hot, plus a sparse crackle whose density grows with energy. Because it feeds
// Time, which changes the loop, which changes the energy, the whole instrument
// is self influencing and never repeats.
class Interference
{
public:
    void prepare (double sampleRate);
    void seed (unsigned int s) noexcept;
    void reset() noexcept;

    // Once per control tick. loopEnv is the loop's own envelope at the
    // feedback tap; loopSample is its last post-saturator sample.
    void tick (float loopEnv, float loopSample);

    // Per sample, -1 to 1: the ramped wander plus the crackle.
    inline float nextSample() noexcept
    {
        wanderCur += wanderInc;

        if (rng.white() > crackleThreshold) // sparse Poisson-ish ticks
            crackleTarget = (rng.white() >= 0.0f ? 1.0f : -1.0f) * modk::kCrackleAmp
                            * (0.5f + 0.5f * std::abs (wanderSlew));

        crackleEnv += (crackleTarget - crackleEnv) * aCrackleAttack;
        crackleTarget *= crackleDecay;

        const float v = wanderCur + crackleEnv;
        return v < -1.0f ? -1.0f : (v > 1.0f ? 1.0f : v);
    }

    float wander() const noexcept { return wanderSlew; }  // control-rate destinations
    float energy01() const noexcept { return energy; }

private:
    double sr = 48000.0;
    float controlRate = 1500.0f;
    Rng rng;

    float x = 1.0f, y = 1.0f, z = 20.0f; // Lorenz state
    float energy = 0.0f, heat = 0.0f;
    float aEnergyUp = 0.0f, aEnergyDown = 0.0f, aHeat = 0.0f, aSlew = 0.0f;
    float wanderSlew = 0.0f, wanderCur = 0.0f, wanderInc = 0.0f;
    float crackleThreshold = 2.0f, crackleEnv = 0.0f, crackleTarget = 0.0f;
    float aCrackleAttack = 0.5f, crackleDecay = 0.5f;
};
