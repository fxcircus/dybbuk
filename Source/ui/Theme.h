#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

// Two complete colour sets, dark brass by default. Components read
// theme::palette() at paint time rather than caching colours, so one
// setTheme() call re-skins the whole UI.
//
// Two traps this pattern has already caught fire on:
//  * juce::Label and juce::TextEditor cache their colours when constructed. If
//    a component owns one, give it an applyThemeColours() method and call it
//    AFTER the theme is known.
//  * Verify a theme by building the editor through the real construction path
//    (Tests/UISnapshot.cpp), not by flipping a global mid-session.
namespace theme
{
    enum class Kind : int { brass = 0, parchment = 1 }; // the values ARE the stored property
    inline constexpr int kThemeCount = 2;
    inline constexpr Kind kDefaultTheme = Kind::brass;
    inline constexpr auto kThemeProperty = "theme";

    struct Palette
    {
        const char* name;
        juce::Colour background, panel, ink, faded, accent,
            emberCore, emberGlow,
            knobBody, knobRing, knobPointer, modulated, runaway,
            meterFill, meterPeak, readout, outline;
    };

    const Palette& palette();
    void setTheme (Kind kind);
    void setTheme (int kindAsInt);
    Kind currentTheme();
    Kind kindFromProperty (const juce::var& value);
    const char* nameOf (Kind kind);

    // Kept so older call sites and the Canvas keep compiling; mirrors of the
    // active palette, refreshed by setTheme.
    extern juce::Colour background;
    extern juce::Colour ink;
    extern juce::Colour faded;
    extern juce::Colour accent;

    inline juce::Colour inkA (float alpha) { return palette().ink.withAlpha (alpha); }

    enum class Weight { regular, semibold, bold };
    juce::Font font (float px, Weight weight = Weight::regular);

    // Small caps with letter spacing, the house caption style.
    void drawCaps (juce::Graphics& g, const juce::String& text, juce::Rectangle<float> area,
                   juce::Justification justification, float px, Weight weight, float tracking,
                   juce::Colour colour);

    float textWidth (const juce::Font& f, const juce::String& text);
} // namespace theme
