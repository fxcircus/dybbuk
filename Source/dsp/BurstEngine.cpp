#include "BurstEngine.h"

#include <cmath>

void BurstEngine::prepare (double sampleRate, int maxBlockSize)
{
    juce::ignoreUnused (maxBlockSize);
    sr = sampleRate;
    capacity = (int) std::ceil (kMaxStepSeconds * sr);
    pool.assign ((size_t) (kMaxSteps + 1) * (size_t) capacity, 0.0f);

    preRollSamples = juce::jmax (1, juce::roundToInt (kPreRollMs * 0.001 * sr));
    preRoll.assign ((size_t) preRollSamples, 0.0f);
    fadeSamples = juce::jmax (1, juce::roundToInt (kFadeMs * 0.001 * sr));
    holdOffSamples = juce::roundToInt (kHoldOffMs * 0.001 * sr);

    const auto coeff = [this] (float ms) { return 1.0f - std::exp (-1.0f / (ms * 0.001f * (float) sr)); };
    aRelease = coeff (kReleaseMs);
    aBaseRise = coeff (kBaselineRiseMs);
    aBaseFall = coeff (kBaselineFallMs);

    reset();
}

void BurstEngine::reset() noexcept
{
    env = 0.0f;
    baseline = 0.0f;
    preRollPos = 0;
    std::fill (preRoll.begin(), preRoll.end(), 0.0f);
    doClear();
    uiClearsServed.store (0, std::memory_order_relaxed);
}

void BurstEngine::doClear() noexcept
{
    count = 0;
    captureSlot = 0;
    gateOpen = false;
    openedFor = 0;
    capWrite = 0;
    playIndex = -1;
    playSlot = -1;
    playPos = 0;
    playLen = 0;
    tickCounter = 0;
    sliceLen.fill (0);
    slicePeak.fill (0.0f);
    for (auto& l : uiStepLevel)
        l.store (0.0f, std::memory_order_relaxed);
    uiStepCount.store (0, std::memory_order_relaxed);
    uiCurrentStep.store (-1, std::memory_order_relaxed);
    uiGate.store (0.0f, std::memory_order_relaxed);
}

// The gate just opened: start a step with the pre-roll at its head.
void BurstEngine::onset() noexcept
{
    gateOpen = true;
    openedFor = 0;
    capWrite = 0;
    slicePeak[(size_t) captureSlot] = 0.0f;
    float* dst = slice (captureSlot);
    for (int i = 0; i < preRollSamples; ++i)
        dst[capWrite++] = preRoll[(size_t) ((preRollPos + i) % preRollSamples)];
    baseline = env;
    uiGate.store (1.0f, std::memory_order_relaxed);
}

// The gate closed: the step joins the pattern. On the first commit the
// sequencer starts right here, so the pattern's phase is the moment the
// first thing you played ended.
void BurstEngine::commit() noexcept
{
    gateOpen = false;
    uiGate.store (0.0f, std::memory_order_relaxed);
    sliceLen[(size_t) captureSlot] = capWrite;
    capWrite = 0;

    const int ceiling = juce::jlimit (1, kMaxSteps, pendingMaxSteps);
    if (count >= ceiling)
    {
        // Replace the oldest: the pattern is the last N things you played.
        const int dropped = pattern[0];
        for (int i = 1; i < count; ++i)
            pattern[(size_t) (i - 1)] = pattern[(size_t) i];
        count = juce::jmin (count, ceiling) - 1;
        if (playSlot == dropped)
            playSlot = -1;
        playIndex = juce::jmax (-1, playIndex - 1);
        pattern[(size_t) count++] = captureSlot;
        captureSlot = dropped;
    }
    else
    {
        pattern[(size_t) count++] = captureSlot;
        // Any slot the pattern does not hold is free; the spare guarantees one.
        for (int s = 0; s <= kMaxSteps; ++s)
        {
            bool held = false;
            for (int i = 0; i < count; ++i)
                held = held || pattern[(size_t) i] == s;
            if (! held) { captureSlot = s; break; }
        }
    }

    for (int i = 0; i < kMaxSteps; ++i)
        uiStepLevel[(size_t) i].store (i < count ? slicePeak[(size_t) pattern[(size_t) i]] : 0.0f,
                                       std::memory_order_relaxed);
    uiStepCount.store (count, std::memory_order_relaxed);

    if (count == 1 && playIndex < 0)
    {
        playIndex = -1;
        tickCounter = 0;   // advance on the very next sample
    }
}

