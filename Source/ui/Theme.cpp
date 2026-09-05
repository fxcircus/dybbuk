#include "Theme.h"

namespace theme
{
    namespace
    {
        struct Spec
        {
            const char* name;
            juce::uint32 c[16];
        };

        // juce::Colour is not constexpr, so the palettes live as ARGB words
        // and become Colours on first use.
        constexpr Spec kBrass {
            "brass",
            { 0xff15110c,   // background
              0xff1f1912,   // panel
              0xffefe5d0,   // ink
              0xff9c8f77,   // faded
              0xffc9a24e,   // accent
              0xffffd98a,   // emberCore
              0xffe26a2c,   // emberGlow
              0xff2b241a,   // knobBody
              0xff4c4131,   // knobRing
              0xffefe5d0,   // knobPointer
              0xff7fb8ad,   // modulated: verdigris, the patina on brass, reads as "not the knob"
              0xffe8502f,   // runaway
              0xffc9a24e,   // meterFill
              0xfff2c76a,   // meterPeak
              0xffefe5d0,   // readout
              0xff6b5c45 }  // outline
        };

        constexpr Spec kParchment {
            "parchment",
            { 0xffefe8db, 0xffe4dccc, 0xff1e1a14, 0xff66604f, 0xff7a5a10,
              0xffffb640, 0xffc94a14, 0xfff7f2e8, 0xffc6bba6, 0xff1e1a14,
              0xff25736a, 0xffb32e1c, 0xff7a5a10, 0xffc94a14, 0xff1e1a14, 0xff9a8d75 }
        };

        Palette makePalette (const Spec& spec)
        {
            auto col = [&spec] (int i) { return juce::Colour (spec.c[i]); };
            return { spec.name, col (0), col (1), col (2), col (3), col (4), col (5), col (6),
                     col (7), col (8), col (9), col (10), col (11), col (12), col (13), col (14),
                     col (15) };
        }

        Kind current = kDefaultTheme;
        Palette active = makePalette (kBrass);
    }

    juce::Colour background { 0xff15110c };
    juce::Colour ink { 0xffefe5d0 };
    juce::Colour faded { 0xff9c8f77 };
    juce::Colour accent { 0xffc9a24e };

    const Palette& palette() { return active; }

    void setTheme (Kind kind)
    {
        current = kind;
        active = makePalette (kind == Kind::parchment ? kParchment : kBrass);
        background = active.background;
        ink = active.ink;
        faded = active.faded;
        accent = active.accent;
    }

    void setTheme (int kindAsInt)
    {
        setTheme (static_cast<Kind> (juce::jlimit (0, kThemeCount - 1, kindAsInt)));
    }

    Kind currentTheme() { return current; }

    // Tolerant of what an older session might hold: an int, a name, or the
    // template's darkMode bool.
    Kind kindFromProperty (const juce::var& value)
    {
        if (value.isVoid())
            return kDefaultTheme;
        if (value.isString())
            return value.toString().equalsIgnoreCase ("parchment") ? Kind::parchment : Kind::brass;
        const int i = (int) value;
        return i == 1 ? Kind::parchment : Kind::brass;
    }

    const char* nameOf (Kind kind) { return kind == Kind::parchment ? "parchment" : "brass"; }

    juce::Font font (float px, Weight weight)
    {
        auto options = juce::FontOptions (px);
        if (weight == Weight::bold)
            options = options.withStyle ("Bold");
        else if (weight == Weight::semibold)
            options = options.withStyle ("Semibold");
        return juce::Font (options);
    }

    void drawCaps (juce::Graphics& g, const juce::String& text, juce::Rectangle<float> area,
                   juce::Justification justification, float px, Weight weight, float tracking,
                   juce::Colour colour)
    {
        const auto upper = text.toUpperCase();
        const auto f = font (px, weight);
        g.setColour (colour);
        g.setFont (f);

        if (tracking <= 0.0f)
        {
            g.drawText (upper, area, justification, false);
            return;
        }

        const float spacing = px * tracking;
        float total = 0.0f;
        for (int i = 0; i < upper.length(); ++i)
            total += textWidth (f, upper.substring (i, i + 1)) + spacing;
        total -= spacing;

        float x = area.getX();
        if (justification.testFlags (juce::Justification::horizontallyCentred))
            x = area.getCentreX() - total * 0.5f;
        else if (justification.testFlags (juce::Justification::right))
            x = area.getRight() - total;

        for (int i = 0; i < upper.length(); ++i)
        {
            const auto glyph = upper.substring (i, i + 1);
            g.drawText (glyph, juce::Rectangle<float> (x, area.getY(), px * 2.0f, area.getHeight()),
                        juce::Justification::centredLeft, false);
            x += textWidth (f, glyph) + spacing;
        }
    }

    float textWidth (const juce::Font& f, const juce::String& text)
    {
        juce::GlyphArrangement glyphs;
        glyphs.addLineOfText (f, text, 0.0f, 0.0f);
        return glyphs.getBoundingBox (0, -1, true).getWidth();
    }
} // namespace theme
