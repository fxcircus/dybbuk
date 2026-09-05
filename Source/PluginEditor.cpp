#include "PluginEditor.h"

#include "dsp/TimeMap.h"

namespace
{
    // How many octaves each knob's own range spans, for placing the modulated
    // arc in knob coordinates.
    constexpr float kTimeKnobOctaves = 7.06f;   // 200 kHz down to 1.5 kHz
    constexpr float kFilterKnobOctaves = 9.81f; // 20 Hz up to 18 kHz
}

void DybbukEditor::Canvas::paint (juce::Graphics& g)
{
    g.fillAll (theme::palette().background);
    if (onPaint)
        onPaint (g);
}

void DybbukEditor::ThemeButton::paint (juce::Graphics& g)
{
    const auto b = getLocalBounds().toFloat();
    const auto c = b.getCentre();
    g.setColour (theme::palette().ink.withAlpha (hovering ? 1.0f : 0.7f));

    if (theme::currentTheme() == theme::Kind::brass)
    {
        // Sun: what you would switch to.
        g.fillEllipse (c.x - 4.0f, c.y - 4.0f, 8.0f, 8.0f);
        for (int i = 0; i < 8; ++i)
        {
            const float a = juce::MathConstants<float>::twoPi * (float) i / 8.0f;
            const juce::Point<float> dir (std::sin (a), -std::cos (a));
            g.drawLine ({ c + dir * 6.0f, c + dir * 9.0f }, 1.3f);
        }
    }
    else
    {
        juce::Path moon;
        moon.addEllipse (c.x - 7.0f, c.y - 7.0f, 14.0f, 14.0f);
        juce::Path bite;
        bite.addEllipse (c.x - 3.0f, c.y - 8.5f, 13.0f, 13.0f);
        moon.setUsingNonZeroWinding (false);
        moon.addPath (bite);
        g.fillPath (moon);
    }
}

juce::RangedAudioParameter& DybbukEditor::param (const char* id) const
{
    return *proc.apvts.getParameter (id);
}

BrassKnob& DybbukEditor::addKnob (std::unique_ptr<BrassKnob>& slot, const char* id, const char* label,
                                  const char* hint, BrassKnob::Size size, juce::Point<int> centre)
{
    slot = std::make_unique<BrassKnob> (param (id), label, hint, size);
    canvas.addAndMakeVisible (*slot);
    slot->setBounds (BrassKnob::boundsFor (size, centre));
    wireReadout (*slot);
    return *slot;
}

void DybbukEditor::wireReadout (BrassKnob& knob)
{
    knob.onHover = [this] (BrassKnob& k) { readout.setProvider ([&k] { return k.readout(); }); };
}

