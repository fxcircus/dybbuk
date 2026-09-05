#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

// One record used by every control's value row and by the readout strip, so
// each control formats its own words ("runaway", "capped", "clip") in exactly
// one place and the strip never has to know what kind of control it is
// showing. There are no tooltips anywhere in this UI: the strip is where
// things explain themselves.
struct Readout
{
    juce::String name;        // caps caption, e.g. "decay"
    juce::String value;       // musician's number with unit, e.g. "0.31 s", "-Inf"
    juce::String aside;       // state word, e.g. "runaway", "3.67 s max", "clip"
    juce::Colour asideColour;
    juce::String hint;        // one sentence, shown on hover when there is no aside

    bool operator== (const Readout& other) const
    {
        return name == other.name && value == other.value && aside == other.aside
               && asideColour == other.asideColour && hint == other.hint;
    }
    bool operator!= (const Readout& other) const { return ! operator== (other); }
};
