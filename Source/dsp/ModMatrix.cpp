#include "ModMatrix.h"

#include <cmath>

#include <juce_core/juce_core.h>

#include "ChipConstants.h"

namespace
{
    // Full-scale excursion per destination, in that destination's own units.
    const float kScale[ModMatrix::numDst] = {
        modk::kScaleTime, modk::kScaleFilter, modk::kScaleResonance, modk::kScaleDecay,
        modk::kScaleAbsorb, modk::kScaleBlend, modk::kScaleStrength
    };

    // How fast each destination is allowed to move. Time is absent: it is the
    // one audio-rate destination and is never smoothed here.
    const float kSmoothHz[ModMatrix::numDst] = { 0.0f, 30.0f, 30.0f, 40.0f, 20.0f, 20.0f, 20.0f };
}

void ModMatrix::prepare (double sampleRate)
{
    const float controlRate = (float) sampleRate / (float) modk::kControlBlock;

    for (int d = 0; d < numDst; ++d)
        aDest[d] = kSmoothHz[d] > 0.0f
                       ? 1.0f - std::exp (-pt::kTwoPi * kSmoothHz[d] / controlRate)
                       : 1.0f;

    aMacro = 1.0f - std::exp (-1.0f / (modk::kMacroSmoothMs * 0.001f * controlRate));

    // The three hero routes. Unipolar sources with a positive depth only push
    // a destination upward from the knob, which is the hardware normal: set
    // Filter dark, and the agitation opens it.
    for (int s = 0; s < numSrc; ++s)
        for (int d = 0; d < numDst; ++d)
            depth[s][d] = 0.0f;

    depth[srcAgitation][dstFilter] = modk::kHeroAgitFilter;
    depth[srcInterference][dstTime] = modk::kHeroIntfTime;
    depth[srcFollower][dstDecay] = modk::kHeroFollowerDecay;

    reset();
}

void ModMatrix::reset() noexcept
{
    for (int d = 0; d < numDst; ++d)
        smoothed[d] = 0.0f;
    out = Offsets();
    macro = macroTarget = 0.0f;
    timeMod = timeModTarget = 0.0f;
    timeDepth = 0.0f;
}

void ModMatrix::setMacro (float agitate01, float timeMod01) noexcept
{
    macroTarget = std::pow (juce::jlimit (0.0f, 1.0f, agitate01), modk::kAgitateCurve);
    timeModTarget = juce::jlimit (0.0f, 1.0f, timeMod01);
}

void ModMatrix::snapMacro() noexcept
{
    macro = macroTarget;
    timeMod = timeModTarget;
}

void ModMatrix::tick (float agitationMean, float follower01, float interferenceWander,
                      float drift) noexcept
{
    macro += (macroTarget - macro) * aMacro;
    timeMod += (timeModTarget - timeMod) * aMacro;

    const float source[numSrc] = { agitationMean, follower01, interferenceWander, drift };

    for (int d = 1; d < numDst; ++d) // Time is handled per sample
    {
        float sum = 0.0f;
        for (int s = 0; s < numSrc; ++s)
            sum += depth[s][d] * source[s];

        const float target = sum * kScale[d] * macro;
        smoothed[d] += (target - smoothed[d]) * aDest[d];
    }

    out.filterOct = smoothed[dstFilter];
    out.resonance = smoothed[dstResonance];
    out.decay = smoothed[dstDecay];
    out.absorb = smoothed[dstAbsorb];
    out.blend = smoothed[dstBlend];
    out.strengthDb = smoothed[dstStrength];

    // The Time column carries two independent depths. The macro scales the
    // matrix routes, as it does everywhere else; Time Mod is its own control
    // for clock FM depth, so it works whether or not Agitate is up. When the
    // Tones oscillator arrives it becomes the Time Mod source, as on the
    // hardware, where Time Mod is normalled to the sub-harmonics output.
    timeDepth = depth[srcInterference][dstTime] * kScale[dstTime] * macro
                + timeMod * modk::kTimeModMaxOct;
}

float ModMatrix::modNorm (Dst d) const noexcept
{
    if (d == dstTime)
        return juce::jlimit (-1.0f, 1.0f, timeDepth / modk::kTimeModClampOct);
    return juce::jlimit (-1.0f, 1.0f, smoothed[d] / juce::jmax (kScale[d], 1.0e-6f));
}
