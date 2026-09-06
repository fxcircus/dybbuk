#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "../state/PresetManager.h"
#include "Theme.h"

// The patch name in the middle of the header, set in script, with a mark to
// step through them and the full list on a click. An edited patch shows a
// mark after the name, so an unsaved tweak is visible without opening
// anything.
class PresetHeader : public juce::Component
{
public:
    explicit PresetHeader (PresetManager& manager);

    std::function<void()> onPresetChanged;
    void tick();

    void paint (juce::Graphics& g) override;
    void mouseMove (const juce::MouseEvent& e) override;
    void mouseExit (const juce::MouseEvent& e) override;
    void mouseDown (const juce::MouseEvent& e) override;

private:
    juce::Rectangle<float> prevArrow() const;
    juce::Rectangle<float> nextArrow() const;
    void showList();

    PresetManager& presets;
    juce::String shownName;
    bool shownDirty = false;
    int hoverZone = -1; // 0 previous, 1 name, 2 next

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PresetHeader)
};
