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

    const float u = juce::jlimit (0.0f, 1.0f, (pt::kLogFsMax - logFs) * pt::kInvLogRange);
    frame.u = u;

    // Reconstruction tracks the clock: this is what collapses bandwidth as
    // Time grows, with no extra parameter.
    const float fcRec = juce::jmin (pt::kRecTrack * fs, pt::kRecFcMax);
    frame.aDac   = 1.0f - std::exp (-pt::kTwoPi * fcRec / fs);   // at CHIP rate: stops shaped noise folding on read
    frame.aGuard = 1.0f - std::exp (-pt::kTwoPi * fcRec * invSr); // at host rate: bites only once fs_chip < fs_host
    frame.recG   = std::tan (pt::kPi * juce::jmin (fcRec, 0.45f * (float) sr) * invSr);

    const float d = pt::kSigmaDriveMin * std::exp (pt::kSigmaDriveSlope * u);
    frame.d = d;
    frame.invD = 1.0f / d;
    frame.biasComp = pt::fastTanh (d * pt::kSigmaBias) * frame.invD;

    const float bits = pt::kBitsMax - (pt::kBitsMax - pt::kBitsMin) * std::pow (u, pt::kBitsCurve);
    // step and invStep are always recomputed as a PAIR. Ramping them apart
    // would put up to 1 dB of gain wobble through the quantizer.
    frame.step = std::exp2 (1.0f - bits);
    frame.invStep = 1.0f / frame.step;

    frame.noiseAmp = std::pow (10.0f, (pt::kNoiseDbShort
                                       + (pt::kNoiseDbLong - pt::kNoiseDbShort) * u) * 0.05f);

    if (fs >= pt::kBleedOnsetHz)
    {
        frame.bleedLvl = 0.0f;
    }
    else
    {
        const float x = std::log (pt::kBleedOnsetHz / fs) * pt::kInvLogBleed;
        frame.bleedLvl = pt::kBleedMaxAmp * std::pow (x, pt::kBleedCurve);
    }
}
