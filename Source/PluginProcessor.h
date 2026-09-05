#pragma once

#include <juce_audio_utils/juce_audio_utils.h>

#include "Parameters.h"
#include "dsp/ExampleEngine.h"
#include "state/PresetManager.h"

class DybbukProcessor : public juce::AudioProcessor
{
public:
    DybbukProcessor();

    juce::AudioProcessorValueTreeState apvts;
    PresetManager presetManager { *this, apvts };

    // Hands the host the parameter its own bypass button should drive (VST3
    // kIsBypass / AU equivalent; 1 means bypassed). The processor crossfades
    // rather than hard-switching — see processBlock.
    juce::AudioProcessorParameter* getBypassParameter() const override
    { return apvts.getParameter (params::id::bypass); }

    // Engine -> UI. Poll these from the editor's timer; never reach into the
    // engine from the message thread.
    float getOutputLevel() const { return engine.uiOutputLevel.load (std::memory_order_relaxed); }

    // ---------------------------------------------------------------------
    // State that is NOT an APVTS parameter must be stamped into the tree here
    // and pushed back on load. This is the single most common source of bugs:
    // anything affecting sound or appearance is either a parameter or it is
    // handled by these two hooks — nothing in between.
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
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

private:
    ExampleEngine engine;

    // Cached so processBlock never looks a parameter up by string.
    std::atomic<float>* pDrive;
    std::atomic<float>* pTone;
    std::atomic<float>* pMix;
    std::atomic<float>* pBypass;

    // Bypass is a crossfade, not a hard switch: jumping between wet and dry
    // is a click at whatever level the wet happened to be. The engine keeps
    // running while bypassed so its state survives the trip.
    juce::AudioBuffer<float> bypassDry;
    juce::SmoothedValue<float> bypassMix;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (DybbukProcessor)
};
