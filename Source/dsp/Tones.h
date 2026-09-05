#pragma once

#include <cmath>

#include "ChipConstants.h"

// A minimal slice of the Strega's left half: the internal oscillator that
// constantly leaks into the delay. Not the full synth voice, just enough to
// buy the drone character the plan describes, plus the sub-harmonic that the
// hardware normals to the Time modulation input.
//
// Triangle core, because that is what the hardware's is, with a sub an octave
// down. The sub runs whenever Time Mod is up even with Tones Level at zero,
// which is why Tones Pitch matters from day one.
class Tones
{
public:
    void prepare (double sampleRate) noexcept
    {
        invSr = 1.0f / (float) sampleRate;
        reset();
    }

    void reset() noexcept
    {
        phase = 0.0f;
        subPhase = 0.0f;
    }

    void setPitch (float hz) noexcept
    {
        inc = juce::jlimit (0.0f, 0.45f, hz * invSr);
    }

    // Advances both oscillators. main() is the drone, sub() is the
    // sub-harmonic that feeds Time Mod.
    inline void advance() noexcept
    {
        phase += inc;
        if (phase >= 1.0f)
            phase -= 1.0f;

        subPhase += 0.5f * inc;
        if (subPhase >= 1.0f)
            subPhase -= 1.0f;
    }

    // Triangle, -1 to 1.
    inline float main() const noexcept { return 4.0f * std::abs (phase - 0.5f) - 1.0f; }

    // The sub-harmonic, one octave down. Softened with a tanh rather than left
    // as a raw triangle so its corners do not put a click into the clock when
    // it is driving Time at full depth.
    inline float sub() const noexcept
    {
        return pt::fastTanh (1.6f * (4.0f * std::abs (subPhase - 0.5f) - 1.0f));
    }

private:
    float phase = 0.0f, subPhase = 0.0f, inc = 0.0f, invSr = 1.0f / 48000.0f;
};