// Step clock tick: next step, from its start. Material longer than the step
// is faded out at the boundary; shorter material leaves a gap, and the gap is
// part of the sound.
void BurstEngine::advance() noexcept
{
    const int len = juce::jmax (1, juce::roundToInt (pendingStepMs * 0.001 * sr));
    tickCounter = len;
    if (count <= 0)
    {
        playSlot = -1;
        return;
    }
    playIndex = (playIndex + 1) % count;
    playSlot = pattern[(size_t) playIndex];
    playPos = 0;
    playLen = juce::jmin (sliceLen[(size_t) playSlot], len);
    uiCurrentStep.store (playIndex, std::memory_order_relaxed);
}

void BurstEngine::process (juce::AudioBuffer<float>& buffer, const Params& p)
{
    const int numCh = buffer.getNumChannels();
    const int n = buffer.getNumSamples();
    if (numCh <= 0 || n <= 0 || capacity <= 0)
        return;

    if (const int req = clearRequests.load (std::memory_order_relaxed); req != clearsSeen)
    {
        clearsSeen = req;
        doClear();
        uiClearsServed.fetch_add (1, std::memory_order_relaxed);
    }

    pendingStepMs = p.stepMs;
    pendingMaxSteps = p.maxSteps;
    const float openLevel = juce::Decibels::decibelsToGain (p.thresholdDb);
    const float closeLevel = juce::Decibels::decibelsToGain (p.thresholdDb + kCloseBelowDb);
    const float wetGain = juce::jlimit (0.0f, 1.0f, p.mix01);
    const float dryGain = 1.0f - wetGain;
    const float chScale = 1.0f / (float) numCh;

    float inPeak = 0.0f, outPeak = 0.0f;

    for (int i = 0; i < n; ++i)
    {
        float x = 0.0f;
        for (int ch = 0; ch < numCh; ++ch)
            x += buffer.getSample (ch, i);
        x *= chScale;
        inPeak = juce::jmax (inPeak, std::abs (x));

        // --- gate ---------------------------------------------------------
        const float r = std::abs (x);
        // Peak follower: instant attack so a pick registers within a cycle,
        // decaying release so the gate closes after the note, not during it.
        env = juce::jmax (r, env + (r - env) * aRelease);

        // Disarmed, the gate stays shut (later it will drive the fills).
        if (! gateOpen)
        {
            if (p.record && env > openLevel)
                onset();
        }
        else
        {
            ++openedFor;
            if (env < closeLevel)
            {
                commit();
            }
            else if (openedFor > holdOffSamples && env > openLevel
                     && env > baseline * kReattackRatio)
            {
                commit();
                onset();
            }
        }
        baseline += (env - baseline) * (env > baseline ? aBaseRise : aBaseFall);

        preRoll[(size_t) preRollPos] = x;
        preRollPos = (preRollPos + 1) % preRollSamples;

        // --- capture -------------------------------------------------------
        if (gateOpen)
        {
            float* dst = slice (captureSlot);
            dst[capWrite++] = x;
            slicePeak[(size_t) captureSlot] = juce::jmax (slicePeak[(size_t) captureSlot], r);
            if (capWrite >= capacity)
                commit();
        }

        // --- sequencer ----------------------------------------------------
        float wet = 0.0f;
        if (count > 0)
        {
            if (--tickCounter <= 0)
                advance();

            if (playSlot >= 0 && playPos < playLen)
            {
                float g = 1.0f;
                if (playPos < fadeSamples)
                    g = (float) playPos / (float) fadeSamples;
                const int remaining = playLen - playPos;
                if (remaining < fadeSamples)
                    g = juce::jmin (g, (float) remaining / (float) fadeSamples);
                wet = slice (playSlot)[playPos] * g;
                ++playPos;
            }
        }
        outPeak = juce::jmax (outPeak, std::abs (wet));

        for (int ch = 0; ch < numCh; ++ch)
            buffer.setSample (ch, i, buffer.getSample (ch, i) * dryGain + wet * wetGain);
    }

    uiInputLevel.store (inPeak, std::memory_order_relaxed);
    uiOutputLevel.store (outPeak, std::memory_order_relaxed);
}
