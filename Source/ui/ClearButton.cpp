#include "ClearButton.h"

void ClearButton::tick()
{
    if (flashTicks > 0)
    {
        --flashTicks;
        repaint();
    }
}

Readout ClearButton::readout() const
{
    Readout r;
    r.name = "clear";
    r.value = "";
    r.hint = "Empties the loop instantly.";
    r.asideColour = theme::palette().faded;
    return r;
}

void ClearButton::paint (juce::Graphics& g)
{
    const auto& p = theme::palette();
    const float lit = flashTicks > 0 ? (float) flashTicks / 8.0f : 0.0f;
    const auto colour = lit > 0.0f ? p.emberCore.withAlpha (juce::jmax (0.4f, lit))
                                   : (hovering ? p.ink : p.faded);

    theme::drawCaps (g, "clear", getLocalBounds().toFloat(), juce::Justification::centred, 10.0f,
                     theme::Weight::semibold, 0.14f, colour);
}

void ClearButton::mouseEnter (const juce::MouseEvent&)
{
    hovering = true;
    if (onHover)
        onHover (*this);
    repaint();
}

void ClearButton::mouseExit (const juce::MouseEvent&)
{
    hovering = false;
    repaint();
}

void ClearButton::mouseDown (const juce::MouseEvent&)
{
    if (onClick)
        onClick();
}
