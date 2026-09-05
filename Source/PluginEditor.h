#pragma once

#include "PluginProcessor.h"
#include "ui/Theme.h"

// Interim editor: the real one (canvas coordinates, brass knobs, the ember,
// the readout strip) is specified in docs/design/04-ui.md and lands in Phase
// 5. This version exists so the standalone build is playable now, which is
// what the Phase 1 and Phase 3 listening gates need.
//
// It keeps the pattern the real one uses: a fixed canvas laid out once in the
// constructor and scaled to the window with the aspect locked.
class DybbukEditor : public juce::AudioProcessorEditor,
                     private juce::Timer
{
public:
    explicit DybbukEditor (DybbukProcessor& p);
    ~DybbukEditor() override;

    void resized() override;

private:
    static constexpr int canvasW = 720;
    static constexpr int canvasH = 576;

    struct Canvas : juce::Component
    {
        void paint (juce::Graphics& g) override;
        std::function<void (juce::Graphics&)> onPaint;
    };

    struct Knob
    {
        juce::Slider slider;
        juce::Label label;
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;
    };

    void timerCallback() override;
    void addKnob (Knob& knob, const char* paramID, const char* text, const char* tooltip,
                  int x, int y, int size);
    void applyTheme();

    DybbukProcessor& proc;
    Canvas canvas;

    Knob timeKnob, decayKnob, filterKnob, blendKnob;
    Knob timeModKnob, strengthKnob, resonanceKnob, absorbKnob;
    Knob agitateKnob, agitSpeedKnob, outKnob;

    juce::ToggleButton syncButton { "Sync" }, bypassButton { "Bypass" };
    juce::ComboBox agitModeBox;
    juce::TextButton clearButton { "Clear" }, themeButton { "Theme" };

    using ButtonAttachment = juce::AudioProcessorValueTreeState::ButtonAttachment;
    using ComboAttachment = juce::AudioProcessorValueTreeState::ComboBoxAttachment;
    std::unique_ptr<ButtonAttachment> syncAttach, bypassAttach;
    std::unique_ptr<ComboAttachment> agitModeAttach;

    float meterLevel = 0.0f;
    float emberLevel = 0.0f;
    juce::String delayText;

    juce::TooltipWindow tooltipWindow { this, 650 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (DybbukEditor)
};
