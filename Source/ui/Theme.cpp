#include "Theme.h"

#include <DybbukFonts.h>

namespace theme
{
    namespace
    {
        struct Spec
        {
            const char* name;
            juce::uint32 paper, ink, faded, red, blue, bright;
            float vignette;
        };

        // juce::Colour is not constexpr, so the sheets live as ARGB words and
        // become Colours on first use.
        constexpr Spec kDark { "dark", 0xff17150f, 0xffece8dc, 0xffa39d8f, 0xffd45a4a, 0xff6fb0dc, 0xfffffdf5, 0.34f };
        constexpr Spec kLight { "light", 0xfff2eee4, 0xff1c1a15, 0xff5c574c, 0xffa8362b, 0xff2e6a9e, 0xff000000, 0.08f };

        Palette makePalette (const Spec& s)
        {
            return { s.name, juce::Colour (s.paper), juce::Colour (s.ink), juce::Colour (s.faded),
                     juce::Colour (s.red), juce::Colour (s.blue), juce::Colour (s.bright), s.vignette };
        }

        Kind current = kDefaultTheme;
        Palette active = makePalette (kDark);

        // Held for the plugin's lifetime: creating a typeface from binary data
        // is not something to do inside paint().
        //
        // The cache is deliberately never destroyed. A static Typeface::Ptr
        // releases its font at static destruction time, which on macOS runs
        // after JUCE has already torn down, and the release then fails on a
        // dead mutex: an exception during host shutdown. Four leaked faces for
        // the life of the process is the standard trade for that.
        juce::Typeface::Ptr typefaceFor (Face face)
        {
            static auto* cached = new juce::Typeface::Ptr[4];
            const int index = (int) face;

            if (cached[index] == nullptr)
            {
                switch (face)
                {
                    case Face::semibold:
                        cached[index] = juce::Typeface::createSystemTypefaceFor (
                            fonts::EBGaramondSemiBold_ttf, fonts::EBGaramondSemiBold_ttfSize);
                        break;
                    case Face::hebrew:
                        cached[index] = juce::Typeface::createSystemTypefaceFor (
                            fonts::FrankRuhlLibreBold_ttf, fonts::FrankRuhlLibreBold_ttfSize);
                        break;
                    case Face::script:
                        cached[index] = juce::Typeface::createSystemTypefaceFor (
                            fonts::PinyonScriptRegular_ttf, fonts::PinyonScriptRegular_ttfSize);
                        break;
                    case Face::text:
                    default:
                        cached[index] = juce::Typeface::createSystemTypefaceFor (
                            fonts::EBGaramondRegular_ttf, fonts::EBGaramondRegular_ttfSize);
                        break;
                }
            }
            return cached[index];
        }
    }

    juce::Colour background { 0xff17150f };
    juce::Colour ink { 0xffece8dc };
    juce::Colour faded { 0xffa39d8f };
    juce::Colour accent { 0xffd45a4a };

    const Palette& palette() { return active; }

    void setTheme (Kind kind)
    {
        current = kind;
        active = makePalette (kind == Kind::light ? kLight : kDark);
        background = active.paper;
        ink = active.ink;
        faded = active.faded;
        accent = active.red;
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
            return value.toString().equalsIgnoreCase ("light") ? Kind::light : Kind::dark;
        return ((int) value) == 1 ? Kind::light : Kind::dark;
    }

    const char* nameOf (Kind kind) { return kind == Kind::light ? "light" : "dark"; }

    juce::Colour inkA (float alpha) { return active.ink.withAlpha (alpha); }

    juce::Font font (Face face, float px)
    {
        return juce::Font (juce::FontOptions (typefaceFor (face)).withHeight (px));
    }

    float textWidth (const juce::Font& f, const juce::String& text)
    {
        juce::GlyphArrangement glyphs;
        glyphs.addLineOfText (f, text, 0.0f, 0.0f);
        return glyphs.getBoundingBox (0, -1, true).getWidth();
    }

