#include "PlateControls.h"

namespace
{
    juce::Path diamondAt (juce::Point<float> centre, float halfWidth, float halfHeight)
    {
        juce::Path p;
        p.startNewSubPath (centre.x, centre.y - halfHeight);
        p.lineTo (centre.x + halfWidth, centre.y);
        p.lineTo (centre.x, centre.y + halfHeight);
        p.lineTo (centre.x - halfWidth, centre.y);
        p.closeSubPath();
        return p;
    }
}

// --- DiamondToggle -----------------------------------------------------------

DiamondToggle::DiamondToggle (juce::RangedAudioParameter& parameterToUse, Style styleToUse,
                              juce::String onLabel, juce::String offLabel)
    : param (parameterToUse),
      attachment (parameterToUse,
                  [this] (float newValue)
                  {
                      normValue = param.convertTo0to1 (newValue);
                      repaint();
                  }),
      style (styleToUse),
      onText (std::move (onLabel)),
      offText (std::move (offLabel))
{
    attachment.sendInitialUpdate();
    setMouseCursor (juce::MouseCursor::PointingHandCursor);
}

bool DiamondToggle::isOn() const noexcept { return normValue >= 0.5f; }

void DiamondToggle::mouseDown (const juce::MouseEvent&)
{
    attachment.setValueAsCompleteGesture (param.convertFrom0to1 (isOn() ? 0.0f : 1.0f));
}

void DiamondToggle::paint (juce::Graphics& g)
{
    const auto& p = theme::palette();
    const auto b = getLocalBounds().toFloat();
    const bool on = isOn();

    if (style == Style::framed)
    {
        // Bypass: the diamond is filled while the plugin is IN circuit, so a
        // lit mark means signal is passing through the plate.
        const auto lineInk = on ? p.faded : (hovering ? p.bright : p.ink);
        g.setColour (lineInk);
        g.drawRect (b, 1.0f);

        const auto mark = juce::Point<float> (b.getX() + 13.0f, b.getCentreY());
        const auto shape = diamondAt (mark, 3.6f, 3.6f);
        if (! on)
        {
            g.setColour (p.red);
            g.fillPath (shape);
        }
        g.setColour (lineInk);
        g.strokePath (shape, juce::PathStrokeType (1.0f));

        theme::drawTracked (g, on ? onText : offText,
                            { b.getX() + 22.0f, b.getY(), b.getWidth() - 26.0f, b.getHeight() },
                            juce::Justification::centred, theme::Face::semibold, 7.5f, 0.14f, lineInk);
        return;
    }

    // Sync: a bare diamond with its name under it.
    const auto centre = juce::Point<float> (b.getCentreX(), b.getY() + 6.0f);
    const auto shape = diamondAt (centre, 4.2f, 4.2f);
    if (on)
    {
        g.setColour (p.ink);
        g.fillPath (shape);
    }
    g.setColour (hovering ? p.bright : p.ink);
    g.strokePath (shape, juce::PathStrokeType (1.0f));

    theme::drawTracked (g, offText, { 0.0f, b.getY() + 12.0f, b.getWidth(), 10.0f },
                        juce::Justification::centred, theme::Face::semibold, 6.5f, 0.12f,
                        on ? p.ink : p.faded);
}

// --- RailSwitch --------------------------------------------------------------

RailSwitch::RailSwitch (juce::RangedAudioParameter& parameterToUse, juce::String leftLabel,
                        juce::String rightLabel)
    : param (parameterToUse),
      attachment (parameterToUse,
                  [this] (float newValue)
                  {
                      normValue = param.convertTo0to1 (newValue);
                      repaint();
                  }),
      leftText (std::move (leftLabel)),
      rightText (std::move (rightLabel))
{
    attachment.sendInitialUpdate();
    setMouseCursor (juce::MouseCursor::PointingHandCursor);
}

void RailSwitch::mouseDown (const juce::MouseEvent&)
{
    attachment.setValueAsCompleteGesture (param.convertFrom0to1 (isRight() ? 0.0f : 1.0f));
}

