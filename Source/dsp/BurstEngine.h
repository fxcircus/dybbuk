#pragma once

#include <juce_audio_basics/juce_audio_basics.h>

#include <array>
#include <cmath>
#include <atomic>
#include <vector>

#include "Rng.h"

// The Burst engine: a gated step recorder feeding a steady step sequencer.
//
//   in L+R -> mono sum -> gate -> the next free step slice
//                                         |
//                      step clock -> plays pattern[i] from its start
//
// Nothing is captured until the input gate opens; while armed, every gated
// event becomes one step, appended to the pattern as it closes, and the
// pattern starts playing on the first commit. Disarm to freeze it and play
// over it. The sequencer's pace is the whole point: the material is whatever
// you played, but it always lands on the step clock. See docs/BURST.md for
// the hardware this is drawn from and where it deliberately differs.
class BurstEngine
{
public:
    static constexpr int kMaxSteps = 16;
    // Ceiling on one step's material. A held note past this commits as-is.
    static constexpr double kMaxStepSeconds = 2.0;

    enum class Direction { forward = 0, reverse, pendulum, drunk, random };   // Random last: the far end of the knob
    static constexpr int kDirectionCount = 5;

    struct Params
    {
        float inputDb = 0.0f;
        float outDb = 0.0f;
        float thresholdDb = -30.0f;   // gate open level (the hardware's Sensitivity)
        // The step clock, resolved by the processor so the engine never sees
        // a playhead: free mode is the knob in seconds; transport mode is a
        // division in seconds plus the distance to the next grid line.
        double stepSeconds = 0.25;
        int gridOffsetSamples = -1;   // samples from this block's start to the next grid line; -1 = free-running
        int maxSteps = 8;             // pattern ceiling, 1..kMaxSteps
        bool record = true;           // armed: every gated event becomes a step; off freezes the pattern
        bool replaceOldest = true;    // full and armed: replace the oldest step (else stop adding)
        float blend01 = 0.5f;         // equal-power dry/wet
        float fills01 = 0.0f;         // disarmed, a gated onset scrambles the order for one cycle, this deep
        float chaos01 = 0.0f;         // per-tick chance of a skip, a ratchet, a reverse or a repeat
        float length01 = 1.0f;        // choke: the fraction of the step a slice may sound
        float fade01 = 0.0f;          // level lost every play; a step that fades out leaves the pattern
        float pitchSemitones = 0.0f;  // resamples every step's material; the step clock is untouched
        Direction direction = Direction::forward;
        bool bypass = false;          // deaf: the gate hears silence, the sequencer keeps its place
    };

    // A consistent copy of the pattern for the message thread (export).
    struct PatternCopy
    {
        std::vector<std::vector<float>> steps;   // in pattern order, each the step's material
        std::array<float, kMaxSteps> gain {};    // the fade state of each
        double sampleRate = 0.0;
    };

    void prepare (double sampleRate, int maxBlockSize);
    void reset() noexcept;
    void process (juce::AudioBuffer<float>& buffer, const Params& p);

    // Start over: momentary command through a mailbox, never a parameter,
    // so a session recall can never empty the pattern on load.
    void requestClear() noexcept { clearRequests.fetch_add (1, std::memory_order_relaxed); }
    void seedForTests (unsigned int s) noexcept { rng.seed (s); }

    // Message-thread read of the pattern: retries while the audio thread is
    // changing it, returns false if it never settles. Allocates.
    bool copyPattern (PatternCopy& out) const;

    // One pass through a pattern, offline, with the same voice as the live
    // sequencer: forward order, the choke and the fades, no chaos or fills.
    // Stereo out, sized by this call. Returns the number of samples.
    static int renderPattern (const PatternCopy& pattern, double stepSeconds, float length01,
                              Direction direction, float pitchSemitones, juce::AudioBuffer<float>& out);

