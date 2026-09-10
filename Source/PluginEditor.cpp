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

    // Four bands under the header, one even rhythm from the rule to the foot
    // of the faders: the hero knobs, the dybbuk's row (the lamp in the
    // middle of the plate with two knobs on each side: how the pattern
    // plays), a knob row across the whole content width with FREEZE in its
    // middle (the hand and the output), and the mode bar along the bottom
    // of the plate. The bar is the one control that changes what the
    // creature IS rather than how much of something it does, so it sits
    // apart from the knobs, as the plate's foot, where a change of player
    // reads as a lever thrown under the whole instrument.
    // The bands are spaced by ink, not by box: about 32 px of paper between
    // each row's readouts and the top ticks of the row under it, and the
    // same again between the knob row's readouts and the bar, and between
    // the bar and the keyline. The dybbuk's longest limb (sixteen steps at
    // Linger's stretch) reaches 60 px above its centre, which is what sets
    // its row's distance from the hero readouts.
    constexpr int kHeroY = 160;   // face centres
    constexpr int kLampY = 322;   // the dybbuk and its four knobs
    constexpr int kMidY = 456;    // the knob row, with FREEZE in its middle
    constexpr int kModeY = 572;   // the mode bar's centre line

    constexpr int kHeroX[4] = { 170, 357, 543, 730 };

    // The two lower rows share one grid: five cells at equal pitch across the
    // content width between the faders (76..824), the dybbuk and FREEZE on
    // the centre cell. GLUE sits nearest the IN fader and SPREAD nearest
    // OUT, so the ends of the chain sit at the ends of the row, and DECAY,
    // FADE, DIRECTION and PITCH stand directly over them.
    constexpr int kContentL = 76, kContentR = 824;
    constexpr int kMidCount = 5;
    constexpr int midX (int k) // the centre of cell k, rounded from the exact pitch
    {
        return kContentL + ((2 * k + 1) * (kContentR - kContentL) + kMidCount) / (2 * kMidCount);
    }
    constexpr int kMidX[kMidCount] = { midX (0), midX (1), midX (2), midX (3), midX (4) };
    constexpr int kCentreX = kMidX[2];
    static_assert (kCentreX == DybbukEditor::canvasW / 2, "FREEZE sits on the plate's centre line");

    // The dybbuk's box, centred on the plate. Wide enough for sixteen limbs
    // around the ember at Linger's stretch, with room to breathe. The knobs
    // in its row are placed by the grid, and their 92 px boxes (a mid face
    // plus its 30 px of legend room) stop about 32 px short of it each side.
    constexpr int kLampSize = 144;
    constexpr int kMidKnobBoxW = 62 + 30;
    static_assert (kMidX[1] + kMidKnobBoxW / 2 <= kCentreX - kLampSize / 2 - 30,
                   "the dybbuk-row knobs clear the dybbuk's box");

    // The mode bar: five cells, sized so POSSESS sits comfortably in its
    // cell at the caption size.
    constexpr int kModeW = 440, kModeH = 28;

    // The Bar diamond hangs directly under the Sync diamond beside Time.
    constexpr int kSyncX = kHeroX[1] + 46, kSyncY = kHeroY - 12;
    constexpr int kBarY = kSyncY + 24;
    constexpr float kBarDimAlpha = 0.4f; // the Bar diamond while Sync is off

    // What the dice last rolled is printed in the strip of clear paper
    // between the knob row's readouts and the mode bar, on the plate's
    // centre line under FREEZE. Nothing else is drawn there, so the
    // caption never brushes a limb or a readout.
    // Centred on ink, not on boxes: a mid knob's readout ends 72 px under
    // its face, with 8 px of empty paper under that inside its box.
    constexpr int kMidReadoutBottom = 72;
    constexpr int kRolledH = 14;
    const juce::Rectangle<int> kRolledArea (kCentreX - 110,
                                            (kMidY + kMidReadoutBottom + kModeY - kModeH / 2) / 2 - kRolledH / 2,
                                            220, kRolledH);

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

        // What the dice last rolled, fading out under the knob row: the name
        // is the character the whole patch has just been given, so it is
        // printed on the plate rather than beside the button.
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
                          { kHeroX[1], kHeroY }, "50 MS", "2 S");
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
    syncToggle->setBounds (kSyncX, kSyncY, 40, 26);

    // Bar: synced, the pattern restarts on every bar line. It only means
    // something on the grid, so it is dimmed while Sync is off.
    barToggle = std::make_unique<DiamondToggle> (param (params::id::barreset),
                                                 DiamondToggle::Style::bare, "BAR", "BAR");
    plate.addAndMakeVisible (*barToggle);
    barToggle->setBounds (kSyncX, kBarY, 40, 26);
    barToggle->setAlpha (proc.isSynced() ? 1.0f : kBarDimAlpha);

    // --- the mode bar, along the foot of the plate -------------------------
    // No caption: the five names are the whole control. It is bound to the
    // Mode choice, so the cells are the parameter's own options in order.
    // Centred between the knob row's readouts and the bottom keyline, on the
    // faders' feet, with the fader readouts well outside its ends.
    modeToggle = std::make_unique<ModeToggle> (param (params::id::mode),
                                               juce::StringArray { "POSSESS", "LINGER", "LEGION", "HAUNT", "SEIZE" });
    plate.addAndMakeVisible (*modeToggle);
    modeToggle->setBounds (kCentreX - kModeW / 2, kModeY - kModeH / 2, kModeW, kModeH);

    // --- the dybbuk's row -----------------------------------------------------
    // The lamp in the centre of the plate and how the pattern plays around
    // it: DECAY and FADE on its left, DIRECTION and PITCH on its right, all
    // on the grid the row below uses, their faces level with the ember.
    plate.addAndMakeVisible (lamp);
    lamp.setBounds (kCentreX - kLampSize / 2, kLampY - kLampSize / 2, kLampSize, kLampSize);

    addKnob (lengthKnob, params::id::length, "DECAY", EngravedKnob::midSpec(),
             { kMidX[0], kLampY }, "5", "100");
    addKnob (fadeKnob, params::id::fade, "FADE", EngravedKnob::midSpec(),
             { kMidX[1], kLampY }, "NEVER", "100");
    auto& direction = addKnob (directionKnob, params::id::direction, "DIRECTION",
                               EngravedKnob::midSpec(), { kMidX[3], kLampY }, "FWD", "RANDOM");
    // Five ways round the pattern: a detent for each, and the word under it.
    direction.setDetents (BurstEngine::kDirectionCount);
    direction.setValueTextProvider ([this]
    {
        auto& d = param (params::id::direction);
        return d.getText (d.getValue(), 32);
    });

    // Pitch: the hardware's CLOCK, the half that repitches. Detented at
    // every semitone, an octave each way.
    auto& pitch = addKnob (pitchKnob, params::id::pitch, "PITCH", EngravedKnob::midSpec(),
                           { kMidX[4], kLampY }, "-12", "+12");
    pitch.setDetents (25);

    // --- the knob row, under the dybbuk --------------------------------------
    // The hand and the output, five at one pitch: FREEZE dead centre with
    // FILLS and CHAOS either side of it, and the ends of the chain at the
    // ends of the row (GLUE by the IN fader, SPREAD by the OUT fader).
    addKnob (glueKnob, params::id::glue, "GLUE", EngravedKnob::midSpec(),
             { kMidX[0], kMidY }, "CLEAN", "100");
    addKnob (fillsKnob, params::id::fills, "FILLS", EngravedKnob::midSpec(),
             { kMidX[1], kMidY }, "OFF", "100");
    addKnob (chaosKnob, params::id::chaos, "CHAOS", EngravedKnob::midSpec(),
             { kMidX[3], kMidY }, "STILL", "100");
    addKnob (spreadKnob, params::id::spread, "SPREAD", EngravedKnob::midSpec(),
             { kMidX[4], kMidY }, "MONO", "100");

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
    lengthKnob->setValueTextProvider (percent (params::id::length));

    // Fills, Chaos, Glue, Spread and Fade say a word at the bottom of their
    // travel rather than "0 %", because "Off", "Still", "Clean", "Mono" and
    // "Never" are what they mean. The words are the parameters' own, so the
    // plate and the host agree.
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
    glueKnob->setValueTextProvider (percentOrWord (params::id::glue, "Clean"));
    spreadKnob->setValueTextProvider (percentOrWord (params::id::spread, "Mono"));
    fadeKnob->setValueTextProvider (percentOrWord (params::id::fade, "Never"));

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
    lamp.setMode (juce::roundToInt (raw (params::id::mode)));

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
        stepKnob->setLegends (synced ? "1/32" : "50 MS", synced ? "1 BAR" : "2 S");
        barToggle->setAlpha (synced ? 1.0f : kBarDimAlpha);
    }
    modeToggle->tick();

    for (auto* knob : { stepKnob.get(), stepsKnob.get(), thresholdKnob.get(), blendKnob.get(),
                        lengthKnob.get(), fadeKnob.get(), directionKnob.get(), pitchKnob.get(),
                        glueKnob.get(), fillsKnob.get(), chaosKnob.get(), spreadKnob.get() })
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
