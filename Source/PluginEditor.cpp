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
    // of the faders: the mode bar in the band directly under the rule, then
    // the hero knobs, the dybbuk's row (the lamp in the middle of the plate
    // with two knobs on each side: how the pattern plays), and a knob row
    // across the whole content width with FREEZE in its middle (the hand
    // and the output). The bar is the one control that changes what the
    // creature IS rather than how much of something it does, so it sits
    // apart from the knobs, above them all, where a change of player reads
    // as the heading over the whole instrument.
    // The bands are spaced by ink, not by box: about 24 px of paper between
    // the rule and the bar, then 32 px between the bar and the hero row's
    // top ticks, the same between each row's readouts and the top ticks of
    // the row under it, and the same again between the knob row's readouts
    // and the foot of the faders. The dybbuk's longest limb (sixteen steps
    // at Trance's stretch) reaches 60 px above its centre, which is what
    // sets its row's distance from the hero readouts.
    constexpr int kModeY = 100;   // the mode bar's centre line
    constexpr int kHeroY = 202;   // face centres
    constexpr int kLampY = 366;   // the dybbuk and its four knobs
    constexpr int kMidY = 502;    // the knob row, with FREEZE in its middle

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
    // around the ember at Trance's stretch, with room to breathe. The knobs
    // in its row are placed by the grid, and their 92 px boxes (a mid face
    // plus its 30 px of legend room) stop about 32 px short of it each side.
    constexpr int kLampSize = 144;
    constexpr int kMidKnobBoxW = 62 + 30;
    static_assert (kMidX[1] + kMidKnobBoxW / 2 <= kCentreX - kLampSize / 2 - 30,
                   "the dybbuk-row knobs clear the dybbuk's box");

    // The mode bar: five cells, sized so GOLEM sits comfortably in its
    // cell at the caption size.
    constexpr int kModeW = 440, kModeH = 28;

    // The Bar diamond hangs directly under the Sync diamond beside Time.
    constexpr int kSyncX = kHeroX[1] + 46, kSyncY = kHeroY - 12;
    constexpr int kBarY = kSyncY + 24;
    constexpr float kBarDimAlpha = 0.4f; // the Bar diamond while Sync is off

    // What the dice last rolled is printed in the strip of clear paper
    // between the knob row's readouts and the foot of the faders, on the
    // plate's centre line under FREEZE. Nothing else is drawn there, so the
    // caption never brushes a limb or a readout.
    // Centred on ink, not on boxes: a mid knob's readout ends 72 px under
    // its face, with 8 px of empty paper under that inside its box.
    constexpr int kMidReadoutBottom = 72;
    constexpr int kFaderFootY = DybbukEditor::canvasH - 12; // where the fader boxes end
    constexpr int kRolledH = 14;
    // The rolled name and the hover hint share the strip directly under the
    // mode bar (Roy, 2026-09-10: the hint reads better next to the modes it
    // explains than at the foot of the plate).
    const juce::Rectangle<int> kRolledArea (kCentreX - 110, kModeY + kModeH / 2 + 6, 220, kRolledH);

    // The hint line shares that strip: one sentence about the control under
    // the mouse, set in tracked small caps across the content width. The
    // rolled name keeps the strip for its three seconds; the hint fades in
    // and out over about 150 ms so neighbours never flicker into each other.
    // Tracked at this size the longest sentence just fits; a longer one
    // shrinks to the width rather than clipping.
    const juce::Rectangle<int> kHintArea (100, kRolledArea.getY(), 700, kRolledH);
    constexpr float kHintPx = 8.5f, kHintTracking = 0.12f;
    constexpr float kHintFadePerTick = 1.0f / 4.5f; // 150 ms at 30 Hz

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
    slot->setName (label); // the test hook finds controls by their caption
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

        // The hint line, on the same strip, faded ink so it reads as a
        // footnote to the plate rather than a control on it.
        if (hintAlpha > 0.0f && shownHint.isNotEmpty())
        {
            const auto text = shownHint.toUpperCase();
            const auto area = kHintArea.toFloat();
            float px = kHintPx;
            const float w = theme::trackedWidth (theme::Face::text, px, kHintTracking, text);
            if (w > area.getWidth())
                px *= area.getWidth() / w;
            theme::drawTracked (g, text, area, juce::Justification::centred, theme::Face::text,
                                px, kHintTracking, pal.faded.withAlpha (hintAlpha));
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

    // --- the mode bar, in the band under the rule ---------------------------
    // No caption: the five names are the whole control. It is bound to the
    // Mode choice, so the cells are the parameter's own options in order.
    // Centred between the rule and the hero row's top ticks, well inside
    // the faders' caps at either edge.
    modeToggle = std::make_unique<ModeToggle> (param (params::id::mode),
                                               juce::StringArray { "GOLEM", "WRAITH", "TRANCE", "LEGION", "TREMOR" });
    plate.addAndMakeVisible (*modeToggle);
    modeToggle->setBounds (kCentreX - kModeW / 2, kModeY - kModeH / 2, kModeW, kModeH);

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
    chaosKnob->setValueTextProvider (percentOrWord (params::id::chaos, "Still"));
    glueKnob->setValueTextProvider (percentOrWord (params::id::glue, "Clean"));
    spreadKnob->setValueTextProvider (percentOrWord (params::id::spread, "Mono"));
    fadeKnob->setValueTextProvider (percentOrWord (params::id::fade, "Never"));

    // Mode changes what three of the knobs mean, and the captions stay put
    // (a caption that changes is a knob you cannot find again), so the
    // readout says it instead: Decay is how much of a step a haunting keeps
    // sounding, in steps; Pitch is Legion's interval; Fills is Tremor's
    // ratchet count. The scales are the engine's own (raw / 100), so the
    // number printed is the number the engine uses.
    auto mode = [this] { return juce::roundToInt (raw (params::id::mode)); };
    lengthKnob->setValueTextProvider ([this, mode]
    {
        const float v = raw (params::id::length);
        switch (mode())
        {
            case Lamp::trance:
            {
                const float dec = juce::jlimit (0.0f, 1.0f, (v * 0.01f - 0.05f) / 0.95f);
                return juce::String (1.0f + 7.0f * dec * dec, 1) + "x slower";
            }
            case Lamp::wraith:
            {
                // Mirrors BurstEngine::hauntDecayPerTick: one to kWraithMaxTicks (8) ticks.
                const int kept = 1 + juce::roundToInt (7.0f * v * 0.01f);
                return "lasts " + juce::String (kept) + (kept == 1 ? " step" : " steps");
            }
            default: return juce::String (juce::roundToInt (v)) + " %";
        }
    });
    pitchKnob->setValueTextProvider ([this, mode]
    {
        const int v = juce::roundToInt (raw (params::id::pitch));
        const auto st = (v > 0 ? "+" : "") + juce::String (v) + " st";
        if (mode() != Lamp::legion)
            return st;
        return v == 0 ? juce::String ("chorus") : st + " apart";
    });
    fillsKnob->setValueTextProvider ([this, mode]
    {
        const float v = raw (params::id::fills);
        if (mode() == Lamp::tremor)
            return "x" + juce::String (1 + juce::roundToInt (3.0f * v * 0.01f));
        return v < 0.5f ? juce::String ("Off") : juce::String (juce::roundToInt (v)) + " %";
    });

    // Names for the test hook, which finds a control by its caption.
    modeToggle->setName ("MODE");
    freezeToggle->setName ("FREEZE");
    syncToggle->setName ("SYNC");
    barToggle->setName ("BAR");
    bypassToggle->setName ("BYPASS");
    inFader->setName ("IN");
    outFader->setName ("OUT");
    lamp.setName ("DYBBUK");
    presetHeader.setName ("PRESET");
    diceAction.setName ("RANDOM");
    clearAction.setName ("CLEAR");
    exportAction.setName ("EXPORT");
    themeMark.setName ("THEME");

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
    hintAlpha = 0.0f; // the name takes the strip at once; two lines on it is a smudge
    plate.repaint (kHintArea);
}

