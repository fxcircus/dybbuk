#pragma once

#include "OnePole.h"
#include "PTCore.h"
#include "TptSvf.h"

// One PT2399 with the analog board around it: the fixed MFB anti-alias filter
// in front, the write guard, the chip, the interpolated read, the
// reconstruction filter that tracks the clock, hiss, and the clock bleed that
// the Strega exposes instead of hiding.
class PTStage
{
public:
    void prepare (double sampleRate);
    void reset() noexcept;
    void seedForTests (unsigned int s) noexcept { rng.seed (s); }

    inline float processSample (float x, const ChipClock::Frame& f) noexcept
    {
        if (f.ctrlStamp != lastCtrlStamp)
        {
            lastCtrlStamp = f.ctrlStamp;
            recSvf.setG (f.recG);
            guard1.a = f.aGuard;
            guard2.a = f.aGuard;
        }

        float a = inPole.lp (x);
        a = inSvf.lowpass (a);
        if constexpr (pt::kGuardPoles >= 1)
            a = guard1.lp (a);
        if constexpr (pt::kGuardPoles >= 2)
            a = guard2.lp (a);

        // Write resampling: linear interpolation onto the chip's clock grid.
        // Deliberately cheap, per the plan: the imperfection is the sound.
        for (int j = 0; j < f.nTicks; ++j)
        {
            const float t = (f.tickOffset + (float) j) * f.invRatio;
            core.tick (xPrev + t * (a - xPrev), f, rng);
        }
        xPrev = a;

        float r = core.dacPrev + f.readFrac * (core.dacCur - core.dacPrev);
        r = recSvf.lowpass (r);

        // Hiss enters AFTER reconstruction. Injected before it, everything at
        // long Time lands under fs_chip/2 (750 Hz at the bottom) and reads as
        // rumble rather than the tape hiss the plan describes.
        r += hissPole.lp (f.noiseAmp * noiseSrScale * rng.white());

        // Clock bleed: a tick train at fs_chip with a square at fs_chip/2
        // under it. After the reconstruction filter (which would otherwise
        // erase it) and before the loop, so it gets re-delayed and burbles.
        bleedEnv *= bleedDecay;
        if (f.nTicks > 0)
        {
            bleedEnv = 1.0f;
            if ((f.nTicks & 1) != 0)
                sub = -sub;
        }
        if (f.bleedLvl > 0.0f)
            r += f.bleedLvl * pt::fastTanh (bleedEnv + pt::kBleedSubRatio * sub);

        return outSvf.lowpass (r);
    }

private:
    PTCore core;
    Rng rng;
    OnePole inPole, guard1, guard2, hissPole;
    TptSvf inSvf, recSvf, outSvf;

    float xPrev = 0.0f;
    float bleedEnv = 0.0f, sub = 1.0f, bleedDecay = 0.5f;
    float noiseSrScale = 1.0f;
    unsigned int lastCtrlStamp = 0;
};
