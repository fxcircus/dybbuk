#include "PluginProcessor.h"

#include "PluginEditor.h"
#include "dsp/TimeMap.h"
#include "state/Randomiser.h"
#include "state/WavExport.h"

namespace
{
    // Bump only when an existing id changes meaning, range or units, or when a
    // root property is renamed. Adding or removing a parameter is not a bump:
    // an absent VALUE falls back to the default, a stale one is ignored.
    // Bumped to 3 for the Burst direction: the delay's ids are gone and the
    // ones that survive (blend, chaos, in, out, bypass) kept their meaning, so
    // there is still no migration to write -- an old session simply carries
    // unknown ids, which restoreMissingParameterDefaults ignores. The version
    // moves anyway so a future migration has a hook to hang on.
    constexpr int currentStateVersion = 3;

    // Runs on every incoming tree before it replaces the live one, session or
    // preset, so an older saved layout can be brought forward.
    void migrateState (juce::ValueTree& tree)
    {
        const int version = (int) tree.getProperty ("stateVersion", 0);
        juce::ignoreUnused (version); // nothing has shipped before version 3
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

    pStep      = apvts.getRawParameterValue (params::id::step);
    pSteps     = apvts.getRawParameterValue (params::id::steps);
    pThreshold = apvts.getRawParameterValue (params::id::threshold);
    pBlend     = apvts.getRawParameterValue (params::id::blend);
    pFreeze    = apvts.getRawParameterValue (params::id::freeze);
    pPitch     = apvts.getRawParameterValue (params::id::pitch);
    pGlue      = apvts.getRawParameterValue (params::id::glue);
    pSpread    = apvts.getRawParameterValue (params::id::spread);
    pMode      = apvts.getRawParameterValue (params::id::mode);
    pBarReset  = apvts.getRawParameterValue (params::id::barreset);
    pFills     = apvts.getRawParameterValue (params::id::fills);
    pChaos     = apvts.getRawParameterValue (params::id::chaos);
    pDirection = apvts.getRawParameterValue (params::id::direction);
    pLength    = apvts.getRawParameterValue (params::id::length);
    pFade      = apvts.getRawParameterValue (params::id::feedback);
    pStepSync  = apvts.getRawParameterValue (params::id::stepsync);
    pInput     = apvts.getRawParameterValue (params::id::input);
    pOut       = apvts.getRawParameterValue (params::id::out);
    pBypass    = apvts.getRawParameterValue (params::id::bypass);

    // So the editor's readout is right the moment the window opens, before
    // the first block has resolved anything.
    stepSecondsNow.store (params::stepSecondsForKnob01 (pStep->load()), std::memory_order_relaxed);

    presetManager.stampExtraState = [this] (juce::ValueTree& s) { stampExtraState (s); };
    presetManager.applyExtraState = [this] { applyExtraState (apvts.state); };
    presetManager.migrateState = [] (juce::ValueTree& t) { migrateState (t); };
}

void DybbukProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    currentSampleRate = sampleRate;
    engine.prepare (sampleRate, samplesPerBlock);

    bypassDry.setSize (2, juce::jmax (1, samplesPerBlock), false, true, true);
    bypassMix.reset (sampleRate, 0.020);
    bypassMix.setCurrentAndTargetValue (pBypass != nullptr && pBypass->load() > 0.5f ? 1.0f : 0.0f);
}

void DybbukProcessor::releaseResources() {}

void DybbukProcessor::randomiseParameters()
{
    Randomiser::randomise (apvts, randomiserRng, randomMask);
}

void DybbukProcessor::setRandomField (unsigned int field, bool enabled) noexcept
{
    randomMask = enabled ? (randomMask | field) : (randomMask & ~field);
}

const char* DybbukProcessor::lastRandomCharacter() const noexcept
{
    return Randomiser::lastCharacterName();
}

BurstEngine::Direction DybbukProcessor::directionParam() const noexcept
{
    const int index = juce::jlimit (0, BurstEngine::kDirectionCount - 1,
                                    juce::roundToInt (pDirection->load()));
    return static_cast<BurstEngine::Direction> (index);
}

