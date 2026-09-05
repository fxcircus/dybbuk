#pragma once

#include <cmath>

// Every tunable number for the PT2399 emulation lives here, so the character
// can be retuned by ear without hunting through the DSP. Values are the
// calibration in docs/design/01-core.md; the "why" for each is there.
//
// Compile-time A/B switches (kStages, kGuardPoles, kFeedbackFromTapSum,
// kFmLawLinear, kNoiseShaping) exist because the plan's open questions are
// listening calls, not analysis calls. Flip, rebuild, listen.

#ifndef DYBBUK_PT_STAGES
 #define DYBBUK_PT_STAGES 3
#endif

namespace pt
{
    // --- topology -----------------------------------------------------------
    inline constexpr int   kStages      = DYBBUK_PT_STAGES; // 3 chips in series: the "three-step repeat"
    inline constexpr int   kMemoryWords = 5504;             // 44 Kbit at 8 bits, the PT2399's RAM
    inline constexpr int   kStageWords  = kMemoryWords / kStages;
    inline constexpr int   kTotalWords  = kStageWords * kStages; // 5502 at three stages

    // Tap weights are normalised to sum to 1 so "Decay 1.15" really is 15 per
    // cent over unity round the loop. That costs the first echo 8 dB, which
    // kWetMakeup can give back on the wet output only (outside the feedback
    // path). It defaults to 1.0: raising it also lifts the noise floor by the
    // same 8 dB, and the plan's "-90 dBFS at short times" is the more
    // quotable spec. Revisit once Blend and Out staging has been auditioned.
    inline constexpr float kTapWeightRaw[3] = { 1.0f, 0.85f, 0.7f };
    inline constexpr float kWetMakeup       = 1.0f;
    inline constexpr bool  kFeedbackFromTapSum = true;  // false = pure series loop, taps still feed the output

    // --- clock --------------------------------------------------------------
    inline constexpr float kFsChipMax     = 200000.0f;  // Time 0: 27.5 ms, in spec, clean
    inline constexpr float kFsChipMin     = 1500.0f;    // Time 1: 3.67 s, far past spec, destroyed
    inline constexpr float kFsChipHardMax = 250000.0f;  // FM excursion headroom before clamping
    inline constexpr float kFsChipHardMin = 750.0f;
    inline constexpr float kLogFsMax      = 12.2060726f; // ln(200000)
    inline constexpr float kLogRange      = 4.8928527f;  // ln(200000 / 1500)
    inline constexpr float kInvLogRange   = 0.2043807f;
    inline constexpr float kLn2           = 0.6931472f;
    inline constexpr float kPi            = 3.14159265f;
    inline constexpr float kTwoPi         = 6.28318531f;

    inline constexpr float kTimeSmoothSec = 0.020f;  // knob sweeps warble rather than zipper
    inline constexpr int   kCtrlInterval  = 16;      // slow coefficients refresh every 16 samples

    // --- the silicon --------------------------------------------------------
    inline constexpr float kClipKnee     = 0.8f;   // the chip's own 1.3 Vrms input ceiling
    inline constexpr float kInvClipSpan  = 5.0f;   // 1 / (1 - kClipKnee)
    inline constexpr float kSigmaDriveMin   = 0.265f; // THD 0.13 % at A_REF 0.5, 31 ms
    inline constexpr float kSigmaDriveSlope = 2.0f;   // ... rising to 1 % at 342 ms
    inline constexpr float kSigmaBias       = 0.03f;  // even harmonics grow with Time, as the chip's do
    inline constexpr float kBitsMax   = 11.0f;
    inline constexpr float kBitsMin   = 8.0f;
    inline constexpr float kBitsCurve = 2.0f;   // the bit drop happens mostly past spec
    inline constexpr bool  kNoiseShaping = true; // first-order error feedback: the delta-sigma SNR collapse
    inline constexpr float kNoiseDbShort = -86.0f;
    inline constexpr float kNoiseDbLong  = -48.0f;
    inline constexpr float kHissFc       = 6000.0f; // hiss enters after reconstruction, or it is rumble at Time 1

