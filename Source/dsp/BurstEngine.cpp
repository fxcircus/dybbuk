#include "BurstEngine.h"

#include <cmath>
#include <cstring>

namespace
{
constexpr float kOutFloorDb = -60.0f;   // the bottom of the Out range is silence

float dbToGain (float db) noexcept { return juce::Decibels::decibelsToGain (db); }
} // namespace

// --- voice -------------------------------------------------------------------

float BurstEngine::Voice::next() noexcept
{
    float g = gain;
    if (pos < fadeSamples)
        g *= (float) pos / (float) fadeSamples;
    const int remaining = len - pos;
    if (remaining < fadeSamples)
        g *= (float) remaining / (float) fadeSamples;
    const int idx = reverse ? len - 1 - pos : pos;
    ++pos;
    return data[idx] * g;
}

// --- lifecycle ---------------------------------------------------------------

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

    for (auto* s : { &inGain, &outGain, &wetMix, &dryMix })
        s->reset (sr, 0.02);

    reset();
}

void BurstEngine::reset() noexcept
{
    env = 0.0f;
    baseline = 0.0f;
    fillArmed = true;
    preRollPos = 0;
    std::fill (preRoll.begin(), preRoll.end(), 0.0f);
    doClear();
    uiClearsServed.store (0, std::memory_order_relaxed);
    inGain.setCurrentAndTargetValue (1.0f);
    outGain.setCurrentAndTargetValue (1.0f);
    wetMix.setCurrentAndTargetValue (std::sin (juce::MathConstants<float>::halfPi * 0.5f));
    dryMix.setCurrentAndTargetValue (std::cos (juce::MathConstants<float>::halfPi * 0.5f));
}

void BurstEngine::doClear() noexcept
{
    beginMutation();
    count = 0;
    captureSlot = 0;
    sliceLen.fill (0);
    slicePeak.fill (0.0f);
    stepGain.fill (1.0f);
    endMutation();

    gateOpen = false;
    openedFor = 0;
    capWrite = 0;
    voice = {};
    playIndex = -1;
    tickCounter = 0;
    ratchetCounter = 0;
    pendulumDir = 1;
    fillTicksLeft = 0;
    uiFill.store (0.0f, std::memory_order_relaxed);
    uiCurrentStep.store (-1, std::memory_order_relaxed);
    uiGate.store (0.0f, std::memory_order_relaxed);
    publishSteps();
}

void BurstEngine::publishSteps() noexcept
{
    for (int i = 0; i < kMaxSteps; ++i)
    {
        uiStepLevel[(size_t) i].store (i < count ? slicePeak[(size_t) pattern[(size_t) i]] : 0.0f,
                                       std::memory_order_relaxed);
        uiStepGain[(size_t) i].store (i < count ? stepGain[(size_t) i] : 0.0f, std::memory_order_relaxed);
    }
    uiStepCount.store (count, std::memory_order_relaxed);
}

// --- capture -----------------------------------------------------------------

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

// Removes a step from the pattern; its slot becomes free. If the voice was
// reading it, the voice stops, because capture may write there next.
void BurstEngine::dropStep (int index) noexcept
{
    if (index < 0 || index >= count)
        return;
    beginMutation();
    const int slot = pattern[(size_t) index];
    for (int i = index + 1; i < count; ++i)
    {
        pattern[(size_t) (i - 1)] = pattern[(size_t) i];
        stepGain[(size_t) (i - 1)] = stepGain[(size_t) i];
    }
    --count;
    endMutation();
    if (voice.data == slice (slot))
        voice = {};
    if (index <= playIndex)
        --playIndex;
    if (! gateOpen)
        captureSlot = slot;
}

