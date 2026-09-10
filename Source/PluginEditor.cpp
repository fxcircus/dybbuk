#include "PluginEditor.h"

#include "dsp/TimeMap.h"

namespace
{
    // Layout, read off the canvas. The plate is a fixed sheet; nothing here is
    // responsive, which is what keeps a hand-drawn window honest at every size.
    // The canvas transitions its sheet over 350 ms.
    constexpr float kThemeFadeSeconds = 0.35f;

    constexpr int kCanvasW = DybbukEditor::canvasW;
    constexpr int kHeaderH = 52;
    constexpr int kRuleY = 62;
    constexpr int kFaderW = 60;

    // The header in Shalal's zones, with a hairline between each pair: the
    // nameplate, the bypass cap, the station, the three actions, the theme.
    // Each control is centred in its zone. The station stays 260 wide on the
    // plate's centre line and its hairlines sit 16 px outside it, as they do
    // on Shalal's sheet.
    constexpr int kStationW = 260;
    constexpr int kHairAfterName = 198;
    constexpr int kHairBeforeStation = (kCanvasW - kStationW) / 2 - 16;
    constexpr int kHairAfterStation = (kCanvasW + kStationW) / 2 + 16;
    constexpr int kHairBeforeTheme = 780;
    constexpr int kHairTop = 8, kHairH = 38;
    constexpr int kBypassW = 70, kThemeW = 70;
    constexpr int kActionW = 52, kActionH = 44, kActionGap = 4;     // RANDOM, CLEAR, EXPORT
    constexpr int kActionsW = 3 * kActionW + 2 * kActionGap;
    constexpr int kActionsX = kHairAfterStation + (kHairBeforeTheme - kHairAfterStation - kActionsW) / 2;

    // Four bands under the header: the hero knobs, the dybbuk's row (the lamp
    // in the middle of the plate with a trim on each side), the small knobs,
    // and the freeze button on the bottom strip. With nothing stacked above
    // or below the dybbuk any more, the bands are spread so the paper between
    // them reads as one rhythm from the rule to the foot of the faders.
    constexpr int kHeroY = 160;   // face centres
    constexpr int kLampY = 310;
    constexpr int kMidY = 462;    // the knob row, with FREEZE in its middle

    constexpr int kHeroX[4] = { 170, 357, 543, 730 };
    constexpr int kMidX[5] = { 151, 300, 450, 600, 749 };

    // The dybbuk's box, centred on the plate. Wide enough for sixteen pips
    // around the ember with room to breathe.
    constexpr int kLampSize = 128;

    // What the dice last rolled is printed just over the dybbuk, in the paper
    // the export stamp used to occupy.
    const juce::Rectangle<int> kRolledArea (kMidX[2] - 110, kLampY - kLampSize / 2 - 24, 220, 16);

    // The trims flank the dybbuk with 50 px of paper between each rail's end
    // and its box.
    constexpr int kTrimW = 260;
    constexpr int kTrimH = 34;

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

        // Hairlines between the header's zones: after the nameplate, before
        // and after the station, before the theme.
        g.setColour (pal.ink.withAlpha (0.45f));
        for (const int x : { kHairAfterName, kHairBeforeStation, kHairAfterStation, kHairBeforeTheme })
            g.fillRect ((float) x, (float) kHairTop, 1.0f, (float) kHairH);

        // What the dice last rolled, fading out over the dybbuk: the name is
        // the character the pattern has just been given, so it is printed on
        // the pattern rather than beside the button.
        if (rolledTicks > 0 && rolledName != nullptr)
        {
            const float fade = juce::jmin (1.0f, (float) rolledTicks / 30.0f);
            theme::drawTracked (g, juce::String (rolledName).toUpperCase(), kRolledArea.toFloat(),
                                juce::Justification::centred, theme::Face::semibold, 9.0f,
                                0.18f, pal.ink.withAlpha (0.75f * fade));
        }

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
    // Shalal's header: identity (nameplate, bypass cap), which patch (the
    // station, alone in the centre), what to do with the pattern (RANDOM /
    // CLEAR / EXPORT, a group behind a hairline), appearance (theme).
    bypassToggle = std::make_unique<DiamondToggle> (param (params::id::bypass),
                                                    DiamondToggle::Style::framed, "BYPASS", "IN");
    plate.addAndMakeVisible (*bypassToggle);
    bypassToggle->setBounds (kHairAfterName + (kHairBeforeStation - kHairAfterName - kBypassW) / 2,
                             kHeaderH / 2 - 11, kBypassW, 22);

