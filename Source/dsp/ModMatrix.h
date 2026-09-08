#pragma once

#include "ModConstants.h"

// Five sources by seven destinations. The three hero routes the plan asks for
// are wired -- Agitation to Filter (the hardware normal), Interference to Time,
// Follower to Decay -- plus the ones that turn the sources into an instrument
// rather than a filter LFO.
//
// Each SOURCE has its own gain, which is what stops one macro welding a
// periodic filter sweep to a chaotic clock. Agitate scales the periodic side
// (Agitation and the Follower), Chaos scales the whole Interference row, and
// Drift is always on at its own tiny fixed depth. Before that split there was
// no setting at which you could have the haunted, self-driven behaviour without
// also imposing an audible triangle LFO on the cutoff, which is most of what
// made the plugin read as "a delay with an LFO".
//
// Only Time is modulated at audio rate, because that is where the FM character
// lives; everything else is combined once per control tick and one-pole
// smoothed, because a stepped gain is a click and a stepped cutoff is a zipper.
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
        // Precomputed from strengthDb once per control tick, because a
        // std::pow per sample to raise 10 to the power of zero is a waste, and
        // because the engine ramps this across the block rather than stepping
        // it: a control-rate staircase on an input gain is a click.
        float strengthGain = 1.0f;
    };

    void prepare (double sampleRate);
    void reset() noexcept;
    void snapMacro() noexcept; // first block and preset load: no 30 ms ramp in from zero

    void setDepth (Src s, Dst d, float bipolarDepth) noexcept { depth[s][d] = bipolarDepth; }
    void setMacro (float agitate01, float chaos01, float timeMod01) noexcept; // per block, raw knob values

    // Once per control tick, with each source's control-rate representative.
    void tick (float agitationMean, float follower01, float interferenceWander, float drift) noexcept;

    // Per sample, in octaves of chip clock, already clamped. Four independent
    // terms, summed and clamped ONCE:
    //   the Interference column, scaled by Chaos
    //   the Agitation column, scaled by Agitate, centred so a unipolar ramp
    //     wobbles around the set Time instead of only bending it sharp
    //   Time Mod, driven by the Tones sub-harmonic as the hardware normals it
    //   the always-on Drift trim
    //
    // Time Mod used to be added into the Interference depth and then multiplied
    // by the chaos sample, so the knob was secretly a second chaos control
    // whose smear rode loop energy -- the metallic grid got LESS defined the
    // harder you played. The Tones term was also added after this clamp, so
    // half the excursion escaped it.
    inline float timeOctave (float intfSample, float driftSample, float agitBipolar,
                             float tonesSub) const noexcept
    {
        const float v = intfTimeDepth * intfSample
                        + agitTimeDepth * agitBipolar
                        + timeModDepth * tonesSub
                        + modk::kDriftTimeOct * driftSample;
        return v < -modk::kTimeModClampOct ? -modk::kTimeModClampOct
                                           : (v > modk::kTimeModClampOct ? modk::kTimeModClampOct : v);
    }

    const Offsets& offsets() const noexcept { return out; }
    float macroValue() const noexcept { return macro; }

    // The band the Time knob's arc should draw: an honest peak excursion, so
    // the plate stops over-reading the chaos (the Lorenz never reaches +-1) and
    // stops omitting Time Mod entirely.
    float timeColumnDepth() const noexcept
    {
        return intfTimeDepth * modk::kIntfWanderPeak + agitTimeDepth + timeModDepth;
    }

    // For the UI's modulated knob rings: how far each destination is being
    // pushed, normalised to its own full scale.
    float modNorm (Dst d) const noexcept;

private:
    float depth[numSrc][numDst] = {};
    float smoothed[numDst] = {};
    float aDest[numDst] = {};

    Offsets out;
    float macro = 0.0f, macroTarget = 0.0f, aMacro = 0.0f;
    float chaos = 0.0f, chaosTarget = 0.0f;
    float timeMod = 0.0f, timeModTarget = 0.0f;

    // The Time column, kept apart so each term reaches the clock on its own
    // terms rather than multiplying one modulator by another's depth.
    float intfTimeDepth = 0.0f, agitTimeDepth = 0.0f, timeModDepth = 0.0f;
};
