#pragma once

#include "PluginProcessor.h"
#include "ui/EngravedKnob.h"
#include "ui/Lamp.h"
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
// dybbuk: the pattern drawn as a ring of pips around the ember, with the
// export stamp over it, the clear stamp under it, and a trim on each side.
//
// A DragAndDropContainer so the export stamp can hand the rendered pattern to
// the OS as a file drag, straight onto a DAW track.
class DybbukEditor : public juce::AudioProcessorEditor,
                     public juce::DragAndDropContainer,
                     private juce::Timer
{
public:
    explicit DybbukEditor (DybbukProcessor& p);
    ~DybbukEditor() override;

    void resized() override;

    // The action the theme mark performs. Public so UISnapshot can exercise
    // the cross-fade without a mouse.
    void toggleTheme();

private:
    static constexpr int canvasW = 900;
    static constexpr int canvasH = 620;
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
    std::unique_ptr<EngravedKnob> fillsKnob, chaosKnob, directionKnob;
    // Aim-and-forget controls, one on each side of the dybbuk: the choke and
    // the fade are set, not ridden.
    std::unique_ptr<EngravedTrim> lengthTrim, fadeTrim;
    std::unique_ptr<VerticalFader> inFader, outFader;
    std::unique_ptr<DiamondToggle> bypassToggle, syncToggle;
    std::unique_ptr<WordToggle> fullToggle, recordToggle;
    ThemeFade themeFade;
    Lamp lamp;
    ClearStamp clearStamp;
    ExportStamp exportStamp;
    PresetHeader presetHeader { proc.presetManager };
    ThemeMark themeMark;
    DiceButton dice;
    const char* rolledName = nullptr;
    int rolledTicks = 0;

    int lastClearsServed = 0;
    bool lastSynced = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (DybbukEditor)
};