    plate.addAndMakeVisible (presetHeader);
    presetHeader.setBounds ((canvasW - kStationW) / 2, kHeaderH / 2 - 14, kStationW, 28);

    int actionX = kActionsX;
    for (auto* action : { &diceAction, &clearAction, &exportAction })
    {
        plate.addAndMakeVisible (*action);
        action->setBounds (actionX, 2, kActionW, kActionH);
        actionX += kActionW + kActionGap;
    }
    diceAction.onClick = [this] { rollDice(); };
    clearAction.onClick = [this] { proc.requestClear(); };
    // EXPORT: drag the tray and the pattern leaves as a WAV; click it for a
    // save dialog. Nothing to drag while there is nothing in the pattern.
    exportAction.onDragStart = [this] { dragPatternOut(); };
    exportAction.onClick = [this] { savePatternAs(); };
    exportAction.setEnabled (proc.canExportPattern());

    plate.addAndMakeVisible (themeMark);
    themeMark.setBounds (kHairBeforeTheme + (canvasW - kHairBeforeTheme - kThemeW) / 2,
                         kHeaderH / 2 - 11, kThemeW, 22);
    themeMark.onClick = [this] { toggleTheme(); };

    // --- faders on both edges -------------------------------------------------
    inFader = std::make_unique<VerticalFader> (param (params::id::input), "IN");
    plate.addAndMakeVisible (*inFader);
    inFader->setBounds (8, kRuleY + 6, kFaderW, canvasH - kRuleY - 18);

    outFader = std::make_unique<VerticalFader> (param (params::id::out), "OUT");
    plate.addAndMakeVisible (*outFader);
    outFader->setBounds (canvasW - kFaderW - 8, kRuleY + 6, kFaderW, canvasH - kRuleY - 18);

    // --- hero row -------------------------------------------------------------
    // Threshold first: it is the first thing the signal meets.
    addKnob (thresholdKnob, params::id::threshold, "THRESHOLD", EngravedKnob::heroSpec(),
             { kHeroX[0], kHeroY }, "-60", "0");
    auto& step = addKnob (stepKnob, params::id::step, "TIME", EngravedKnob::heroSpec(),
                          { kHeroX[1], kHeroY }, "20 MS", "2 S");
    auto& steps = addKnob (stepsKnob, params::id::steps, "STEPS", EngravedKnob::heroSpec(),
                           { kHeroX[2], kHeroY }, "1", "16");
    addKnob (blendKnob, params::id::blend, "BLEND", EngravedKnob::heroSpec(),
             { kHeroX[3], kHeroY }, "DRY", "WET");

    // Step reads as a time, or as a note division while synced, because a
    // knob position cannot say what it means. The free readout shares its
    // formatter with the host string; the synced one IS the host string.
    step.setValueTextProvider ([this]
    {
        if (proc.isSynced())
            return juce::String (timemap::kDivisions[timemap::divisionIndexForTime01 (raw (params::id::step))].name);
        return params::stepReadout (params::stepSecondsForKnob01 (raw (params::id::step)));
    });

    // Steps is a count: a detent on every integer and a bare number under it.
    steps.setDetents (BurstEngine::kMaxSteps);
    steps.setValueTextProvider ([this] { return juce::String (juce::roundToInt (raw (params::id::steps))); });

    thresholdKnob->setValueTextProvider ([this]
    {
        return juce::String (raw (params::id::threshold), 1) + " dB";
    });

