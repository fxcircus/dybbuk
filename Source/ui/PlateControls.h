#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include "Theme.h"

// The small marks on the plate: the bypass and sync diamonds, the Record and
// Full rails, the header's three actions and the theme mark. Each is a
// handful of lines, so they share one file rather than seven.

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

// A two-position slide: a rail with a travelling diamond and the two names
// beneath it, so the state is readable without knowing which way is on. Not
// on the plate at the moment (Record and Full became WordToggles, below), but
// kept: it is the right control for a choice whose two ends are peers.
class RailSwitch : public juce::Component
{
public:
    RailSwitch (juce::RangedAudioParameter& parameterToUse, juce::String leftLabel,
                juce::String rightLabel, juce::String caption = {});

    // Fill the carriage red at one end of the travel (-1 none, 0 left, 1
    // right): the plate's sign for "this is recording".
    void setRedAt (int side) noexcept { redSide = side; repaint(); }
    bool isRight() const noexcept { return normValue >= 0.5f; }

    void paint (juce::Graphics& g) override;
    void mouseEnter (const juce::MouseEvent&) override { hovering = true; repaint(); }
    void mouseExit (const juce::MouseEvent&) override { hovering = false; repaint(); }
    void mouseDown (const juce::MouseEvent& e) override;

private:
    juce::RangedAudioParameter& param;
    juce::ParameterAttachment attachment;
    juce::String leftText, rightText, captionText;
    float normValue = 0.0f;
    int redSide = -1;
    bool hovering = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (RailSwitch)
};

// A two-state button that says which state it is in: a caption over an
// engraved rounded box with the CURRENT state's word inside. Off is paper
// with an ink outline and an ink word; on is filled red with a paper word,
// the plate's sign for "this is recording" (Record: FROZEN / ARMED) and for
// a ceiling that holds (Full: REPLACE / HOLD). Replaces the rails for those
// two, which read as a slider and made people drag them.
class WordToggle : public juce::Component
{
public:
    WordToggle (juce::RangedAudioParameter& parameterToUse, juce::String caption,
                juce::String offWord, juce::String onWord);

    // Layout, so the editor can place the box by its centre: the caption
    // takes kCaptionH, then kGap, then the box takes the rest of the height.
    static constexpr int kBoxW = 108, kBoxH = 30, kCaptionH = 14, kGap = 4;
    static constexpr int kHeight = kCaptionH + kGap + kBoxH;
    static juce::Rectangle<int> boundsFor (juce::Point<int> boxCentre);

    bool isOn() const noexcept { return normValue >= 0.5f; }

    void paint (juce::Graphics& g) override;
    void mouseEnter (const juce::MouseEvent&) override { hovering = true; repaint(); }
    void mouseExit (const juce::MouseEvent&) override { hovering = false; repaint(); }
    void mouseDown (const juce::MouseEvent& e) override;

private:
    juce::RangedAudioParameter& param;
    juce::ParameterAttachment attachment;
    juce::String captionText, offText, onText;
    float normValue = 0.0f;
    bool hovering = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (WordToggle)
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

// The header's actions, in the house style shared with Shalal: a hairline
// glyph over a tracked small-caps label. The glyph set, its proportions and
// its stroke are that sheet's GlyphButton, so the two headers read as one
// family; only the colours are the plate's own. Hover brightens, a click
// thumps the glyph, disabled fades the whole button to one alpha. A drag
// source (EXPORT) fires onDragStart once the mouse has travelled a few
// pixels, and a gesture that dragged never clicks.
//   die     RANDOM  -- an isometric hairline die: roll a patch
//   trash   CLEAR   -- the wastebasket, for every "throw away"
//   wavOut  EXPORT  -- a filing tray with an arrow dropping into it
class HeaderAction : public juce::Component
{
public:
    enum class Glyph { die, trash, wavOut };

    HeaderAction (Glyph glyph, juce::String label);

    std::function<void()> onClick;
    std::function<void()> onDragStart;

    void pulse();   // the thump a click gives
    void flash();   // the thump plus a red beat: the engine's acknowledgement, not the click
    void tick();

    void paint (juce::Graphics& g) override;
    void enablementChanged() override { repaint(); }
    void mouseDown (const juce::MouseEvent&) override { dragStarted = false; }
    void mouseDrag (const juce::MouseEvent& e) override;
    void mouseUp (const juce::MouseEvent& e) override;
    void mouseEnter (const juce::MouseEvent&) override { hovering = true; repaint(); }
    void mouseExit (const juce::MouseEvent&) override { hovering = false; repaint(); }

    static void drawGlyph (juce::Graphics& g, Glyph glyph, juce::Rectangle<float> box,
                           juce::Colour colour, float strokeWidth);

private:
    Glyph glyph;
    juce::String label;
    bool hovering = false, dragStarted = false;
    int flashTicks = 0;
    float pulseAnim = 0.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (HeaderAction)
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