DybbukEditor::DybbukEditor (DybbukProcessor& p)
    : juce::AudioProcessorEditor (p), proc (p)
{
    // The theme must be set before anything caches a colour.
    theme::setTheme (theme::kindFromProperty (proc.apvts.state.getProperty (theme::kThemeProperty)));

    addAndMakeVisible (canvas);
    canvas.setBounds (0, 0, canvasW, canvasH);
    canvas.onPaint = [] (juce::Graphics& g)
    {
        const auto& pal = theme::palette();

        g.setColour (pal.ink);
        g.setFont (theme::font (22.0f, theme::Weight::bold));
        g.drawText ("Dybbuk", 24, 11, 96, 26, juce::Justification::centredLeft, false);

        g.setColour (pal.faded);
        g.setFont (theme::font (19.0f));
        g.drawText (juce::String::fromUTF8 ("\xd7\x93\xd7\x99\xd7\x91\xd7\x95\xd7\xa7"),
                    124, 12, 56, 24, juce::Justification::centredLeft, false);

        g.setColour (pal.ink.withAlpha (0.15f));
        g.fillRect (24, 46, 672, 1);
    };

    // --- hero row -----------------------------------------------------------
    auto& time = addKnob (timeKnob, params::id::time, "time",
                          "How long sound takes to come back around. Sweeping it bends the pitch of what is already in the loop.",
                          BrassKnob::Size::large, { 108, 112 });
    auto& decay = addKnob (decayKnob, params::id::decay, "decay",
                           "How many times the sound goes around. Past 1.00 the loop feeds itself.",
                           BrassKnob::Size::large, { 276, 112 });
    addKnob (filterKnob, params::id::filter, "filter",
             "Darkens the loop with every pass. Full right lets everything through.",
             BrassKnob::Size::large, { 444, 112 });
    addKnob (blendKnob, params::id::blend, "blend", "Dry to wet balance, equal power in the middle.",
             BrassKnob::Size::large, { 612, 112 });

    // Time reads as a delay, or as a note division while synced, because a
    // 0 to 1 knob cannot say what it means.
    time.readoutProvider = [this]
    {
        Readout r;
        r.name = "time";
        r.hint = "How long sound takes to come back around. Sweeping it bends the pitch of what is already in the loop.";
        r.asideColour = theme::palette().faded;

        const float knob = proc.apvts.getRawParameterValue (params::id::time)->load();
        if (proc.apvts.getRawParameterValue (params::id::timesync)->load() >= 0.5f)
        {
            const int division = timemap::divisionIndexForTime01 (knob);
            const auto resolved = timemap::syncedDelaySeconds (division, proc.getLastKnownBpm(),
                                                               (double) proc.getBeatsPerBar());
            r.value = juce::String (timemap::kDivisions[division].name);
            r.aside = params::timeReadout (resolved.seconds);
            if (resolved.clamped)
            {
                r.aside += " max";
                r.asideColour = theme::palette().runaway;
            }
        }
        else
        {
            r.value = params::timeReadout ((double) pt::delaySecondsForTime01 (knob));
        }
        return r;
    };

    // Decay names its own red zone rather than leaving the colour to explain it.
    decay.setRunawayZone (1.0f / pt::kDecayMax);
    decay.readoutProvider = [this]
    {
        Readout r;
        r.name = "decay";
        r.hint = "How many times the sound goes around. Double-click to empty the loop.";
        const float v = proc.apvts.getRawParameterValue (params::id::decay)->load();
        r.value = juce::String (v, 2);
        r.asideColour = theme::palette().faded;
        if (v > 1.005f)
        {
            r.aside = "runaway";
            r.asideColour = theme::palette().runaway;
        }
        else if (v > 0.995f)
        {
            r.aside = "unity";
        }
        return r;
    };
    decay.onDoubleClick = [this]
    {
        proc.requestClear();
        return true; // not a reset to default
    };

    syncToggle = std::make_unique<PillToggle> (param (params::id::timesync),
                                               juce::StringArray { "free", "sync" }, "time sync",
                                               "Locks Time to note values at the host tempo.");
    canvas.addAndMakeVisible (*syncToggle);
    syncToggle->setBounds (156, 62, 44, 16);
    syncToggle->onHover = [this] (PillToggle& t) { readout.setProvider ([&t] { return t.readout(); }); };

    // --- second row ---------------------------------------------------------
    addKnob (timeModKnob, params::id::timemod, "time mod",
             "Audio rate wobble on the clock from the sub tone, for metallic clangs.",
             BrassKnob::Size::medium, { 99, 254 });
    addKnob (strengthKnob, params::id::strength, "strength",
             "Input gain into the loop. More than a little starts to drive.",
             BrassKnob::Size::medium, { 249, 254 });
    addKnob (resonanceKnob, params::id::resonance, "resonance",
             "Sharpens the filter into a whistle, and near the top it sings on its own.",
             BrassKnob::Size::medium, { 471, 254 });
    addKnob (absorbKnob, params::id::absorb, "absorb",
             "Pulls the wet signal into the earth: quieter and older sounding as you turn it.",
             BrassKnob::Size::medium, { 621, 254 });

    modeToggle = std::make_unique<PillToggle> (param (params::id::agitmode),
                                               juce::StringArray { "loop", "gate" }, "agit mode",
                                               "Loop cycles the agitation on its own. Gate fires one cycle when you play.");
    canvas.addAndMakeVisible (*modeToggle);
    modeToggle->setBounds (316, 240, 88, 28);
    modeToggle->onHover = [this] (PillToggle& t) { readout.setProvider ([&t] { return t.readout(); }); };

    // --- bottom strip -------------------------------------------------------
    addKnob (agitateKnob, params::id::agitate, "agitate",
             "How hard the internal modulation shakes the loop.",
             BrassKnob::Size::mediumLarge, { 200, 388 });
    addKnob (speedKnob, params::id::agitspeed, "speed",
             "How fast the agitation cycles, from a minute per breath up to audio rate.",
             BrassKnob::Size::medium, { 520, 388 });

    canvas.addAndMakeVisible (ember);
    ember.setBounds (300, 336, 120, 120);

    canvas.addAndMakeVisible (clearButton);
    clearButton.setBounds (332, 458, 56, 16);
    clearButton.onClick = [this] { proc.requestClear(); };
    clearButton.onHover = [this] (ClearButton& c) { readout.setProvider ([&c] { return c.readout(); }); };

    // --- readout and output -------------------------------------------------
    canvas.addAndMakeVisible (readout);
    readout.setBounds (24, 484, 672, 24);

    outSlider = std::make_unique<OutSlider> (param (params::id::out), "Output level after the blend.");
    canvas.addAndMakeVisible (*outSlider);
    outSlider->setBounds (24, 518, 672, 40);
    outSlider->onHover = [this] (OutSlider& s) { readout.setProvider ([&s] { return s.readout(); }); };

    // --- header -------------------------------------------------------------
    canvas.addAndMakeVisible (presetBar);
    presetBar.setBounds (230, 10, 260, 26);
    presetBar.onHover = [this] (PresetBar& b) { readout.setProvider ([&b] { return b.readout(); }); };
    presetBar.onPresetChanged = [this] { readout.setProvider (nullptr); };

    canvas.addAndMakeVisible (themeButton);
    themeButton.setBounds (676, 13, 20, 20);
    themeButton.onClick = [this]
    {
        const int next = (static_cast<int> (theme::currentTheme()) + 1) % theme::kThemeCount;
        theme::setTheme (next);
        // A property, not a parameter: the theme must never dirty the preset
        // or appear in a host automation lane.
        proc.apvts.state.setProperty (theme::kThemeProperty, next, nullptr);
        applyTheme();
    };

    // At rest the strip names the patch rather than going blank.
    readout.setRestReadout ({ "dybbuk", proc.presetManager.getCurrentName(), {},
                              theme::palette().faded,
                              "Hover a control to read what it does." });

    startTimerHz (kUiHz);

    setResizable (true, true);
    setResizeLimits (canvasW / 2, canvasH / 2, canvasW * 2, canvasH * 2);
    getConstrainer()->setFixedAspectRatio ((double) canvasW / (double) canvasH);
    setSize (canvasW, canvasH);
}