    float trackedWidth (Face face, float px, float tracking, const juce::String& text)
    {
        const auto f = font (face, px);
        const float spacing = px * tracking;
        float total = 0.0f;
        for (int i = 0; i < text.length(); ++i)
            total += textWidth (f, text.substring (i, i + 1)) + spacing;
        return total > 0.0f ? total - spacing : 0.0f;
    }

    void drawTracked (juce::Graphics& g, const juce::String& text, juce::Rectangle<float> area,
                      juce::Justification justification, Face face, float px, float tracking,
                      juce::Colour colour)
    {
        const auto f = font (face, px);
        g.setColour (colour);
        g.setFont (f);

        if (tracking <= 0.0f)
        {
            g.drawText (text, area, justification, false);
            return;
        }

        const float total = trackedWidth (face, px, tracking, text);
        float x = area.getX();
        if (justification.testFlags (juce::Justification::horizontallyCentred))
            x = area.getCentreX() - total * 0.5f;
        else if (justification.testFlags (juce::Justification::right))
            x = area.getRight() - total;

        const float spacing = px * tracking;
        for (int i = 0; i < text.length(); ++i)
        {
            const auto glyph = text.substring (i, i + 1);
            const float w = textWidth (f, glyph);
            g.drawText (glyph, juce::Rectangle<float> (x, area.getY(), w + 2.0f, area.getHeight()),
                        juce::Justification::centredLeft, false);
            x += w + spacing;
        }
    }

    // --- PlateLookAndFeel ---------------------------------------------------

    PlateLookAndFeel::PlateLookAndFeel()
    {
        applyPalette();
    }

    void PlateLookAndFeel::applyPalette()
    {
        // Paper and ink, the hovered row inverted like a selected line of
        // type, the section header in the captions' faded ink. The tick is
        // drawn in the row's text colour, so it needs nothing of its own.
        const auto& p = palette();
        setColour (juce::PopupMenu::backgroundColourId, p.paper);
        setColour (juce::PopupMenu::textColourId, p.ink);
        setColour (juce::PopupMenu::highlightedBackgroundColourId, p.ink);
        setColour (juce::PopupMenu::highlightedTextColourId, p.paper);
        setColour (juce::PopupMenu::headerTextColourId, p.faded);
    }

    juce::Font PlateLookAndFeel::getPopupMenuFont()
    {
        return font (Face::text, 15.0f);
    }

    void PlateLookAndFeel::drawPopupMenuSectionHeader (juce::Graphics& g, const juce::Rectangle<int>& area,
                                                       const juce::String& sectionName)
    {
        // A tracked small-caps caption, like every heading on the plate,
        // with a hairline under it so the list reads as a labelled column.
        const auto& p = palette();
        drawTracked (g, sectionName.toUpperCase(), area.toFloat().reduced (12.0f, 0.0f),
                     juce::Justification::centredLeft, Face::semibold, 9.5f, 0.16f, p.faded);
        g.setColour (p.ink.withAlpha (0.28f));
        g.fillRect ((float) area.getX() + 8.0f, (float) area.getBottom() - 1.0f, (float) area.getWidth() - 16.0f, 1.0f);
    }

    void PlateLookAndFeel::getIdealPopupMenuItemSize (const juce::String& text, bool isSeparator,
                                                      int standardMenuItemHeight, int& idealWidth, int& idealHeight)
    {
        LookAndFeel_V4::getIdealPopupMenuItemSize (text, isSeparator, standardMenuItemHeight, idealWidth, idealHeight);
        // A little more air per row than the stock list, and enough width
        // that a ticklist of one-word names does not come out as a sliver.
        if (! isSeparator)
        {
            idealHeight = juce::jmax (idealHeight, 24);
            idealWidth = juce::jmax (idealWidth, 150);
        }
    }
} // namespace theme