    // --- the board around the chip -----------------------------------------
    inline constexpr float kInMfbFc  = 8800.0f; // 3-pole MFB anti-alias in front of the chip
    inline constexpr float kInMfbQ   = 0.9f;
    inline constexpr float kRecTrack = 0.45f;   // reconstruction tracks min(0.45 fs_chip, 8 kHz)
    inline constexpr float kRecFcMax = 8000.0f;
    inline constexpr float kRecQ     = 0.6f;
    inline constexpr float kOutMfbFc = 4500.0f; // fixed darkness: "flat to about 1 kHz then rolls off"
    inline constexpr float kOutMfbQ  = 0.6f;
    inline constexpr int   kGuardPoles = 1;     // 0, 1 or 2 tracking write-guard poles (long-Time character)

    // Clock bleed: the ticking and burbling the Strega exposes instead of hiding.
    inline constexpr float kBleedOnsetHz  = 20000.0f; // audible from 275 ms down
    inline constexpr float kBleedMaxAmp   = 0.01f;    // -40 dBFS at the bottom of the range
    inline constexpr float kBleedCurve    = 1.5f;
    inline constexpr float kInvLogBleed   = 0.3860600f; // 1 / ln(20000 / 1500)
    inline constexpr float kBleedSubRatio = 0.5f;       // fs_chip/2 burble under the tick train
    inline constexpr float kBleedTickTau  = 40.0e-6f;

    // --- the loop -----------------------------------------------------------
    inline constexpr float kSvfSatLimit  = 0.5f;  // bounds self-oscillation, analog style
    inline constexpr float kResCurve     = 1.5f;
    inline constexpr float kResOverdrive = 0.03f; // the last of the knob goes active (negative damping)
    inline constexpr float kResOverdriveStart = 0.92f;
    inline constexpr float kAbsorbShelfFc  = 1200.0f;
    inline constexpr float kAbsorbShelfMax = 0.7f;  // -10.5 dB of HF per iteration at full Absorb
    inline constexpr float kAbsorbDrive    = 1.0f;  // and extra grit into the saturator
    inline constexpr float kAbsorbOutMaxDb = 18.0f; // how much Absorb quiets the wet
    inline constexpr float kAbsorbFbMaxDb  = 4.0f;  // how much it shortens Decay
    inline constexpr float kAbsorbFeedbackShare = 1.0f;
    inline constexpr float kSatDrive = 1.0f;
    inline constexpr float kSatBias  = 0.05f;  // slight asymmetry: warmth, even harmonics
    inline constexpr float kDcBlockHz = 10.0f;
    inline constexpr float kDecayMax  = 1.15f;
    inline constexpr float kDecaySmoothSec = 0.03f;
    inline constexpr float kClearFadeSec   = 0.006f; // a hard cut of a -4 dBFS runaway clicks
    inline constexpr float kInputCeiling   = 1.0e4f; // per-sample non-finite guard
    inline constexpr float kEnergyReleaseSec = 0.05f; // ember: peak with a 50 ms release

    inline constexpr bool  kFmLawLinear = false; // false = exponential (octaves), true = linear in clock rate

    // --- shared helpers -----------------------------------------------------

    // Pade tanh. Cubic coefficient is 8/27 = 0.296 (true tanh: 1/3), which is
    // what the THD calibration above is pinned to. Bounded by the clamp.
    inline float fastTanh (float x) noexcept
    {
        x = x < -3.0f ? -3.0f : (x > 3.0f ? 3.0f : x);
        const float x2 = x * x;
        return x * (27.0f + x2) / (27.0f + 9.0f * x2);
    }

    inline float fsChipForTime01 (float t) noexcept
    {
        return kFsChipMax * std::exp (-kLogRange * (t < 0.0f ? 0.0f : (t > 1.0f ? 1.0f : t)));
    }

    inline float delaySecondsForTime01 (float t) noexcept
    {
        return (float) kTotalWords / fsChipForTime01 (t);
    }

    // Inverse, for the Time Sync clamp and the delay-time readout.
    inline float time01ForDelaySeconds (float seconds) noexcept
    {
        const float fs = (float) kTotalWords / (seconds > 1.0e-6f ? seconds : 1.0e-6f);
        const float t = (kLogFsMax - std::log (fs)) * kInvLogRange;
        return t < 0.0f ? 0.0f : (t > 1.0f ? 1.0f : t);
    }

    inline constexpr float kDelayMinSec = 0.02751f; // kTotalWords / kFsChipMax
    inline constexpr float kDelayMaxSec = 3.668f;   // kTotalWords / kFsChipMin
} // namespace pt
