#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

// Mutable colour globals plus a dark-mode flip. Components read `theme::ink`
// etc. at paint time rather than caching colours, so one setDarkMode() call
// re-skins the whole UI.
//
// Two traps this pattern has already caught fire on, both worth remembering:
//  * juce::Label / juce::TextEditor cache their colours when constructed. If a
//    component owns one, give it an applyThemeColours() method and call it
//    AFTER the theme is known — member construction happens too early.
//  * Verify dark mode by building the editor through the real construction
//    path (see Tests/UISnapshot.cpp), not by flipping a global mid-session.
namespace theme
{
    extern juce::Colour background;
    extern juce::Colour ink;
    extern juce::Colour faded;
    extern juce::Colour accent;

    void setDarkMode (bool shouldBeDark);
    bool isDarkMode();

    inline juce::Colour inkA (float alpha) { return ink.withAlpha (alpha); }

    // Width of a string in a font, without the deprecated Font::getStringWidth.
    float textWidth (const juce::Font& font, const juce::String& text);
} // namespace theme
