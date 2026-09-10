#pragma once

#include <juce_audio_utils/juce_audio_utils.h>

#include <atomic>

#include "Parameters.h"
#include "dsp/BurstEngine.h"
#include "state/PresetManager.h"

class DybbukProcessor : public juce::AudioProcessor
{
public:
    DybbukProcessor();

    juce::AudioProcessorValueTreeState apvts;
    PresetManager presetManager { *this, apvts };

    // Hands the host the parameter its own bypass button should drive (VST3
    // kIsBypass / AU equivalent; 1 means bypassed). The processor crossfades
    // rather than hard-switching, and the engine keeps running, deaf, so the
    // pattern survives the trip out of circuit and back.
    juce::AudioProcessorParameter* getBypassParameter() const override
    { return apvts.getParameter (params::id::bypass); }

    // Engine -> UI. Poll these from the editor's timer; never reach into the
    // engine from the message thread. The lamp is the pattern.
    float getInputLevel() const noexcept { return engine.uiInputLevel.load (std::memory_order_relaxed); }
    float getOutputLevel() const noexcept { return engine.uiOutputLevel.load (std::memory_order_relaxed); }
    int getStepCount() const noexcept { return engine.uiStepCount.load (std::memory_order_relaxed); }
    int getCurrentStep() const noexcept { return engine.uiCurrentStep.load (std::memory_order_relaxed); }
    int getTicks() const noexcept { return engine.uiTicks.load (std::memory_order_relaxed); }
    float getStepLevel (int i) const noexcept
    {
        return i >= 0 && i < BurstEngine::kMaxSteps
                   ? engine.uiStepLevel[(size_t) i].load (std::memory_order_relaxed) : 0.0f;
    }
    float getStepGain (int i) const noexcept
    {
        return i >= 0 && i < BurstEngine::kMaxSteps
                   ? engine.uiStepGain[(size_t) i].load (std::memory_order_relaxed) : 0.0f;
    }
    bool isGateOpen() const noexcept { return engine.uiGate.load (std::memory_order_relaxed) > 0.5f; }
    bool isFillRunning() const noexcept { return engine.uiFill.load (std::memory_order_relaxed) > 0.5f; }
    int getClearsServed() const noexcept { return engine.uiClearsServed.load (std::memory_order_relaxed); }

    // The step clock as the editor should show it. Synced or free, this is
    // the length the sequencer is actually using, resolved once per block.
    bool isSynced() const noexcept { return pStepSync != nullptr && pStepSync->load() >= 0.5f; }
    double getStepSeconds() const noexcept { return stepSecondsNow.load (std::memory_order_relaxed); }
    // Hosts may report nothing at all (the standalone player reports an
    // engaged position with every field unset), so sync falls back to the
    // last tempo actually seen rather than to garbage.
    double lastKnownBpm() const noexcept { return knownBpm.load (std::memory_order_relaxed); }

    // The dice. Message thread only, like every other parameter edit.
    void randomiseParameters();
    const char* lastRandomCharacter() const noexcept;

    // UI -> engine. Momentary, lock free, never a parameter and never saved:
    // a Clear in a session recall would empty the pattern on load.
    void requestClear() { engine.requestClear(); }

    // Export, message thread. The pattern is rendered offline through the
    // same voice as the live sequencer, one pass in the current direction, at
    // the step length currently resolved, and written as 32-bit float stereo.
    static juce::File exportFolder();
    bool canExportPattern() const noexcept { return getStepCount() > 0; }
    juce::String exportFileName() const;   // "Dybbuk pattern 3 steps 250 ms.wav"
    bool writePatternWav (const juce::File& dest) const;
    juce::File renderPatternToFile() const; // into exportFolder(); invalid on failure

    // ---------------------------------------------------------------------
    // State that is NOT an APVTS parameter must be stamped into the tree here
    // and pushed back on load. Today that is only the version marker: the
    // pattern itself is performance state, forgotten like the hardware does
    // on power-off, and theme and window scale are editor properties that
    // already ride on the tree.
    void stampExtraState (juce::ValueTree& state) const;
    void applyExtraState (const juce::ValueTree& state);
    // ---------------------------------------------------------------------

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    // One step's material at most: the sequencer keeps playing after the
    // input stops, but a host asking for a tail wants the longest single
    // sound, not the loop.
    double getTailLengthSeconds() const override { return BurstEngine::kMaxStepSeconds; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

private:
    BurstEngine::Direction directionParam() const noexcept;

    BurstEngine engine;

    // Cached so processBlock never looks a parameter up by string.
    std::atomic<float>* pStep;
    std::atomic<float>* pSteps;
    std::atomic<float>* pThreshold;
    std::atomic<float>* pBlend;
    std::atomic<float>* pFreeze;
    std::atomic<float>* pFills;
    std::atomic<float>* pChaos;
    std::atomic<float>* pDirection;
    std::atomic<float>* pLength;
    std::atomic<float>* pFade;
    std::atomic<float>* pFull;
    std::atomic<float>* pStepSync;
    std::atomic<float>* pInput;
    std::atomic<float>* pOut;
    std::atomic<float>* pBypass;

    juce::Random randomiserRng;

    double currentSampleRate = 48000.0;
    std::atomic<double> knownBpm { 120.0 };
    std::atomic<double> stepSecondsNow { 0.25 };

    // Bypass is a crossfade, not a hard switch.
    juce::AudioBuffer<float> bypassDry;
    juce::SmoothedValue<float> bypassMix;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (DybbukProcessor)
};
