#pragma once

#include "ChipConstants.h"

// Zavalishin TPT state variable filter in the Cytomic form, header-only and
// hot. JUCE's StateVariableTPTFilter hides its integrator states, and the
// in-loop filter needs to bound one of them: that bound is what makes
// self-oscillation settle at a level instead of running away, and what makes
// it sound analog rather than like a divide by zero.
class TptSvf
{
public:
    // g = tan(pi * fc / fs); k = 1/Q, and may go slightly negative for an
    // actively resonating filter (the state limit keeps that bounded).
    void setG (float newG) noexcept { g = newG; updateCoeffs(); }
    void setK (float newK) noexcept { k = newK; updateCoeffs(); }
    void setStateLimit (float limit) noexcept
    {
        stateLimit = limit;
        invStateLimit = limit > 0.0f ? 1.0f / limit : 0.0f;
    }

    void reset() noexcept { ic1 = 0.0f; ic2 = 0.0f; }

    inline float lowpass (float x) noexcept
    {
        const float v3 = x - ic2;
        const float v1 = a1 * ic1 + a2 * v3;
        const float v2 = ic2 + a2 * ic1 + a3 * v3;
        ic1 = 2.0f * v1 - ic1;
        ic2 = 2.0f * v2 - ic2;

        if (stateLimit > 0.0f)
            ic1 = stateLimit * pt::fastTanh (ic1 * invStateLimit);

        return v2;
    }

private:
    void updateCoeffs() noexcept
    {
        // The one division, at control rate. Guarded: 1 + g(g + k) stays
        // positive for every (g, k) the loop can ask for.
        const float denom = 1.0f + g * (g + k);
        a1 = 1.0f / (denom > 1.0e-6f ? denom : 1.0e-6f);
        a2 = g * a1;
        a3 = g * a2;
    }

    float g = 1.0f, k = 1.0f;
    float a1 = 0.5f, a2 = 0.5f, a3 = 0.5f;
    float ic1 = 0.0f, ic2 = 0.0f;
    float stateLimit = 0.0f, invStateLimit = 0.0f;
};
