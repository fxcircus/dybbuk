#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include "Readout.h"
#include "Theme.h"

// The output fader with its meter behind it, full width along the bottom.
// One object rather than a fader plus a meter, so the level you set and the
// level you are getting occupy the same piece of the panel.
class OutSlider : public juce::Component
{
public:
    OutSlider (juce::RangedAudioParameter& parameterToUse, juce::String hint);

    std::function<void (OutSlider&)> onHover;
    Readout readout() const;

    void setPeak (float linearPeak) noexcept; // engine's block peak
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
    float railX() const noexcept { return 44.0f; }
    float railWidth() const noexcept { return (float) getWidth() - 44.0f - 60.0f; }

    juce::RangedAudioParameter& param;
    juce::ParameterAttachment attachment;
    juce::String hintText;

    float normValue = 0.0f;
    float meterDb = -60.0f, peakHoldDb = -60.0f, clipHold = 0.0f;
    float lastPaintedDb = -120.0f;
    bool hovering = false, dragging = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (OutSlider)
};