// The gate closed: the step joins the pattern. On the first commit the
// sequencer starts right here (or on the next grid line, synced), so the
// pattern's phase is the moment the first thing you played ended.
void BurstEngine::commit() noexcept
{
    gateOpen = false;
    uiGate.store (0.0f, std::memory_order_relaxed);
    const int len = capWrite;
    capWrite = 0;

    const int ceiling = juce::jlimit (1, kMaxSteps, cur.maxSteps);
    if (count >= ceiling)
    {
        if (! cur.replaceOldest)
            return;                       // Hold: full means full
        // Replace the oldest: the pattern is the last N things you played.
        const int keepSlot = captureSlot;
        while (count >= ceiling)
            dropStep (0);
        captureSlot = keepSlot;
    }

    beginMutation();
    sliceLen[(size_t) captureSlot] = len;
    pattern[(size_t) count] = captureSlot;
    stepGain[(size_t) count] = 1.0f;
    ++count;
    endMutation();

    // Any slot the pattern does not hold is free; the spare guarantees one.
    for (int s = 0; s <= kMaxSteps; ++s)
    {
        bool held = false;
        for (int i = 0; i < count; ++i)
            held = held || pattern[(size_t) i] == s;
        if (! held) { captureSlot = s; break; }
    }
    publishSteps();

    if (count == 1)
    {
        playIndex = -1;
        pendulumDir = 1;
        tickCounter = cur.gridOffsetSamples < 0 ? 0 : samplesToGrid();
    }
}

// Samples from the current sample to the next grid line, from the offset the
// processor resolved for the block start.
int BurstEngine::samplesToGrid() const noexcept
{
    const int stepSamples = juce::jmax (1, juce::roundToInt (cur.stepSeconds * sr));
    int remaining = cur.gridOffsetSamples - blockPos;
    while (remaining < 0)
        remaining += stepSamples;
    return remaining;
}

// --- sequencer ---------------------------------------------------------------

int BurstEngine::activeCount() const noexcept
{
    return juce::jmin (count, juce::jlimit (1, kMaxSteps, cur.maxSteps));
}

int BurstEngine::nextIndex() noexcept
{
    const int n = activeCount();
    if (n <= 1)
        return 0;
    const int i = juce::jlimit (0, n - 1, playIndex);   // -1 before the first step
    switch (cur.direction)
    {
        case Direction::reverse:  return playIndex < 0 ? n - 1 : (i - 1 + n) % n;
        case Direction::pendulum:
            if (playIndex < 0) { pendulumDir = 1; return 0; }
            if (i + pendulumDir < 0 || i + pendulumDir >= n)
                pendulumDir = -pendulumDir;
            return i + pendulumDir;
        case Direction::random:   return (int) (rng.unit() * (float) n) % n;
        case Direction::drunk:    return playIndex < 0 ? 0 : (i + (rng.white() < 0.0f ? -1 : 1) + n) % n;
        case Direction::forward:
        default:                  return playIndex < 0 ? 0 : (i + 1) % n;
    }
}

void BurstEngine::startStep (int index, int stepSamples, bool ratchet, bool reverse) noexcept
{
    const int slot = pattern[(size_t) index];
    const int choke = juce::jmax (fadeSamples * 2, juce::roundToInt ((float) stepSamples * juce::jlimit (0.05f, 1.0f, cur.length01)));
    voice = {};
    voice.data = slice (slot);
    voice.len = juce::jmin (sliceLen[(size_t) slot], ratchet ? juce::jmin (choke, stepSamples / 2) : choke);
    voice.reverse = reverse;
    voice.gain = stepGain[(size_t) index];
    voice.fadeSamples = fadeSamples;
    ratchetCounter = ratchet ? stepSamples / 2 : 0;

    // Fade is paid on the way in, so the play you hear is at the level the
    // step had, and the next one is quieter.
    if (cur.fade01 > 0.0f)
        stepGain[(size_t) index] *= dbToGain (-kFadeMaxDb * juce::jlimit (0.0f, 1.0f, cur.fade01));
    uiCurrentStep.store (index, std::memory_order_relaxed);
}

// Disarmed, an onset scrambles the order for one cycle: the hardware's
// fills. Depth is how many pairs are swapped.
void BurstEngine::beginFill() noexcept
{
    const int n = activeCount();
    if (n < 2)
        return;
    for (int i = 0; i < n; ++i)
        fillOrder[(size_t) i] = i;
    const int swaps = juce::jmax (1, juce::roundToInt ((float) n * juce::jlimit (0.0f, 1.0f, cur.fills01)));
    for (int s = 0; s < swaps; ++s)
    {
        const int a = (int) (rng.unit() * (float) n) % n;
        const int b = (int) (rng.unit() * (float) n) % n;
        std::swap (fillOrder[(size_t) a], fillOrder[(size_t) b]);
    }
    fillTicksLeft = n;
    uiFill.store (1.0f, std::memory_order_relaxed);
}

