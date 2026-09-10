#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include "Theme.h"

// The mode selector as a SEGMENTED TOGGLE, ported from Teder: one outlined
// bar divided into labelled cells, the selected cell carrying the plate's
// hatched-thumb fill. The modern web control, spoken in the engraving
// vocabulary: visually distinct from the knobs (continuous) and the trims
// (aim and forget), and the highlight glides mechanically between cells
// rather than jumping, so a change of player reads as a lever thrown.
//
// Bound to a choice parameter; the cells are the choices in order. No timer
// of its own: the editor ticks it.
class ModeToggle : public juce::Component
{
public:
    ModeToggle (juce::RangedAudioParameter& parameterToUse, juce::StringArray optionLabels);

    void tick(); // 30 Hz: the thumb glides to its cell

    int index() const noexcept { return selected; }

    // Which cell a point (in this component's coordinates) is over, or -1
    // outside the bar. The editor's hint line asks, so a hover over the bar
    // can describe the player under the mouse rather than the bar as a whole.
    int cellAt (juce::Point<float> localPoint) const noexcept;

    void paint (juce::Graphics& g) override;
    void mouseEnter (const juce::MouseEvent&) override { hovering = true; repaint(); }
    void mouseExit (const juce::MouseEvent&) override { hovering = false; repaint(); }
    void mouseDown (const juce::MouseEvent& e) override;
    void mouseDrag (const juce::MouseEvent& e) override;

private:
    void setIndexFromX (float x);

    juce::RangedAudioParameter& param;
    juce::ParameterAttachment attachment;
    juce::StringArray labels;
    int selected = 0;
    float highlight = 0.0f; // continuous cell position, 0 .. n-1
    bool hovering = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ModeToggle)
};
