#pragma once

#include <vector>

#include "Agitation.h"
#include "Drift.h"
#include "InputFollower.h"
#include "Interference.h"
#include "ModMatrix.h"
#include "TimeFilterLoop.h"

// The whole effect, mono core with stereo I/O, matching the hardware:
//
//   in L+R -> mono sum -> STRENGTH -> Time/Filter loop -> equal-power BLEND -> OUT
//
// with four modulation sources feeding the loop through the matrix. Only Time
// is modulated at audio rate, because that is where the FM character lives;
// everything else moves on a 32-sample control tick that is counted
// persistently, so nothing depends on how the host chops up the block.
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

        float agitate01 = 0.0f;
        float agitSpeedHz = 0.35f;
        bool agitGateMode = false;
        float timeMod01 = 0.0f;

        bool bypass = false;
    };

    DybbukEngine();

    void prepare (double sampleRate, int maxBlockSize);
    void reset() noexcept;
    void process (juce::AudioBuffer<float>& buffer, const Params& p);

    void requestClear() noexcept { loop.requestClear(); }
    void requestTimeSnap() noexcept { loop.requestTimeSnap(); snapPending = true; }
    void seedForTests (unsigned int s) noexcept;

    // Polled by the editor; never read the engine directly from the UI.
    std::atomic<float> uiOutputLevel { 0.0f };
    float getLoopEnergy() const noexcept { return loop.uiLoopEnergy.load (std::memory_order_relaxed); }
    float getDelaySeconds() const noexcept { return loop.uiDelaySeconds.load (std::memory_order_relaxed); }
    int getClearsServed() const noexcept { return loop.uiClearsServed.load (std::memory_order_relaxed); }

    // Modulation, published for the knob rings: how far each destination is
    // being pushed right now, normalised to its own full scale.
    float getTimeMod() const noexcept { return uiTimeMod.load (std::memory_order_relaxed); }
    float getFilterMod() const noexcept { return uiFilterMod.load (std::memory_order_relaxed); }
    float getDecayMod() const noexcept { return uiDecayMod.load (std::memory_order_relaxed); }
    float getInterferenceEnergy() const noexcept { return uiInterference.load (std::memory_order_relaxed); }

private:
    void processChunk (juce::AudioBuffer<float>& buffer, int start, int len, const Params& p);

    TimeFilterLoop loop;
    Agitation agitation;
    InputFollower follower;
    Interference interference;
    Drift drift;
    ModMatrix matrix;

    std::vector<float> monoBuf, wetBuf, modBuf;

    juce::SmoothedValue<float> strengthSmooth, dryGainSmooth, wetGainSmooth, outSmooth;

    std::atomic<float> uiTimeMod { 0.0f }, uiFilterMod { 0.0f }, uiDecayMod { 0.0f },
        uiInterference { 0.0f };

    // Persistent across host blocks: the control tick must stay 32 samples
    // apart whatever size the host hands us.
    int samplesUntilTick = modk::kControlBlock;
    float agitSum = 0.0f;
    float agitMean = 0.0f;

    double sr = 48000.0;
    int maxBlock = 512;
    float blockPeak = 0.0f;
    bool firstBlock = true;
    bool snapPending = false;
};
