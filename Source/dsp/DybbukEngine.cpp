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

void DybbukEngine::prepare (double sampleRate, int maxBlockSize)
{
    sr = sampleRate;
    maxBlock = juce::jmax (1, maxBlockSize);

    loop.prepare (sampleRate, maxBlock);

    monoBuf.assign ((size_t) maxBlock, 0.0f);
    wetBuf.assign ((size_t) maxBlock, 0.0f);

    const double smoothSec = 0.03;
    strengthSmooth.reset (sampleRate, smoothSec);
    dryGainSmooth.reset (sampleRate, smoothSec);
    wetGainSmooth.reset (sampleRate, smoothSec);
    outSmooth.reset (sampleRate, smoothSec);

    firstBlock = true;
}

void DybbukEngine::reset() noexcept
{
    loop.reset();
    firstBlock = true;
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

    if (firstBlock)
    {
        strengthSmooth.setCurrentAndTargetValue (strengthSmooth.getTargetValue());
        wetGainSmooth.setCurrentAndTargetValue (wetGainSmooth.getTargetValue());
        dryGainSmooth.setCurrentAndTargetValue (dryGainSmooth.getTargetValue());
        outSmooth.setCurrentAndTargetValue (outSmooth.getTargetValue());
        firstBlock = false;
    }

    blockPeak = 0.0f;
    for (int start = 0; start < n; start += maxBlock)
        processChunk (buffer, start, juce::jmin (maxBlock, n - start), p);

    uiOutputLevel.store (blockPeak, std::memory_order_relaxed);
}

void DybbukEngine::processChunk (juce::AudioBuffer<float>& buffer, int start, int len, const Params& p)
{
    const int numChannels = juce::jmin (2, buffer.getNumChannels());
    float* mono = monoBuf.data();
    float* wet = wetBuf.data();

    const float* left = buffer.getReadPointer (0, start);
    const float* right = numChannels > 1 ? buffer.getReadPointer (1, start) : left;

    for (int i = 0; i < len; ++i)
    {
        const float dry = 0.5f * (left[i] + right[i]);
        // While bypassed the loop runs on silence: a delay should not collect
        // what you played while it was out of circuit.
        mono[i] = p.bypass ? 0.0f : softClip (dry * strengthSmooth.getNextValue());
    }

    TimeFilterLoop::Params lp;
    lp.time01 = p.time01;
    lp.decay = p.decay;
    lp.filterHz = p.filterHz;
    lp.resonance01 = p.resonance01;
    lp.absorb01 = p.absorb01;

    loop.process (mono, wet, len, lp, nullptr);

    float peak = blockPeak;
    for (int i = 0; i < len; ++i)
    {
        const float dry = 0.5f * (left[i] + right[i]);
        const float outGain = outSmooth.getNextValue();
        const float y = (dry * dryGainSmooth.getNextValue() + wet[i] * wetGainSmooth.getNextValue())
                        * outGain;
        for (int ch = 0; ch < numChannels; ++ch)
            buffer.getWritePointer (ch, start)[i] = y;
        const float mag = std::abs (y);
        peak = mag > peak ? mag : peak;
    }

    blockPeak = peak;
}