BurstEngine::Mode DybbukProcessor::modeParam() const noexcept
{
    const int index = juce::jlimit (0, BurstEngine::kModeCount - 1, juce::roundToInt (pMode->load()));
    return static_cast<BurstEngine::Mode> (index);
}

bool DybbukProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto in = layouts.getMainInputChannelSet();
    const auto out = layouts.getMainOutputChannelSet();

    // Stereo in or mono in to a stereo out (Live's effect chains are always
    // stereo; a mono input is copied to both sides before the engine), and
    // mono in to mono out for hosts that run mono tracks mono (Logic,
    // GarageBand). The engine takes any channel count; Spread simply has
    // nowhere to go on one channel.
    const bool mono = in == juce::AudioChannelSet::mono();
    if (out == juce::AudioChannelSet::stereo())
        return in == juce::AudioChannelSet::stereo() || mono;
    return out == juce::AudioChannelSet::mono() && mono;
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

    // Resolve the step clock here, not in the engine: the engine never sees
    // an AudioPlayHead, which is what lets EngineTest drive sync by writing
    // two numbers. Free and synced Step end up in the same field.
    double barBeats = 4.0;
    double bpm = knownBpm.load (std::memory_order_relaxed);
    bool playing = false;
    double ppq = 0.0, barStart = 0.0;
    bool havePpq = false;

    if (auto* playHead = getPlayHead())
    {
        if (const auto pos = playHead->getPosition())
        {
            const double reported = pos->getBpm().orFallback (bpm);
            if (std::isfinite (reported) && reported > 1.0 && reported < 999.0)
                bpm = reported;

            const auto sig = pos->getTimeSignature().orFallback (juce::AudioPlayHead::TimeSignature {});
            if (sig.numerator > 0 && sig.denominator > 0)
                barBeats = sig.numerator * 4.0 / sig.denominator;

            playing = pos->getIsPlaying();
            if (const auto q = pos->getPpqPosition())
            {
                ppq = *q;
                havePpq = std::isfinite (ppq);
            }
            barStart = pos->getPpqPositionOfLastBarStart().orFallback (0.0);
            if (! std::isfinite (barStart))
                barStart = 0.0;
        }
    }
    knownBpm.store (bpm, std::memory_order_relaxed);

    BurstEngine::Params p;
    const float stepKnob = pStep->load();
    p.gridOffsetSamples = -1;
    p.barOffsetSamples = -1;
    p.barReset = pBarReset->load() >= 0.5f;

    if (pStepSync->load() >= 0.5f)
    {
        const auto& d = timemap::kDivisions[timemap::divisionIndexForTime01 (stepKnob)];
        const double divBeats = d.isBars ? d.beats * barBeats : d.beats;
        p.stepSeconds = divBeats * 60.0 / bpm;

        // The grid is counted from the last bar line the host reported (0 if
        // it did not), so a division lands where the DAW's own grid draws it
        // in any time signature. Exactly on a line means offset 0, not a
        // whole division late; the epsilon absorbs the host's rounding.
        if (playing && havePpq)
        {
            const double rel = ppq - barStart;
            const double k = std::ceil (rel / divBeats - 1.0e-6);
            const double offsetBeats = juce::jmax (0.0, k * divBeats - rel);
            p.gridOffsetSamples = juce::roundToInt (offsetBeats * 60.0 / bpm * currentSampleRate);
            // The next bar line, for Bar: a block that starts on one gets 0.
            const double toBar = rel < 1.0e-6 ? 0.0 : juce::jmax (0.0, barBeats - rel);
            p.barOffsetSamples = juce::roundToInt (toBar * 60.0 / bpm * currentSampleRate);
        }
    }
    else
    {
        p.stepSeconds = params::stepSecondsForKnob01 (stepKnob);
    }
    stepSecondsNow.store (p.stepSeconds, std::memory_order_relaxed);

    p.inputDb = pInput->load();
    p.outDb = pOut->load();
    p.thresholdDb = pThreshold->load();
    p.maxSteps = juce::jlimit (1, BurstEngine::kMaxSteps, juce::roundToInt (pSteps->load()));
    p.record = pFreeze->load() < 0.5f;   // armed unless frozen
    p.replaceOldest = true;   // a full pattern is the last N things you played
    p.blend01 = pBlend->load() * 0.01f;
    p.fills01 = pFills->load() * 0.01f;
    p.chaos01 = pChaos->load() * 0.01f;
    p.length01 = pLength->load() * 0.01f;
    p.feedback01 = pFade->load() * 0.01f;
    p.direction = directionParam();
    p.pitchSemitones = pPitch->load();
    p.glue01 = pGlue->load() * 0.01f;
    p.spread01 = pSpread->load() * 0.01f;
    p.mode = modeParam();

    // Bypassed the engine keeps running but hears silence: it collects
    // nothing you play while out of circuit, and the pattern keeps its place
    // for the way back in. The crossfade below is what the host hears.
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

