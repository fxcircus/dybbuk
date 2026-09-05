#include "PluginEditor.h"

#include "dsp/TimeMap.h"

void DybbukEditor::Canvas::paint (juce::Graphics& g)
{
    g.fillAll (theme::background);
    if (onPaint)
        onPaint (g);
}

DybbukEditor::DybbukEditor (DybbukProcessor& p)
    : juce::AudioProcessorEditor (p), proc (p)
{
    // The theme must be set before any Label or TextEditor caches its colours.
    theme::setTheme ((int) proc.apvts.state.getProperty (theme::kThemeProperty, theme::kDefaultTheme));

    addAndMakeVisible (canvas);
    canvas.setBounds (0, 0, canvasW, canvasH);
    canvas.onPaint = [this] (juce::Graphics& g)
    {
        g.setColour (theme::ink);
        g.setFont (juce::FontOptions (26.0f).withStyle ("Bold"));
        g.drawText ("Dybbuk", 24, 16, 240, 30, juce::Justification::centredLeft);

        g.setColour (theme::faded);
        g.setFont (juce::FontOptions (13.0f));
        g.drawText (juce::String::fromUTF8 ("\xd7\x93\xd7\x99\xd7\x91\xd7\x95\xd7\xa7"),
                    150, 22, 90, 20, juce::Justification::centredLeft);

        // The ember: the loop's own energy, the centrepiece of the real UI.
        const auto centre = juce::Point<float> (canvasW * 0.5f, 400.0f);
        const float radius = 14.0f + 30.0f * emberLevel;
        g.setColour (theme::ember.withAlpha (0.16f * emberLevel));
        g.fillEllipse (centre.x - radius, centre.y - radius, radius * 2.0f, radius * 2.0f);
        g.setColour (theme::ember.withAlpha (juce::jlimit (0.15f, 1.0f, emberLevel)));
        g.fillEllipse (centre.x - 9.0f, centre.y - 9.0f, 18.0f, 18.0f);
        g.setColour (theme::inkA (0.35f));
        g.drawEllipse (centre.x - 16.0f, centre.y - 16.0f, 32.0f, 32.0f, 1.0f);

        // Readout strip: what Time actually is, which a 0..1 knob cannot say.
        g.setColour (theme::faded);
        g.setFont (juce::FontOptions (13.0f));
        g.drawText (delayText, 24, 486, canvasW - 48, 18, juce::Justification::centred);

        // Output meter behind the Out row.
        const auto meter = juce::Rectangle<float> (24.0f, 528.0f, canvasW - 48.0f, 10.0f);
        g.setColour (theme::inkA (0.22f));
        g.drawRect (meter, 1.0f);
        g.setColour (theme::accent);
        g.fillRect (meter.reduced (1.0f).withWidth ((meter.getWidth() - 2.0f)
                                                    * juce::jlimit (0.0f, 1.0f, meterLevel)));
    };

    // Hero row, then the second row, matching the shape of the designed UI.
    addKnob (timeKnob, params::id::time, "Time",
             "How long sound takes to come back around. Sweeping it bends the pitch of what is already in the loop.", 40, 70, 116);
    addKnob (decayKnob, params::id::decay, "Decay",
             "How many times the sound goes around. Past 1.00 the loop feeds itself and runs away.", 208, 70, 116);
    addKnob (filterKnob, params::id::filter, "Filter",
             "Darkens the loop with every pass. Full right lets everything through.", 376, 70, 116);
    addKnob (blendKnob, params::id::blend, "Blend", "Dry to wet balance, equal power in the middle.", 544, 70, 116);

    addKnob (timeModKnob, params::id::timemod, "Time Mod",
             "Audio rate wobble on the time, for metallic clangs and ring mod colours.", 30, 230, 86);
    addKnob (strengthKnob, params::id::strength, "Strength",
             "Input gain into the loop. More than a little starts to drive.", 168, 230, 86);
    addKnob (resonanceKnob, params::id::resonance, "Resonance",
             "Sharpens the filter's edge into a whistle, and past 90 percent it sings on its own.", 442, 230, 86);
    addKnob (absorbKnob, params::id::absorb, "Absorb",
             "Pulls the wet signal into the earth: quieter and older sounding the further you turn it.", 580, 230, 86);

    addKnob (agitateKnob, params::id::agitate, "Agitate",
             "How hard the internal modulation shakes the loop.", 90, 350, 100);
    addKnob (agitSpeedKnob, params::id::agitspeed, "Speed",
             "How fast the agitation cycles, from a minute per breath up to audio rate.", 500, 350, 100);
    addKnob (outKnob, params::id::out, "Out", "Output level after the blend.", 316, 528, 0);

    canvas.addAndMakeVisible (syncButton);
    syncButton.setBounds (156, 190, 70, 22);
    syncButton.setTooltip ("Locks Time to note values at the host tempo. Long divisions cap at the memory's ceiling.");

    canvas.addAndMakeVisible (agitModeBox);
    agitModeBox.addItemList ({ "Loop", "Gate" }, 1);
    agitModeBox.setBounds (306, 300, 108, 24);
    agitModeBox.setTooltip ("Loop cycles the agitation on its own. Gate fires one cycle each time you play.");

    canvas.addAndMakeVisible (clearButton);
    clearButton.setBounds (330, 448, 60, 22);
    clearButton.setTooltip ("Empties the loop instantly.");
    clearButton.onClick = [this] { proc.requestClear(); };

    canvas.addAndMakeVisible (bypassButton);
    bypassButton.setBounds (24, 546, 90, 22);
    bypassButton.setTooltip ("Takes Dybbuk out of circuit with a short crossfade. The loop keeps running underneath.");

    canvas.addAndMakeVisible (themeButton);
    themeButton.setBounds (canvasW - 90, 18, 66, 22);
    themeButton.onClick = [this]
    {
        const int next = (theme::currentTheme() + 1) % theme::kThemeCount;
        theme::setTheme (next);
        // A property, not a parameter: changing the theme must never dirty the
        // preset or show up in a host automation lane.
        proc.apvts.state.setProperty (theme::kThemeProperty, next, nullptr);
        applyTheme();
    };

    syncAttach = std::make_unique<ButtonAttachment> (proc.apvts, params::id::timesync, syncButton);
    bypassAttach = std::make_unique<ButtonAttachment> (proc.apvts, params::id::bypass, bypassButton);
    agitModeAttach = std::make_unique<ComboAttachment> (proc.apvts, params::id::agitmode, agitModeBox);

    applyTheme();
    startTimerHz (30);

    setResizable (true, true);
    setResizeLimits (canvasW / 2, canvasH / 2, canvasW * 2, canvasH * 2);
    getConstrainer()->setFixedAspectRatio ((double) canvasW / (double) canvasH);
    setSize (canvasW, canvasH);
}

