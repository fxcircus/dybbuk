#include "TimeFilterLoop.h"

void TimeFilterLoop::prepare (double sampleRate, int maxBlockSize)
{
    juce::ignoreUnused (maxBlockSize); // the loop holds no scratch; the engine chunks
    sr = sampleRate;

    clock.prepare (sampleRate);
    for (auto& stage : stages)
        stage.prepare (sampleRate);

    loopFilter.setStateLimit (pt::kSvfSatLimit);
    absorbShelf.a = OnePole::coeffFor (pt::kAbsorbShelfFc, sampleRate);
    sat.prepare (sampleRate);

    const double smoothSec = (double) pt::kDecaySmoothSec;
    decaySmooth.reset (sampleRate, smoothSec);
    resSmooth.reset (sampleRate, smoothSec);
    absorbSmooth.reset (sampleRate, smoothSec);
    cutoffSmooth.reset (sampleRate, smoothSec);
    cutoffSmooth.setCurrentAndTargetValue (18000.0f);

    // Normalised so the tap sum has unity gain: that is what makes "Decay
    // 1.15" mean 15 per cent over unity round the loop.
    float sum = 0.0f;
    for (int i = 0; i < pt::kStages; ++i)
        sum += pt::kTapWeightRaw[i];
    for (int i = 0; i < pt::kStages; ++i)
        tapWeight[(size_t) i] = pt::kTapWeightRaw[i] / sum;

    clearStep = 1.0f / (pt::kClearFadeSec * (float) sampleRate);
    energyRelease = std::exp (-1.0f / (pt::kEnergyReleaseSec * (float) sampleRate));

    firstBlock = true;
    reset();
}

void TimeFilterLoop::reset() noexcept
{
    flushAll();
    clearPhase = ClearPhase::idle;
    clearGain = 1.0f;
    energyEnv = 0.0f;
    controlCountdown = 0;
}

void TimeFilterLoop::seedForTests (unsigned int s) noexcept
{
    for (int i = 0; i < pt::kStages; ++i)
        stages[(size_t) i].seedForTests (s + 977u * (unsigned int) i);
}

void TimeFilterLoop::flushAll() noexcept
{
    for (auto& stage : stages)
        stage.reset();
    loopFilter.reset();
    absorbShelf.reset();
    sat.reset();
    clock.reset();
    fb = 0.0f;
}

void TimeFilterLoop::refreshLoopCoeffs() noexcept
{
    const float fc = juce::jlimit (20.0f, 0.45f * (float) sr, cutoffSmooth.skip (pt::kCtrlInterval));
    loopFilter.setG (std::tan (pt::kPi * fc / (float) sr));

    const float res = resSmooth.skip (pt::kCtrlInterval);
    // k = 1/Q. The last of the knob travel takes k slightly negative, which
    // is an actively resonating filter; the state limit is what bounds it.
    const float over = juce::jlimit (0.0f, 1.0f,
                                     (res - pt::kResOverdriveStart) / (1.0f - pt::kResOverdriveStart));
    const float k = 2.0f * std::pow (1.0f - res, pt::kResCurve)
                    - pt::kResOverdrive * (over * over * (3.0f - 2.0f * over));
    loopFilter.setK (k);

    const float a = absorbSmooth.skip (pt::kCtrlInterval);
    absorbShelfDepth = pt::kAbsorbShelfMax * a;
    sat.setDrive (pt::kSatDrive * (1.0f + pt::kAbsorbDrive * a));
    absorbOutGain = std::pow (10.0f, -pt::kAbsorbOutMaxDb * a * 0.05f);
    absorbFbGain = std::pow (10.0f, -pt::kAbsorbFbMaxDb * pt::kAbsorbFeedbackShare * a * 0.05f);

    decayGain = decaySmooth.skip (pt::kCtrlInterval);
}

