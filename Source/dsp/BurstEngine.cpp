#include "BurstEngine.h"

#include <cmath>
#include <cstring>

namespace
{
constexpr float kOutFloorDb = -60.0f;   // the bottom of the Out range is silence

float dbToGain (float db) noexcept { return juce::Decibels::decibelsToGain (db); }
} // namespace

// --- voice -------------------------------------------------------------------

float BurstEngine::Voice::next (float playRate) noexcept
{
    const double p = pos;
    pos += (double) juce::jmax (0.01f, playRate * rateMul);

    // The fades are in material samples, so at a high rate they are short
    // in time: still enough to take the click off a cut.
    float g = gain;
    if (p - start < (double) fadeSamples)
        g *= (float) ((p - start) / (double) fadeSamples);
    const double remaining = (double) len - p;
    if (remaining < (double) fadeSamples)
        g *= (float) (remaining / (double) fadeSamples);

    double rp = reverse ? (double) (len - 1) - p : p;
    if (rp < 0.0)
        rp = 0.0;
    const int i0 = juce::jmin (len - 1, (int) rp);
    const float frac = (float) (rp - (double) i0);
    const float a = data[i0];
    const float b = i0 + 1 < len ? data[i0 + 1] : a;
    return (a + (b - a) * frac) * g;
}

void BurstEngine::Grains::begin (const float* d, int n, double headStart, double adv, int grainSize, int outSamples, Rng& rng) noexcept
{
    scatter = false;
    data = d;
    len = n;
    head = juce::jlimit (0.0, (double) juce::jmax (0, n - 1), headStart);
    advance = adv;
    size = juce::jmax (2, grainSize);
    left = outSamples;
    // The second grain starts half a grain in, so the two windows always
    // sum to one.
    for (int k = 0; k < 2; ++k)
    {
        start[k] = head;
        age[k] = k == 0 ? 0 : size / 2;
    }
    juce::ignoreUnused (rng);
}

float BurstEngine::Grains::next (float rate, Rng& rng) noexcept
{
    if (! active())
        return 0.0f;
    float out = 0.0f;
    const double lastIndex = (double) (len - 1);
    for (int k = 0; k < 2; ++k)
    {
        if (age[k] >= size)
        {
            // Respawn near the head, at the offset that best continues the
            // waveform this grain was reading (the WSOLA idea): the samples
            // just before each candidate are matched against the samples
            // this grain just read, so a stretched or frozen note keeps its
            // phase from grain to grain instead of smearing. At real time
            // the head already continues it, so no search.
            // The reference is what the OTHER grain is reading right now, so
            // the two stay in phase with each other, not each with its own past.
            const int other = k ^ 1;
            const double continuing = start[other] + (double) age[other] * (double) (rate * rateMul);
            double best = scatter ? (double) (rng.unit() * (float) juce::jmax (1, len - size - 1)) : head;
            const int win = juce::jmin (256, size / 2);
            const int reach = size / 4;
            const int cEnd = (int) continuing;            // the sample the other grain reads now
            if (! scatter && std::abs (advance) < 0.999 && len > size * 2 && cEnd - win >= 0 && cEnd <= len)
            {
                float bestScore = -1.0e30f;
                for (int off = -reach; off <= reach; off += 2)
                {
                    const int hEnd = (int) head + off;      // candidate start; its history is the win before it
                    if (hEnd - win < 0 || hEnd + size >= len)
                        continue;
                    float score = 0.0f, energy = 1.0e-9f;
                    for (int i = 0; i < win; ++i)
                    {
                        const float a = data[cEnd - win + i], b = data[hEnd - win + i];
                        score += a * b;
                        energy += b * b;
                    }
                    score /= std::sqrt (energy);
                    if (score > bestScore)
                    {
                        bestScore = score;
                        best = (double) hEnd;
                    }
                }
            }
            start[k] = juce::jlimit (0.0, lastIndex, best);
            age[k] = 0;
        }
        const float w = 0.5f - 0.5f * std::cos (juce::MathConstants<float>::twoPi * (float) age[k] / (float) size);
        double rp = start[k] + (double) age[k] * (double) (rate * rateMul);
        if (rp > lastIndex)
            rp = lastIndex;
        const int i0 = (int) rp;
        const float frac = (float) (rp - (double) i0);
        const float a = data[i0];
        const float b = i0 + 1 < len ? data[i0 + 1] : a;
        out += (a + (b - a) * frac) * w;
        ++age[k];
    }
    head = juce::jlimit (0.0, juce::jmax (0.0, lastIndex - (double) size), head + advance);   // a grain always fits after the head
    --left;
    return out * gain;
}

