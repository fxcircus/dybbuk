#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

// Mutable colour globals plus a theme index. Components read `theme::ink` etc.
// at paint time rather than caching colours, so one setTheme() call re-skins
// the whole UI.
//
// Two traps this pattern has already caught fire on:
//  * juce::Label and juce::TextEditor cache their colours when constructed. If
//    a component owns one, give it an applyThemeColours() method and call it
//    AFTER the theme is known.
//  * Verify a theme by building the editor through the real construction path
//    (Tests/UISnapshot.cpp), not by flipping a global mid-session.
//
// Dark brass is theme 0 and the default, per the plan. The full token sets
// arrive with the designed UI in Phase 5; these are the working colours.
namespace theme
{
    inline constexpr int kThemeCount = 2;
    inline constexpr int kDefaultTheme = 0; // dark brass
    inline constexpr auto kThemeProperty = "theme";

    extern juce::Colour background;
    extern juce::Colour panel;
    extern juce::Colour ink;
    extern juce::Colour faded;
    extern juce::Colour accent;
    extern juce::Colour ember;
    extern juce::Colour runaway;

    void setTheme (int index);
    int currentTheme();
    const char* themeName (int index);

    inline juce::Colour inkA (float alpha) { return ink.withAlpha (alpha); }

    // Width of a string in a font, without the deprecated Font::getStringWidth.
    float textWidth (const juce::Font& font, const juce::String& text);
} // namespace theme
