#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include "Readout.h"
#include "Theme.h"

// A small two-state pill, used for Sync (a bool sitting quietly beside Time)
// and for the agitation's Loop / Gate choice. Both captions are always
// visible, so the state is readable without knowing which way is "on".
class PillToggle : public juce::Component
{
public:
    PillToggle (juce::RangedAudioParameter& parameterToUse, juce::StringArray captions,
                juce::String name, juce::String hint);

    std::function<void (PillToggle&)> onHover;
    std::function<void()> onChange;
    Readout readout() const;
    int selectedIndex() const noexcept;

    void paint (juce::Graphics& g) override;
    void mouseEnter (const juce::MouseEvent& e) override;
    void mouseExit (const juce::MouseEvent& e) override;
    void mouseDown (const juce::MouseEvent& e) override;

private:
    juce::RangedAudioParameter& param;
    juce::ParameterAttachment attachment;
    juce::StringArray captions;
    juce::String nameText, hintText;
    float normValue = 0.0f;
    bool hovering = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PillToggle)
};