// --- export ------------------------------------------------------------------

juce::File DybbukProcessor::exportFolder()
{
    const auto folder = juce::File::getSpecialLocation (juce::File::userMusicDirectory)
                            .getChildFile ("Dybbuk");
    folder.createDirectory();
    return folder;
}

juce::String DybbukProcessor::exportFileName() const
{
    const int n = getStepCount();
    juce::String name = "Dybbuk pattern " + juce::String (n) + (n == 1 ? " step " : " steps ");

    if (isSynced())
    {
        // "1/16" is not a file name; "1-16" reads the same on a disk.
        const juce::String division (timemap::kDivisions[timemap::divisionIndexForTime01 (pStep->load())].name);
        name += division.replaceCharacter ('/', '-') + " "
                + juce::String (juce::roundToInt (lastKnownBpm())) + " bpm";
    }
    else
    {
        name += juce::String (juce::roundToInt (getStepSeconds() * 1000.0)) + " ms";
    }

    return name + ".wav";
}

bool DybbukProcessor::writePatternWav (const juce::File& dest) const
{
    BurstEngine::PatternCopy pattern;
    if (! engine.copyPattern (pattern) || pattern.steps.empty())
        return false;

    juce::AudioBuffer<float> rendered;
    BurstEngine::RenderSettings settings;
    settings.stepSeconds = getStepSeconds();
    settings.length01 = pLength->load() * 0.01f;
    settings.direction = directionParam();
    settings.pitchSemitones = pPitch->load();
    settings.glue01 = pGlue->load() * 0.01f;
    settings.spread01 = pSpread->load() * 0.01f;
    settings.mode = modeParam();
    const int samples = BurstEngine::renderPattern (pattern, settings, rendered);
    if (samples <= 0)
        return false;

    const double rate = pattern.sampleRate > 0.0 ? pattern.sampleRate : currentSampleRate;
    return wavexport::writeStereoFloat (dest, rendered, rate);
}

juce::File DybbukProcessor::renderPatternToFile() const
{
    if (! canExportPattern())
        return {};

    const auto dest = exportFolder().getChildFile (exportFileName()).getNonexistentSibling (false);
    return writePatternWav (dest) ? dest : juce::File();
}

// --- state -------------------------------------------------------------------

void DybbukProcessor::stampExtraState (juce::ValueTree& state) const
{
    state.setProperty ("stateVersion", currentStateVersion, nullptr);
    state.setProperty ("randomFields", (int) randomMask, nullptr);
    // No engine-side extra state. The pattern's contents are deliberately not
    // saved: a reloaded session starts listening, as the hardware would after
    // power up. Theme and window scale are editor properties already on the
    // tree, kept out of preset files by PresetManager.
}

void DybbukProcessor::applyExtraState (const juce::ValueTree& state)
{
    randomMask = (unsigned int) (int) state.getProperty ("randomFields", (int) Randomiser::fieldDefault);
    juce::ignoreUnused (state);
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
