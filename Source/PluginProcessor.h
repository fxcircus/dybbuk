#pragma once

#include <juce_audio_utils/juce_audio_utils.h>

#include "Parameters.h"
#include "dsp/DybbukEngine.h"
#include "state/PresetManager.h"

class DybbukProcessor : public juce::AudioProcessor
{
public:
    DybbukProcessor();

    juce::AudioProcessorValueTreeState apvts;
    PresetManager presetManager { *this, apvts };

    // Hands the host the parameter its own bypass button should drive (VST3
    // kIsBypass / AU equivalent; 1 means bypassed). The processor crossfades
    // rather than hard-switching, and the engine keeps running on silence so
    // the loop survives the trip out of circuit and back.
    juce::AudioProcessorParameter* getBypassParameter() const override
    { return apvts.getParameter (params::id::bypass); }

    // Engine -> UI. Poll these from the editor's timer; never reach into the
    // engine from the message thread.
    float getOutputLevel() const { return engine.uiOutputLevel.load (std::memory_order_relaxed); }
    float getInputLevel() const { return engine.uiInputLevel.load (std::memory_order_relaxed); }
    float getLoopEnergy() const { return engine.getLoopEnergy(); }
    float getDelaySeconds() const { return engine.getDelaySeconds(); }
    int getClearsServed() const { return engine.getClearsServed(); }
    // Live modulation depth per destination, -1 to 1, for the knob rings.
    float getTimeModDepthOct() const { return engine.getTimeModDepthOct(); }
    float getFilterModOct() const { return engine.getFilterModOct(); }
    float getDecayModLinear() const { return engine.getDecayModLinear(); }
    float getInterferenceEnergy() const { return engine.getInterferenceEnergy(); }
    bool isSyncClamped() const { return syncClamped.load (std::memory_order_relaxed); }
    // Enough for the editor to resolve a synced Time itself, so its readout is
    // right the moment the window opens rather than after the first block.
    double getLastKnownBpm() const { return lastKnownBpm; }
    float getBeatsPerBar() const { return beatsPerBar.load (std::memory_order_relaxed); }

    // UI -> engine. Momentary, lock free, never a parameter and never saved:
    // a Clear in a session recall would empty the loop on load.
    void requestClear() { engine.requestClear(); }

    // ---------------------------------------------------------------------
    // State that is NOT an APVTS parameter must be stamped into the tree here
    // and pushed back on load. Today that is only the version marker: theme
    // and window scale are editor properties that already ride on the tree,
    // and PresetManager keeps them out of preset files.
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
    // The longest delay is 3.67 s and Decay can hold it round several times.
    double getTailLengthSeconds() const override { return 12.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

private:
    DybbukEngine engine;

    // Cached so processBlock never looks a parameter up by string.
    std::atomic<float>* pTime;
    std::atomic<float>* pDecay;
    std::atomic<float>* pFilter;
    std::atomic<float>* pResonance;
    std::atomic<float>* pAbsorb;
    std::atomic<float>* pBlend;
    std::atomic<float>* pAgitate;
    std::atomic<float>* pAgitSpeed;
    std::atomic<float>* pStrength;
    std::atomic<float>* pOut;
    std::atomic<float>* pTimeMod;
    std::atomic<float>* pTimeSync;
    std::atomic<float>* pAgitMode;
    std::atomic<float>* pTonesLevel;
    std::atomic<float>* pTonesPitch;
    std::atomic<float>* pSpread;
    std::atomic<float>* pBypass;
    std::atomic<float>* pInput;
    std::atomic<float>* pChaos;
    std::atomic<float>* pCrust;
    std::atomic<float>* pTonesFold;
    std::atomic<float>* pColour;

    // Hosts may report nothing at all (the standalone player reports an
    // engaged position with every field unset), so sync falls back to the
    // last tempo actually seen rather than to garbage.
    double lastKnownBpm = 120.0;
    std::atomic<bool> syncClamped { false };
    std::atomic<float> beatsPerBar { 4.0f };

    // Bypass is a crossfade, not a hard switch.
    juce::AudioBuffer<float> bypassDry;
    juce::SmoothedValue<float> bypassMix;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (DybbukProcessor)
};
