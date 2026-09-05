#pragma once

#include "ModConstants.h"

// Four sources by seven destinations, with the three hero routes the plan
// asks for wired by default: Agitation to Filter (the hardware normal),
// Interference to Time, Follower to Decay. The Agitate knob is a macro over
// the whole table, so one control means "how much modulation overall".
//
// Only Time is modulated at audio rate; everything else is combined once per
// control tick and one-pole smoothed, because a stepped gain is a click and a
// stepped cutoff is a zipper.
class ModMatrix
{
public:
    enum Src { srcAgitation = 0, srcFollower, srcInterference, srcDrift, numSrc };
    enum Dst { dstTime = 0, dstFilter, dstResonance, dstDecay, dstAbsorb, dstBlend, dstStrength, numDst };

    // What the engine adds to the knob values, in each destination's own units.
    struct Offsets
    {
        float filterOct = 0.0f;
        float resonance = 0.0f;
        float decay = 0.0f;
        float absorb = 0.0f;
        float blend = 0.0f;
        float strengthDb = 0.0f;
    };

    void prepare (double sampleRate);
    void reset() noexcept;
    void snapMacro() noexcept; // first block and preset load: no 30 ms ramp in from zero

    void setDepth (Src s, Dst d, float bipolarDepth) noexcept { depth[s][d] = bipolarDepth; }
    void setMacro (float agitate01, float timeMod01) noexcept; // per block, raw knob values

    // Once per control tick, with each source's control-rate representative.
    void tick (float agitationMean, float follower01, float interferenceWander, float drift) noexcept;

    // Per sample, in octaves of chip clock, already clamped.
    inline float timeOctave (float interferenceSample, float driftSample) const noexcept
    {
        const float v = timeDepth * interferenceSample + modk::kDriftTimeOct * driftSample;
        return v < -modk::kTimeModClampOct ? -modk::kTimeModClampOct
                                           : (v > modk::kTimeModClampOct ? modk::kTimeModClampOct : v);
    }

    const Offsets& offsets() const noexcept { return out; }
    float macroValue() const noexcept { return macro; }

    // For the UI's modulated knob rings: how far each destination is being
    // pushed, normalised to its own full scale.
    float modNorm (Dst d) const noexcept;

private:
    float depth[numSrc][numDst] = {};
    float smoothed[numDst] = {};
    float aDest[numDst] = {};

    Offsets out;
    float macro = 0.0f, macroTarget = 0.0f, aMacro = 0.0f;
    float timeMod = 0.0f, timeModTarget = 0.0f;
    float timeDepth = 0.0f;
};
