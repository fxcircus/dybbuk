#include "PluginProcessor.h"

#include "PluginEditor.h"

namespace
{
    // Bump when the state layout changes; setStateInformation can then migrate
    // older sessions instead of silently mis-reading them.
    constexpr int currentStateVersion = 1;
}

DybbukProcessor::DybbukProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput ("Input", juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "Dybbuk", params::createParameterLayout())
{
    setLatencySamples (0);

    pDrive  = apvts.getRawParameterValue (params::id::drive);
    pTone   = apvts.getRawParameterValue (params::id::tone);
    pMix    = apvts.getRawParameterValue (params::id::mix);
    pBypass = apvts.getRawParameterValue (params::id::bypass);

    // Presets carry non-parameter state through these hooks.
    presetManager.stampExtraState = [this] (juce::ValueTree& s) { stampExtraState (s); };
    presetManager.applyExtraState = [this] { applyExtraState (apvts.state); };
}

void DybbukProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    engine.prepare (sampleRate, samplesPerBlock);

    bypassDry.setSize (2, juce::jmax (1, samplesPerBlock), false, true, true);
    bypassMix.reset (sampleRate, 0.020);
    bypassMix.setCurrentAndTargetValue (pBypass != nullptr && pBypass->load() > 0.5f ? 1.0f : 0.0f);
}

void DybbukProcessor::releaseResources() {}

bool DybbukProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto in  = layouts.getMainInputChannelSet();
    const auto out = layouts.getMainOutputChannelSet();

    if (out != juce::AudioChannelSet::stereo())
        return false;

    return in == juce::AudioChannelSet::stereo() || in == juce::AudioChannelSet::mono();
}

void DybbukProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    juce::ignoreUnused (midi);

    const int numSamples = buffer.getNumSamples();
    if (numSamples == 0)
        return;

    // Mono in, stereo out: duplicate rather than leaving channel 1 stale.
    if (getTotalNumInputChannels() < 2 && buffer.getNumChannels() > 1)
        buffer.copyFrom (1, 0, buffer, 0, 0, numSamples);

    // One snapshot per block — the engine never sees the APVTS.
    ExampleEngine::Params p;
    p.driveDb = pDrive->load();
    p.toneHz  = pTone->load();
    p.mixPct  = pMix->load();
    // Bypass: crossfade to the dry input over 20 ms rather than hard-switch.
    // The engine keeps running (feed it silence if its state must not collect
    // what plays while out of circuit — an engine-specific decision), so wet
    // state survives the trip and a host automating bypass never clicks.
    const bool wantBypass = pBypass->load() >= 0.5f;
    bypassMix.setTargetValue (wantBypass ? 1.0f : 0.0f);
    const bool blending = wantBypass || bypassMix.isSmoothing()
                          || bypassMix.getCurrentValue() > 0.0f;

    if (blending)
        for (int ch = 0; ch < juce::jmin (bypassDry.getNumChannels(), buffer.getNumChannels()); ++ch)
            bypassDry.copyFrom (ch, 0, buffer, ch, 0,
                                juce::jmin (numSamples, bypassDry.getNumSamples()));

    p.bypass = wantBypass;

    engine.process (buffer, p);

    if (blending)
    {
        const int channels = juce::jmin (bypassDry.getNumChannels(), buffer.getNumChannels());
        const int n = juce::jmin (numSamples, bypassDry.getNumSamples());
        for (int i = 0; i < n; ++i)
        {
            const float mix = bypassMix.getNextValue();
            for (int ch = 0; ch < channels; ++ch)
                buffer.setSample (ch, i,
                                  buffer.getSample (ch, i) * (1.0f - mix)
                                      + bypassDry.getSample (ch, i) * mix);
        }
    }
}

void DybbukProcessor::stampExtraState (juce::ValueTree& state) const
{
    // Example: state.setProperty ("myThing", value, nullptr);
    juce::ignoreUnused (state);
}

void DybbukProcessor::applyExtraState (const juce::ValueTree& state)
{
    // Example: engine.requestThing ((float) state.getProperty ("myThing", 0.0));
    juce::ignoreUnused (state);
}

juce::AudioProcessorEditor* DybbukProcessor::createEditor()
{
    return new DybbukEditor (*this);
}

void DybbukProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    state.setProperty ("stateVersion", currentStateVersion, nullptr);
    stampExtraState (state);

    if (auto xml = state.createXml())
        copyXmlToBinary (*xml, destData);
}

void DybbukProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    auto xml = getXmlFromBinary (data, sizeInBytes);

    if (xml == nullptr || ! xml->hasTagName (apvts.state.getType()))
        return;

    apvts.replaceState (juce::ValueTree::fromXml (*xml));
    applyExtraState (apvts.state);
    presetManager.refreshReferenceFromDisk();
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new DybbukProcessor();
}
