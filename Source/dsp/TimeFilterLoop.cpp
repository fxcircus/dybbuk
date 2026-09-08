#include "TimeFilterLoop.h"

#include "ModConstants.h"

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
    crustSmooth.reset (sampleRate, (double) pt::kCrustSmoothSec);
    colourSmooth.reset (sampleRate, (double) pt::kCrustSmoothSec);
    cutoffSmooth.setCurrentAndTargetValue (18000.0f);

    // Normalised so the tap sum has unity gain: that is what makes "Decay
    // 1.15" mean 15 per cent over unity round the loop.
    float sum = 0.0f;
    for (int i = 0; i < pt::kStages; ++i)
        sum += pt::kTapWeightRaw[i];
    for (int i = 0; i < pt::kStages; ++i)
        tapWeight[(size_t) i] = pt::kTapWeightRaw[i] / sum;

    clearStep = 1.0f / (pt::kClearFadeSec * (float) sampleRate);
    aLoopEnvAttack = 1.0f - std::exp (-1.0f / (modk::kLoopEnvAttackMs * 0.001f * (float) sampleRate));
    aLoopEnvRelease = 1.0f - std::exp (-1.0f / (modk::kLoopEnvReleaseMs * 0.001f * (float) sampleRate));
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
    loopEnv = 0.0f;
    lastOut = 0.0f;
}

void TimeFilterLoop::refreshLoopCoeffs() noexcept
{
    // Knob value first (smoothed), then the modulation offset on top. Filter
    // modulation is in octaves so a sweep sounds the same wherever the knob
    // sits, which is the trap the sibling plugin's tone sweep already fell in.
    const float knobHz = cutoffSmooth.skip (pt::kCtrlInterval);

    // Upward modulation is COMPRESSED against the ceiling rather than clipped
    // at it. A hard clamp squared off the top of every sweep -- at the old
    // default of 8 kHz that was 52 per cent of each agitation cycle sitting
    // flat against a wall, so what got through was a flat-topped square rather
    // than the contour the generator was producing. It was also 0.45 * sr, so
    // the same patch swept a full octave further at 96 kHz than at 48.
    float m = modFilterOct;
    if (m > 0.0f)
    {
        const float up = std::log2 (juce::jmin (0.45f * (float) sr, pt::kFilterModCeilHz)
                                    / juce::jmax (knobHz, 1.0f));
        m = up > 0.05f ? up * pt::fastTanh (m / up) : 0.0f;
    }
    // Downward keeps its hard floor, which is correct: there is no wall at
    // 20 Hz, the knob simply stops.
    const float modulatedHz = std::abs (m) > 1.0e-9f ? knobHz * std::exp2 (m) : knobHz;
    const float fc = juce::jlimit (20.0f, 0.45f * (float) sr, modulatedHz);
    loopFilter.setG (std::tan (pt::kPi * fc / (float) sr));

    const float res = juce::jlimit (0.0f, 1.0f, resSmooth.skip (pt::kCtrlInterval) + modResonance);
    // k = 1/Q. The last of the knob travel takes k slightly negative, which
    // is an actively resonating filter; the state limit is what bounds it.
    const float over = juce::jlimit (0.0f, 1.0f,
                                     (res - pt::kResOverdriveStart) / (1.0f - pt::kResOverdriveStart));
    const float k = 2.0f * std::pow (1.0f - res, pt::kResCurve)
                    - pt::kResOverdrive * (over * over * (3.0f - 2.0f * over));
    loopFilter.setK (k);

    const float a = juce::jlimit (0.0f, 1.0f, absorbSmooth.skip (pt::kCtrlInterval) + modAbsorb);
    absorbShelfDepth = pt::kAbsorbShelfMax * a;
    sat.setDrive (pt::kSatDrive * (1.0f + pt::kAbsorbDrive * a));
    absorbOutGain = std::pow (10.0f, -pt::kAbsorbOutMaxDb * a * 0.05f);
    absorbFbGain = std::pow (10.0f, -pt::kAbsorbFbMaxDb * pt::kAbsorbFeedbackShare * a * 0.05f);

    // The KNOB is bounded by kDecayMax; the knob PLUS modulation is allowed
    // past it. Clamping the sum to the same ceiling meant that at the top of
    // Decay the follower contributed exactly nothing, so the one gesture that
    // could make the loop surge and recover was clipped away precisely where it
    // would have mattered.
    decayGain = juce::jlimit (0.0f, pt::kDecayMax * pt::kDecayModHeadroom,
                              decaySmooth.skip (pt::kCtrlInterval) + modDecay);

    clock.setCrust01 (crustSmooth.skip (pt::kCtrlInterval));
    loopFilter.setMode (colourSmooth.skip (pt::kCtrlInterval));
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

    modFilterOct = p.filterModOct;
    modResonance = p.resonanceMod;
    modDecay = p.decayMod;
    modAbsorb = p.absorbMod;

    clock.setTime01 (p.time01);
    decaySmooth.setTargetValue (juce::jlimit (0.0f, pt::kDecayMax, p.decay));
    cutoffSmooth.setTargetValue (juce::jlimit (20.0f, 0.45f * (float) sr, p.filterHz));
    resSmooth.setTargetValue (juce::jlimit (0.0f, 1.0f, p.resonance01));
    absorbSmooth.setTargetValue (juce::jlimit (0.0f, 1.0f, p.absorb01));
    crustSmooth.setTargetValue (juce::jlimit (0.0f, 1.0f, p.crust01));
    colourSmooth.setTargetValue (juce::jlimit (0.0f, 1.0f, p.colour01));

    if (firstBlock || snapRequested.exchange (false, std::memory_order_acquire))
    {
        // Snap, or the clock ramps up from its hard minimum over 20 ms and
        // everything written during the ramp comes back as a chirp.
        clock.snapTime();
        decaySmooth.setCurrentAndTargetValue (decaySmooth.getTargetValue());
        cutoffSmooth.setCurrentAndTargetValue (cutoffSmooth.getTargetValue());
        resSmooth.setCurrentAndTargetValue (resSmooth.getTargetValue());
        absorbSmooth.setCurrentAndTargetValue (absorbSmooth.getTargetValue());
        crustSmooth.setCurrentAndTargetValue (crustSmooth.getTargetValue());
        colourSmooth.setCurrentAndTargetValue (colourSmooth.getTargetValue());
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

        // The bleed rides the loop's own level: with nothing in the delay
        // there is nothing for the clock to bleed into.
        const float bleedGate = juce::jlimit (0.0f, 1.0f, loopEnv * pt::kBleedGateScale);

        float s = node;
        float tapSum = 0.0f;
        for (int k = 0; k < pt::kStages; ++k)
        {
            s = stages[(size_t) k].processSample (s, f, bleedGate);
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

        // The loop's own envelope, which is what Interference listens to.
        loopEnv += (mag - loopEnv) * (mag > loopEnv ? aLoopEnvAttack : aLoopEnvRelease);
        lastOut = y;

        lastFsChip = f.fsChip;
    }

    // Peak with a 50 ms release, so the ember reads the same at any block size.
    const float db = 20.0f * std::log10 (juce::jmax (energyEnv, 1.0e-7f));
    uiLoopEnergy.store (juce::jlimit (0.0f, 1.0f, (db + 60.0f) / 60.0f), std::memory_order_relaxed);
    uiDelaySeconds.store ((float) pt::kTotalWords / lastFsChip, std::memory_order_relaxed);
}