    syncToggle = std::make_unique<DiamondToggle> (param (params::id::stepsync),
                                                  DiamondToggle::Style::bare, "SYNC", "SYNC");
    plate.addAndMakeVisible (*syncToggle);
    syncToggle->setBounds (kHeroX[1] + 46, kHeroY - 12, 40, 26);

    // --- the dybbuk's row -----------------------------------------------------
    // The lamp in the centre of the plate and a trim on each side: LENGTH to
    // its left, FADE to its right, their rails level with the ember.
    plate.addAndMakeVisible (lamp);
    lamp.setBounds (kMidX[2] - kLampSize / 2, kLampY - kLampSize / 2, kLampSize, kLampSize);

    lengthTrim = std::make_unique<EngravedTrim> (param (params::id::length), "LENGTH");
    plate.addAndMakeVisible (*lengthTrim);
    lengthTrim->setBounds (76, kLampY - kTrimH / 2, kTrimW, kTrimH);
    lengthTrim->setValueTextProvider ([this]
    {
        return juce::String (juce::roundToInt (raw (params::id::length))) + " %";
    });

    fadeTrim = std::make_unique<EngravedTrim> (param (params::id::fade), "FADE");
    plate.addAndMakeVisible (*fadeTrim);
    fadeTrim->setBounds (canvasW - 76 - kTrimW, kLampY - kTrimH / 2, kTrimW, kTrimH);
    fadeTrim->setValueTextProvider ([this]
    {
        const float v = raw (params::id::fade);
        return v < 0.5f ? juce::String ("NEVER") : juce::String (juce::roundToInt (v)) + " %";
    });

    // --- the small knobs, under the dybbuk -----------------------------------
    addKnob (fillsKnob, params::id::fills, "FILLS", EngravedKnob::midSpec(),
             { kMidX[0], kMidY }, "OFF", "100");
    addKnob (chaosKnob, params::id::chaos, "CHAOS", EngravedKnob::midSpec(),
             { kMidX[1], kMidY }, "STILL", "100");
    auto& direction = addKnob (directionKnob, params::id::direction, "DIRECTION",
                               EngravedKnob::midSpec(), { kMidX[3], kMidY }, "FWD", "DRUNK");
    // Five ways round the pattern: a detent for each, and the word under it.
    direction.setDetents (BurstEngine::kDirectionCount);

    // Pitch: the hardware's CLOCK, the half that repitches. Detented at
    // every semitone, an octave each way.
    auto& pitch = addKnob (pitchKnob, params::id::pitch, "PITCH", EngravedKnob::midSpec(),
                           { kMidX[4], kMidY }, "-12", "+12");
    pitch.setDetents (25);
    direction.setValueTextProvider ([this]
    {
        auto& d = param (params::id::direction);
        return d.getText (d.getValue(), 32);
    });

    // Freeze in the middle of the knob row, under the dybbuk, no caption:
    // the word is the whole control, lit blue while it holds. It and the
    // header's CLEAR are the whole performance.
    freezeToggle = std::make_unique<WordToggle> (param (params::id::freeze), juce::String(), "FREEZE", "FREEZE");
    plate.addAndMakeVisible (*freezeToggle);
    freezeToggle->setBounds (WordToggle::boundsFor ({ kMidX[2], kMidY }));
    freezeToggle->setAccent (WordToggle::Accent::blue);

    // The plate spells out every unit, because a bare number under a knob is
    // only readable if you already know what the knob is.
    auto percent = [this] (const char* id)
    {
        return [this, id] { return juce::String (juce::roundToInt (raw (id))) + " %"; };
    };
    blendKnob->setValueTextProvider (percent (params::id::blend));

