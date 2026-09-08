#pragma once

#include <juce_dsp/juce_dsp.h>

#include "ChipConstants.h"

// The PT2399's variable clock, and the single most important object here.
//
// The chip is a FIXED-SIZE memory read at a variable rate, so Time does not
// move a read tap: it changes the sample clock of everything already stored.
// Sweeping Time therefore repitches the buffer (tape smear, not crossfade)
// and audio-rate modulation of the clock gives the metallic FM clang, both
// for free. Every stage shares one clock, so one exp and one division per
// host sample serve all three chips.
class ChipClock
{
public:
    struct Frame
    {
        // Per host sample.
        int   nTicks     = 0;     // chip ticks that fall inside this host sample (0 to about 14)
        float invRatio   = 1.0f;  // 1 / (fs_chip / fs_host)
        float tickOffset = 1.0f;  // tick j sits at host fraction (tickOffset + j) * invRatio
        float readFrac   = 0.0f;  // how far past the last tick we are, for the read interpolation

        // Refreshed every kCtrlInterval samples; stamp changes when they do.
        unsigned int ctrlStamp = 0;
        float aDac     = 0.22f;
        float aGuard   = 0.65f;
        float recG     = 0.6f;
        float d        = 0.265f;
        float invD     = 3.774f;
        float biasComp = 0.03f;
        float step     = 0.0009766f;
        float invStep  = 1024.0f;
        float noiseAmp = 0.0f;
        float bleedLvl = 0.0f;
        float u        = 0.0f;     // 0 = in spec and clean, 1 = overclocked low and destroyed
        float fsChip   = pt::kFsChipMax;
    };

    void prepare (double sampleRate);
    void reset() noexcept;      // phase to zero and force a coefficient refresh; the Time target survives
    void snapTime() noexcept;   // first block, prepare, preset load: no 20 ms chirp up from the hard minimum
    void setTime01 (float t) noexcept;
    // 0 to 1, smoothed by the caller. Independent of Time: it pushes the
    // degradation coordinate toward the floor wherever the clock happens to be.
    void setCrust01 (float c) noexcept { crust01 = c < 0.0f ? 0.0f : (c > 1.0f ? 1.0f : c); }

    inline const Frame& advance (float modOctaves) noexcept
    {
        float logFs = logFsSmooth.getNextValue();

        if constexpr (pt::kFmLawLinear)
        {
            // Linear in clock rate: symmetric sidebands in Hz. The hardware's
            // Time CV drives a current-controlled VCO, so which law matches
            // the demos is a listening call (docs/design/01-core.md, Q2).
            const float mult = 1.0f + modOctaves;
            logFs += std::log (mult > 0.05f ? mult : 0.05f);
        }
        else
        {
            logFs += modOctaves * pt::kLn2; // exponential: modulation in octaves
        }

        float fs = std::exp (logFs);
        fs = fs < pt::kFsChipHardMin ? pt::kFsChipHardMin
                                     : (fs > pt::kFsChipHardMax ? pt::kFsChipHardMax : fs);

        const float ratio = fs * invSr;
        const float before = phase;
        phase += ratio;

        const int ticks = (int) phase;      // phase is always >= 0, so this is a floor
        phase -= (float) ticks;             // wrapped every sample: the accumulator never grows

        frame.nTicks     = ticks;
        frame.invRatio   = 1.0f / ratio;
        frame.tickOffset = 1.0f - before;
        frame.readFrac   = phase;

        if (--ctrlCountdown <= 0)
        {
            ctrlCountdown = pt::kCtrlInterval;
            refreshSlow (fs, logFs);
        }

        return frame;
    }

    float currentFsChip() const noexcept { return frame.fsChip; }

private:
    void refreshSlow (float fs, float logFs) noexcept;

    juce::SmoothedValue<float> logFsSmooth;
    Frame frame;
    double sr = 48000.0;
    float invSr = 1.0f / 48000.0f;
    float phase = 0.0f;
    int ctrlCountdown = 0;
    unsigned int ctrlStamp = 0;
    float crust01 = 0.0f;
};
