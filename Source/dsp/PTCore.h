#pragma once

#include <array>

#include "ChipClock.h"
#include "Rng.h"

// The silicon: a fixed-size memory clocked by ChipClock, with the delta-sigma
// converter's grunge modelled where the chip puts it.
//
// The nonlinearity sits in the WRITE path on purpose. Distortion is therefore
// stored, repitched with the buffer when Time moves, and accumulated over
// iterations and stages, which is why three chips in series really do sound
// like three times the THD rather than one chip played louder.
class PTCore
{
public:
    void reset() noexcept
    {
        memory.fill (0.0f);
        ptr = 0;
        qErr = 0.0f;
        dacPrev = 0.0f;
        dacCur = 0.0f;
    }

    inline void tick (float xin, const ChipClock::Frame& f, Rng& rng) noexcept
    {
        // The chip's own input ceiling (about 1.3 Vrms on hardware). Linear
        // below the knee so nominal levels stay clean.
        float v = xin;
        const float ax = std::abs (v);
        if (ax > pt::kClipKnee)
            v = (v < 0.0f ? -1.0f : 1.0f)
                * (pt::kClipKnee + (1.0f - pt::kClipKnee)
                                       * pt::fastTanh ((ax - pt::kClipKnee) * pt::kInvClipSpan));

        // Converter distortion, calibrated against the ElectroSmash THD table:
        // 0.13 % at 31 ms rising to 1 % at 342 ms, with the even-harmonic
        // share growing too. Unity small-signal gain, so Time does not change
        // level as it changes grit.
        v = pt::fastTanh (f.d * (v + pt::kSigmaBias)) * f.invD - f.biasComp;

        float w = v;
        if constexpr (pt::kNoiseShaping)
            w -= qErr; // first-order error feedback: NTF = 1 - z^-1, the modulator's own shaping

        const float dither = 0.5f * (rng.white() + rng.white()) * f.step; // TPDF, +-1 LSB
        float q = f.step * (float) juce::roundToInt ((w + dither) * f.invStep);
        q = juce::jlimit (-1.0f, 1.0f, q);

        if constexpr (pt::kNoiseShaping)
            qErr = q - w;

        // Read the oldest word, then overwrite it: one pointer, fixed size.
        const float y = memory[(size_t) ptr];
        memory[(size_t) ptr] = q;
        if (++ptr == pt::kStageWords)
            ptr = 0;

        dacPrev = dacCur;
        dacCur += f.aDac * (y - dacCur); // the DAC's first analog pole, at chip rate
    }

    float dacPrev = 0.0f, dacCur = 0.0f;

private:
    std::array<float, (size_t) pt::kStageWords> memory {};
    int ptr = 0;
    float qErr = 0.0f;
};