bool BurstEngine::StepVoice::sounding() const noexcept
{
    if (stretch.active() || rattleLeft > 0)
        return true;
    for (int k = 0; k < voices; ++k)
        if (v[k].active())
            return true;
    return false;
}

void BurstEngine::StepVoice::restart() noexcept
{
    for (int k = 0; k < voices; ++k)
        v[k].pos = v[k].start;
    if (stretch.data != nullptr)
    {
        stretch.head = stretch.start[0];
        stretch.age[0] = 0;
        stretch.age[1] = stretch.size / 2;
    }
}

void BurstEngine::StepVoice::stop() noexcept
{
    for (auto& x : v)
        x = {};
    voices = 0;
    stretch = {};
    rattleLeft = 0;
}

float BurstEngine::StepVoice::next (float rate, Rng& rng) noexcept
{
    if (stretch.data != nullptr)
        return stretch.next (rate, rng);
    if (rattleLeft > 0)
    {
        // The buzz: the slice again the moment it ends, for as long as the step lasts.
        if (! v[0].active() && v[0].data != nullptr)
            v[0].pos = v[0].start;
        --rattleLeft;
    }
    float out = 0.0f;
    for (int k = 0; k < voices; ++k)
        if (v[k].active())
            out += v[k].next (rate);
    return out;
}

const float* BurstEngine::StepVoice::material() const noexcept
{
    return stretch.data != nullptr ? stretch.data : (voices > 0 ? v[0].data : nullptr);
}

int BurstEngine::StepVoice::materialLen() const noexcept
{
    return stretch.data != nullptr ? stretch.len : (voices > 0 ? v[0].len : 0);
}

double BurstEngine::StepVoice::reached() const noexcept
{
    return stretch.data != nullptr ? stretch.head : (voices > 0 ? juce::jmin (v[0].pos, (double) v[0].len) : 0.0);
}

void BurstEngine::Haunts::clear() noexcept
{
    for (int k = 0; k < kMax; ++k)
    {
        layer[k] = {};
        panL[k] = panR[k] = 1.0f;
    }
}

// A new moment takes the quietest slot: the oldest haunting gives way.
void BurstEngine::Haunts::spawn (const float* material, int len, double reached, int grainSize, float gain,
                                 float pl, float pr, Rng& rng) noexcept
{
    if (material == nullptr || len < grainSize * 2 || gain <= 0.0f)
        return;
    int slot = -1;
    for (int k = 0; k < kMax && slot < 0; ++k)
        if (! layer[k].active())
            slot = k;
    if (slot < 0)
    {
        slot = 0;
        for (int k = 1; k < kMax; ++k)
            if (layer[k].gain < layer[slot].gain)
                slot = k;
    }
    const double at = juce::jlimit (0.0, (double) (len - grainSize - 1), reached - (double) grainSize);
    layer[slot].begin (material, len, at, 0.0, grainSize, 1 << 30, rng);
    layer[slot].gain = gain;
    panL[slot] = pl;
    panR[slot] = pr;
}

void BurstEngine::Haunts::tick (float decayPerTick) noexcept
{
    for (int k = 0; k < kMax; ++k)
    {
        layer[k].gain *= decayPerTick;
        if (layer[k].gain < kWraithGain * 0.1f)   // -20 dB down: gone
            layer[k] = {};
    }
}