// Step clock tick: next step, from its start. Material longer than the step
// is faded out at the boundary; shorter material leaves a gap, and the gap is
// part of the sound.
void BurstEngine::advance() noexcept
{
    const int stepSamples = juce::jmax (1, juce::roundToInt (cur.stepSeconds * sr));
    tickCounter = stepSamples;
    uiTicks.fetch_add (1, std::memory_order_relaxed);
    ratchetCounter = 0;

    if (count <= 0)
    {
        voice = {};
        return;
    }

    bool skip = false, ratchet = false, reverse = false, repeat = false;
    const float chance = juce::jlimit (0.0f, 1.0f, cur.chaos01) * kChaosMaxChance;
    if (chance > 0.0f && rng.unit() < chance)
    {
        switch ((int) (rng.unit() * 4.0f) % 4)
        {
            case 0:  skip = true; break;
            case 1:  ratchet = true; break;
            case 2:  reverse = true; break;
            default: repeat = true; break;
        }
    }

    int index = repeat && playIndex >= 0 ? juce::jmin (playIndex, activeCount() - 1) : nextIndex();
    playIndex = index;
    if (fillTicksLeft > 0)
    {
        index = juce::jmin (fillOrder[(size_t) index], activeCount() - 1);
        if (--fillTicksLeft == 0)
            uiFill.store (0.0f, std::memory_order_relaxed);
    }

    // A step that has faded under the floor leaves the pattern instead of
    // playing; the pattern shrinks like a delay dying away.
    const float floor = dbToGain (kFadeFloorDb);
    int guard = kMaxSteps;
    while (count > 0 && guard-- > 0 && stepGain[(size_t) index] < floor)
    {
        dropStep (index);
        publishSteps();
        if (count == 0)
        {
            voice = {};
            playIndex = -1;
            uiCurrentStep.store (-1, std::memory_order_relaxed);
            return;
        }
        index = juce::jmin (index, activeCount() - 1);
        playIndex = index;
    }

    if (skip)
    {
        voice = {};
        uiCurrentStep.store (index, std::memory_order_relaxed);
        return;
    }
    startStep (index, stepSamples, ratchet, reverse);
    publishSteps();
}

// --- process -----------------------------------------------------------------

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

    cur = p;
    const float openLevel = dbToGain (p.thresholdDb);
    const float closeLevel = dbToGain (p.thresholdDb + kCloseBelowDb);
    const float chScale = 1.0f / (float) numCh;
    const float blend = juce::jlimit (0.0f, 1.0f, p.blend01);
    inGain.setTargetValue (dbToGain (p.inputDb));
    outGain.setTargetValue (p.outDb <= kOutFloorDb ? 0.0f : dbToGain (p.outDb));
    wetMix.setTargetValue (std::sin (juce::MathConstants<float>::halfPi * blend));
    dryMix.setTargetValue (blend >= 1.0f ? 0.0f : std::cos (juce::MathConstants<float>::halfPi * blend));   // cos (pi/2) is not 0 in float

    // Synced: the processor says where the next grid line falls in this
    // block, and the tick is put there. Re-resolved every block, so a
    // relocate or a tempo change lands within one block.
    if (p.gridOffsetSamples >= 0 && count > 0)
        tickCounter = p.gridOffsetSamples + 1;

    float inPeak = 0.0f, outPeak = 0.0f;

    for (int i = 0; i < n; ++i)
    {
        blockPos = i;
        const float ig = inGain.getNextValue();
        float x = 0.0f;
        for (int ch = 0; ch < numCh; ++ch)
            x += buffer.getSample (ch, i);
        x *= chScale * ig;
        if (p.bypass)
            x = 0.0f;   // deaf: nothing played while out of circuit is collected
        inPeak = juce::jmax (inPeak, std::abs (x));

        // --- gate ---------------------------------------------------------
        // Peak follower: instant attack so a pick registers within a cycle,
        // decaying release so the gate closes after the note, not during it.
        const float r = std::abs (x);
        env = juce::jmax (r, env + (r - env) * aRelease);

        if (! gateOpen)
        {
            if (p.record)
            {
                if (env > openLevel)
                    onset();
            }
            else
            {
                // Disarmed, the gate drives the fills instead of capture.
                if (fillArmed && env > openLevel)
                {
                    fillArmed = false;
                    if (p.fills01 > 0.0f && fillTicksLeft == 0)
                        beginFill();
                }
                else if (env < closeLevel)
                {
                    fillArmed = true;
                }
            }
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
            else if (ratchetCounter > 0 && --ratchetCounter == 0)
                voice.pos = 0;   // the ratchet: the same slice again, mid-step

            if (voice.active())
                wet = voice.next();
        }
        outPeak = juce::jmax (outPeak, std::abs (wet));

        const float wg = wetMix.getNextValue(), dg = dryMix.getNextValue(), og = outGain.getNextValue();
        for (int ch = 0; ch < numCh; ++ch)
            buffer.setSample (ch, i, (buffer.getSample (ch, i) * ig * dg + wet * wg) * og);
    }

    uiInputLevel.store (inPeak, std::memory_order_relaxed);
    uiOutputLevel.store (outPeak, std::memory_order_relaxed);
}