void DybbukEditor::showHintForTests (const juce::String& controlName)
{
    pinnedHint = controlName;
    updateHint();
    // Land at once: a snapshot should not have to wait out the fade.
    shownHint = wantedHint;
    hintAlpha = shownHint.isNotEmpty() ? 1.0f : 0.0f;
    plate.repaint (kHintArea);
}

juce::String DybbukEditor::hintFor (juce::Component* component, juce::Point<int> platePoint) const
{
    // Walk up to the child of the plate the mouse is in, so a hit inside
    // a control's own children (none today, but a popup list would be one)
    // still names the control.
    const juce::Component* c = component;
    while (c != nullptr && c != &plate && c->getParentComponent() != &plate)
        c = c->getParentComponent();
    if (c == nullptr)
        return {};

    // The dybbuk lets the mouse through to the plate, so it is found by
    // its box rather than by the mouse landing on it.
    if (c == &plate)
        c = lamp.getBounds().contains (platePoint) ? &lamp : nullptr;
    if (c == nullptr)
        return {};

    const int mode = juce::roundToInt (raw (params::id::mode));

    if (c == thresholdKnob.get()) return "Gate level. A note over it is captured as a step; under it, nothing is.";
    if (c == stepKnob.get())      return "How long each step lasts. Synced, a note division on the host's grid.";
    if (c == syncToggle.get())    return "Steps follow the host's grid instead of Time's milliseconds.";
    if (c == barToggle.get())     return "Synced, the pattern restarts from its first step on every bar line.";
    if (c == stepsKnob.get())     return "How many steps play, 1 to 16. Down loops the first few; up brings the rest back.";
    if (c == blendKnob.get())     return "Dry against the pattern, equal power.";
    if (c == lengthKnob.get())
    {
        if (mode == Lamp::trance) return "How many times slower the material plays, from its start; the step holds what fits.";
        if (mode == Lamp::wraith)  return "How many steps a frozen moment keeps sounding, 1 to 8.";
        return "How much of each step sounds before it is cut.";
    }
    if (c == fadeKnob.get())      return "Level a step loses every play. A step that fades out leaves the pattern.";
    if (c == directionKnob.get()) return "The order the steps play: forward, reverse, pendulum, drunk, random.";
    if (c == pitchKnob.get())
        return mode == Lamp::legion ? "The interval between the three voices, in semitones."
                                    : "Transposes every step's material, in semitones.";
    if (c == glueKnob.get())      return "Saturation on the pattern, level matched: colour and squash, not volume.";
    if (c == fillsKnob.get())
        return mode == Lamp::tremor ? "How many times the held step repeats within each step, 1 to 4."
                                   : "Frozen, a note over the threshold scrambles the order for one cycle, this deeply.";
    if (c == freezeToggle.get())  return "Off, every note you play becomes a step. On, the pattern is held and you play over it.";
    if (c == chaosKnob.get())     return "Per step: skips, ratchets, reverses, jumps, intervals, offsets, chokes, accents. More is more at once.";
    if (c == spreadKnob.get())    return "Alternate steps left and right.";
    if (c == modeToggle.get())
    {
        static const char* const cells[] = {
            "The pattern as you played it.",
            "Every step leaves a frozen moment that keeps sounding under the next ones.",
            "Every step played slower from its start, at its own pitch; Decay how much.",
            "Every step sung three times over, Pitch the interval.",
            "Playing over the threshold holds and ratchets the current step.",
        };
        const int cell = modeToggle->cellAt (modeToggle->getLocalPoint (&plate, platePoint).toFloat());
        return cell >= 0 && cell < Lamp::kModeCount ? cells[cell] : "";
    }
    if (c == &diceAction)         return "Roll a patch: a character, then every pattern knob inside it.";
    if (c == &clearAction)        return "Start over: the pattern is emptied and the dybbuk listens.";
    if (c == &exportAction)       return "Drag one cycle of the pattern into your DAW as a WAV, or click to save it.";
    if (c == inFader.get())       return "Input level, also what the gate hears.";
    if (c == outFader.get())      return "Output level.";
    if (c == bypassToggle.get())  return "Bypass. The dybbuk goes deaf and keeps its pattern.";
    if (c == &themeMark)          return "Light or dark sheet.";
    if (c == &presetHeader)       return "Presets: click the name for the list, the arrows to step.";
    if (c == &lamp)               return "The pattern: one limb per step, the sounding one lit.";
    return {};
}

