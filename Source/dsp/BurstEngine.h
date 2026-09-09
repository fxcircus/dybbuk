#pragma once

#include <juce_audio_basics/juce_audio_basics.h>

#include <array>
#include <atomic>
#include <vector>

// The Burst engine: a gated step recorder feeding a steady step sequencer.
//
//   in L+R -> mono sum -> gate -> the next free step slice
//                                         |
//                      step clock -> plays pattern[i] from its start
//
// Nothing is captured until the input gate opens; while armed, every gated
// event becomes one step, appended to the pattern as it closes, and the
// pattern starts playing on the first commit. Disarm to freeze it and play
// over it. The sequencer's pace is the whole point: the
// material is whatever you played, but it always lands on the step clock.
// See docs/BURST.md for the hardware this is drawn from and where it
// deliberately differs.
class BurstEngine
{
public:
    static constexpr int kMaxSteps = 16;
    // Ceiling on one step's material. A held note past this commits as-is.
    static constexpr double kMaxStepSeconds = 2.0;

    struct Params
    {
        float thresholdDb = -30.0f;   // gate open level (the hardware's Sensitivity)
        float stepMs = 250.0f;        // free-mode step length
        int maxSteps = 8;             // pattern ceiling; a new step past it replaces the oldest
        bool record = true;           // armed: every gated event becomes a step; off freezes the pattern
        float mix01 = 1.0f;           // dry/wet
    };

    void prepare (double sampleRate, int maxBlockSize);
    void reset() noexcept;
    void process (juce::AudioBuffer<float>& buffer, const Params& p);

    // Start over: momentary command through a mailbox, never a parameter,
    // so a session recall can never empty the pattern on load.
    void requestClear() noexcept { clearRequests.fetch_add (1, std::memory_order_relaxed); }

    // Polled by the editor; the lamp is going to be the pattern.
    std::atomic<int> uiStepCount { 0 };
    std::atomic<int> uiCurrentStep { -1 };     // -1 while listening
    std::atomic<int> uiClearsServed { 0 };
    std::atomic<float> uiGate { 0.0f };        // 1 while capturing
    std::atomic<float> uiInputLevel { 0.0f };
    std::atomic<float> uiOutputLevel { 0.0f };
    std::array<std::atomic<float>, kMaxSteps> uiStepLevel {};

private:
    // Gate constants. Attack fast enough to catch a pick, release short so the
    // silence after a muted note closes the step promptly; the hysteresis
    // stops a decaying tail from chattering the gate.
    static constexpr float kReleaseMs = 20.0f;
    static constexpr float kCloseBelowDb = -6.0f;
    static constexpr float kHoldOffMs = 20.0f;
    // A re-attack inside an open gate (the next pick of a phrase with no gap)
    // splits the step: the envelope jumps this far over its lagging baseline.
    // The baseline rises much faster than it falls, so the first attack has
    // been absorbed by the time the hold-off expires and cannot split itself,
    // while a fresh pick still outruns it.
    static constexpr float kReattackRatio = 2.5f;
    static constexpr float kBaselineRiseMs = 30.0f;
    static constexpr float kBaselineFallMs = 80.0f;
    // Detection lags the transient; the pre-roll is prepended so attacks keep
    // their front edge. Fades are applied on playback, never to the material.
    static constexpr float kPreRollMs = 4.0f;
    static constexpr float kFadeMs = 2.0f;

    float* slice (int slot) noexcept { return pool.data() + (size_t) slot * (size_t) capacity; }
    void onset() noexcept;
    void commit() noexcept;
    void advance() noexcept;
    void doClear() noexcept;

    double sr = 48000.0;
    int capacity = 0;                       // samples per slice
    int preRollSamples = 0, fadeSamples = 1, holdOffSamples = 0;
    float aRelease = 0.0f, aBaseRise = 0.0f, aBaseFall = 0.0f;

    // One spare slot beyond the ceiling so capture never writes into a slice
    // the sequencer may be reading.
    std::vector<float> pool;
    std::array<int, kMaxSteps + 1> sliceLen {};
    std::array<float, kMaxSteps + 1> slicePeak {};
    std::array<int, kMaxSteps> pattern {};
    int count = 0;
    int captureSlot = 0;

    std::vector<float> preRoll;
    int preRollPos = 0;

    float env = 0.0f, baseline = 0.0f;
    bool gateOpen = false;
    int openedFor = 0;
    int capWrite = 0;

    int playIndex = -1, playSlot = -1, playPos = 0, playLen = 0;
    int tickCounter = 0;
    float pendingStepMs = 250.0f;   // the block's params, read at commit/tick time
    int pendingMaxSteps = 8;

    std::atomic<int> clearRequests { 0 };
    int clearsSeen = 0;
};