DybbukEditor::~DybbukEditor() = default;

void DybbukEditor::applyTheme()
{
    readout.setRestReadout ({ "dybbuk", proc.presetManager.getCurrentName(), {},
                              theme::palette().faded,
                              "Hover a control to read what it does." });
    canvas.repaint();
    for (auto* child : canvas.getChildren())
        child->repaint();
}

void DybbukEditor::showInReadout (const char* paramId)
{
    BrassKnob* match = nullptr;
    for (auto* knob : { timeKnob.get(), decayKnob.get(), filterKnob.get(), blendKnob.get(),
                        timeModKnob.get(), strengthKnob.get(), resonanceKnob.get(),
                        absorbKnob.get(), agitateKnob.get(), speedKnob.get() })
        if (knob != nullptr && knob->parameter().paramID == paramId)
            match = knob;

    if (match != nullptr)
        readout.setProvider ([match] { return match->readout(); });
}

juce::String DybbukEditor::readoutLine() const
{
    const auto r = readout.current();
    return r.name + " | " + r.value + (r.aside.isEmpty() ? juce::String() : " | " + r.aside);
}

void DybbukEditor::timerCallback()
{
    const float energy = proc.getLoopEnergy();
    const float decayValue = proc.apvts.getRawParameterValue (params::id::decay)->load();

    ember.setEnergy (energy);
    ember.setRunaway (decayValue > 1.005f && energy > kRunawayEnergy);
    ember.tick();

    outSlider->setPeak (proc.getOutputLevel());
    outSlider->tick();

    // Sync turns the Time knob into fourteen detents, and says so on the ring.
    const bool synced = proc.apvts.getRawParameterValue (params::id::timesync)->load() >= 0.5f;
    if (synced != lastSynced)
    {
        lastSynced = synced;
        timeKnob->setDetents (synced ? timemap::kDivisionCount : 0);
    }

    // Where modulation has actually taken each destination, in that knob's own
    // coordinates. The pointer stays on what you set; the dot is what you hear.
    const float timeDepthOct = proc.getTimeModDepthOct();
    const float timeNorm = timeKnob->normalisedValue();
    timeKnob->setModulation (timeDepthOct > 0.001f, timeNorm,
                             timeDepthOct / kTimeKnobOctaves);

    const float filterOct = proc.getFilterModOct();
    filterKnob->setModulation (std::abs (filterOct) > 0.001f,
                               filterKnob->normalisedValue() + filterOct / kFilterKnobOctaves, 0.0f);

    const float decayMod = proc.getDecayModLinear();
    decayKnob->setModulation (std::abs (decayMod) > 0.001f,
                              decayKnob->normalisedValue() + decayMod / pt::kDecayMax, 0.0f);

    for (auto* knob : { timeKnob.get(), decayKnob.get(), filterKnob.get(), blendKnob.get(),
                        timeModKnob.get(), strengthKnob.get(), resonanceKnob.get(),
                        absorbKnob.get(), agitateKnob.get(), speedKnob.get() })
        knob->tick();

    // The Clear button lights on the engine's acknowledgement, not on the
    // click, so what you see is the loop actually being emptied.
    const int served = proc.getClearsServed();
    if (served != lastClearsServed)
    {
        lastClearsServed = served;
        clearButton.flash();
        ember.flash();
    }
    clearButton.tick();

    bool anyDragging = outSlider->isDragging();
    for (auto* knob : { timeKnob.get(), decayKnob.get(), filterKnob.get(), blendKnob.get(),
                        timeModKnob.get(), strengthKnob.get(), resonanceKnob.get(),
                        absorbKnob.get(), agitateKnob.get(), speedKnob.get() })
        anyDragging = anyDragging || knob->isDragging();

    readout.setDragging (anyDragging);
    readout.setBypassed (proc.apvts.getRawParameterValue (params::id::bypass)->load() >= 0.5f);
    readout.tick();
    presetBar.tick();
}

void DybbukEditor::resized()
{
    const float scale = juce::jmin ((float) getWidth() / (float) canvasW,
                                    (float) getHeight() / (float) canvasH);
    canvas.setTransform (juce::AffineTransform::scale (scale));
}
