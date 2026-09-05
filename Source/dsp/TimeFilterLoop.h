#pragma once

#include <array>
#include <atomic>

#include "LoopSaturator.h"
#include "PTStage.h"

// The Time/Filter Experiment: kStages chips in series inside a filtered
// feedback loop.
//
//   in -> [+] -> chip 1 -> chip 2 -> chip 3 -> tap sum -> FILTER -> ABSORB
//          ^                                                          |
//          +------------------ DECAY <- saturator <-------------------+
//
// Decay runs to 1.15, above unity on purpose: the loop is meant to regenerate
// and self-oscillate. Two bounds keep that musical instead of digital, the
// saturator's tanh asymptote and the filter's limited resonance state, so the
// wet tap can never reach full scale and nothing hard clips.
class TimeFilterLoop
{
public:
    struct Params
    {
        // Harness defaults. The user's defaults live in the APVTS layout
        // (CLAUDE.md: defaults belong to the user).
        float time01 = 0.3f;
        float decay = 0.5f;
        float filterHz = 18000.0f;
        float resonance01 = 0.2f;
        float absorb01 = 0.0f;
    };

    void prepare (double sampleRate, int maxBlockSize);
    void reset() noexcept;

    // modOct is per-sample Time modulation in octaves of fs_chip, or nullptr.
    // It must be audio rate: at control rate the FM clang disappears.
    void process (const float* in, float* wet, int n, const Params& p, const float* modOct);

    // Momentary commands from the message thread. Never parameters, never state.
    void requestClear() noexcept { clearCounter.fetch_add (1, std::memory_order_release); }
    void requestTimeSnap() noexcept { snapRequested.store (true, std::memory_order_release); }

    void seedForTests (unsigned int s) noexcept;

    // Engine to UI. Tearing is fine: these are pictures, not data.
    std::atomic<float> uiLoopEnergy { 0.0f };   // 0 to 1, log mapped over 60 dB, for the ember
    std::atomic<float> uiDelaySeconds { 0.0f };
    std::atomic<int> uiClearsServed { 0 };

private:
    void refreshLoopCoeffs() noexcept;
    void flushAll() noexcept;

    ChipClock clock;
    std::array<PTStage, (size_t) pt::kStages> stages;
    TptSvf loopFilter;
    OnePole absorbShelf;
    LoopSaturator sat;

    juce::SmoothedValue<float> decaySmooth, resSmooth, absorbSmooth;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Multiplicative> cutoffSmooth;

    std::array<float, 3> tapWeight { { 1.0f, 0.0f, 0.0f } };

    float fb = 0.0f;
    float decayGain = 0.0f, absorbOutGain = 1.0f, absorbFbGain = 1.0f, absorbShelfDepth = 0.0f;
    float energyEnv = 0.0f, energyRelease = 0.999f;
    int controlCountdown = 0; // persists across blocks: smoothing must not depend on host block size

    enum class ClearPhase { idle, fadingOut, fadingIn };
    ClearPhase clearPhase = ClearPhase::idle;
    float clearGain = 1.0f, clearStep = 0.001f;
    std::atomic<int> clearCounter { 0 };
    std::atomic<bool> snapRequested { false };
    int lastClearSeen = 0;

    double sr = 48000.0;
    bool firstBlock = true;
};