DybbukEditor::~DybbukEditor() = default;

void DybbukEditor::addKnob (Knob& knob, const char* paramID, const char* text, const char* tooltip,
                            int x, int y, int size)
{
    const bool horizontal = size == 0; // the Out fader runs across the bottom
    knob.slider.setSliderStyle (horizontal ? juce::Slider::LinearHorizontal
                                           : juce::Slider::RotaryHorizontalVerticalDrag);
    knob.slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, horizontal ? 90 : size - 20, 18);
    knob.slider.setTooltip (tooltip);
    canvas.addAndMakeVisible (knob.slider);

    if (horizontal)
    {
        knob.slider.setBounds (110, 540, canvasW - 240, 24);
        knob.slider.setTextBoxStyle (juce::Slider::TextBoxRight, false, 70, 20);
    }
    else
    {
        knob.slider.setBounds (x, y, size, size);
        knob.label.setText (text, juce::dontSendNotification);
        knob.label.setJustificationType (juce::Justification::centred);
        canvas.addAndMakeVisible (knob.label);
        knob.label.setBounds (x, y + size + 2, size, 16);
    }

    knob.attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
        proc.apvts, paramID, knob.slider);
}

void DybbukEditor::applyTheme()
{
    auto skin = [] (Knob& knob)
    {
        knob.slider.setColour (juce::Slider::rotarySliderFillColourId, theme::accent);
        knob.slider.setColour (juce::Slider::rotarySliderOutlineColourId, theme::inkA (0.25f));
        knob.slider.setColour (juce::Slider::thumbColourId, theme::ink);
        knob.slider.setColour (juce::Slider::trackColourId, theme::accent);
        knob.slider.setColour (juce::Slider::textBoxTextColourId, theme::ink);
        knob.slider.setColour (juce::Slider::textBoxOutlineColourId, theme::inkA (0.2f));
        knob.slider.setColour (juce::Slider::textBoxBackgroundColourId, theme::panel);
        knob.label.setColour (juce::Label::textColourId, theme::faded);
    };

    for (Knob* k : { &timeKnob, &decayKnob, &filterKnob, &blendKnob, &timeModKnob, &strengthKnob,
                     &resonanceKnob, &absorbKnob, &agitateKnob, &agitSpeedKnob, &outKnob })
        skin (*k);

    for (juce::ToggleButton* b : { &syncButton, &bypassButton })
    {
        b->setColour (juce::ToggleButton::textColourId, theme::ink);
        b->setColour (juce::ToggleButton::tickColourId, theme::accent);
    }

    for (juce::TextButton* b : { &clearButton, &themeButton })
    {
        b->setColour (juce::TextButton::buttonColourId, theme::panel);
        b->setColour (juce::TextButton::textColourOffId, theme::ink);
    }

    agitModeBox.setColour (juce::ComboBox::backgroundColourId, theme::panel);
    agitModeBox.setColour (juce::ComboBox::textColourId, theme::ink);
    agitModeBox.setColour (juce::ComboBox::outlineColourId, theme::inkA (0.25f));

    canvas.repaint();
}

