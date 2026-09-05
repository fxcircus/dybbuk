#pragma once

#include "PluginProcessor.h"
#include "ui/Theme.h"

// A fixed-size canvas scaled to the window with the aspect ratio locked. Lay
// everything out in canvas coordinates once, in the constructor; resized()
// only applies the scale transform. This keeps a hand-drawn UI honest at every
// window size and is far less work than per-component responsive layout.
class DybbukEditor : public juce::AudioProcessorEditor,
                             private juce::Timer
{
public:
    explicit DybbukEditor (DybbukProcessor& p);
    ~DybbukEditor() override;

    void resized() override;

private:
    static constexpr int canvasW = 620;
    static constexpr int canvasH = 380;

    struct Canvas : juce::Component
    {
        void paint (juce::Graphics& g) override;
        std::function<void (juce::Graphics&)> onPaint;
    };

    void timerCallback() override;
    juce::RangedAudioParameter& param (const char* id) const;

    DybbukProcessor& proc;
    Canvas canvas;

    juce::Slider driveSlider, toneSlider, mixSlider;
    juce::Label driveLabel, toneLabel, mixLabel;
    juce::ToggleButton bypassButton { "Bypass" };

    // Attachments own the parameter<->control link. Declare them AFTER the
    // controls so they are destroyed first.
    using SliderAttachment = juce::AudioProcessorValueTreeState::SliderAttachment;
    using ButtonAttachment = juce::AudioProcessorValueTreeState::ButtonAttachment;
    std::unique_ptr<SliderAttachment> driveAttach, toneAttach, mixAttach;
    std::unique_ptr<ButtonAttachment> bypassAttach;

    float meterLevel = 0.0f;

    juce::TooltipWindow tooltipWindow { this, 650 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (DybbukEditor)
};
