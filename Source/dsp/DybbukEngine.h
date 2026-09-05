#pragma once

#include <vector>

#include "TimeFilterLoop.h"

// The whole effect, mono core with stereo I/O, matching the hardware:
//
//   in L+R -> mono sum -> STRENGTH (gain into a soft clip)
//          -> Time/Filter loop -> equal-power BLEND with dry -> OUT
//
// Same shape as the template's ExampleEngine: a Params struct snapshotted
// once per block, prepare() allocating the worst case, process() allocating
// nothing and taking no locks.
class DybbukEngine
{
public:
    struct Params
    {
        float strengthDb = 0.0f;
        float time01 = 0.3f;
        float decay = 0.5f;
        float filterHz = 18000.0f;
        float resonance01 = 0.2f;
        float absorb01 = 0.0f;
        float blend01 = 0.5f;
        float outDb = 0.0f;
        bool bypass = false;
    };

    void prepare (double sampleRate, int maxBlockSize);
    void reset() noexcept;
    void process (juce::AudioBuffer<float>& buffer, const Params& p);

    void requestClear() noexcept { loop.requestClear(); }
    void requestTimeSnap() noexcept { loop.requestTimeSnap(); }
    void seedForTests (unsigned int s) noexcept { loop.seedForTests (s); }

    // Polled by the editor; never read the engine directly from the UI.
    std::atomic<float> uiOutputLevel { 0.0f };
    float getLoopEnergy() const noexcept { return loop.uiLoopEnergy.load (std::memory_order_relaxed); }
    float getDelaySeconds() const noexcept { return loop.uiDelaySeconds.load (std::memory_order_relaxed); }
    int getClearsServed() const noexcept { return loop.uiClearsServed.load (std::memory_order_relaxed); }

private:
    void processChunk (juce::AudioBuffer<float>& buffer, int start, int len, const Params& p);

    TimeFilterLoop loop;
    std::vector<float> monoBuf, wetBuf;

    juce::SmoothedValue<float> strengthSmooth, dryGainSmooth, wetGainSmooth, outSmooth;

    double sr = 48000.0;
    int maxBlock = 512;
    float blockPeak = 0.0f;
    bool firstBlock = true;
};
