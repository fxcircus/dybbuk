#pragma once

// Every tunable number for the modulation system. The plan calls the
// generative behaviour the pass/fail test for the whole concept, and it lives
// almost entirely in these constants, so they are gathered here to be tuned by
// ear rather than hunted through the DSP.
namespace modk
{
    // --- control rate --------------------------------------------------------
    // Modulation resolution: 3 kHz at 48 k. The inverse is DERIVED, not written
    // out again -- as a literal 1.0f/32.0f it was a landmine, because changing
    // the block alone would silently break both the source ramp interpolation
    // and the agitation mean with no compile error.
    inline constexpr int   kControlBlock    = 16;
    inline constexpr float kInvControlBlock = 1.0f / (float) kControlBlock;

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
    // A -6 dBFS peak used to read 0.92, so anything above normal playing pinned
    // the source. The follower now drives three destinations including input
    // drive, so a slam needs somewhere left to go: at 0.65 a -24/-18/-12/-6/0
    // dBFS peak reads about 0.09 / 0.18 / 0.35 / 0.71 / 1.00.
    inline constexpr float kFollowerFullScale = 0.65f;

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
    // Lorenz time units per second. At 18 the wander could not cross zero faster
    // than about 26 times a second at any setting, so every chaotic Time
    // excursion the plugin could produce was a slow drunken bend and the whole
    // register between there and audio rate was empty. 45 at the 3 kHz control
    // rate is dt = 0.015, comfortably inside RK2's accurate range (0.04 was
    // measured wrecking the attractor). Speed still tracks loop energy, so
    // digging in is what buys the flutter.
    inline constexpr float kIntfSpeedMin = 6.0f;
    inline constexpr float kIntfSpeedMax = 45.0f;
    inline constexpr float kLorenzSigma  = 10.0f;
    inline constexpr float kLorenzBeta   = 2.6666667f;
    inline constexpr float kLorenzBound  = 60.0f;
    inline constexpr float kIntfWanderScale = 0.05f;
    inline constexpr float kIntfSlewHz   = 120.0f;
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

    // The wired routes. Agitation to Filter is the hardware's normalled
    // connection; the rest exist because the matrix shipped with three of its
    // twenty-eight cells non-zero and setDepth had no caller anywhere, so four
    // of the seven destinations were dead for the life of the plugin and every
    // patch modulated the same three things in the same direction.
    //
    // Depths are chosen so that a route ARRIVES rather than merely exists: at
    // full macro each one has to move the sound in a way a player would notice
    // and reach for again. The numbers below are what the EngineTest `routes`
    // scenario prints, in each destination's own units.
    inline constexpr float kHeroAgitFilter      = 0.75f; // +-3.0 oct, centred on the knob
    inline constexpr float kHeroAgitTime        = 0.35f; // +-0.7 oct: wow, vibrato, then clang
    inline constexpr float kHeroFollowerDecay   = 0.30f; // dig in and the tail lengthens
    inline constexpr float kHeroFollowerFilter  = 0.40f; // +1.6 oct: the loop opens when you play
    inline constexpr float kHeroFollowerStrength = 0.45f; // +9 dB into the drive, ahead of the loop
    inline constexpr float kHeroIntfTime        = 0.50f;
    inline constexpr float kHeroIntfFilter      = 0.35f; // +-1.4 oct: the static finally reaches tone
    inline constexpr float kHeroIntfResonance   = 0.35f; // tips a high Resonance over intermittently
    inline constexpr float kHeroIntfAbsorb      = 0.30f; // the loop breathing in and out of the earth

    // The Lorenz wander is a tanh and never reaches +-1; this is its measured
    // peak, used so the UI's Time arc draws an honest band rather than one that
    // over-reads the chaos by 60 per cent.
    inline constexpr float kIntfWanderPeak = 0.63f;

    inline constexpr float kAgitateCurve  = 1.5f;    // subtle in the lower half, wild at the top
    inline constexpr float kMacroSmoothMs = 30.0f;

    // Clock FM depth. Two octaves on a LINEAR knob was unusable past about an
    // eighth of the travel, and the answer to that was an 8x cut of the
    // ceiling -- which deleted the destroyed-pitch region instead of relocating
    // it, and left the knob topping out at exactly the point the original
    // complaint was about.
    //
    // The taper is the fix a bad taper deserves. timeModOctaves is the old
    // linear curve PLUS a quartic tail: at 13 % of the knob it gives 0.0328
    // octaves against the old 0.0325, so every position Roy tuned by ear is
    // unchanged to within one per cent, and the top 30 % is the region that
    // never existed. 15 semitones at the top.
    //
    // ONE helper, used by both application sites and by the UI's arc, because
    // the two sites drifting apart is how the double-count below happened.
    inline constexpr float kTimeModMaxOct = 1.25f;
    inline constexpr float kTimeModClampOct = 1.5f;  // total excursion from everything stacked

    inline float timeModOctaves (float t) noexcept
    {
        const float x = t < 0.0f ? 0.0f : (t > 1.0f ? 1.0f : t);
        return kTimeModMaxOct * (0.2f * x + 0.8f * x * x * x * x);
    }
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
