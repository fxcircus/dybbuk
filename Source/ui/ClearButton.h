#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "Readout.h"
#include "Theme.h"

// Understated on purpose: it lives beside the ember as a utility, not as a
// performance control. It flashes on the engine's acknowledgement rather than
// on the click, so what lights up is the loop actually being emptied.
class ClearButton : public juce::Component
{
public:
    ClearButton() { setMouseCursor (juce::MouseCursor::PointingHandCursor); }

    std::function<void()> onClick;
    std::function<void (ClearButton&)> onHover;

    void flash() noexcept { flashTicks = 8; }
    void tick();
    Readout readout() const;

    void paint (juce::Graphics& g) override;
    void mouseEnter (const juce::MouseEvent& e) override;
    void mouseExit (const juce::MouseEvent& e) override;
    void mouseDown (const juce::MouseEvent& e) override;

private:
    bool hovering = false;
    int flashTicks = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ClearButton)
};
