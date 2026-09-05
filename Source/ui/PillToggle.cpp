#include "PillToggle.h"

PillToggle::PillToggle (juce::RangedAudioParameter& parameterToUse, juce::StringArray captionsToUse,
                        juce::String name, juce::String hint)
    : param (parameterToUse),
      attachment (parameterToUse,
                  [this] (float newValue)
                  {
                      normValue = param.convertTo0to1 (newValue);
                      repaint();
                      if (onChange)
                          onChange();
                  }),
      captions (std::move (captionsToUse)),
      nameText (std::move (name)),
      hintText (std::move (hint))
{
    attachment.sendInitialUpdate();
    setMouseCursor (juce::MouseCursor::PointingHandCursor);
}

int PillToggle::selectedIndex() const noexcept
{
    const int count = juce::jmax (1, captions.size());
    return juce::jlimit (0, count - 1, (int) std::lround (normValue * (float) (count - 1)));
}

Readout PillToggle::readout() const
{
    Readout r;
    r.name = nameText;
    r.value = captions[selectedIndex()];
    r.hint = hintText;
    r.asideColour = theme::palette().faded;
    return r;
}

void PillToggle::paint (juce::Graphics& g)
{
    const auto& p = theme::palette();
    const auto bounds = getLocalBounds().toFloat();
    const int count = juce::jmax (1, captions.size());
    const float cellWidth = bounds.getWidth() / (float) count;
    const int selected = selectedIndex();

    g.setColour (p.panel);
    g.fillRoundedRectangle (bounds, bounds.getHeight() * 0.5f);

    // Narrow pills (the Sync tab beside Time) show only the state they are in.
    // Two captions squeezed into 44 px is unreadable, and next to a knob the
    // one word that matters is the one that is true.
    const bool compact = bounds.getWidth() < 60.0f;

    if (compact)
    {
        if (selected > 0)
        {
            g.setColour (p.accent.withAlpha (hovering ? 0.95f : 0.8f));
            g.fillRoundedRectangle (bounds.reduced (1.5f), (bounds.getHeight() - 3.0f) * 0.5f);
        }
        theme::drawCaps (g, captions[selected], bounds, juce::Justification::centred, 9.0f,
                         theme::Weight::semibold, 0.10f,
                         selected > 0 ? p.background : (hovering ? p.ink : p.faded));
    }
    else
    {
        const juce::Rectangle<float> thumb (bounds.getX() + cellWidth * (float) selected,
                                            bounds.getY(), cellWidth, bounds.getHeight());
        g.setColour (p.accent.withAlpha (hovering ? 0.9f : 0.75f));
        g.fillRoundedRectangle (thumb.reduced (1.5f), (bounds.getHeight() - 3.0f) * 0.5f);

        for (int i = 0; i < count; ++i)
        {
            const juce::Rectangle<float> cell (bounds.getX() + cellWidth * (float) i, bounds.getY(),
                                               cellWidth, bounds.getHeight());
            const auto colour = i == selected ? p.background : p.faded;
            theme::drawCaps (g, captions[i], cell, juce::Justification::centred,
                             juce::jmin (9.0f, bounds.getHeight() * 0.55f), theme::Weight::semibold,
                             0.08f, colour);
        }
    }

    g.setColour (p.outline.withAlpha (0.5f));
    g.drawRoundedRectangle (bounds.reduced (0.5f), bounds.getHeight() * 0.5f, 1.0f);
}

void PillToggle::mouseEnter (const juce::MouseEvent&)
{
    hovering = true;
    if (onHover)
        onHover (*this);
    repaint();
}

void PillToggle::mouseExit (const juce::MouseEvent&)
{
    hovering = false;
    repaint();
}

void PillToggle::mouseDown (const juce::MouseEvent& e)
{
    const int count = juce::jmax (1, captions.size());
    int next = selectedIndex();

    if (count == 2 && getWidth() >= 60)
    {
        // Click a caption to pick it, click anywhere else to toggle.
        const int clicked = (int) (e.position.x / juce::jmax (1.0f, (float) getWidth() / (float) count));
        next = juce::jlimit (0, 1, clicked);
        if (next == selectedIndex())
            next = 1 - next;
    }
    else
    {
        next = (next + 1) % count;
    }

    attachment.setValueAsCompleteGesture (param.convertFrom0to1 ((float) next / (float) juce::jmax (1, count - 1)));
    if (onHover)
        onHover (*this);
}