void BurstEngine::Haunts::release (float perSample) noexcept
{
    for (int k = 0; k < kMax; ++k)
    {
        if (layer[k].data == nullptr)
            continue;
        layer[k].gain *= perSample;
        if (layer[k].gain < 0.0005f)
            layer[k] = {};
    }
}

void BurstEngine::Haunts::next (float rate, Rng& rng, float& l, float& r) noexcept
{
    for (int k = 0; k < kMax; ++k)
    {
        if (! layer[k].active())
            continue;
        const float s = layer[k].next (rate, rng);
        l += s * panL[k];
        r += s * panR[k];
    }
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
    // A per-sample multiplier that reaches -60 dB in kWraithReleaseMs.
    aWraithRelease = std::pow (0.001f, 1.0f / (kWraithReleaseMs * 0.001f * (float) sr));
    aBaseFall = coeff (kBaselineFallMs);

    for (auto* s : { &inGain, &outGain, &wetMix, &dryMix, &rate, &glueAmount })
        s->reset (sr, 0.02);
    glueL.prepare (sr);
    glueR.prepare (sr);

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
    rate.setCurrentAndTargetValue (1.0f);
    currentRate = 1.0f;
    glueAmount.setCurrentAndTargetValue (0.0f);
    glueL.reset();
    glueR.reset();
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
    voice.stop();
    wraiths.clear();
    barCountdown = 0;
    playIndex = -1;
    tickCounter = 0;
    ratchetCounter = 0;
    ratchetPeriod = 0;
    ratchetsLeft = 0;
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
    if (voice.material() == slice (slot))
        voice.stop();
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
    // The gate closes a release after the sound stops, so the slice ends in
    // silence. Trim it to what was audible (plus a little), or Trance would
    // stretch the silence and Wraith would freeze it.
    int len = capWrite;
    {
        const float* src = slice (captureSlot);
        const float floorLevel = slicePeak[(size_t) captureSlot] * 0.001f;   // -60 dB under the peak
        int last = 0;
        for (int i = 0; i < capWrite; ++i)
            if (std::abs (src[i]) > floorLevel)
                last = i;
        len = juce::jmin (capWrite, last + juce::roundToInt (0.005 * sr));
    }
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
    uiCommits.fetch_add (1, std::memory_order_relaxed);

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

int BurstEngine::grainSizeFor (double sampleRate, int materialLen) noexcept
{
    const int wanted = juce::roundToInt (kGrainMs * 0.001 * sampleRate);
    return juce::jmax (juce::roundToInt (0.002 * sampleRate), juce::jmin (wanted, materialLen / 2));
}

float BurstEngine::hauntDecayPerTick (float length01) noexcept
{
    const int ticks = 1 + juce::roundToInt ((float) (kWraithMaxTicks - 1) * juce::jlimit (0.0f, 1.0f, length01));
    return std::pow (10.0f, -0.9f / (float) ticks);   // -18 dB over `ticks` ticks, then it is dropped
}

// One step starting, in any mode. The choke is a fraction of the step in
// output time; at this step's rate that is this much material. A ratchet
// divides the step.
void BurstEngine::startStepVoice (StepVoice& sv, const float* material, int len, float stepGain,
                                  const StepSetup& s, const Deviation& d, Rng& rng) noexcept
{
    sv.stop();
    panFor (s.index, s.spread01, sv.panL, sv.panR);
    const int window = juce::jmax (1, s.stepSamples / juce::jmax (1, d.ratchets));
    const float share = juce::jlimit (0.05f, 1.0f, s.length01 * d.choke01);
    const int offset = juce::jlimit (0, juce::jmax (0, len - s.fadeSamples * 4), juce::roundToInt ((float) len * d.offset01));
    const float gain = stepGain * d.gainMul;

    if (s.mode == Mode::miasma)
    {
        // A cloud: grains from anywhere in the material, for the whole
        // share of the step. Decay is the grain, 10 to 80 ms: short is a
        // crackle, long is a wash.
        const float dec = juce::jlimit (0.0f, 1.0f, (s.length01 - 0.05f) / 0.95f);
        const int grain = juce::jmax (juce::roundToInt (0.002 * s.sampleRate),
                                      juce::jmin (len / 2, juce::roundToInt ((0.010 + 0.070 * dec) * s.sampleRate)));
        const int outSamples = juce::jmax (s.fadeSamples * 2, window);
        sv.stretch.begin (material, len, (double) offset, 0.0, grain, outSamples, rng);
        sv.stretch.scatter = true;
        sv.stretch.gain = gain;
        sv.stretch.rateMul = d.rateMulOr1();
        return;
    }

    if (s.mode == Mode::trance)
    {
        // A slowdown from the start of the material: Decay says how many
        // times slower (1x at its floor, 8x at the top, on a square so the
        // bottom half is subtle), and the step holds what fits. It used to
        // stretch material to fill the step, which left any note longer than
        // the step untouched, so Trance seemed to do nothing until Pitch
        // shortened the material (Roy, playing it).
        const float dec = juce::jlimit (0.0f, 1.0f, (s.length01 - 0.05f) / 0.95f);
        const double factor = 1.0 + 7.0 * (double) (dec * dec);
        const int grain = grainSizeFor (s.sampleRate, len);
        // The whole material, tail included, lasts factor times longer; the
        // head stops a grain before the end and the last grain holds the tail.
        const double remaining = (double) juce::jmax (1, len - offset);
        const int outSamples = juce::jmax (s.fadeSamples * 2, juce::jmin (window, (int) (remaining * factor)));
        const double advance = (1.0 / factor) * (d.reverse ? -1.0 : 1.0);
        sv.stretch.begin (material, len, d.reverse ? (double) (len - 1 - grain) : (double) offset, advance,
                          grain, outSamples, rng);
        sv.stretch.gain = gain;
        sv.stretch.rateMul = d.rateMulOr1();
        return;
    }

    // Golem, Wraith and Tremor play the material; Legion plays it three
    // times over at intervals. Wraith is never choked: the whole moment is
    // what will be left behind.
    const float stepRate = s.rate * d.rateMulOr1();
    // Rattle: the slice is Decay's 10 to 60 ms of material, and it loops
    // for the whole step (see StepVoice::next).
    const int rattleSlice = juce::roundToInt ((0.010 + 0.050 * juce::jlimit (0.0f, 1.0f, (s.length01 - 0.05f) / 0.95f)) * s.sampleRate);
    const int choke = s.mode == Mode::wraith
                          ? len
                          : (s.mode == Mode::rattle
                                 ? juce::jmax (s.fadeSamples * 2, rattleSlice)
                                 : juce::jmax (s.fadeSamples * 2, juce::roundToInt ((float) window * share * (s.mode == Mode::trance ? 1.0f : stepRate))));
    const int n = s.mode == Mode::legion ? 3 : 1;
    sv.voices = n;
    for (int k = 0; k < n; ++k)
    {
        Voice& v = sv.v[k];
        v = {};
        v.data = material;
        v.start = (double) offset;
        v.pos = v.start;
        v.len = juce::jmin (len, offset + choke);
        v.reverse = s.mode == Mode::mirror ? ! d.reverse : d.reverse;   // Mirror: everything backwards; chaos flips it back now and then
        v.fadeSamples = s.fadeSamples;
        v.gain = gain;
        v.rateMul = d.rateMulOr1();
        if (s.mode == Mode::legion)
        {
            // Pitch is the interval here: unison, up and down by it. At
            // zero it is octaves, the classic many-voices sound; a few
            // cents of chorus was not enough to hear (Roy).
            const float st = s.pitchSemitones;
            const float interval = st == 0.0f ? 12.0f : st;
            const float semis = k == 0 ? 0.0f : (k == 1 ? interval : -interval);
            v.rateMul *= std::pow (2.0f, semis / 12.0f);
            v.gain *= 0.6f;
            // The extra voices come and go.
            if (k > 0 && rng.unit() > 0.7f)
                v.len = 0;
        }
    }
    sv.rattleLeft = s.mode == Mode::rattle ? window : 0;
}

void BurstEngine::startStep (int index, int stepSamples, const Deviation& d) noexcept
{
    const int slot = pattern[(size_t) index];
    StepSetup s;
    s.mode = cur.mode;
    s.length01 = cur.length01;
    s.pitchSemitones = cur.pitchSemitones;
    s.spread01 = cur.spread01;
    s.index = index;
    s.stepSamples = stepSamples;
    s.rate = currentRate;
    s.fadeSamples = fadeSamples;
    s.sampleRate = sr;
    startStepVoice (voice, slice (slot), sliceLen[(size_t) slot], stepGain[(size_t) index], s, d, rng);

    const int window = juce::jmax (1, stepSamples / juce::jmax (1, d.ratchets));
    ratchetPeriod = d.ratchets > 1 ? window : 0;
    ratchetsLeft = d.ratchets > 1 ? d.ratchets - 1 : 0;
    ratchetCounter = ratchetPeriod;

    // Feedback is paid on the way in, so the play you hear is at the level
    // the step had, and the next one is quieter.
    if (cur.feedback01 < 1.0f)
        stepGain[(size_t) index] *= juce::jmax (0.0f, cur.feedback01);
    uiCurrentStep.store (index, std::memory_order_relaxed);
}

// Chaos: what happens to a step besides being played. At low depth one
// mild thing now and then; at full depth most steps get something and
// some get three things at once. Every event stays on the clock: a step
// still starts on its tick, so it is never arrhythmic, only wrong.
BurstEngine::Deviation BurstEngine::rollChaos() noexcept
{
    Deviation d;
    const float c = juce::jlimit (0.0f, 1.0f, cur.chaos01);
    if (c <= 0.0f || rng.unit() >= c * kChaosMaxChance)
        return d;

    int events = 1;
    if (c > 0.5f && rng.unit() < (c - 0.5f) * 1.6f) ++events;
    if (c > 0.75f && rng.unit() < (c - 0.75f) * 2.0f) ++events;

    for (int e = 0; e < events; ++e)
    {
        switch ((int) (rng.unit() * 9.0f) % 9)
        {
            case 0: d.skip = true; break;
            case 1: d.ratchets = 2 + (int) (rng.unit() * 3.0f) % 3; break;                 // 2, 3 or 4
            case 2: d.reverse = true; break;
            case 3: d.repeat = true; break;
            case 4: d.jump = true; break;
            case 5:
            {
                // Musical intervals at any depth; past half, seconds and thirds too.
                static constexpr float mild[] = { -12.0f, -7.0f, -5.0f, 5.0f, 7.0f, 12.0f };
                static constexpr float sour[] = { -12.0f, -7.0f, -5.0f, -3.0f, -2.0f, 2.0f, 3.0f, 5.0f, 7.0f, 12.0f };
                d.semitones = c > 0.5f ? sour[(int) (rng.unit() * 10.0f) % 10] : mild[(int) (rng.unit() * 6.0f) % 6];
                break;
            }
            case 6: d.offset01 = 0.15f + 0.6f * rng.unit(); break;                          // start mid-material
            case 7: d.choke01 = 0.15f + 0.3f * rng.unit(); break;                           // a clipped note
            default: d.gainMul = rng.unit() < 0.5f ? 1.5f : 0.35f; break;                   // accent or ghost
        }
    }
    return d;
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

    // Wraith: the step that just ended leaves its moment behind, and every
    // haunting already there fades a little.
    if (cur.mode == Mode::wraith)
    {
        wraiths.tick (hauntDecayPerTick (cur.length01));
        if (voice.material() != nullptr)
            wraiths.spawn (voice.material(), voice.materialLen(), voice.reached(),
                          grainSizeFor (sr, voice.materialLen()), kWraithGain, voice.panL, voice.panR, rng);
    }

    if (count <= 0)
    {
        voice.stop();
        return;
    }

    Deviation d = rollChaos();

    // Tremor: a hand on the pattern. While the input is hot the current
    // step is held and ratcheted, Fills setting how densely.
    const bool seized = cur.mode == Mode::tremor && inputHot && playIndex >= 0;
    if (seized)
    {
        d.repeat = true;
        d.jump = false;
        d.skip = false;
        d.ratchets = juce::jmax (d.ratchets, 1 + juce::roundToInt (3.0f * juce::jlimit (0.0f, 1.0f, cur.fills01)));
    }

    int index = d.jump                       ? (int) (rng.unit() * (float) activeCount()) % activeCount()
                : (d.repeat && playIndex >= 0) ? juce::jmin (playIndex, activeCount() - 1)
                                               : nextIndex();
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
            voice.stop();
            playIndex = -1;
            uiCurrentStep.store (-1, std::memory_order_relaxed);
            return;
        }
        index = juce::jmin (index, activeCount() - 1);
        playIndex = index;
    }

    if (d.skip)
    {
        voice.stop();
        uiCurrentStep.store (index, std::memory_order_relaxed);
        return;
    }
    startStep (index, stepSamples, d);
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
    // Legion reads Pitch as the interval between its voices, so the global
    // rate goes to unity there.
    rate.setTargetValue (p.mode == Mode::legion ? 1.0f : rateForSemitones (juce::jlimit (-24.0f, 24.0f, p.pitchSemitones)));
    glueAmount.setTargetValue (juce::jlimit (0.0f, 1.0f, p.glue01));
    dryMix.setTargetValue (blend >= 1.0f ? 0.0f : std::cos (juce::MathConstants<float>::halfPi * blend));   // cos (pi/2) is not 0 in float

    // Synced: the processor says where the next grid line falls in this
    // block, and the tick is put there. Re-resolved every block, so a
    // relocate or a tempo change lands within one block.
    if (p.gridOffsetSamples >= 0 && count > 0)
        tickCounter = p.gridOffsetSamples + 1;
    // Bar: the pattern restarts from its first step on the bar line, even
    // if that line falls between grid ticks (a dotted or triplet division).
    barCountdown = p.barReset && p.barOffsetSamples >= 0 && count > 0 ? p.barOffsetSamples + 1 : 0;

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
        inputHot = env > openLevel;

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
                    if (p.fills01 > 0.0f && fillTicksLeft == 0 && p.mode != Mode::tremor)
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
        float wetL = 0.0f, wetR = 0.0f;
        currentRate = rate.getNextValue();
        if (count > 0)
        {
            if (barCountdown > 0 && --barCountdown == 0)
            {
                playIndex = -1;
                pendulumDir = 1;
                tickCounter = 1;
            }
            if (--tickCounter <= 0)
                advance();
            else if (ratchetCounter > 0 && --ratchetCounter == 0 && ratchetsLeft > 0)
            {
                // The ratchet: the same slice again, from where it began.
                voice.restart();
                --ratchetsLeft;
                ratchetCounter = ratchetsLeft > 0 ? ratchetPeriod : 0;
            }

            if (voice.sounding())
            {
                const float w = voice.next (currentRate, rng);
                wetL = w * voice.panL;
                wetR = w * voice.panR;
            }
        }
        // Nothing sustains a haunting once the mode has moved on or the
        // pattern has emptied: there are no more ticks to decay it, so it
        // lets go here instead of hanging over every mode that follows.
        if (p.mode != Mode::wraith || count == 0)
            wraiths.release (aWraithRelease);
        wraiths.next (currentRate, rng, wetL, wetR);

        // Glue, end of the pattern's chain and before the blend: the old
        // loop's saturator, level-matched (see GlueStage), one per side. At
        // zero it is skipped entirely, so Glue off is bit-exact.
        const float ga = glueAmount.getNextValue();
        if (ga > 0.0f)
        {
            const float drive = glueDrive (ga);
            wetL = glueL.process (wetL, drive);
            wetR = glueR.process (wetR, drive);
        }
        outPeak = juce::jmax (outPeak, juce::jmax (std::abs (wetL), std::abs (wetR)));

        const float wg = wetMix.getNextValue(), dg = dryMix.getNextValue(), og = outGain.getNextValue();
        for (int ch = 0; ch < numCh; ++ch)
        {
            const float w = numCh < 2 ? 0.5f * (wetL + wetR) : (ch == 0 ? wetL : (ch == 1 ? wetR : 0.5f * (wetL + wetR)));
            buffer.setSample (ch, i, (buffer.getSample (ch, i) * ig * dg + w * wg) * og);
        }
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