void DybbukEditor::timerCallback()
{
    bool needsRepaint = false;

    const float level = proc.getOutputLevel();
    if (std::abs (level - meterLevel) > 0.005f)
    {
        meterLevel = level;
        needsRepaint = true;
    }

    const float energy = proc.getLoopEnergy();
    if (std::abs (energy - emberLevel) > 0.004f)
    {
        emberLevel = energy;
        needsRepaint = true;
    }

    // A 0 to 1 Time knob cannot say what it means, so the strip does. It is
    // derived from the parameters rather than from the engine's published
    // value, so it is correct the moment the window opens and after a preset
    // load, not only once audio has been flowing.
    const bool synced = proc.apvts.getRawParameterValue (params::id::timesync)->load() >= 0.5f;
    const float timeKnobValue = proc.apvts.getRawParameterValue (params::id::time)->load();
    juce::String text;
    if (synced)
    {
        const int division = timemap::divisionIndexForTime01 (timeKnobValue);
        const auto resolved = timemap::syncedDelaySeconds (division, proc.getLastKnownBpm(),
                                                           (double) proc.getBeatsPerBar());
        text = juce::String (timemap::kDivisions[division].name) + "  "
               + params::timeReadout (resolved.seconds);
        if (resolved.clamped)
            text += "  (capped)";
    }
    else
    {
        text = params::timeReadout ((double) pt::delaySecondsForTime01 (timeKnobValue));
    }

    if (text != delayText)
    {
        delayText = text;
        needsRepaint = true;
    }

    if (needsRepaint)
        canvas.repaint();
}

void DybbukEditor::resized()
{
    const float scale = juce::jmin ((float) getWidth() / (float) canvasW,
                                    (float) getHeight() / (float) canvasH);
    canvas.setTransform (juce::AffineTransform::scale (scale));
}
