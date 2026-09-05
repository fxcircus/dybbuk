#include "Theme.h"

namespace theme
{
    juce::Colour background { 0xff15110c };
    juce::Colour panel      { 0xff1e1913 };
    juce::Colour ink        { 0xffe9dcc0 };
    juce::Colour faded      { 0xff8a7a5c };
    juce::Colour accent     { 0xffc9a227 };
    juce::Colour ember      { 0xffff7b3a };
    juce::Colour runaway    { 0xffd2452f };

    namespace { int current = kDefaultTheme; }

    void setTheme (int index)
    {
        current = juce::jlimit (0, kThemeCount - 1, index);

        if (current == 0) // dark brass: the default, and what the plan describes
        {
            background = juce::Colour (0xff15110c);
            panel      = juce::Colour (0xff1e1913);
            ink        = juce::Colour (0xffe9dcc0);
            faded      = juce::Colour (0xff8a7a5c);
            accent     = juce::Colour (0xffc9a227);
            ember      = juce::Colour (0xffff7b3a);
            runaway    = juce::Colour (0xffd2452f);
        }
        else // parchment: the light alternate
        {
            background = juce::Colour (0xffefe8db);
            panel      = juce::Colour (0xffe2d9c6);
            ink        = juce::Colour (0xff2b2419);
            faded      = juce::Colour (0xff8b7f66);
            accent     = juce::Colour (0xff96701a);
            ember      = juce::Colour (0xffcf5a1e);
            runaway    = juce::Colour (0xffb03020);
        }
    }

    int currentTheme() { return current; }

    const char* themeName (int index) { return index == 0 ? "brass" : "parchment"; }

    float textWidth (const juce::Font& font, const juce::String& text)
    {
        juce::GlyphArrangement glyphs;
        glyphs.addLineOfText (font, text, 0.0f, 0.0f);
        return glyphs.getBoundingBox (0, -1, true).getWidth();
    }
} // namespace theme
