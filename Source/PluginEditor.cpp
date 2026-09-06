#include "PluginEditor.h"

#include "dsp/TimeMap.h"

namespace
{
    // How many octaves each knob's own range spans, for placing the modulation
    // arc in that knob's coordinates.
    constexpr float kTimeKnobOctaves = 7.06f;   // 200 kHz down to 1.5 kHz
    constexpr float kFilterKnobOctaves = 9.81f; // 20 Hz up to 18 kHz

    // Layout, read off the canvas. The plate is a fixed sheet; nothing here is
    // responsive, which is what keeps a hand-drawn window honest at every size.
    // The canvas transitions its sheet over 350 ms.
    constexpr float kThemeFadeSeconds = 0.35f;

    constexpr int kHeaderH = 52;
    constexpr int kRuleY = 62;
    constexpr int kFaderW = 60;

    constexpr int kHeroY = 174;   // face centres
    constexpr int kMidY = 380;
    constexpr int kBottomY = 535;

    constexpr int kHeroX[4] = { 170, 357, 543, 730 };
    constexpr int kMidX[5] = { 151, 300, 450, 600, 749 };

    juce::Image makeGrain (int w, int h, juce::Random& rng)
    {
        // A still field of noise over the whole plate. The canvas overlays it
        // at low opacity in blend mode; here it is pre-multiplied into an
        // image so paint() stays cheap.
        juce::Image image (juce::Image::ARGB, w, h, true);
        juce::Image::BitmapData data (image, juce::Image::BitmapData::writeOnly);
        for (int y = 0; y < h; ++y)
            for (int x = 0; x < w; ++x)
            {
                const auto v = (juce::uint8) rng.nextInt (256);
                data.setPixelColour (x, y, juce::Colour (v, v, v).withAlpha (0.5f));
            }
        return image;
    }
}

void DybbukEditor::Plate::paint (juce::Graphics& g)
{
    g.fillAll (theme::palette().paper);
    if (onPaint)
        onPaint (g);
}

juce::RangedAudioParameter& DybbukEditor::param (const char* id) const
{
    return *proc.apvts.getParameter (id);
}

EngravedKnob& DybbukEditor::addKnob (std::unique_ptr<EngravedKnob>& slot, const char* id,
                                     const char* label, EngravedKnob::Spec spec,
                                     juce::Point<int> faceCentre, const char* minLegend,
                                     const char* maxLegend)
{
    slot = std::make_unique<EngravedKnob> (param (id), label, spec);
    plate.addAndMakeVisible (*slot);
    slot->setBounds (EngravedKnob::boundsFor (spec, faceCentre));
    slot->setLegends (minLegend, maxLegend);
    return *slot;
}