juce::String DybbukEditor::hintUnderMouse() const
{
    if (pinnedHint.isNotEmpty())
    {
        // The test hook: the named control, probed at its centre, or a
        // mode cell probed in its cell.
        static const juce::StringArray modeNames { "GOLEM", "WRAITH", "TRANCE", "LEGION", "TREMOR" };
        if (const int cell = modeNames.indexOf (pinnedHint); cell >= 0)
        {
            const auto b = modeToggle->getBounds();
            return hintFor (modeToggle.get(), { b.getX() + (2 * cell + 1) * b.getWidth() / (2 * modeNames.size()), b.getCentreY() });
        }
        for (auto* child : plate.getChildren())
            if (child->getName() == pinnedHint)
                return hintFor (child, child->getBounds().getCentre());
        return {};
    }

    const auto& mouse = juce::Desktop::getInstance().getMainMouseSource();
    auto* under = mouse.getComponentUnderMouse();
    if (under == nullptr || (under != &plate && ! plate.isParentOf (under)))
        return {};
    return hintFor (under, plate.getLocalPoint (nullptr, mouse.getScreenPosition()).roundToInt());
}

void DybbukEditor::updateHint()
{
    wantedHint = hintUnderMouse();
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

    // Three readouts change their wording with the mode, not only with
    // their own value, so a change of player repaints them.
    const int mode = juce::roundToInt (raw (params::id::mode));
    if (mode != lastMode)
    {
        lastMode = mode;
        // The knob a mode reinterprets wears a red caption while it does.
        lengthKnob->setAccent (mode == (int) Lamp::trance || mode == (int) Lamp::wraith);
        pitchKnob->setAccent (mode == (int) Lamp::legion);
        fillsKnob->setAccent (mode == (int) Lamp::tremor);
        for (auto* knob : { lengthKnob.get(), pitchKnob.get(), fillsKnob.get() })
            knob->repaint();
    }

    // The hint line: the sentence fades out before it is swapped, so a
    // hand crossing from one knob to its neighbour sees one line give way
    // to the next rather than the two flickering. The rolled name keeps
    // the strip while it shows.
    updateHint();
    {
        const bool swap = shownHint != wantedHint;
        const float target = (swap || rolledTicks > 0 || shownHint.isEmpty()) ? 0.0f : 1.0f;
        const float before = hintAlpha;
        hintAlpha = target > hintAlpha ? juce::jmin (target, hintAlpha + kHintFadePerTick)
                                       : juce::jmax (target, hintAlpha - kHintFadePerTick);
        if (swap && hintAlpha <= 0.0f)
            shownHint = wantedHint;
        if (std::abs (hintAlpha - before) > 0.0f)
            plate.repaint (kHintArea);
    }

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