void RailSwitch::paint (juce::Graphics& g)
{
    const auto& p = theme::palette();
    const float w = (float) getWidth();
    const float railY = 10.0f;
    const float left = 4.0f, right = w - 4.0f;
    const bool atRight = isRight();

    g.setColour (hovering ? p.bright : p.ink);
    g.drawLine (left, railY, right, railY, 1.0f);
    g.drawLine (left, railY - 4.0f, left, railY + 5.0f, 1.0f);
    g.drawLine (right, railY - 4.0f, right, railY + 5.0f, 1.0f);

    // A tick marks the end the carriage is not sitting on.
    const float tickX = atRight ? left : right;
    g.drawLine (tickX, railY - 3.0f, tickX, railY + 4.0f, 1.0f);

    const auto carriage = diamondAt ({ atRight ? right : left, railY }, 5.0f, 6.0f);
    g.setColour (p.paper);
    g.strokePath (carriage, juce::PathStrokeType (2.6f));
    g.setColour (p.ink);
    g.fillPath (carriage);

    theme::drawTracked (g, leftText, { 0.0f, railY + 9.0f, w * 0.5f, 12.0f },
                        juce::Justification::centredLeft, theme::Face::semibold, 8.5f, 0.14f,
                        atRight ? p.faded : p.ink);
    theme::drawTracked (g, rightText, { w * 0.5f, railY + 9.0f, w * 0.5f, 12.0f },
                        juce::Justification::centredRight, theme::Face::semibold, 8.5f, 0.14f,
                        atRight ? p.ink : p.faded);
}

// --- ClearStamp --------------------------------------------------------------

ClearStamp::ClearStamp()
{
    setMouseCursor (juce::MouseCursor::PointingHandCursor);
}

void ClearStamp::tick()
{
    if (flashTicks > 0)
    {
        --flashTicks;
        repaint();
    }
}

void ClearStamp::paint (juce::Graphics& g)
{
    const auto& p = theme::palette();
    const auto b = getLocalBounds().toFloat();
    const auto lineInk = flashTicks > 0 ? p.red : (hovering ? p.ink : p.faded);

    const juce::Rectangle<float> ring (b.getCentreX() - 13.0f, b.getY(), 26.0f, 26.0f);
    g.setColour (lineInk);
    g.drawEllipse (ring, 1.0f);

    // A bin, drawn at the canvas's proportions inside the ring.
    const float cx = ring.getCentreX(), cy = ring.getCentreY();
    g.drawLine (cx - 4.8f, cy - 3.6f, cx + 4.8f, cy - 3.6f, 1.1f);

    juce::Path lid;
    lid.startNewSubPath (cx - 1.6f, cy - 3.6f);
    lid.lineTo (cx - 1.6f, cy - 5.0f);
    lid.lineTo (cx + 1.6f, cy - 5.0f);
    lid.lineTo (cx + 1.6f, cy - 3.6f);
    g.strokePath (lid, juce::PathStrokeType (1.1f));

    juce::Path body;
    body.startNewSubPath (cx - 3.4f, cy - 1.6f);
    body.lineTo (cx - 2.6f, cy + 5.2f);
    body.lineTo (cx + 2.6f, cy + 5.2f);
    body.lineTo (cx + 3.4f, cy - 1.6f);
    g.strokePath (body, juce::PathStrokeType (1.1f));

    g.drawLine (cx - 1.3f, cy, cx - 1.05f, cy + 3.6f, 0.9f);
    g.drawLine (cx + 1.3f, cy, cx + 1.05f, cy + 3.6f, 0.9f);

    theme::drawTracked (g, "CLEAR", { 0.0f, ring.getBottom() + 3.0f, b.getWidth(), 10.0f },
                        juce::Justification::centred, theme::Face::semibold, 7.0f, 0.16f, lineInk);
}

// --- ThemeMark ---------------------------------------------------------------

void ThemeMark::paint (juce::Graphics& g)
{
    const auto& p = theme::palette();
    const auto b = getLocalBounds().toFloat();

    theme::drawTracked (g, "THEME:", { 0.0f, 0.0f, 40.0f, b.getHeight() },
                        juce::Justification::centredLeft, theme::Face::text, 8.5f, 0.14f, p.faded);

    const juce::Point<float> c (b.getRight() - 12.0f, b.getCentreY());
    g.setColour (p.ink.withAlpha (hovering ? 1.0f : 0.75f));

    if (theme::currentTheme() == theme::Kind::dark)
    {
        g.fillEllipse (c.x - 4.5f, c.y - 4.5f, 9.0f, 9.0f);
        for (int i = 0; i < 8; ++i)
        {
            const float a = juce::degreesToRadians (45.0f * (float) i);
            const juce::Point<float> dir (std::sin (a), -std::cos (a));
            g.drawLine ({ c + dir * 6.5f, c + dir * 9.5f }, 1.4f);
        }
    }
    else
    {
        juce::Path moon;
        moon.addEllipse (c.x - 7.0f, c.y - 7.0f, 14.0f, 14.0f);
        juce::Path bite;
        bite.addEllipse (c.x - 3.0f, c.y - 8.5f, 13.0f, 13.0f);
        moon.setUsingNonZeroWinding (false);
        moon.addPath (bite);
        g.fillPath (moon);
    }
}
