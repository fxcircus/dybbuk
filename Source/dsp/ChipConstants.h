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

    // Tap weights are normalised to sum to 1 so "Decay 1.45" really is 45 per
    // cent over unity round the loop. Normalisation costs the first arrival,
    // and at { 1, 0.85, 0.7 } it cost 8.13 dB: the wet sat that far under the
    // dry by construction, so equal-power Blend had to pass 76 % before the
    // effect merely MATCHED the source. Weighting the first tap more heavily
    // buys some of that back at ZERO noise-floor cost, because it changes the
    // mix rather than the gain -- which is exactly why this and not
    // kWetMakeup, which would lift the hiss by the same amount it lifts the
    // signal and cost the plan's "-90 dBFS at short times". It also lifts the
    // feedback comb's nulls, so off-grid material dies more slowly.
    //
    // How far to go is bounded by the plan's tell 1, the tape smear on a Time
    // sweep, and that boundary is sharp. `EngineTest repitch` ramps Time under
    // a 450 Hz tone and tracks the summed wet's pitch: at { 1, 0.7, 0.5 } it
    // still bends to 325 Hz, and at { 1, 0.6, 0.4 } it only reaches 400 Hz,
    // because the shortest tap is the one that repitches least and once it
    // dominates the mix it is what you hear. So this stops at +1.28 dB rather
    // than the +2.79 dB { 1, 0.5, 0.35 } would give: the extra 1.5 dB costs
    // the one behaviour the plan says to stop and tune on.
    inline constexpr float kTapWeightRaw[3] = { 1.0f, 0.7f, 0.5f };
    inline constexpr float kWetMakeup       = 1.0f;

    // Selects the node that feeds BOTH the loop filter and the wet output, so
    // false does not mean "pure series loop, taps still feed the output" as it
    // once claimed: it deletes the three-step repeat from the output too and
    // makes the plugin a single 3D echo. Do not flip it expecting an A/B. An
    // honest split needs a second loop filter, Absorb shelf and saturator with
    // their own state; the tap weights above take what the flip was reaching
    // for, which was a shallower comb.
    inline constexpr bool  kFeedbackFromTapSum = true;

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
    //
    // Tuned down hard after a playthrough. Once the clock falls into the audio
    // band a steady pulse train is not "ticking", it is a tone: at a 1 s delay
    // the fs/2 square sat at 2.75 kHz and -44 dBFS, audible with no input at
    // all, and Clear could not touch it because the clock makes it rather than
    // the buffer. Three changes: the fs/2 square is off, the level is 15 dB
    // lower, and what is left is gated by what is actually in the loop, so it
    // rides the repeats instead of standing on its own.
    inline constexpr float kBleedOnsetHz  = 20000.0f; // audible from 275 ms down
    inline constexpr float kBleedMaxAmp   = 0.0018f;  // -55 dBFS at the bottom of the range
    inline constexpr float kBleedCurve    = 1.5f;
    inline constexpr float kInvLogBleed   = 0.3860600f; // 1 / ln(20000 / 1500)
    inline constexpr float kBleedSubRatio = 0.0f;       // the fs_chip/2 square: the squeal, off
    inline constexpr float kBleedTickTau  = 40.0e-6f;
    inline constexpr float kBleedGateScale = 4.0f;      // loop level at which bleed reaches full

    // --- the loop -----------------------------------------------------------
    // Bounds self-oscillation, analog style. It is an ABSOLUTE clamp on the
    // filter's bandpass state, so it is also a compressor: at 0.5 the state
    // saturated at the level the loop actually runs at, and `EngineTest
    // resonance` measured the whole Resonance knob as worth 5.8 dB there
    // (+37.9 dB at an amplitude of 0.001 against -1.5 dB at 0.5 -- turning
    // Resonance up during a runaway REDUCED the loop gain). Four times the
    // headroom moves the bound off the integrator and onto the loop saturator,
    // which is where TimeFilterLoop's own comment always said it was.
    inline constexpr float kSvfSatLimit  = 2.0f;
    // k = 2 * (1 - res)^kResCurve, then the overdrive term takes k negative.
    // At curve 1.5 and start 0.92 the whole approach to oscillation happened
    // inside four of a hundred steps, so the knob was a switch: nothing, then
    // squeal. Curve 2.0 spreads the useful Q over the whole travel and start
    // 0.78 makes the active region twelve steps wide instead of four.
    inline constexpr float kResCurve     = 2.0f;
    inline constexpr float kResOverdrive = 0.08f; // the last of the knob goes active (negative damping)
    inline constexpr float kResOverdriveStart = 0.78f;
    inline constexpr float kAbsorbShelfFc  = 1200.0f;
    // Absorb was four attenuators on one knob and nothing additive, so the one
    // control the manual makes central to "the age and quality of the tape"
    // was indistinguishable from turning Out down. Two of the four come in and
    // the character moves into the third: the per-iteration HF shelf nearly
    // doubles, so Absorb compounds into darkness over repeats instead of
    // taking 18 dB off the wet in one go against an Out fader that stops at
    // +6. The feedback trim is the one that matters most: at 4 dB against a
    // budget of 1.2 dB, a fifth of this knob vetoed the whole runaway zone.
    inline constexpr float kAbsorbShelfMax = 0.9f;  // -20 dB of HF per iteration at full Absorb
    inline constexpr float kAbsorbDrive    = 1.0f;  // and extra grit into the saturator
    inline constexpr float kAbsorbOutMaxDb = 6.0f;  // how much Absorb quiets the wet
    inline constexpr float kAbsorbFbMaxDb  = 2.0f;  // how much it shortens Decay
    inline constexpr float kAbsorbFeedbackShare = 1.0f;
    // Drive is normalised by 1/drive inside the saturator, so raising it does
    // not raise the small-signal gain: it lowers the tanh asymptote (1.05 at
    // drive 1.0, 0.875 at 1.2) and squares the runaway off sooner. It is the
    // one lever that raises the RMS of a self-oscillation while LOWERING its
    // peak, which is what pays for the bigger Decay budget below.
    inline constexpr float kSatDrive = 1.2f;
    inline constexpr float kSatBias  = 0.05f;  // slight asymmetry: warmth, even harmonics
    inline constexpr float kDcBlockHz = 10.0f;
    // The runaway zone. At 1.15 the whole zone was 1.21 dB of excess against
    // about 0.23 dB of loop loss, so self-oscillation always settled as a sine
    // (measured crest 1.41 at every setting) and Absorb's 4 dB per iteration
    // vetoed it outright at a fifth of its travel. The knob's sub-unity half
    // is unchanged: Parameters.cpp maps unity to kDecayUnityNorm of the travel
    // so every position below 1.0 sits exactly where it did, and the new range
    // is spent entirely on the red zone.
    inline constexpr float kDecayMax  = 1.45f;
    inline constexpr float kDecayUnityNorm = 0.8695652f; // = 1/1.15: where unity sat, and stays
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
