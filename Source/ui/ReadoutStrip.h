#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "Readout.h"
#include "Theme.h"

// One fixed strip instead of floating tooltips: whatever you touched last
// keeps explaining itself, so reading a value never depends on hovering and
// waiting.
class ReadoutStrip : public juce::Component
{
public:
    ReadoutStrip() { setInterceptsMouseClicks (false, false); }

    void setProvider (std::function<Readout()> newProvider);
    void setRestReadout (Readout r) { rest = std::move (r); }
    void setBypassed (bool shouldBeBypassed) noexcept { bypassed = shouldBeBypassed; }
    void setDragging (bool isDragging) noexcept { dragging = isDragging; }
    void tick();
    Readout current() const { return shown; }

    void paint (juce::Graphics& g) override;

private:
    std::function<Readout()> provider;
    Readout shown, rest;
    bool bypassed = false, dragging = false, lastBypassed = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ReadoutStrip)
};
