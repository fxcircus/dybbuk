#pragma once

#include "PluginProcessor.h"
#include "ui/EngravedKnob.h"
#include "ui/Lamp.h"
#include "ui/ModeToggle.h"
#include "ui/PlateControls.h"
#include "ui/PresetHeader.h"
#include "ui/Theme.h"
#include "ui/VerticalFader.h"

// The plate, from the v3 canvas: a fixed 900 x 620 sheet scaled to the window
// with the aspect locked. Everything is laid out once in the constructor in
// canvas coordinates; resized() only applies the scale.
//
// Values live under their own knobs rather than in a shared strip, so nothing
// has to be hovered to be read. The red dot in the middle of the plate is the
// dybbuk: the pattern drawn as a ring of pips around the ember, with two
// knobs on each side. Everything done TO the pattern (roll, clear, export) lives in
// the header's actions zone, in Shalal's arrangement, so the middle of the
// plate is the pattern and nothing else.
//
// A DragAndDropContainer so the EXPORT action can hand the rendered pattern
// to the OS as a file drag, straight onto a DAW track.
class DybbukEditor : public juce::AudioProcessorEditor,
                     public juce::DragAndDropContainer,
                     private juce::Timer
{
public:
    explicit DybbukEditor (DybbukProcessor& p);
    ~DybbukEditor() override;

    void resized() override;

    // The actions the theme mark and the dice perform. Public so UISnapshot
    // can exercise the cross-fade and the rolled caption without a mouse.
    void toggleTheme();
    void rollDice();

    // The hint line without a mouse: pins the hint for the control whose
    // caption this is ("DECAY", "WRAITH", "EXPORT", "DYBBUK"...), so a
    // snapshot can review the line as it is drawn. An empty name unpins.
    void showHintForTests (const juce::String& controlName);

    static constexpr int canvasW = 900;
    static constexpr int canvasH = 620;

private:
    static constexpr int kUiHz = 30;

    struct Plate : juce::Component
    {
        void paint (juce::Graphics& g) override;
        std::function<void (juce::Graphics&)> onPaint;
    };

    // A still of the outgoing sheet, fading out over the recoloured one. The
    // whole plate is captured rather than just the background, because every
    // line, letter and needle changes colour too, and fading only the paper
    // under fully-swapped ink reads as a flicker rather than a dissolve.
    struct ThemeFade : juce::Component
    {
        ThemeFade() { setInterceptsMouseClicks (false, false); }

        void paint (juce::Graphics& g) override
        {
            if (progress <= 0.0f || ! image.isValid())
                return;

            // Eased, so the old sheet holds for a moment and then lets go
            // rather than dimming at a constant rate.
            const float eased = progress * progress * (3.0f - 2.0f * progress);
            g.setOpacity (eased);
            g.drawImage (image, getLocalBounds().toFloat());
        }

        juce::Image image;
        float progress = 0.0f; // 1 at the moment of the switch, 0 when finished
    };

    void timerCallback() override;
    void applyTheme();

    // The hint line: what the control under the mouse does, in one sentence,
    // printed on the plate under the knob row. Looked up by component (and,
    // for the mode bar, by cell) and by the current mode, because a few
    // knobs mean different things to different players.
    juce::String hintFor (juce::Component* component, juce::Point<int> platePoint) const;
    juce::String hintUnderMouse() const;
    void updateHint();
    juce::RangedAudioParameter& param (const char* id) const;
    EngravedKnob& addKnob (std::unique_ptr<EngravedKnob>& slot, const char* id, const char* label,
                           EngravedKnob::Spec spec, juce::Point<int> faceCentre,
                           const char* minLegend, const char* maxLegend);
    float raw (const char* id) const { return proc.apvts.getRawParameterValue (id)->load(); }

    // Export: the drag hands the OS a file rendered into the Dybbuk folder;
    // the click asks where to put one.
    void dragPatternOut();
    void savePatternAs();

    DybbukProcessor& proc;
    Plate plate;
    juce::Image grain;

    std::unique_ptr<EngravedKnob> stepKnob, stepsKnob, thresholdKnob, blendKnob;
    // The dybbuk's row, how the pattern plays: the choke and the fade on its
    // left, the direction and the pitch on its right.
    std::unique_ptr<EngravedKnob> lengthKnob, fadeKnob, directionKnob, pitchKnob;
    // The bottom row, the hand and the output: FREEZE in the middle with
    // Fills and Chaos either side, Glue by the IN fader, Spread by OUT.
    std::unique_ptr<EngravedKnob> glueKnob, fillsKnob, chaosKnob, spreadKnob;
    std::unique_ptr<VerticalFader> inFader, outFader;
    std::unique_ptr<DiamondToggle> bypassToggle, syncToggle, barToggle;
    std::unique_ptr<WordToggle> freezeToggle;
    // Which player has the pattern: a segmented bar along the foot of the
    // plate, the one control on the plate that changes what the creature IS
    // rather than how much of something it does.
    std::unique_ptr<ModeToggle> modeToggle;
    ThemeFade themeFade;
    Lamp lamp;
    PresetHeader presetHeader { proc.presetManager };
    HeaderAction diceAction { HeaderAction::Glyph::die, "RANDOM" };
    HeaderAction clearAction { HeaderAction::Glyph::trash, "CLEAR" };
    HeaderAction exportAction { HeaderAction::Glyph::wavOut, "EXPORT" };
    ThemeMark themeMark;
    const char* rolledName = nullptr;
    int rolledTicks = 0;

    // The hint line. `wantedHint` is what the mouse is over this frame,
    // `shownHint` the sentence on the plate, which fades out before it is
    // swapped so neighbours never flicker into each other. `pinnedHint`
    // is the test hook's name for a control, overriding the mouse.
    juce::String wantedHint, shownHint, pinnedHint;
    float hintAlpha = 0.0f;
    int lastMode = -1;

    int lastClearsServed = 0;
    bool lastSynced = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (DybbukEditor)
};