DybbukEditor::DybbukEditor (DybbukProcessor& p)
    : juce::AudioProcessorEditor (p), proc (p)
{
    // The theme must be set before anything caches a colour.
    theme::setTheme (theme::kindFromProperty (proc.apvts.state.getProperty (theme::kThemeProperty)));

    juce::Random rng (0x0d1bb0c);
    grain = makeGrain (canvasW, canvasH, rng);

    addAndMakeVisible (plate);
    plate.setBounds (0, 0, canvasW, canvasH);
    plate.onPaint = [this] (juce::Graphics& g)
    {
        const auto& pal = theme::palette();

        theme::drawTracked (g, "DYBBUK", { 30.0f, 12.0f, 120.0f, 28.0f },
                            juce::Justification::centredLeft, theme::Face::semibold, 20.0f, 0.2f,
                            pal.ink);

        g.setFont (theme::font (theme::Face::hebrew, 20.0f));
        g.setColour (pal.ink);
        g.drawText (juce::String::fromUTF8 ("\xd7\x93\xd7\x99\xd7\x91\xd7\x95\xd7\xa7"),
                    juce::Rectangle<float> (150.0f, 12.0f, 60.0f, 28.0f),
                    juce::Justification::centredLeft, false);

        // The rule under the header, stopping short of the plate edges.
        g.setColour (pal.ink.withAlpha (0.28f));
        g.fillRect (46.0f, (float) kRuleY, (float) canvasW - 92.0f, 1.0f);

        // Grain, then the vignette, then the inner keyline: the three things
        // that make it read as a printed plate rather than a panel.
        g.setOpacity (0.05f);
        g.drawImageAt (grain, 0, 0);
        g.setOpacity (1.0f);

        juce::ColourGradient vignette (juce::Colours::transparentBlack, (float) canvasW * 0.5f,
                                       (float) canvasH * 0.3f,
                                       juce::Colours::black.withAlpha (pal.vignette),
                                       (float) canvasW * 0.5f, (float) canvasH * 1.05f, true);
        vignette.addColour (0.58, juce::Colours::transparentBlack);
        g.setGradientFill (vignette);
        g.fillRect (getLocalBounds());

        g.setColour (pal.ink.withAlpha (0.18f));
        g.drawRect (5, 5, canvasW - 10, canvasH - 10, 1);
        g.setColour (pal.ink.withAlpha (0.45f));
        g.drawRect (0, 0, canvasW, canvasH, 1);
    };

    // --- header --------------------------------------------------------------
    bypassToggle = std::make_unique<DiamondToggle> (param (params::id::bypass),
                                                    DiamondToggle::Style::framed, "BYPASS", "IN");
    plate.addAndMakeVisible (*bypassToggle);
    bypassToggle->setBounds (222, kHeaderH / 2 - 11, 70, 22);

    plate.addAndMakeVisible (presetHeader);
    presetHeader.setBounds (canvasW / 2 - 130, kHeaderH / 2 - 14, 260, 28);

    plate.addAndMakeVisible (themeMark);
    themeMark.setBounds (canvasW - 30 - 70, kHeaderH / 2 - 11, 70, 22);
    themeMark.onClick = [this] { toggleTheme(); };

    // --- faders on both edges -------------------------------------------------
    inFader = std::make_unique<VerticalFader> (param (params::id::input), "IN");
    plate.addAndMakeVisible (*inFader);
    inFader->setBounds (8, kRuleY + 6, kFaderW, canvasH - kRuleY - 18);

    outFader = std::make_unique<VerticalFader> (param (params::id::out), "OUT");
    plate.addAndMakeVisible (*outFader);
    outFader->setBounds (canvasW - kFaderW - 8, kRuleY + 6, kFaderW, canvasH - kRuleY - 18);

    // --- hero row -------------------------------------------------------------
    auto& time = addKnob (timeKnob, params::id::time, "TIME", EngravedKnob::heroSpec(),
                          { kHeroX[0], kHeroY }, "27 MS", "3.6 S");
    auto& decay = addKnob (decayKnob, params::id::decay, "DECAY", EngravedKnob::heroSpec(),
                           { kHeroX[1], kHeroY }, "0", juce::String::fromUTF8 ("\xe2\x88\x9e").toRawUTF8());
    addKnob (filterKnob, params::id::filter, "FILTER", EngravedKnob::heroSpec(),
             { kHeroX[2], kHeroY }, "20", "18K");
    addKnob (blendKnob, params::id::blend, "BLEND", EngravedKnob::heroSpec(),
             { kHeroX[3], kHeroY }, "DRY", "WET");

    // Time reads as a delay, or as a note division while synced, because a
    // knob position cannot say what it means.
    time.setValueTextProvider ([this]
    {
        const float knob = proc.apvts.getRawParameterValue (params::id::time)->load();
        if (proc.apvts.getRawParameterValue (params::id::timesync)->load() >= 0.5f)
        {
            const int division = timemap::divisionIndexForTime01 (knob);
            const auto resolved = timemap::syncedDelaySeconds (division, proc.getLastKnownBpm(),
                                                               (double) proc.getBeatsPerBar());
            auto text = juce::String (timemap::kDivisions[division].name);
            if (resolved.clamped)
                text += " max";
            return text;
        }
        return params::timeReadout ((double) pt::delaySecondsForTime01 (knob));
    });

    // Decay names its own red zone rather than leaving the colour to explain it.
    decay.setDangerFrom (1.0f / pt::kDecayMax);
    decay.setValueTextProvider ([this]
    {
        const float v = proc.apvts.getRawParameterValue (params::id::decay)->load();
        return juce::String (v, 2) + (v > 1.005f ? " runaway" : "");
    });
    decay.onDoubleClick = [this]
    {
        proc.requestClear();
        return true;
    };

    syncToggle = std::make_unique<DiamondToggle> (param (params::id::timesync),
                                                  DiamondToggle::Style::bare, "SYNC", "SYNC");
    plate.addAndMakeVisible (*syncToggle);
    syncToggle->setBounds (kHeroX[0] + 46, kHeroY - 12, 40, 26);

    // --- second row -----------------------------------------------------------
    addKnob (timeModKnob, params::id::timemod, "TIME MOD", EngravedKnob::midSpec(),
             { kMidX[0], kMidY }, "0", "100");
    addKnob (strengthKnob, params::id::strength, "STRENGTH", EngravedKnob::midSpec(),
             { kMidX[1], kMidY }, "0", "+40");
    addKnob (resonanceKnob, params::id::resonance, "RESONANCE", EngravedKnob::midSpec(),
             { kMidX[3], kMidY }, "0", "100");
    addKnob (absorbKnob, params::id::absorb, "ABSORB", EngravedKnob::midSpec(),
             { kMidX[4], kMidY }, "0", "100");

    plate.addAndMakeVisible (lamp);
    lamp.setBounds (kMidX[2] - 52, kMidY - 56, 104, 104);

    plate.addAndMakeVisible (clearStamp);
    clearStamp.setBounds (kMidX[2] - 24, kMidY + 52, 48, 40);
    clearStamp.onClick = [this] { proc.requestClear(); };

    // --- bottom strip ---------------------------------------------------------
    addKnob (agitateKnob, params::id::agitate, "AGITATE", EngravedKnob::macroSpec(),
             { 330, kBottomY }, "0", "100");
    addKnob (speedKnob, params::id::agitspeed, "SPEED", EngravedKnob::speedSpec(),
             { 570, kBottomY }, "", "");

    modeSwitch = std::make_unique<RailSwitch> (param (params::id::agitmode), "LOOP", "GATE");
    plate.addAndMakeVisible (*modeSwitch);
    modeSwitch->setBounds (canvasW / 2 - 48, kBottomY - 15, 96, 34);

    // The plate spells out every unit, because a bare number under a knob is
    // only readable if you already know what the knob is. These follow the
    // canvas, which differs from the host strings in one place: Filter reads
    // kHz here and integers in the DAW's automation lane.
    auto percent = [this] (const char* id)
    {
        return [this, id] { return juce::String (juce::roundToInt (
                                       proc.apvts.getRawParameterValue (id)->load())) + " %"; };
    };

    filterKnob->setValueTextProvider ([this]
    {
        const float hz = proc.apvts.getRawParameterValue (params::id::filter)->load();
        return hz >= 1000.0f ? juce::String (hz / 1000.0f, 1) + " kHz"
                             : juce::String (juce::roundToInt (hz)) + " Hz";
    });
    blendKnob->setValueTextProvider (percent (params::id::blend));
    timeModKnob->setValueTextProvider (percent (params::id::timemod));
    resonanceKnob->setValueTextProvider (percent (params::id::resonance));
    absorbKnob->setValueTextProvider (percent (params::id::absorb));

    strengthKnob->setValueTextProvider ([this]
    {
        return "+" + juce::String (proc.apvts.getRawParameterValue (params::id::strength)->load(), 1)
               + " dB";
    });

    // Agitate reads as a multiplier on the whole modulation system, which is
    // what it is: one control over every route at once.
    agitateKnob->setValueTextProvider ([this]
    {
        const float v = proc.apvts.getRawParameterValue (params::id::agitate)->load() * 0.01f;
        return juce::String::fromUTF8 ("\xc3\x97 ") + juce::String (v * 2.0f, 2);
    });
    agitateKnob->setLegends ("0", juce::String::fromUTF8 ("\xc3\x97" "2"));

    speedKnob->setValueTextProvider ([this]
    {
        const float hz = proc.apvts.getRawParameterValue (params::id::agitspeed)->load();
        if (hz < 1.0f)
            return juce::String (hz, 3) + " Hz";
        if (hz < 100.0f)
            return juce::String (hz, 1) + " Hz";
        return juce::String (juce::roundToInt (hz)) + " Hz";
    });


    plate.addChildComponent (themeFade);
    themeFade.setBounds (0, 0, canvasW, canvasH);

    applyTheme();
    startTimerHz (kUiHz);

    setResizable (true, true);
    setResizeLimits (canvasW / 2, canvasH / 2, canvasW * 2, canvasH * 2);
    getConstrainer()->setFixedAspectRatio ((double) canvasW / (double) canvasH);
    setSize (canvasW, canvasH);
}

