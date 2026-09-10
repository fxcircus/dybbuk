#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

// The design's two sheets: ink on near-black paper, and ink on parchment.
// Light (parchment) is the default since 2026-09-09. Components read theme::palette() at paint time rather
// than caching colours, so one setTheme() call re-skins the whole window.
//
// Two traps this pattern has already caught fire on:
//  * juce::Label and juce::TextEditor cache their colours when constructed. If
//    a component owns one, give it an applyThemeColours() method and call it
//    AFTER the theme is known.
//  * Verify a theme by building the editor through the real construction path
//    (Tests/UISnapshot.cpp), not by flipping a global mid-session.
namespace theme
{
    enum class Kind : int { dark = 0, light = 1 }; // the values ARE the stored property
    inline constexpr int kThemeCount = 2;
    inline constexpr Kind kDefaultTheme = Kind::light;
    inline constexpr auto kThemeProperty = "theme";

    struct Palette
    {
        const char* name;
        juce::Colour paper;  // the plate the controls are engraved into
        juce::Colour ink;    // every line and every letter
        juce::Colour faded;  // captions, minimum and maximum labels
        juce::Colour red;    // the danger zone, the lamp, the meters
        juce::Colour blue;   // frozen: the freeze button and the dybbuk while it holds
        juce::Colour bright; // hover and drag
        float vignette;      // how hard the corners fall away
    };

    const Palette& palette();
    void setTheme (Kind kind);
    void setTheme (int kindAsInt);
    Kind currentTheme();
    Kind kindFromProperty (const juce::var& value);
    const char* nameOf (Kind kind);

    // Ink at an alpha, which is most of the design's secondary strokes.
    juce::Colour inkA (float alpha);

    // Kept so any older call site keeps compiling; mirrors of the palette.
    extern juce::Colour background;
    extern juce::Colour ink;
    extern juce::Colour faded;
    extern juce::Colour accent;

    // The three faces, embedded so the plugin looks the same on a machine that
    // has never seen them.
    enum class Face { text, semibold, hebrew, script };
    juce::Font font (Face face, float px);

    // Letter-spaced capitals, which is how every caption in the design is set.
    void drawTracked (juce::Graphics& g, const juce::String& text, juce::Rectangle<float> area,
                      juce::Justification justification, Face face, float px, float tracking,
                      juce::Colour colour);

    float trackedWidth (Face face, float px, float tracking, const juce::String& text);
    float textWidth (const juce::Font& f, const juce::String& text);
} // namespace theme
