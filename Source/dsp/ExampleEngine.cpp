#include "ExampleEngine.h"

void ExampleEngine::prepare (double sampleRate, int maxBlockSize)
{
    sr = sampleRate;
    maxBlock = juce::jmax (1, maxBlockSize);

    const juce::dsp::ProcessSpec spec { sr, (juce::uint32) maxBlock, 2 };
    toneFilter.prepare (spec);
    toneFilter.setType (juce::dsp::StateVariableTPTFilterType::lowpass);

    const double smoothingSeconds = 0.03;
    driveSmooth.reset (sr, smoothingSeconds);
    mixSmooth.reset (sr, smoothingSeconds);
    toneSmooth.reset (sr, smoothingSeconds);

    firstBlock = true;
}

void ExampleEngine::process (juce::AudioBuffer<float>& buffer, const Params& p)
{
    const int numSamples = buffer.getNumSamples();
    if (numSamples == 0 || p.bypass)
        return;

    driveSmooth.setTargetValue (juce::Decibels::decibelsToGain (p.driveDb));
    mixSmooth.setTargetValue (juce::jlimit (0.0f, 1.0f, p.mixPct * 0.01f));
    toneSmooth.setTargetValue (p.toneHz);

    if (firstBlock)
    {
        // Snap to targets so transport start doesn't fade in from stale values.
        driveSmooth.setCurrentAndTargetValue (driveSmooth.getTargetValue());
        mixSmooth.setCurrentAndTargetValue (mixSmooth.getTargetValue());
        toneSmooth.setCurrentAndTargetValue (toneSmooth.getTargetValue());
        firstBlock = false;
    }

    toneFilter.setCutoffFrequency (juce::jlimit (20.0f, (float) (sr * 0.45),
                                                toneSmooth.skip (numSamples)));

    float peak = 0.0f;
    const int numChannels = juce::jmin (2, buffer.getNumChannels());

    for (int i = 0; i < numSamples; ++i)
    {
        const float drive = driveSmooth.getNextValue();
        const float mix = mixSmooth.getNextValue();

        for (int ch = 0; ch < numChannels; ++ch)
        {
            auto* data = buffer.getWritePointer (ch);
            const float dry = data[i];
            const float wet = toneFilter.processSample (ch, std::tanh (dry * drive));
            data[i] = dry * (1.0f - mix) + wet * mix;
            peak = juce::jmax (peak, std::abs (data[i]));
        }
    }

    uiOutputLevel.store (peak, std::memory_order_relaxed);
}