DybbukEditor::~DybbukEditor() = default;

void DybbukEditor::toggleTheme()
{
    // Grab the outgoing sheet before anything changes colour. Captured at 2x
    // so it stays crisp while the window is scaled up; it lives for 350 ms.
    themeFade.setVisible (false); // never photograph the overlay itself
    themeFade.image = plate.createComponentSnapshot (plate.getLocalBounds(), false, 2.0f);
    themeFade.progress = 1.0f;
    themeFade.setVisible (true);
    themeFade.toFront (false);

    const int next = (static_cast<int> (theme::currentTheme()) + 1) % theme::kThemeCount;
    theme::setTheme (next);
    // A property, not a parameter: the sheet you work on must never dirty the
    // preset or appear in a host automation lane.
    proc.apvts.state.setProperty (theme::kThemeProperty, next, nullptr);
    applyTheme();
}

void DybbukEditor::applyTheme()
{
    plate.repaint();
    for (auto* child : plate.getChildren())
        child->repaint();
}

void DybbukEditor::timerCallback()
{
    const float energy = proc.getLoopEnergy();
    const float decayValue = proc.apvts.getRawParameterValue (params::id::decay)->load();
    const bool bypassed = proc.apvts.getRawParameterValue (params::id::bypass)->load() >= 0.5f;

    lamp.setEnergy (energy);
    lamp.setRunaway (decayValue > 1.005f && energy > kRunawayEnergy);
    lamp.setBypassed (bypassed);
    lamp.setAgitation (proc.apvts.getRawParameterValue (params::id::agitate)->load() * 0.01f,
                       agitateKnob != nullptr ? speedKnob->normalisedValue() : 0.35f);
    lamp.tick();

    inFader->setLevel (proc.getInputLevel());
    outFader->setLevel (proc.getOutputLevel());
    inFader->tick();
    outFader->tick();

    // Sync turns the Time knob into detents, and marks them on the ring.
    const bool synced = proc.apvts.getRawParameterValue (params::id::timesync)->load() >= 0.5f;
    if (synced != lastSynced)
    {
        lastSynced = synced;
        timeKnob->setDetents (synced ? timemap::kDivisionCount : 0);
        timeKnob->setLegends (synced ? "1/32" : "27 MS", synced ? "1 BAR" : "3.6 S");
    }

    // Where modulation has actually taken each destination, in that knob's own
    // coordinates. The needle stays on what you set; the arc is what you hear.
    const float timeDepthOct = proc.getTimeModDepthOct();
    timeKnob->setModulation (timeDepthOct > 0.001f,
                             timeKnob->normalisedValue() + timeDepthOct / kTimeKnobOctaves);

    const float filterOct = proc.getFilterModOct();
    filterKnob->setModulation (std::abs (filterOct) > 0.001f,
                               filterKnob->normalisedValue() + filterOct / kFilterKnobOctaves);

    const float decayMod = proc.getDecayModLinear();
    decayKnob->setModulation (std::abs (decayMod) > 0.001f,
                              decayKnob->normalisedValue() + decayMod / pt::kDecayMax);

    for (auto* knob : { timeKnob.get(), decayKnob.get(), filterKnob.get(), blendKnob.get(),
                        timeModKnob.get(), strengthKnob.get(), resonanceKnob.get(),
                        absorbKnob.get(), agitateKnob.get(), speedKnob.get() })
        knob->tick();

    // The clear stamp lights on the engine's acknowledgement, not on the
    // click, so what you see is the loop actually being emptied.
    const int served = proc.getClearsServed();
    if (served != lastClearsServed)
    {
        lastClearsServed = served;
        clearStamp.flash();
        lamp.flash();
    }
    clearStamp.tick();

    presetHeader.tick();

    if (themeFade.progress > 0.0f)
    {
        themeFade.progress = juce::jmax (0.0f, themeFade.progress
                                                   - 1.0f / (kThemeFadeSeconds * (float) kUiHz));
        if (themeFade.progress <= 0.0f)
        {
            themeFade.setVisible (false);
            themeFade.image = juce::Image(); // 2x of the whole plate is worth releasing
        }
        themeFade.repaint();
    }
}

void DybbukEditor::resized()
{
    const float scale = juce::jmin ((float) getWidth() / (float) canvasW,
                                    (float) getHeight() / (float) canvasH);
    plate.setTransform (juce::AffineTransform::scale (scale));
}