int BurstEngine::renderPattern (const PatternCopy& pattern, const RenderSettings& st, juce::AudioBuffer<float>& out)
{
    const float playRate = rateForSemitones (juce::jlimit (-24.0f, 24.0f, st.pitchSemitones));
    const int n = (int) pattern.steps.size();
    if (n == 0 || pattern.sampleRate <= 0.0)
    {
        out.setSize (2, 0);
        return 0;
    }
    const int stepSamples = juce::jmax (1, juce::roundToInt (st.stepSeconds * pattern.sampleRate));
    const int fade = juce::jmax (1, juce::roundToInt (kFadeMs * 0.001 * pattern.sampleRate));

    // One cycle in the direction's own order; random and drunk have no
    // cycle, so they export forward.
    std::vector<int> order;
    if (st.direction == Direction::reverse)
        for (int i = n - 1; i >= 0; --i) order.push_back (i);
    else if (st.direction == Direction::pendulum && n > 1)
    {
        for (int i = 0; i < n; ++i) order.push_back (i);
        for (int i = n - 2; i >= 1; --i) order.push_back (i);
    }
    else
        for (int i = 0; i < n; ++i) order.push_back (i);

    // The same players as the live sequencer, fresh, seeded the same way
    // every time so an export is repeatable. Legion, whose voices come and
    // go, plays all three here; Tremor has no input to seize with.
    Rng rng;
    rng.seed (7u);
    StepVoice sv;
    Haunts wraiths;
    wraiths.clear();
    GlueStage gl, gr;
    gl.prepare (pattern.sampleRate);
    gr.prepare (pattern.sampleRate);
    const float ga = juce::jlimit (0.0f, 1.0f, st.glue01);
    const float drive = glueDrive (ga);

    StepSetup setup;
    setup.mode = st.mode == Mode::tremor ? Mode::golem : st.mode;
    setup.length01 = st.length01;
    setup.pitchSemitones = st.pitchSemitones;
    setup.spread01 = st.spread01;
    setup.stepSamples = stepSamples;
    setup.rate = st.mode == Mode::legion ? 1.0f : playRate;
    setup.fadeSamples = fade;
    setup.sampleRate = pattern.sampleRate;

    const int total = (int) order.size() * stepSamples;
    out.setSize (2, total);
    out.clear();
    int pos = 0;
    for (const int idx : order)
    {
        const auto& material = pattern.steps[(size_t) idx];
        if (setup.mode == Mode::wraith)
        {
            wraiths.tick (hauntDecayPerTick (st.length01));
            if (sv.material() != nullptr)
                wraiths.spawn (sv.material(), sv.materialLen(), sv.reached(), grainSizeFor (pattern.sampleRate, sv.materialLen()),
                              kWraithGain, sv.panL, sv.panR, rng);
        }
        setup.index = idx;
        Deviation none;
        startStepVoice (sv, material.data(), (int) material.size(), pattern.gain[(size_t) idx], setup, none, rng);
        if (setup.mode == Mode::legion)
            for (auto& v : sv.v)
                if (v.data != nullptr && v.len == 0)
                    v.len = sv.v[0].len;   // every voice sings in the export

        for (int i = 0; i < stepSamples; ++i)
        {
            float l = 0.0f, r = 0.0f;
            if (sv.sounding())
            {
                const float w = sv.next (setup.rate, rng);
                l = w * sv.panL;
                r = w * sv.panR;
            }
            wraiths.next (setup.rate, rng, l, r);
            if (ga > 0.0f)
            {
                l = gl.process (l, drive);
                r = gr.process (r, drive);
            }
            out.setSample (0, pos + i, l);
            out.setSample (1, pos + i, r);
        }
        pos += stepSamples;
    }
    return total;
}
