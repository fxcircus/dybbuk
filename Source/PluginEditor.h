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
// has to be hovered to be read.
class DybbukEditor : public juce::AudioProcessorEditor,
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
    static constexpr float kRunawayEnergy = 0.5f;

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

    DybbukProcessor& proc;
    Plate plate;
    juce::Image grain;

    std::unique_ptr<EngravedKnob> timeKnob, decayKnob, filterKnob, blendKnob;
    std::unique_ptr<EngravedKnob> timeModKnob, strengthKnob, resonanceKnob, absorbKnob;
    std::unique_ptr<EngravedKnob> agitateKnob, speedKnob;
    // The bottom strip's two free windows, added with the wildness pass:
    // Chaos and Crust beside the IN trim, Tones and Pitch beside OUT.
    std::unique_ptr<EngravedKnob> chaosKnob, crustKnob, tonesKnob, pitchKnob;
    std::unique_ptr<VerticalFader> inFader, outFader;
    std::unique_ptr<DiamondToggle> bypassToggle, syncToggle;
    std::unique_ptr<RailSwitch> modeSwitch;
    ThemeFade themeFade;
    Lamp lamp;
    ClearStamp clearStamp;
    PresetHeader presetHeader { proc.presetManager };
    ThemeMark themeMark;

    int lastClearsServed = 0;
    bool lastSynced = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (DybbukEditor)
};
