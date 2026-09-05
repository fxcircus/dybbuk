#pragma once

#include "ModConstants.h"

// The Agitation function generator: a looping or gated attack/decay ramp,
// from a minute per cycle up to audio rate. On the hardware its output is
// normalled to the filter cutoff, which is the first of the three hero routes.
class Agitation
{
public:
    enum class Mode { loop = 0, gate };

    void prepare (double sampleRate);
    void reset() noexcept;
    void setSpeedHz (float hz);
    void setAngle (float angle01);
    void setMode (Mode m) noexcept;

    // Per sample, 0 to 1 (unipolar, like the hardware's 0 to 6 V).
    inline float processSample (bool onset) noexcept
    {
        if (mode == Mode::gate && onset)
        {
            // Relaunch the attack FROM the current output rather than from
            // zero, so retriggering mid-cycle does not step the filter.
            phase = (double) lastOut * (double) attackFraction;
            running = true;
        }

        if (running)
        {
            phase += phaseInc;
            if (phase >= 1.0)
            {
                if (mode == Mode::loop)
                    phase -= 1.0;
                else
                {
                    phase = 0.0;
                    running = false; // one shot done, resting at zero
                }
            }
        }

        const float phi = (float) phase;
        lastOut = (running || mode == Mode::loop)
                      ? (phi < attackFraction ? phi * invAttack : (1.0f - phi) * invDecay)
                      : 0.0f;
        return lastOut;
    }

    float getPhase() const noexcept { return (float) phase; }

private:
    void recomputeShape();

    double sr = 48000.0;
    double phase = 0.0, phaseInc = 0.0;
    float speedHz = 0.5f, angle = modk::kAgitAngle;
    float attackFraction = 0.5f, invAttack = 2.0f, invDecay = 2.0f;
    float lastOut = 0.0f;
    Mode mode = Mode::loop;
    bool running = true;
};
