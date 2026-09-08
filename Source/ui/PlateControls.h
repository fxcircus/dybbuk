#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include "Theme.h"

// The small marks on the plate: the bypass and sync diamonds, the loop/gate
// rail, the clear stamp and the theme mark. Each is a handful of lines, so
// they share one file rather than five.

// A filled diamond means engaged. Used for Bypass in the header (where the
// label reads IN or BYPASS) and for Sync beside the Time knob.
class DiamondToggle : public juce::Component
{
public:
    enum class Style { framed, bare }; // framed draws the header's box around it

    DiamondToggle (juce::RangedAudioParameter& parameterToUse, Style style,
                   juce::String onLabel, juce::String offLabel);

    bool isOn() const noexcept;
    void paint (juce::Graphics& g) override;
    void mouseEnter (const juce::MouseEvent&) override { hovering = true; repaint(); }
    void mouseExit (const juce::MouseEvent&) override { hovering = false; repaint(); }
    void mouseDown (const juce::MouseEvent& e) override;

private:
    juce::RangedAudioParameter& param;
    juce::ParameterAttachment attachment;
    Style style;
    juce::String onText, offText;
    float normValue = 0.0f;
    bool hovering = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (DiamondToggle)
};

// The agitation's Loop / Gate slide: a rail with a travelling diamond and the
// two names beneath it, so the state is readable without knowing which way is
// on.
class RailSwitch : public juce::Component
{
public:
    RailSwitch (juce::RangedAudioParameter& parameterToUse, juce::String leftLabel,
                juce::String rightLabel);

    void paint (juce::Graphics& g) override;
    void mouseEnter (const juce::MouseEvent&) override { hovering = true; repaint(); }
    void mouseExit (const juce::MouseEvent&) override { hovering = false; repaint(); }
    void mouseDown (const juce::MouseEvent& e) override;

private:
    bool isRight() const noexcept { return normValue >= 0.5f; }

    juce::RangedAudioParameter& param;
    juce::ParameterAttachment attachment;
    juce::String leftText, rightText;
    float normValue = 0.0f;
    bool hovering = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (RailSwitch)
};

// A ringed bin stamp. It lights on the engine's acknowledgement rather than on
// the click, so what flashes is the loop actually being emptied.
class ClearStamp : public juce::Component
{
public:
    ClearStamp();

    std::function<void()> onClick;
    void flash() noexcept { flashTicks = 6; }
    void tick();

    void paint (juce::Graphics& g) override;
    void mouseEnter (const juce::MouseEvent&) override { hovering = true; repaint(); }
    void mouseExit (const juce::MouseEvent&) override { hovering = false; repaint(); }
    void mouseDown (const juce::MouseEvent&) override { if (onClick) onClick(); }

private:
    bool hovering = false;
    int flashTicks = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ClearStamp)
};

// A horizontal engraved trim: a caption, a hairline rail with a travelling
// diamond, and the value right-aligned. Used for the two controls you aim and
// then play against rather than ride -- Tones Fold and Colour -- which is why
// they are not knobs: the plate's two knob rows are full, and a trim says
// "set this and forget it" where a knob says "turn me".
//
// Same rail idiom as RailSwitch, so it reads as part of the same plate.
class EngravedTrim : public juce::Component
{
public:
    EngravedTrim (juce::RangedAudioParameter& parameterToUse, juce::String caption);

    void setValueTextProvider (std::function<juce::String()> provider);

    void paint (juce::Graphics& g) override;
    void mouseEnter (const juce::MouseEvent&) override { hovering = true; repaint(); }
    void mouseExit (const juce::MouseEvent&) override { hovering = false; repaint(); }
    void mouseDown (const juce::MouseEvent& e) override;
    void mouseDrag (const juce::MouseEvent& e) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;

private:
    float normFromX (float x) const noexcept;

    juce::RangedAudioParameter& param;
    juce::ParameterAttachment attachment;
    juce::String captionText;
    std::function<juce::String()> valueProvider;
    float normValue = 0.0f;
    bool hovering = false, dragging = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (EngravedTrim)
};

// The dice, beside the theme mark. A pipped face that rolls to a new number on
// every click, so the control shows that it did something even when the sound
// it produced is one you do not like.
class DiceButton : public juce::Component
{
public:
    DiceButton();

    std::function<void()> onClick;

    void paint (juce::Graphics& g) override;
    void mouseEnter (const juce::MouseEvent&) override { hovering = true; repaint(); }
    void mouseExit (const juce::MouseEvent&) override { hovering = false; repaint(); }
    void mouseDown (const juce::MouseEvent&) override;

private:
    int face = 5;
    bool hovering = false;
    juce::Random rng;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (DiceButton)
};

// Sun while dark, moon while light: it shows the sheet you would switch to.
class ThemeMark : public juce::Component
{
public:
    ThemeMark() { setMouseCursor (juce::MouseCursor::PointingHandCursor); }

    std::function<void()> onClick;
    void paint (juce::Graphics& g) override;
    void mouseEnter (const juce::MouseEvent&) override { hovering = true; repaint(); }
    void mouseExit (const juce::MouseEvent&) override { hovering = false; repaint(); }
    void mouseUp (const juce::MouseEvent& e) override
    {
        if (onClick && getLocalBounds().contains (e.getPosition()))
            onClick();
    }

private:
    bool hovering = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ThemeMark)
};
