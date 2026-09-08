#include "ChipClock.h"

void ChipClock::prepare (double sampleRate)
{
    sr = sampleRate;
    invSr = 1.0f / (float) sampleRate;
    logFsSmooth.reset (sampleRate, (double) pt::kTimeSmoothSec);
    reset();
}

void ChipClock::reset() noexcept
{
    phase = 0.0f;
    ctrlCountdown = 0; // the next advance() refreshes before anything reads the frame
}

void ChipClock::snapTime() noexcept
{
    // Without this the smoother starts at zero, the clock ramps up from the
    // 750 Hz hard minimum over 20 ms, and everything written during that ramp
    // is read back time-compressed into a chirp about 27 ms later.
    logFsSmooth.setCurrentAndTargetValue (logFsSmooth.getTargetValue());
}

void ChipClock::setTime01 (float t) noexcept
{
    const float clamped = t < 0.0f ? 0.0f : (t > 1.0f ? 1.0f : t);
    // A linear ramp in LOG fs is a constant-rate pitch glide, which is what a
    // clock sweep sounds like. Ramping fs itself would glide fast then slow.
    logFsSmooth.setTargetValue (pt::kLogFsMax - clamped * pt::kLogRange);
}

void ChipClock::refreshSlow (float fs, float logFs) noexcept
{
    frame.ctrlStamp = ++ctrlStamp;
    frame.fsChip = fs;

    // Two coordinates, not one.
    //
    // frame.u keeps its old meaning -- where the CLOCK is, clamped to 0..1 --
    // because the reconstruction filter and anything the UI reads should follow
    // the clock and nothing else.
    //
    // uDeg is how DESTROYED the chip is, and it has two inputs: the clock, and
    // Crust. It is computed from the clamped fs rather than the raw logFs and
    // is allowed past 1, so a deep downward FM dive now gets dirtier as well as
    // darker -- before, u saturated at the nominal minimum and a dive collapsed
    // the bandwidth while adding no grit at all, which is the "push it and it
    // just gets damped" shape in miniature.
    const float u = juce::jlimit (0.0f, 1.0f, (pt::kLogFsMax - logFs) * pt::kInvLogRange);
    frame.u = u;

    const float uClock = (pt::kLogFsMax - std::log (fs)) * pt::kInvLogRange;
    const float uDeg = juce::jlimit (0.0f, pt::kUDegMax,
                                     uClock + crust01 * (pt::kUDegMax - uClock));

    // Reconstruction tracks the clock: this is what collapses bandwidth as
    // Time grows, with no extra parameter.
    // Crust closes the reconstruction filter too, four octaves at full: the
    // bandwidth collapse is most of what a badly overclocked chip sounds like,
    // and without it Crust would only be noise and grit on a still-bright
    // delay, which is a bitcrusher rather than a tired PT2399.
    const float fcRec = juce::jmin (pt::kRecTrack * fs,
                                    pt::kRecFcMax * std::exp2 (-pt::kCrustBandOct * crust01));
    frame.aDac   = 1.0f - std::exp (-pt::kTwoPi * fcRec / fs);   // at CHIP rate: stops shaped noise folding on read
    frame.aGuard = 1.0f - std::exp (-pt::kTwoPi * fcRec * invSr); // at host rate: bites only once fs_chip < fs_host
    frame.recG   = std::tan (pt::kPi * juce::jmin (fcRec, 0.45f * (float) sr) * invSr);

    const float d = pt::kSigmaDriveMin * std::exp (pt::kSigmaDriveSlope * uDeg);
    frame.d = d;
    frame.invD = 1.0f / d;
    frame.biasComp = pt::fastTanh (d * pt::kSigmaBias) * frame.invD;

    const float bits = juce::jmax (pt::kBitsFloor,
                                   pt::kBitsMax - (pt::kBitsMax - pt::kBitsMin)
                                                      * std::pow (uDeg, pt::kBitsCurve));
    // step and invStep are always recomputed as a PAIR. Ramping them apart
    // would put up to 1 dB of gain wobble through the quantizer.
    frame.step = std::exp2 (1.0f - bits);
    frame.invStep = 1.0f / frame.step;

    frame.noiseAmp = std::pow (10.0f, (pt::kNoiseDbShort
                                       + (pt::kNoiseDbLong - pt::kNoiseDbShort) * uDeg) * 0.05f);

    if (fs >= pt::kBleedOnsetHz)
    {
        frame.bleedLvl = 0.0f;
    }
    else
    {
        // CLAMPED, and it must stay clamped: kInvLogBleed normalises against
        // 1500 Hz, so below that x exceeds 1 and the bleed rises past its
        // calibrated maximum -- 1.43x at 750 Hz, 2.07x at 500 Hz. Widening the
        // clock's hard minimum without this quietly re-opens the pitched squeal
        // that was removed by ear in 6917f69 (fs/2 at a 1 s delay is 2751 Hz,
        // which is the 2.75 kHz tone that was reported).
        const float x = juce::jmin (1.0f, std::log (pt::kBleedOnsetHz / fs) * pt::kInvLogBleed);
        frame.bleedLvl = pt::kBleedMaxAmp * std::pow (x, pt::kBleedCurve);
    }
}
