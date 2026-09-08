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
    const float kSmoothHz[ModMatrix::numDst] = { 0.0f, 120.0f, 30.0f, 40.0f, 20.0f, 20.0f, 20.0f };
}

void ModMatrix::prepare (double sampleRate)
{
    const float controlRate = (float) sampleRate / (float) modk::kControlBlock;

    for (int d = 0; d < numDst; ++d)
        aDest[d] = kSmoothHz[d] > 0.0f
                       ? 1.0f - std::exp (-pt::kTwoPi * kSmoothHz[d] / controlRate)
                       : 1.0f;

    aMacro = 1.0f - std::exp (-1.0f / (modk::kMacroSmoothMs * 0.001f * controlRate));

    for (int s = 0; s < numSrc; ++s)
        for (int d = 0; d < numDst; ++d)
            depth[s][d] = 0.0f;

    // The periodic side, scaled by Agitate.
    //
    // Agitation to Filter is the hardware's normalled connection, and it is
    // BIPOLAR here rather than the hardware's unipolar 0-6 V: see the source
    // array in tick() for why, and docs/DESIGN.md section 4a for the fact that
    // this is a deliberate departure rather than a fidelity claim.
    depth[srcAgitation][dstFilter] = modk::kHeroAgitFilter;
    depth[srcAgitation][dstTime]   = modk::kHeroAgitTime;
    depth[srcFollower][dstDecay]   = modk::kHeroFollowerDecay;
    depth[srcFollower][dstFilter]  = modk::kHeroFollowerFilter;
    depth[srcFollower][dstStrength] = modk::kHeroFollowerStrength;

    // The chaotic side, scaled by Chaos. Interference reached only the clock,
    // so the loop's own state could repitch what was already stored but could
    // never change its colour, its ring or how much of it survived.
    depth[srcInterference][dstTime]      = modk::kHeroIntfTime;
    depth[srcInterference][dstFilter]    = modk::kHeroIntfFilter;
    depth[srcInterference][dstResonance] = modk::kHeroIntfResonance;
    depth[srcInterference][dstAbsorb]    = modk::kHeroIntfAbsorb;

    reset();
}

void ModMatrix::reset() noexcept
{
    for (int d = 0; d < numDst; ++d)
        smoothed[d] = 0.0f;
    out = Offsets();
    macro = macroTarget = 0.0f;
    chaos = chaosTarget = 0.0f;
    timeMod = timeModTarget = 0.0f;
    intfTimeDepth = agitTimeDepth = timeModDepth = 0.0f;
}

void ModMatrix::setMacro (float agitate01, float chaos01, float timeMod01) noexcept
{
    macroTarget = std::pow (juce::jlimit (0.0f, 1.0f, agitate01), modk::kAgitateCurve);
    chaosTarget = juce::jlimit (0.0f, 1.0f, chaos01);
    timeModTarget = juce::jlimit (0.0f, 1.0f, timeMod01);
}

void ModMatrix::snapMacro() noexcept
{
    macro = macroTarget;
    chaos = chaosTarget;
    timeMod = timeModTarget;
}

void ModMatrix::tick (float agitationMean, float follower01, float interferenceWander,
                      float drift) noexcept
{
    macro += (macroTarget - macro) * aMacro;
    chaos += (chaosTarget - chaos) * aMacro;
    timeMod += (timeModTarget - timeMod) * aMacro;

    // Agitation is CENTRED here, for every destination. It is a unipolar 0..1
    // ramp whose long-run mean is exactly 0.5, so with a positive depth half of
    // the route was a permanent DC offset and only half was a sweep. On the
    // filter that DC sat pinned against the cutoff ceiling before the sweep
    // even started; on the clock it would have detuned the delay statically.
    const float source[numSrc] = { 2.0f * agitationMean - 1.0f, follower01,
                                   interferenceWander, drift };

    // Per-source gains: one knob per kind of modulation, rather than one macro
    // over all of them. Drift is deliberately always on and outside both.
    const float srcGain[numSrc] = { macro, macro, chaos, 1.0f };

    for (int d = 1; d < numDst; ++d) // Time is handled per sample
    {
        float sum = 0.0f;
        for (int s = 0; s < numSrc; ++s)
            sum += depth[s][d] * source[s] * srcGain[s];

        const float target = sum * kScale[d];
        smoothed[d] += (target - smoothed[d]) * aDest[d];
    }

    out.filterOct = smoothed[dstFilter];
    out.resonance = smoothed[dstResonance];
    out.decay = smoothed[dstDecay];
    out.absorb = smoothed[dstAbsorb];
    out.blend = smoothed[dstBlend];
    out.strengthDb = smoothed[dstStrength];
    // Once per tick, not once per sample: the engine ramps this across the
    // block. std::pow to raise 10 to the power of zero, 48000 times a second,
    // for a route that was dead, was the old shape of this line.
    out.strengthGain = std::pow (10.0f, out.strengthDb * 0.05f);

    // The Time column, three independent depths rather than one sum that got
    // multiplied by a single modulator.
    intfTimeDepth = depth[srcInterference][dstTime] * kScale[dstTime] * chaos;
    agitTimeDepth = depth[srcAgitation][dstTime] * kScale[dstTime] * macro;
    timeModDepth = modk::timeModOctaves (timeMod);
}

float ModMatrix::modNorm (Dst d) const noexcept
{
    if (d == dstTime)
        return juce::jlimit (-1.0f, 1.0f, timeColumnDepth() / modk::kTimeModClampOct);
    return juce::jlimit (-1.0f, 1.0f, smoothed[d] / juce::jmax (kScale[d], 1.0e-6f));
}
