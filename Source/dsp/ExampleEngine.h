#pragma once

#include <juce_dsp/juce_dsp.h>

// A deliberately small engine that demonstrates the house patterns. Replace
// its guts, keep its shape:
//
//  * Params is a plain struct snapshotted once per block by the processor, so
//    processBlock never touches the APVTS or looks a parameter up by string.
//  * prepare() allocates for the worst case and is safe to call repeatedly
//    (hosts call it on every sample-rate or block-size change).
//  * process() allocates nothing, takes no locks, and does no I/O.
//  * Continuous values move through SmoothedValue so parameter changes can't
//    click.
//  * UI-visible state is published through atomics the editor polls; it is
//    never read back out of the audio path directly.
class ExampleEngine
{
public:
    struct Params
    {
        float driveDb = 0.0f;
        float toneHz = 20000.0f;
        float mixPct = 100.0f;
        bool bypass = false;
    };

    void prepare (double sampleRate, int maxBlockSize);
    void process (juce::AudioBuffer<float>& buffer, const Params& p);

    // Written by the audio thread each block, polled by the editor. Tearing is
    // acceptable for a meter; it is a picture, not data.
    std::atomic<float> uiOutputLevel { 0.0f };

private:
    juce::dsp::StateVariableTPTFilter<float> toneFilter;
    juce::SmoothedValue<float> driveSmooth, mixSmooth, toneSmooth;

    double sr = 44100.0;
    int maxBlock = 0;
    bool firstBlock = true;
};