    // Fills and Chaos say a word at the bottom of their travel rather than
    // "0 %", because "Off" and "Still" are what they mean. The words are the
    // parameters' own, so the plate and the host agree.
    auto percentOrWord = [this] (const char* id, const char* word)
    {
        return [this, id, word]
        {
            const float v = raw (id);
            return v < 0.5f ? juce::String (word) : juce::String (juce::roundToInt (v)) + " %";
        };
    };
    fillsKnob->setValueTextProvider (percentOrWord (params::id::fills, "Off"));
    chaosKnob->setValueTextProvider (percentOrWord (params::id::chaos, "Still"));

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

void DybbukEditor::rollDice()
{
    proc.randomiseParameters();
    // Say what was rolled. A dice that changes a dozen numbers at once is
    // otherwise unreadable, and the character's name is the one piece of
    // information that makes the patch make sense.
    rolledName = proc.lastRandomCharacter();
    rolledTicks = 90; // about three seconds at the editor's tick rate
    plate.repaint (kRolledArea);
}

void DybbukEditor::dragPatternOut()
{
    // Rendered on the spot into the Dybbuk folder, then handed to the OS: the
    // drop lands on a DAW track as a plain WAV, and the folder keeps a copy.
    const auto file = proc.renderPatternToFile();
    if (file == juce::File())
        return;
    juce::DragAndDropContainer::performExternalDragDropOfFiles ({ file.getFullPathName() }, true, this);
}

void DybbukEditor::savePatternAs()
{
    if (! proc.canExportPattern())
        return;

    auto chooser = std::make_shared<juce::FileChooser> ("Export the pattern as WAV",
                                                        DybbukProcessor::exportFolder().getChildFile (proc.exportFileName()),
                                                        "*.wav");
    // The chooser outlives an editor a host closes under it: the panel is
    // self-owned, so the completion must not assume `this` is alive.
    juce::Component::SafePointer<DybbukEditor> safe (this);
    chooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles
                              | juce::FileBrowserComponent::warnAboutOverwriting,
                          [safe, chooser] (const juce::FileChooser& fc)
                          {
                              if (safe == nullptr)
                                  return;
                              const auto file = fc.getResult();
                              if (file != juce::File())
                                  safe->proc.writePatternWav (file.withFileExtension ("wav"));
                          });
}

void DybbukEditor::timerCallback()
{
    const bool bypassed = raw (params::id::bypass) >= 0.5f;

    // The dybbuk: everything the engine publishes, once per frame.
    const int count = proc.getStepCount();
    for (int i = 0; i < BurstEngine::kMaxSteps; ++i)
        lamp.setStep (i, proc.getStepLevel (i), proc.getStepGain (i));
    lamp.setPattern (count, proc.getCurrentStep(), proc.getTicks());
    lamp.setCeiling (juce::roundToInt (raw (params::id::steps)), false);
    lamp.setGateOpen (proc.isGateOpen());
    lamp.setFillRunning (proc.isFillRunning());
    lamp.setBypassed (bypassed);
    lamp.setFrozen (raw (params::id::freeze) >= 0.5f);

    // The rolled character's name fades out over about three seconds.
    if (rolledTicks > 0 && --rolledTicks >= 0)
        plate.repaint (kRolledArea);
    lamp.tick();

    inFader->setLevel (proc.getInputLevel());
    outFader->setLevel (proc.getOutputLevel());
    inFader->tick();
    outFader->tick();

    // Sync turns the Step knob into detents, and marks them on the ring.
    const bool synced = proc.isSynced();
    if (synced != lastSynced)
    {
        lastSynced = synced;
        stepKnob->setDetents (synced ? timemap::kDivisionCount : 0);
        stepKnob->setLegends (synced ? "1/32" : "20 MS", synced ? "1 BAR" : "2 S");
    }

    for (auto* knob : { stepKnob.get(), stepsKnob.get(), thresholdKnob.get(), blendKnob.get(),
                        fillsKnob.get(), chaosKnob.get(), directionKnob.get(), pitchKnob.get() })
        knob->tick();

    // CLEAR lights on the engine's acknowledgement, not on the click, so
    // what you see is the pattern actually being emptied.
    const int served = proc.getClearsServed();
    if (served != lastClearsServed)
    {
        lastClearsServed = served;
        clearAction.flash();
        lamp.flash();
    }
    clearAction.tick();

    // Nothing to drag while there is nothing in the pattern.
    const bool exportable = proc.canExportPattern();
    if (exportable != exportAction.isEnabled())
        exportAction.setEnabled (exportable);

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
