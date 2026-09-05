#include "PTStage.h"

void PTStage::prepare (double sampleRate)
{
    const float sr = (float) sampleRate;
    const float nyquistGuard = 0.45f * sr;

    inPole.a = OnePole::coeffFor (juce::jmin (pt::kInMfbFc, nyquistGuard), sampleRate);
    inSvf.setK (1.0f / pt::kInMfbQ);
    inSvf.setG (std::tan (pt::kPi * juce::jmin (pt::kInMfbFc, nyquistGuard) / sr));

    outSvf.setK (1.0f / pt::kOutMfbQ);
    outSvf.setG (std::tan (pt::kPi * juce::jmin (pt::kOutMfbFc, nyquistGuard) / sr));

    recSvf.setK (1.0f / pt::kRecQ);
    recSvf.setG (std::tan (pt::kPi * juce::jmin (pt::kRecFcMax, nyquistGuard) / sr));

    hissPole.a = OnePole::coeffFor (juce::jmin (pt::kHissFc, nyquistGuard), sampleRate);

    // White noise of a fixed per-sample amplitude spreads its power over
    // 0 to sr/2, so holding the in-band DENSITY constant as the sample rate
    // rises means scaling the amplitude up, not down.
    noiseSrScale = std::sqrt (sr / 48000.0f);

    bleedDecay = std::exp (-1.0f / (pt::kBleedTickTau * sr));

    // Seeded from the clock and this object's address, so two instances with
    // identical settings never produce bit-identical output (the plan's tell
    // 4: the same knob settings never land in the same place twice).
    // seedForTests() overrides this after prepare when a test needs repeats.
    rng.seed ((unsigned int) (juce::Time::getHighResolutionTicks()
                              ^ (juce::int64) (juce::pointer_sized_int) this));

    reset();
}

void PTStage::reset() noexcept
{
    core.reset();
    inPole.reset();
    guard1.reset();
    guard2.reset();
    hissPole.reset();
    inSvf.reset();
    recSvf.reset();
    outSvf.reset();
    xPrev = 0.0f;
    bleedEnv = 0.0f;
    sub = 1.0f;
    lastCtrlStamp = 0;
}
