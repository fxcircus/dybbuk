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
    std::unique_ptr<VerticalFader> inFader, outFader;
    std::unique_ptr<DiamondToggle> bypassToggle, syncToggle;
    std::unique_ptr<RailSwitch> modeSwitch;
    Lamp lamp;
    ClearStamp clearStamp;
    PresetHeader presetHeader { proc.presetManager };
    ThemeMark themeMark;

    int lastClearsServed = 0;
    bool lastSynced = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (DybbukEditor)
};
