#include "DybbukEngine.h"

namespace
{
    // Strength is one knob for gain and drive, so it has to stay clean at
    // nominal levels and only bite when pushed: linear below the knee, soft
    // above it, asymptotic to 1.
    constexpr float kStrengthKnee = 0.7f;
    constexpr float kInvStrengthSpan = 1.0f / (1.0f - kStrengthKnee);

    inline float softClip (float x) noexcept
    {
        const float ax = std::abs (x);
        if (ax <= kStrengthKnee)
            return x;
        return (x < 0.0f ? -1.0f : 1.0f)
               * (kStrengthKnee + (1.0f - kStrengthKnee)
                                      * pt::fastTanh ((ax - kStrengthKnee) * kInvStrengthSpan));
    }

    inline float gainFromDb (float db) noexcept
    {
        return db <= -59.9f ? 0.0f : std::pow (10.0f, db * 0.05f);
    }
}

DybbukEngine::DybbukEngine()
{
    // Production seeds come from the system RNG so two instances of the plugin
    // never drift in lockstep. Tests override this with seedForTests.
    const unsigned int base = (unsigned int) juce::Random::getSystemRandom().nextInt();
    interference.seed (base);
    drift.seed (base * 2654435761u);
}

void DybbukEngine::prepare (double sampleRate, int maxBlockSize)
{
    sr = sampleRate;
    maxBlock = juce::jmax (1, maxBlockSize);

    loop.prepare (sampleRate, maxBlock);
    agitation.prepare (sampleRate);
    follower.prepare (sampleRate);
    interference.prepare (sampleRate);
    drift.prepare (sampleRate);
    matrix.prepare (sampleRate);

    monoBuf.assign ((size_t) maxBlock, 0.0f);
    wetBuf.assign ((size_t) maxBlock, 0.0f);
    modBuf.assign ((size_t) maxBlock, 0.0f);

    const double smoothSec = 0.03;
    strengthSmooth.reset (sampleRate, smoothSec);
    dryGainSmooth.reset (sampleRate, smoothSec);
    wetGainSmooth.reset (sampleRate, smoothSec);
    outSmooth.reset (sampleRate, smoothSec);

    samplesUntilTick = modk::kControlBlock;
    agitSum = 0.0f;
    agitMean = 0.0f;
    firstBlock = true;
}

void DybbukEngine::reset() noexcept
{
    loop.reset();
    agitation.reset();
    follower.reset();
    interference.reset();
    drift.reset();
    matrix.reset();
    samplesUntilTick = modk::kControlBlock;
    agitSum = 0.0f;
    agitMean = 0.0f;
    firstBlock = true;
}

void DybbukEngine::seedForTests (unsigned int s) noexcept
{
    loop.seedForTests (s);
    interference.seed (s * 2654435761u);
    drift.seed (s ^ 0xA5A5A5A5u);
}

void DybbukEngine::process (juce::AudioBuffer<float>& buffer, const Params& p)
{
    juce::ScopedNoDenormals noDenormals;

    const int n = buffer.getNumSamples();
    if (n == 0)
        return;

    strengthSmooth.setTargetValue (gainFromDb (p.strengthDb));
    // True equal power, so a centred Blend does not lose 3 dB and a runaway
    // plus dry cannot add up past full scale.
    const float b = juce::jlimit (0.0f, 1.0f, p.blend01);
    wetGainSmooth.setTargetValue (std::sin (b * 0.5f * pt::kPi));
    dryGainSmooth.setTargetValue (std::cos (b * 0.5f * pt::kPi));
    outSmooth.setTargetValue (gainFromDb (p.outDb));

    agitation.setSpeedHz (p.agitSpeedHz);
    agitation.setMode (p.agitGateMode ? Agitation::Mode::gate : Agitation::Mode::loop);
    matrix.setMacro (p.agitate01, p.timeMod01);

    if (firstBlock || snapPending)
    {
        strengthSmooth.setCurrentAndTargetValue (strengthSmooth.getTargetValue());
        wetGainSmooth.setCurrentAndTargetValue (wetGainSmooth.getTargetValue());
        dryGainSmooth.setCurrentAndTargetValue (dryGainSmooth.getTargetValue());
        outSmooth.setCurrentAndTargetValue (outSmooth.getTargetValue());
        matrix.snapMacro();
        firstBlock = false;
        snapPending = false;
    }

    blockPeak = 0.0f;
    for (int start = 0; start < n; start += maxBlock)
        processChunk (buffer, start, juce::jmin (maxBlock, n - start), p);

    uiOutputLevel.store (blockPeak, std::memory_order_relaxed);
    uiTimeMod.store (matrix.modNorm (ModMatrix::dstTime), std::memory_order_relaxed);
    uiFilterMod.store (matrix.modNorm (ModMatrix::dstFilter), std::memory_order_relaxed);
    uiDecayMod.store (matrix.modNorm (ModMatrix::dstDecay), std::memory_order_relaxed);
    uiInterference.store (interference.energy01(), std::memory_order_relaxed);
}