// --- export ------------------------------------------------------------------

bool BurstEngine::copyPattern (PatternCopy& out) const
{
    for (int attempt = 0; attempt < 16; ++attempt)
    {
        const int g1 = patternGen.load (std::memory_order_acquire);
        if (g1 & 1)
        {
            juce::Thread::yield();
            continue;
        }
        const int n = count;
        if (n < 0 || n > kMaxSteps)
            continue;
        out.steps.assign ((size_t) n, {});
        out.gain.fill (0.0f);
        out.sampleRate = sr;
        for (int i = 0; i < n; ++i)
        {
            const int slot = pattern[(size_t) i];
            if (slot < 0 || slot > kMaxSteps)
                break;
            const int len = juce::jlimit (0, capacity, sliceLen[(size_t) slot]);
            out.steps[(size_t) i].assign (slice (slot), slice (slot) + len);
            out.gain[(size_t) i] = stepGain[(size_t) i];
        }
        std::atomic_thread_fence (std::memory_order_acquire);
        if (patternGen.load (std::memory_order_acquire) == g1)
            return true;
    }
    return false;
}

int BurstEngine::renderPattern (const PatternCopy& pattern, double stepSeconds, float length01,
                                Direction direction, juce::AudioBuffer<float>& out)
{
    const int n = (int) pattern.steps.size();
    if (n == 0 || pattern.sampleRate <= 0.0)
    {
        out.setSize (2, 0);
        return 0;
    }
    const int stepSamples = juce::jmax (1, juce::roundToInt (stepSeconds * pattern.sampleRate));
    const int fade = juce::jmax (1, juce::roundToInt (kFadeMs * 0.001 * pattern.sampleRate));
    const int choke = juce::jmax (fade * 2, juce::roundToInt ((float) stepSamples * juce::jlimit (0.05f, 1.0f, length01)));

    // One cycle in the direction's own order; random and drunk have no
    // cycle, so they export forward.
    std::vector<int> order;
    if (direction == Direction::reverse)
        for (int i = n - 1; i >= 0; --i) order.push_back (i);
    else if (direction == Direction::pendulum && n > 1)
    {
        for (int i = 0; i < n; ++i) order.push_back (i);
        for (int i = n - 2; i >= 1; --i) order.push_back (i);
    }
    else
        for (int i = 0; i < n; ++i) order.push_back (i);

    const int total = (int) order.size() * stepSamples;
    out.setSize (2, total);
    out.clear();
    int pos = 0;
    for (const int idx : order)
    {
        const auto& material = pattern.steps[(size_t) idx];
        Voice v;
        v.data = material.data();
        v.len = juce::jmin ((int) material.size(), choke);
        v.gain = pattern.gain[(size_t) idx];
        v.fadeSamples = fade;
        for (int i = 0; i < stepSamples && v.active(); ++i)
        {
            const float s = v.next();
            out.setSample (0, pos + i, s);
            out.setSample (1, pos + i, s);
        }
        pos += stepSamples;
    }
    return total;
}