void TimeFilterLoop::process (const float* in, float* wet, int n, const Params& p, const float* modOct)
{
    if (n <= 0)
        return;

    const int requested = clearCounter.load (std::memory_order_acquire);
    if (requested != lastClearSeen)
    {
        lastClearSeen = requested;
        clearPhase = ClearPhase::fadingOut; // a hard cut of a runaway would click
    }

    if (! std::isfinite (fb)) // backstop: a poisoned loop never recovers on its own
        flushAll();

    clock.setTime01 (p.time01);
    decaySmooth.setTargetValue (juce::jlimit (0.0f, pt::kDecayMax, p.decay));
    cutoffSmooth.setTargetValue (juce::jlimit (20.0f, 0.45f * (float) sr, p.filterHz));
    resSmooth.setTargetValue (juce::jlimit (0.0f, 1.0f, p.resonance01));
    absorbSmooth.setTargetValue (juce::jlimit (0.0f, 1.0f, p.absorb01));

    if (firstBlock || snapRequested.exchange (false, std::memory_order_acquire))
    {
        // Snap, or the clock ramps up from its hard minimum over 20 ms and
        // everything written during the ramp comes back as a chirp.
        clock.snapTime();
        decaySmooth.setCurrentAndTargetValue (decaySmooth.getTargetValue());
        cutoffSmooth.setCurrentAndTargetValue (cutoffSmooth.getTargetValue());
        resSmooth.setCurrentAndTargetValue (resSmooth.getTargetValue());
        absorbSmooth.setCurrentAndTargetValue (absorbSmooth.getTargetValue());
        controlCountdown = 0;
        refreshLoopCoeffs();
        firstBlock = false;
    }

    float lastFsChip = clock.currentFsChip();

    for (int i = 0; i < n; ++i)
    {
        const ChipClock::Frame& f = clock.advance (modOct != nullptr ? modOct[i] : 0.0f);

        if (--controlCountdown <= 0)
        {
            controlCountdown = pt::kCtrlInterval;
            refreshLoopCoeffs();
        }

        float x = in[i];
        if (! (std::abs (x) < pt::kInputCeiling)) // catches NaN, inf and absurd values in one test
            x = 0.0f;

        const float node = x + fb;

        float s = node;
        float tapSum = 0.0f;
        for (int k = 0; k < pt::kStages; ++k)
        {
            s = stages[(size_t) k].processSample (s, f);
            tapSum += tapWeight[(size_t) k] * s;
        }

        // Tap sum feeds the filter (the plan's topology). The alternative is
        // a pure series loop where the delay is the whole chain; that is the
        // kFeedbackFromTapSum A/B, and it changes the comb the runaway locks
        // onto, so it has to be judged by ear.
        float y = loopFilter.lowpass (pt::kFeedbackFromTapSum ? tapSum : s);

        // Absorb darkens as it attenuates: Filter and Absorb together are
        // what the manual calls the age and quality of the tape.
        const float hp = y - absorbShelf.lp (y);
        y -= absorbShelfDepth * hp;

        y = sat.process (y);

        if (clearPhase == ClearPhase::fadingOut)
        {
            clearGain -= clearStep;
            if (clearGain <= 0.0f)
            {
                clearGain = 0.0f;
                flushAll(); // a memset, no allocation and no lock
                clearPhase = ClearPhase::fadingIn;
                uiClearsServed.fetch_add (1, std::memory_order_relaxed);
            }
        }
        else if (clearPhase == ClearPhase::fadingIn)
        {
            clearGain += clearStep;
            if (clearGain >= 1.0f)
            {
                clearGain = 1.0f;
                clearPhase = ClearPhase::idle;
            }
        }

        y *= clearGain;

        fb = y * decayGain * absorbFbGain;
        wet[i] = y * absorbOutGain * pt::kWetMakeup;

        const float mag = std::abs (y);
        energyEnv = mag > energyEnv ? mag : energyEnv * energyRelease;

        lastFsChip = f.fsChip;
    }

    // Peak with a 50 ms release, so the ember reads the same at any block size.
    const float db = 20.0f * std::log10 (juce::jmax (energyEnv, 1.0e-7f));
    uiLoopEnergy.store (juce::jlimit (0.0f, 1.0f, (db + 60.0f) / 60.0f), std::memory_order_relaxed);
    uiDelaySeconds.store ((float) pt::kTotalWords / lastFsChip, std::memory_order_relaxed);
}
