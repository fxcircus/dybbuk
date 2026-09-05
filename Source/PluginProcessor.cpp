#include "PluginProcessor.h"

#include "PluginEditor.h"
#include "dsp/TimeMap.h"

namespace
{
    // Bump only when an existing id changes meaning, range or units, or when a
    // root property is renamed. Adding or removing a parameter is not a bump:
    // an absent VALUE falls back to the default, a stale one is ignored.
    constexpr int currentStateVersion = 1;

    // Runs on every incoming tree before it replaces the live one, session or
    // preset, so an older saved layout can be brought forward.
    void migrateState (juce::ValueTree& tree)
    {
        const int version = (int) tree.getProperty ("stateVersion", 0);
        juce::ignoreUnused (version); // nothing has shipped before version 1
        tree.setProperty ("stateVersion", currentStateVersion, nullptr);
    }
}

DybbukProcessor::DybbukProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput ("Input", juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "Dybbuk", params::createParameterLayout())
{
    setLatencySamples (0);

    pTime       = apvts.getRawParameterValue (params::id::time);
    pDecay      = apvts.getRawParameterValue (params::id::decay);
    pFilter     = apvts.getRawParameterValue (params::id::filter);
    pResonance  = apvts.getRawParameterValue (params::id::resonance);
    pAbsorb     = apvts.getRawParameterValue (params::id::absorb);
    pBlend      = apvts.getRawParameterValue (params::id::blend);
    pAgitate    = apvts.getRawParameterValue (params::id::agitate);
    pAgitSpeed  = apvts.getRawParameterValue (params::id::agitspeed);
    pStrength   = apvts.getRawParameterValue (params::id::strength);
    pOut        = apvts.getRawParameterValue (params::id::out);
    pTimeMod    = apvts.getRawParameterValue (params::id::timemod);
    pTimeSync   = apvts.getRawParameterValue (params::id::timesync);
    pAgitMode   = apvts.getRawParameterValue (params::id::agitmode);
    pTonesLevel = apvts.getRawParameterValue (params::id::toneslevel);
    pTonesPitch = apvts.getRawParameterValue (params::id::tonespitch);
    pSpread     = apvts.getRawParameterValue (params::id::spread);
    pBypass     = apvts.getRawParameterValue (params::id::bypass);

    presetManager.stampExtraState = [this] (juce::ValueTree& s) { stampExtraState (s); };
    presetManager.applyExtraState = [this] { applyExtraState (apvts.state); };
    presetManager.migrateState = [] (juce::ValueTree& t) { migrateState (t); };
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
    const auto in = layouts.getMainInputChannelSet();
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

    if (getTotalNumInputChannels() < 2 && buffer.getNumChannels() > 1)
        buffer.copyFrom (1, 0, buffer, 0, 0, numSamples);

    // Resolve Time Sync here, not in the engine: the engine never sees an
    // AudioPlayHead, which is what lets EngineTest drive sync by writing two
    // numbers. Free and synced Time end up in the same field and therefore
    // go through the same smoother, so changing division smears the pitch
    // exactly like turning the knob.
    double barBeats = 4.0;
    if (auto* playHead = getPlayHead())
    {
        if (const auto pos = playHead->getPosition())
        {
            const double bpm = pos->getBpm().orFallback (lastKnownBpm);
            if (std::isfinite (bpm) && bpm > 1.0 && bpm < 999.0)
                lastKnownBpm = bpm;

            const auto sig = pos->getTimeSignature().orFallback (juce::AudioPlayHead::TimeSignature {});
            if (sig.numerator > 0 && sig.denominator > 0)
                barBeats = sig.numerator * 4.0 / sig.denominator;
        }
    }

    beatsPerBar.store ((float) barBeats, std::memory_order_relaxed);

    DybbukEngine::Params p;
    const float timeKnob = pTime->load();

    if (pTimeSync->load() >= 0.5f)
    {
        const auto synced = timemap::syncedDelaySeconds (timemap::divisionIndexForTime01 (timeKnob),
                                                         lastKnownBpm, barBeats);
        p.time01 = pt::time01ForDelaySeconds ((float) synced.seconds);
        syncClamped.store (synced.clamped, std::memory_order_relaxed);
    }
    else
    {
        p.time01 = timeKnob;
        syncClamped.store (false, std::memory_order_relaxed);
    }

    p.strengthDb = pStrength->load();
    p.decay = pDecay->load();
    p.agitate01 = pAgitate->load() * 0.01f;
    p.agitSpeedHz = pAgitSpeed->load();
    p.agitGateMode = pAgitMode->load() >= 0.5f;
    p.timeMod01 = pTimeMod->load() * 0.01f;
    p.tonesLevel01 = pTonesLevel->load() * 0.01f;
    p.tonesPitchHz = pTonesPitch->load();
    p.spread01 = pSpread->load() * 0.01f;
    p.filterHz = pFilter->load();
    p.resonance01 = pResonance->load() * 0.01f;
    p.absorb01 = pAbsorb->load() * 0.01f;
    p.blend01 = pBlend->load() * 0.01f;
    p.outDb = pOut->load();

    const bool wantBypass = pBypass->load() >= 0.5f;
    p.bypass = wantBypass;

    bypassMix.setTargetValue (wantBypass ? 1.0f : 0.0f);
    const bool blending = wantBypass || bypassMix.isSmoothing() || bypassMix.getCurrentValue() > 0.0f;

    if (blending)
        for (int ch = 0; ch < juce::jmin (bypassDry.getNumChannels(), buffer.getNumChannels()); ++ch)
            bypassDry.copyFrom (ch, 0, buffer, ch, 0,
                                juce::jmin (numSamples, bypassDry.getNumSamples()));

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
    state.setProperty ("stateVersion", currentStateVersion, nullptr);
    // No engine-side extra state yet. The loop's contents are deliberately not
    // saved: a reloaded session starts empty, as the hardware would after
    // power up. Theme and window scale are editor properties already on the
    // tree, kept out of preset files by PresetManager.
}

void DybbukProcessor::applyExtraState (const juce::ValueTree& state)
{
    juce::ignoreUnused (state);
    // A freshly loaded patch may be a long way from where Time was. Snap the
    // clock instead of gliding across the whole range, which would otherwise
    // chirp everything currently in the buffer.
    engine.requestTimeSnap();
}

juce::AudioProcessorEditor* DybbukProcessor::createEditor()
{
    return new DybbukEditor (*this);
}

void DybbukProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    stampExtraState (state);

    if (auto xml = state.createXml())
        copyXmlToBinary (*xml, destData);
}

void DybbukProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    auto xml = getXmlFromBinary (data, sizeInBytes);

    if (xml == nullptr || ! xml->hasTagName (apvts.state.getType()))
        return;

    auto incoming = juce::ValueTree::fromXml (*xml);
    migrateState (incoming);

    // A session reload is the one case where the theme SHOULD come from the
    // file: it is the user's own window, saved as they left it.
    apvts.replaceState (incoming);
    applyExtraState (apvts.state);
    presetManager.refreshReferenceFromDisk();
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new DybbukProcessor();
}
