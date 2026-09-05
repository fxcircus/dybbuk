#include "Theme.h"

namespace theme
{
    juce::Colour background { 0xfff2efe6 };
    juce::Colour ink        { 0xff1a1a1a };
    juce::Colour faded      { 0xff8a8578 };
    juce::Colour accent     { 0xffb03a2e };

    namespace { bool darkMode = false; }

    void setDarkMode (bool shouldBeDark)
    {
        darkMode = shouldBeDark;
        background = shouldBeDark ? juce::Colour (0xff141412) : juce::Colour (0xfff2efe6);
        ink        = shouldBeDark ? juce::Colour (0xffe8e4d8) : juce::Colour (0xff1a1a1a);
        faded      = shouldBeDark ? juce::Colour (0xff6f6a5e) : juce::Colour (0xff8a8578);
        accent     = shouldBeDark ? juce::Colour (0xffc75d4f) : juce::Colour (0xffb03a2e);
    }

    bool isDarkMode() { return darkMode; }

    float textWidth (const juce::Font& font, const juce::String& text)
    {
        juce::GlyphArrangement glyphs;
        glyphs.addLineOfText (font, text, 0.0f, 0.0f);
        return glyphs.getBoundingBox (0, -1, true).getWidth();
    }
} // namespace theme
