#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include "Theme.h"

// The IN and OUT trims on the edges of the plate. The rail is solid ink, the
// meter is a hatched red column rising from the bottom of it, and the handle
// is a hatched block: the level you set and the level you are getting share
// one piece of the panel rather than sitting side by side.
class VerticalFader : public juce::Component
{
public:
    VerticalFader (juce::RangedAudioParameter& parameterToUse, juce::String caption);

    void setLevel (float linearPeak) noexcept; // engine's block peak
    void tick();
    bool isDragging() const noexcept { return dragging; }

    void paint (juce::Graphics& g) override;
    void mouseEnter (const juce::MouseEvent& e) override;
    void mouseExit (const juce::MouseEvent& e) override;
    void mouseDown (const juce::MouseEvent& e) override;
    void mouseDrag (const juce::MouseEvent& e) override;
    void mouseUp (const juce::MouseEvent& e) override;
    void mouseDoubleClick (const juce::MouseEvent& e) override;

private:
    juce::Rectangle<float> trackArea() const;
    void applyDrag (const juce::MouseEvent& e);

    juce::RangedAudioParameter& param;
    juce::ParameterAttachment attachment;
    juce::String captionText;

    float normValue = 0.0f;
    float meterDb = -60.0f, peakDb = -60.0f, lastPaintedDb = -120.0f;
    bool hovering = false, dragging = false;
    float dragStartValue = 0.0f, dragStartY = 0.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (VerticalFader)
};
