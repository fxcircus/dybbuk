#include "ReadoutStrip.h"

void ReadoutStrip::setProvider (std::function<Readout()> newProvider)
{
    provider = std::move (newProvider);
    tick();
}

void ReadoutStrip::tick()
{
    const Readout next = provider ? provider() : rest;
    if (next != shown || bypassed != lastBypassed)
    {
        shown = next;
        lastBypassed = bypassed;
        repaint();
    }
}

void ReadoutStrip::paint (juce::Graphics& g)
{
    const auto& p = theme::palette();
    const auto bounds = getLocalBounds().toFloat();

    g.setColour (p.panel);
    g.fillRoundedRectangle (bounds, 3.0f);

    const float nameWidth = shown.name.isEmpty()
                                ? 0.0f
                                : theme::textWidth (theme::font (10.0f, theme::Weight::semibold),
                                                    shown.name.toUpperCase())
                                      + 10.0f * 0.12f * (float) shown.name.length() + 2.0f;

    if (shown.name.isNotEmpty())
        theme::drawCaps (g, shown.name, { 12.0f, 0.0f, nameWidth, bounds.getHeight() },
                         juce::Justification::centredLeft, 10.0f, theme::Weight::semibold, 0.12f,
                         p.faded);

    const float valueX = 12.0f + nameWidth + (shown.name.isEmpty() ? 0.0f : 10.0f);
    g.setFont (theme::font (13.0f));
    g.setColour (p.readout);
    g.drawText (shown.value, juce::Rectangle<float> (valueX, 0.0f, bounds.getWidth() - valueX - 12.0f,
                                                     bounds.getHeight()),
                juce::Justification::centredLeft, false);

    // Right slot, in priority order: bypass beats a state word beats a hint.
    juce::String right;
    juce::Colour rightColour = p.faded;
    if (bypassed)
    {
        right = "bypassed";
        rightColour = p.runaway;
    }
    else if (shown.aside.isNotEmpty())
    {
        right = shown.aside;
        rightColour = shown.asideColour;
    }
    else if (shown.hint.isNotEmpty() && ! dragging)
    {
        right = shown.hint;
    }

    if (right.isEmpty())
        return;

    const float valueWidth = theme::textWidth (theme::font (13.0f), shown.value);
    const float available = bounds.getWidth() - 12.0f - (valueX + valueWidth + 16.0f);
    const auto rightFont = theme::font (12.0f);
    if (available < 40.0f)
        return;

    if (theme::textWidth (rightFont, right) > available)
    {
        while (right.length() > 4 && theme::textWidth (rightFont, right + "...") > available)
            right = right.dropLastCharacters (1);
        right = right.trimEnd() + "...";
    }

    g.setFont (rightFont);
    g.setColour (rightColour);
    g.drawText (right, bounds.withTrimmedRight (12.0f), juce::Justification::centredRight, false);
}