    // Polled by the editor; the lamp is the pattern.
    std::atomic<int> uiStepCount { 0 };
    std::atomic<int> uiCurrentStep { -1 };     // -1 while listening
    std::atomic<int> uiTicks { 0 };            // counts step-clock ticks, for a pulse
    std::atomic<int> uiClearsServed { 0 };
    std::atomic<float> uiGate { 0.0f };        // 1 while capturing
    std::atomic<float> uiFill { 0.0f };        // 1 while a fill's scrambled order is running
    std::atomic<float> uiInputLevel { 0.0f };
    std::atomic<float> uiOutputLevel { 0.0f };
    std::array<std::atomic<float>, kMaxSteps> uiStepLevel {};   // peak of each step's material
    std::array<std::atomic<float>, kMaxSteps> uiStepGain {};    // its fade state, 1 = fresh

private:
    // Gate constants. Release short so the silence after a muted note closes
    // the step promptly; the hysteresis stops a decaying tail from chattering
    // the gate.
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
    // Fade: the level a step loses every play is kFadeMaxDb times the
    // square of the knob, so the bottom half of the travel is gentle (10 %
    // is 0.18 dB a play, some 300 plays) and the top is a delay dying in a
    // few repeats (100 % is 18 dB, gone in three). A linear law with 24 dB
    // at the top made every small setting fade too fast (Roy, playing it).
    // A step under the floor leaves the pattern.
    static constexpr float kFadeMaxDb = 18.0f;
    static constexpr float kFadeFloorDb = -60.0f;
    // Chaos at full depth: the chance per tick that something happens, and
    // past half depth a second and third thing can happen to the same step.
    static constexpr float kChaosMaxChance = 0.9f;

    // One step playing: the voice both the live sequencer and the offline
    // render use, so an export sounds like the plugin did.
    // Pitch is a playback rate: the read head moves `rate` material samples
    // per output sample with linear interpolation, so an octave up plays the
    // material twice as fast and the aliasing of a decimated read is part of
    // the sound, as it is on the hardware's CLOCK.
    struct Voice
    {
        const float* data = nullptr;
        int len = 0;          // samples of material that will sound
        double pos = 0.0;     // fractional read position in the material
        double start = 0.0;   // where the read began, for a ratchet's restart
        bool reverse = false;
        float gain = 1.0f;
        float rateMul = 1.0f; // a per-step pitch on top of the global rate
        int fadeSamples = 1;
        bool active() const noexcept { return data != nullptr && pos < (double) len; }
        float next (float rate) noexcept;
    };

    float* slice (int slot) noexcept { return pool.data() + (size_t) slot * (size_t) capacity; }
    const float* slice (int slot) const noexcept { return pool.data() + (size_t) slot * (size_t) capacity; }
    void onset() noexcept;
    void commit() noexcept;
    void dropStep (int index) noexcept;
    void advance() noexcept;
    int nextIndex() noexcept;
    int activeCount() const noexcept;
    int samplesToGrid() const noexcept;
    // What chaos did to this step, rolled once per tick.
    struct Deviation
    {
        bool skip = false, reverse = false, repeat = false, jump = false;
        int ratchets = 1;         // 1 = none; 2..4 = the slice that many times in the step
        float semitones = 0.0f;   // a per-step pitch
        float offset01 = 0.0f;    // start this far into the material
        float choke01 = 1.0f;     // a shorter choke than Length asks for
        float gainMul = 1.0f;     // an accent or a ghost
        float rateMulOr1() const noexcept { return semitones == 0.0f ? 1.0f : std::pow (2.0f, semitones / 12.0f); }
    };
    Deviation rollChaos() noexcept;
    void startStep (int index, int stepSamples, const Deviation& d) noexcept;
    static float rateForSemitones (float st) noexcept { return std::pow (2.0f, st / 12.0f); }
    void beginFill() noexcept;
    void doClear() noexcept;
    void publishSteps() noexcept;
    void beginMutation() noexcept { patternGen.fetch_add (1, std::memory_order_release); }
    void endMutation() noexcept { patternGen.fetch_add (1, std::memory_order_release); }

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
    std::array<float, kMaxSteps> stepGain {};   // fade state, in pattern order
    int count = 0;
    int captureSlot = 0;
    std::atomic<int> patternGen { 0 };          // even = stable, odd = mid-change

    std::vector<float> preRoll;
    int preRollPos = 0;

    float env = 0.0f, baseline = 0.0f;
    bool gateOpen = false;
    int openedFor = 0;
    int capWrite = 0;

    // Sequencer
    Voice voice;
    int playIndex = -1;
    int tickCounter = 0;
    int ratchetCounter = 0, ratchetPeriod = 0, ratchetsLeft = 0;
    int blockPos = 0;
    bool fillArmed = true;
    int pendulumDir = 1;
    int fillTicksLeft = 0;
    std::array<int, kMaxSteps> fillOrder {};
    Params cur;                                 // this block's params, read at commit/tick time
    Rng rng;

    juce::SmoothedValue<float> inGain, outGain, wetMix, dryMix, rate;
    float currentRate = 1.0f;

    std::atomic<int> clearRequests { 0 };
    int clearsSeen = 0;
};
