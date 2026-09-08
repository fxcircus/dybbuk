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

    // 0 is the bare triangle, 1 is folded eight times over. Nothing in the
    // plugin could ADD high frequency: five stages of lowpass remove it and one
    // tanh puts a little back, so the net direction of every control was darker
    // and pushing anything converged on mud. A folded oscillator is the
    // hardware's answer -- its Tones knob morphs triangle to folded to saw to
    // pulse to jagged square -- and a folder is only reasonable to build where
    // the fundamental is known, which inside an oscillator it is.
    void setShape (float shape01) noexcept
    {
        fold01 = shape01 < 0.0f ? 0.0f : (shape01 > 1.0f ? 1.0f : shape01);
        foldAmt = 1.0f + 7.0f * fold01;
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

    // Triangle at shape 0, folded above it. Written as a blend FROM the
    // triangle rather than as a plain sine of the folded ramp, so shape 0 is
    // BIT-IDENTICAL to what it always was: sin(pi/2 * t) is a sinusoid with
    // about 15 dB less third harmonic than the triangle it would have replaced,
    // which would have made "default 0" a quiet character change.
    inline float main() const noexcept
    {
        const float t = 4.0f * std::abs (phase - 0.5f) - 1.0f;
        return fold01 > 0.0f
                   ? t + fold01 * (std::sin (0.5f * pt::kPi * t * foldAmt) - t)
                   : t;
    }

    // The sub-harmonic, one octave down. Softened with a tanh rather than left
    // as a raw triangle so its corners do not put a click into the clock when
    // it is driving Time at full depth.
    inline float sub() const noexcept
    {
        return pt::fastTanh (1.6f * (4.0f * std::abs (subPhase - 0.5f) - 1.0f));
    }

private:
    float phase = 0.0f, subPhase = 0.0f, inc = 0.0f, invSr = 1.0f / 48000.0f;
    float fold01 = 0.0f, foldAmt = 1.0f;
};
