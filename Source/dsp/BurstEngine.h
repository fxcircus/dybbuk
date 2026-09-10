#pragma once

#include <juce_audio_basics/juce_audio_basics.h>

#include <array>
#include <cmath>
#include <atomic>
#include <vector>

#include "LoopSaturator.h"
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

    // What a step does with its material during its step: the hardware's
    // Radio stations, as other players for the same pattern. See
    // docs/BURST.md, B5.
    //   golem  the sequencer as it is: the material, choked by Decay
    //   trance   the material stretched to fill Decay's share of the step, at its own pitch
    //   legion   three voices, Pitch the interval between them, coming and going
    //   wraith    each step leaves a frozen moment that keeps sounding under the next ones
    //   seize    while the input is over the threshold, the current step is held and ratcheted
    enum class Mode { golem = 0, wraith, trance, legion, tremor };   // Wraith second: Roy's favourite, beside the default
    static constexpr int kModeCount = 5;

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
        int barOffsetSamples = -1;    // samples from this block's start to the next bar line; -1 = none
        bool barReset = false;        // synced: the pattern restarts from its first step on every bar line
        int maxSteps = 8;             // pattern ceiling, 1..kMaxSteps
        bool record = true;           // armed: every gated event becomes a step; off freezes the pattern
        bool replaceOldest = true;    // full and armed: replace the oldest step (else stop adding)
        float blend01 = 0.5f;         // equal-power dry/wet
        float fills01 = 0.0f;         // disarmed, a gated onset scrambles the order for one cycle, this deep
        float chaos01 = 0.0f;         // per-tick chance of a skip, a ratchet, a reverse or a repeat
        float length01 = 1.0f;        // choke: the fraction of the step a slice may sound
        float feedback01 = 1.0f;      // the level a step keeps every play, like a delay's feedback; under 1 it fades and leaves
        float pitchSemitones = 0.0f;  // resamples every step's material; the step clock is untouched
        float glue01 = 0.0f;          // the end-of-chain saturator on the pattern: warmth to thrash
        float spread01 = 0.0f;        // alternate steps left and right, this far
        Mode mode = Mode::golem;
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

    // What the offline render needs to sound like the live sequencer.
    struct RenderSettings
    {
        double stepSeconds = 0.25;
        float length01 = 1.0f;
        Direction direction = Direction::forward;
        float pitchSemitones = 0.0f;
        float glue01 = 0.0f;
        float spread01 = 0.0f;
        Mode mode = Mode::golem;
    };

    // One pass through a pattern, offline, with the same voice as the live
    // sequencer: the direction's own order, the choke, the fades, the pitch,
    // the spread and the glue, no chaos or fills. Stereo out, sized by this
    // call. Returns the number of samples.
    static int renderPattern (const PatternCopy& pattern, const RenderSettings& settings, juce::AudioBuffer<float>& out);

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
    // Grains: the size Trance and Wraith read with, capped so short material
    // still gets two grains; the jitter that keeps two grains from combing.
    static constexpr float kGrainMs = 40.0f;
    static constexpr float kGrainJitterMs = 2.0f;
    // Wraith: a moment lasts this many ticks at Decay full, one at Decay's floor.
    static constexpr int kWraithMaxTicks = 8;
    static constexpr float kWraithGain = 0.7f;

    // Feedback: a step's gain is multiplied by it every play, so 100 % keeps
    // every step forever and 50 % is 6 dB a play. A step under the floor
    // leaves the pattern. (Shipped first as Fade, a loss per play; Roy: the
    // delay word says what it does.)
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

    // A grain player: two Hann grains half a grain apart, so their sum is
    // flat. Trance walks its head through the material slower than real
    // time; Wraith holds it still. Each grain is read at the pitch rate from
    // where it was spawned, with a little jitter so two grains never comb.
    struct Grains
    {
        const float* data = nullptr;
        int len = 0;
        double head = 0.0, advance = 0.0;
        int size = 0;
        double start[2] { 0.0, 0.0 };
        int age[2] { 0, 0 };
        float gain = 1.0f, rateMul = 1.0f;
        int left = 0;              // output samples still to play
        bool active() const noexcept { return data != nullptr && left > 0 && gain > 0.0f; }
        void begin (const float* d, int n, double headStart, double adv, int grainSize, int outSamples, Rng& rng) noexcept;
        float next (float rate, Rng& rng) noexcept;
    };

    // Everything that can sound for one step, in any mode: up to three
    // voices (Legion), or the grain player (Trance). One pan for all of it.
    struct StepVoice
    {
        Voice v[3];
        int voices = 0;
        Grains stretch;
        float panL = 1.0f, panR = 1.0f;
        bool sounding() const noexcept;
        void restart() noexcept;   // the ratchet: from the start again
        void stop() noexcept;
        float next (float rate, Rng& rng) noexcept;
        // The material this step was playing and how far it got, for Wraith.
        const float* material() const noexcept;
        int materialLen() const noexcept;
        double reached() const noexcept;
    };

    // Wraith's frozen moments: up to four, each a held grain fading over the
    // ticks that follow, keeping the pan of the step it came from.
    struct Haunts
    {
        static constexpr int kMax = 4;
        Grains layer[kMax];
        float panL[kMax] {}, panR[kMax] {};
        void clear() noexcept;
        void spawn (const float* material, int len, double reached, int grainSize, float gain, float pl, float pr, Rng& rng) noexcept;
        void tick (float decayPerTick) noexcept;
        void next (float rate, Rng& rng, float& l, float& r) noexcept;
    };

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

    // How a step is to be started: the mode, the knobs that matter to it,
    // and the clock. Shared by the live sequencer and the offline render.
    struct StepSetup
    {
        Mode mode = Mode::golem;
        float length01 = 1.0f;
        float pitchSemitones = 0.0f;
        float spread01 = 0.0f;
        int index = 0;
        int stepSamples = 1;
        float rate = 1.0f;         // the global pitch rate now
        int fadeSamples = 1;
        double sampleRate = 48000.0;
    };
    static void startStepVoice (StepVoice& sv, const float* material, int len, float stepGain,
                                const StepSetup& s, const Deviation& d, Rng& rng) noexcept;
    static int grainSizeFor (double sampleRate, int materialLen) noexcept;
    static float hauntDecayPerTick (float length01) noexcept;
    void startStep (int index, int stepSamples, const Deviation& d) noexcept;

    float* slice (int slot) noexcept { return pool.data() + (size_t) slot * (size_t) capacity; }
    const float* slice (int slot) const noexcept { return pool.data() + (size_t) slot * (size_t) capacity; }
    void onset() noexcept;
    void commit() noexcept;
    void dropStep (int index) noexcept;
    void advance() noexcept;
    int nextIndex() noexcept;
    int activeCount() const noexcept;
    int samplesToGrid() const noexcept;
    static float rateForSemitones (float st) noexcept { return std::pow (2.0f, st / 12.0f); }
    // Spread: alternate steps sit left and right. Unity in the middle so
    // Spread at zero is exactly today's mono, the far side falling to nothing.
    static void panFor (int index, float spread01, float& l, float& r) noexcept
    {
        const float pos = (index % 2 == 0 ? -1.0f : 1.0f) * juce::jlimit (0.0f, 1.0f, spread01);
        l = juce::jmin (1.0f, 1.0f - pos);
        r = juce::jmin (1.0f, 1.0f + pos);
    }
    // Glue: the saturator with a level match behind it. The tanh, divided by
    // its drive, has unity gain for small signals and squashes the loud
    // ones; a slow RMS tracker then lifts the result back to the input's
    // average level. So Glue compresses and colours, and the volume stays
    // where it was. (A fixed makeup was tried first and lifted quiet
    // material by 18 dB at full, which Roy heard as "louder", not "glued".)
    struct GlueStage
    {
        LoopSaturator sat;
        float envIn = 0.0f, envOut = 0.0f, gain = 1.0f, a = 0.0f;
        static constexpr float kWindowMs = 120.0f;
        static constexpr float kMaxLift = 16.0f;

        void prepare (double sampleRate) noexcept
        {
            sat.prepare (sampleRate);
            a = 1.0f - std::exp (-1.0f / (kWindowMs * 0.001f * (float) sampleRate));
            reset();
        }
        void reset() noexcept { sat.reset(); envIn = envOut = 0.0f; gain = 1.0f; }
        inline float process (float x, float drive) noexcept
        {
            sat.setDrive (drive);
            const float y = sat.process (x);              // tanh (drive x) / drive: unity for small x
            envIn += (x * x - envIn) * a;
            envOut += (y * y - envOut) * a;
            if (envOut > 1.0e-9f)
            {
                const float wanted = juce::jlimit (1.0f, kMaxLift, std::sqrt (envIn / envOut));
                gain += (wanted - gain) * a;
            }
            return y * gain;
        }
    };
    static float glueDrive (float glue01) noexcept { return 1.0f + 15.0f * juce::jlimit (0.0f, 1.0f, glue01); }
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
    StepVoice voice;
    Haunts wraiths;
    int barCountdown = 0;
    bool inputHot = false;
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

    juce::SmoothedValue<float> inGain, outGain, wetMix, dryMix, rate, glueAmount;
    float currentRate = 1.0f;
    GlueStage glueL, glueR;

    std::atomic<int> clearRequests { 0 };
    int clearsSeen = 0;
};