void DybbukEngine::processChunk (juce::AudioBuffer<float>& buffer, int start, int len, const Params& p)
{
    const int numChannels = juce::jmin (2, buffer.getNumChannels());
    float* mono = monoBuf.data();
    float* wet = wetBuf.data();
    float* mod = modBuf.data();

    const float* left = buffer.getReadPointer (0, start);
    const float* right = numChannels > 1 ? buffer.getReadPointer (1, start) : left;

    int pos = 0;
    while (pos < len)
    {
        const int sub = juce::jmin (len - pos, samplesUntilTick);

        // A control tick opens this sub-block. Interference reads the loop's
        // own envelope, which is the feedback path that makes the instrument
        // self influencing: energy drives chaos drives Time drives energy.
        if (samplesUntilTick == modk::kControlBlock)
        {
            drift.tick();
            interference.tick (loop.getLoopEnvelope(), loop.getLastSample());
            matrix.tick (agitMean, follower.value01(), interference.wander(), drift.current());
        }

        for (int i = 0; i < sub; ++i)
        {
            const int k = pos + i;
            const float dry = 0.5f * (left[k] + right[k]);

            // While bypassed the loop runs on silence: a delay should not
            // collect what you played while it was out of circuit.
            const float driven = p.bypass
                                     ? 0.0f
                                     : softClip (dry * strengthSmooth.getNextValue()
                                                 * gainFromDb (matrix.offsets().strengthDb));
            mono[k] = driven;

            const bool onset = follower.processSample (driven);
            agitSum += agitation.processSample (onset);

            mod[k] = matrix.timeOctave (interference.nextSample(), drift.nextSample());
        }

        TimeFilterLoop::Params lp;
        lp.time01 = p.time01;
        lp.decay = p.decay;
        lp.filterHz = p.filterHz;
        lp.resonance01 = p.resonance01;
        lp.absorb01 = p.absorb01;
        lp.filterModOct = matrix.offsets().filterOct;
        lp.resonanceMod = matrix.offsets().resonance;
        lp.decayMod = matrix.offsets().decay;
        lp.absorbMod = matrix.offsets().absorb;

        loop.process (mono + pos, wet + pos, sub, lp, mod + pos);

        pos += sub;
        samplesUntilTick -= sub;
        if (samplesUntilTick <= 0)
        {
            // The mean over the tick, not a point sample: a 1 kHz generator
            // point-sampled at the control rate aliases into garbage on the
            // filter, while its mean converges to the shape's average.
            agitMean = agitSum * modk::kInvControlBlock;
            agitSum = 0.0f;
            samplesUntilTick = modk::kControlBlock;
        }
    }

    float peak = blockPeak;
    const float blendMod = matrix.offsets().blend;
    for (int i = 0; i < len; ++i)
    {
        const float dry = 0.5f * (left[i] + right[i]);
        const float outGain = outSmooth.getNextValue();
        const float wetGain = juce::jlimit (0.0f, 1.5f, wetGainSmooth.getNextValue() + blendMod);
        const float y = (dry * dryGainSmooth.getNextValue() + wet[i] * wetGain) * outGain;
        for (int ch = 0; ch < numChannels; ++ch)
            buffer.getWritePointer (ch, start)[i] = y;
        const float mag = std::abs (y);
        peak = mag > peak ? mag : peak;
    }

    blockPeak = peak;
}
