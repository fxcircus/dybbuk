#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "../state/PresetManager.h"
#include "Readout.h"
#include "Theme.h"

// Preset selector in the header: previous, name, next, with the full list on a
// click. A dirty preset shows a dot after its name, so an unsaved tweak is
// visible without opening anything.
class PresetBar : public juce::Component
{
public:
    explicit PresetBar (PresetManager& manager);

    std::function<void (PresetBar&)> onHover;
    std::function<void()> onPresetChanged;
    Readout readout() const;
    void tick();

    void paint (juce::Graphics& g) override;
    void mouseEnter (const juce::MouseEvent& e) override;
    void mouseExit (const juce::MouseEvent& e) override;
    void mouseMove (const juce::MouseEvent& e) override;
    void mouseDown (const juce::MouseEvent& e) override;

private:
    juce::Rectangle<float> prevArrow() const;
    juce::Rectangle<float> nextArrow() const;
    void showList();

    PresetManager& presets;
    juce::String shownName;
    bool shownDirty = false;
    int hoverZone = -1; // 0 prev, 1 name, 2 next

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PresetBar)
};
