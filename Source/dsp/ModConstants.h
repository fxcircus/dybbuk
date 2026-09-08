#pragma once

// Every tunable number for the modulation system. The plan calls the
// generative behaviour the pass/fail test for the whole concept, and it lives
// almost entirely in these constants, so they are gathered here to be tuned by
// ear rather than hunted through the DSP.
namespace modk
{
    // --- control rate --------------------------------------------------------
    inline constexpr int   kControlBlock    = 32;      // modulation resolution: 1.5 kHz at 48 k
    inline constexpr float kInvControlBlock = 1.0f / 32.0f;

    // --- Agitation -----------------------------------------------------------
    inline constexpr float kAgitSpeedMin    = 0.016f;  // a minute per cycle
    inline constexpr float kAgitSpeedMax    = 1000.0f; // audio rate
    inline constexpr float kAngleMin        = 0.01f;   // 1/99 rise to fall at the extremes
    inline constexpr float kAgitMinSegmentMs = 0.5f;   // no one-sample cliff: a cliff into Time is a click
    inline constexpr float kAgitAngle       = 0.5f;    // fixed triangle in v1

    // --- gate detection (Agit Mode: Gate) ------------------------------------
    inline constexpr float kGateOnLevel  = 0.0316f;    // -30 dBFS
    inline constexpr float kGateOffLevel = 0.0158f;    // -36 dBFS, hysteresis
    inline constexpr float kOnsetRatio   = 1.8f;       // and above a lagging baseline, so it fires on attacks
    inline constexpr float kGateBaselineMs = 300.0f;
    inline constexpr float kGateHoldMs   = 20.0f;      // double-trigger guard on picked transients

    // --- input follower ------------------------------------------------------
    inline constexpr float kFollowerAttackMs  = 5.0f;
    inline constexpr float kFollowerReleaseMs = 100.0f;
    inline constexpr float kFollowerFullScale = 0.5f;  // a -6 dBFS peak reads 1.0

    // --- Interference --------------------------------------------------------
    // The loop's own energy drives a Lorenz system across its chaos threshold,
    // so a quiet loop wanders gently and a hot one thrashes. The output is
    // correlated with the audio because it IS the audio loop.
    inline constexpr float kEnergyFloorDb = -54.0f;
    inline constexpr float kEnergyCeilDb  = -6.0f;
    inline constexpr float kEnergyUpMs    = 20.0f;
    inline constexpr float kEnergyDownMs  = 400.0f;    // sets the floor of the breathing period
    inline constexpr float kLoopEnvAttackMs  = 3.0f;
    inline constexpr float kLoopEnvReleaseMs = 150.0f;

    inline constexpr float kIntfRhoMin   = 18.0f;      // below 24.74 the system comes to rest
    inline constexpr float kIntfRhoMax   = 45.0f;
    inline constexpr float kIntfHeatRho  = 8.0f;       // a loop hot for a while gets wilder still
    inline constexpr float kIntfHeatSeconds = 6.0f;
    inline constexpr float kIntfSpeedMin = 6.0f;       // Lorenz time units per second
    inline constexpr float kIntfSpeedMax = 18.0f;
    inline constexpr float kLorenzSigma  = 10.0f;
    inline constexpr float kLorenzBeta   = 2.6666667f;
    inline constexpr float kLorenzBound  = 60.0f;
    inline constexpr float kIntfWanderScale = 0.05f;
    inline constexpr float kIntfSlewHz   = 40.0f;
    inline constexpr float kIntfInject   = 0.15f;      // the loop's own sample nudges the chaos state

    inline constexpr float kCrackleMaxRate  = 400.0f;  // ticks per second at full energy
    inline constexpr float kCrackleAmp      = 0.6f;
    inline constexpr float kCrackleAttackMs = 0.3f;    // never an instantaneous clock jump
    inline constexpr float kCrackleDecayMs  = 1.5f;

    // --- Drift ---------------------------------------------------------------
    inline constexpr float kDriftLpHz     = 1.0f;
    inline constexpr float kDriftHpHz     = 0.05f;     // never parks on one offset
    inline constexpr float kDriftTargetRms = 0.3f;
    // Always on, outside the matrix and outside the macro: the plan's promise
    // that the same settings never land in the same place twice.
    inline constexpr float kDriftTimeOct  = 0.006f;    // about 7 cents peak

    // --- matrix --------------------------------------------------------------
    // Full-scale excursion per destination, in that destination's own units.
    inline constexpr float kScaleTime      = 2.0f;   // octaves of chip clock
    inline constexpr float kScaleFilter    = 4.0f;   // octaves
    inline constexpr float kScaleResonance = 0.5f;
    inline constexpr float kScaleDecay     = 0.5f;
    inline constexpr float kScaleAbsorb    = 0.5f;
    inline constexpr float kScaleBlend     = 0.5f;
    inline constexpr float kScaleStrength  = 20.0f;  // dB

    // The three v1 hero routes. Agitation to Filter is the hardware normal.
    inline constexpr float kHeroAgitFilter    = 0.75f;
    inline constexpr float kHeroIntfTime      = 0.50f;
    inline constexpr float kHeroFollowerDecay = 0.30f;

    inline constexpr float kAgitateCurve  = 1.5f;    // subtle in the lower half, wild at the top
    inline constexpr float kMacroSmoothMs = 30.0f;
    // Two octaves of clock FM at full depth was unusable past about an
    // eighth of the knob. A quarter of an octave, three semitones, puts the
    // whole travel where the metallic edge lives.
    inline constexpr float kTimeModMaxOct = 0.25f;   // Time Mod at 100 %, independent of the macro
    inline constexpr float kTimeModClampOct = 2.0f;  // total excursion from everything stacked
    inline constexpr float kLoopEnvCeiling = 100.0f; // sanitises the one modulation feedback input

    // --- Tones and Spread (the optional character, both off by default) ------
    inline constexpr float kTonesFullLevel = 0.35f;  // drone level into the loop at Tones 100 %
    inline constexpr float kTonesSubMix    = 0.45f;  // how much sub sits under the drone
    inline constexpr float kSpreadMaxMs    = 14.0f;  // Haas offset behind the side component
    // The side is the one output path the loop saturator does not bound, and
    // the loop is hotter in RMS than it was, so this comes in to meet it: the
    // 30-minute soak recorded a 1.35 stereo peak against a 0.98 mono peak.
    inline constexpr float kSpreadMaxWidth = 0.6f;   // side gain at Spread 100 %
} // namespace modk
