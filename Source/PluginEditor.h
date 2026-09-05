#pragma once

#include "PluginProcessor.h"
#include "ui/BrassKnob.h"
#include "ui/ClearButton.h"
#include "ui/Ember.h"
#include "ui/OutSlider.h"
#include "ui/PillToggle.h"
#include "ui/PresetBar.h"
#include "ui/ReadoutStrip.h"
#include "ui/Theme.h"

// A fixed 720 x 576 canvas scaled to the window with the aspect locked.
// Everything is laid out once in the constructor in canvas coordinates;
// resized() only applies the scale.
//
// There are no tooltips: the readout strip along the bottom is where controls
// explain themselves, and whatever you touched last keeps explaining itself
// until you touch something else.
class DybbukEditor : public juce::AudioProcessorEditor,
                     private juce::Timer
{
public:
    explicit DybbukEditor (DybbukProcessor& p);
    ~DybbukEditor() override;

    void resized() override;

    // Used by UISnapshot to capture a specific state without a mouse.
    void showInReadout (const char* paramId);
    juce::String readoutLine() const;

private:
    static constexpr int canvasW = 720;
    static constexpr int canvasH = 576;
    static constexpr int kUiHz = 30;
    static constexpr float kRunawayEnergy = 0.5f;

    struct Canvas : juce::Component
    {
        void paint (juce::Graphics& g) override;
        std::function<void (juce::Graphics&)> onPaint;
    };

    // Sun while brass, moon while parchment: it shows the mode you would
    // switch to, following the two sibling plugins.
    class ThemeButton : public juce::Component
    {
    public:
        ThemeButton() { setMouseCursor (juce::MouseCursor::PointingHandCursor); }
        std::function<void()> onClick;
        void paint (juce::Graphics& g) override;
        void mouseUp (const juce::MouseEvent& e) override
        {
            if (onClick && getLocalBounds().contains (e.getPosition()))
                onClick();
        }
        void mouseEnter (const juce::MouseEvent&) override { hovering = true; repaint(); }
        void mouseExit (const juce::MouseEvent&) override { hovering = false; repaint(); }

    private:
        bool hovering = false;
    };

    void timerCallback() override;
    void applyTheme();
    void wireReadout (BrassKnob& knob);
    juce::RangedAudioParameter& param (const char* id) const;
    BrassKnob& addKnob (std::unique_ptr<BrassKnob>& slot, const char* id, const char* label,
                        const char* hint, BrassKnob::Size size, juce::Point<int> centre);

    DybbukProcessor& proc;
    Canvas canvas;

    std::unique_ptr<BrassKnob> timeKnob, decayKnob, filterKnob, blendKnob;
    std::unique_ptr<BrassKnob> timeModKnob, strengthKnob, resonanceKnob, absorbKnob;
    std::unique_ptr<BrassKnob> agitateKnob, speedKnob;
    std::unique_ptr<PillToggle> syncToggle, modeToggle;
    std::unique_ptr<OutSlider> outSlider;
    Ember ember;
    ClearButton clearButton;
    ReadoutStrip readout;
    PresetBar presetBar { proc.presetManager };
    ThemeButton themeButton;

    int lastClearsServed = 0;
    bool lastSynced = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (DybbukEditor)
};
