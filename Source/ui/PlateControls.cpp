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

    // The rail stops short of the component edges so the carriage diamond has
    // room at both ends.
    constexpr float kRailLeft = 6.0f;
    constexpr float kRailRight = 6.0f;
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
                        juce::String rightLabel, juce::String caption)
    : param (parameterToUse),
      attachment (parameterToUse,
                  [this] (float newValue)
                  {
                      normValue = param.convertTo0to1 (newValue);
                      repaint();
                  }),
      leftText (std::move (leftLabel)),
      rightText (std::move (rightLabel)),
      captionText (std::move (caption))
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
    // A captioned rail sits lower, with its name over it like a knob's under it.
    const float railY = captionText.isNotEmpty() ? 26.0f : 10.0f;
    const float left = 4.0f, right = w - 4.0f;
    const bool atRight = isRight();
    const bool lit = redSide == (atRight ? 1 : 0);

    if (captionText.isNotEmpty())
        theme::drawTracked (g, captionText, { 0.0f, 0.0f, w, 14.0f }, juce::Justification::centred,
                            theme::Face::semibold, 10.5f, 0.14f, hovering ? p.bright : p.ink);

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
    g.setColour (lit ? p.red : p.ink);
    g.fillPath (carriage);
    if (lit)
    {
        g.setColour (p.ink);
        g.strokePath (carriage, juce::PathStrokeType (1.0f));
    }

    theme::drawTracked (g, leftText, { 0.0f, railY + 9.0f, w * 0.5f, 12.0f },
                        juce::Justification::centredLeft, theme::Face::semibold, 8.5f, 0.14f,
                        atRight ? p.faded : p.ink);
    theme::drawTracked (g, rightText, { w * 0.5f, railY + 9.0f, w * 0.5f, 12.0f },
                        juce::Justification::centredRight, theme::Face::semibold, 8.5f, 0.14f,
                        atRight ? p.ink : p.faded);
}

// --- WordToggle --------------------------------------------------------------

WordToggle::WordToggle (juce::RangedAudioParameter& parameterToUse, juce::String caption,
                        juce::String offWord, juce::String onWord)
    : param (parameterToUse),
      attachment (parameterToUse,
                  [this] (float newValue)
                  {
                      normValue = param.convertTo0to1 (newValue);
                      repaint();
                  }),
      captionText (std::move (caption)),
      offText (std::move (offWord)),
      onText (std::move (onWord))
{
    attachment.sendInitialUpdate();
    setMouseCursor (juce::MouseCursor::PointingHandCursor);
}

juce::Rectangle<int> WordToggle::boundsFor (juce::Point<int> boxCentre)
{
    return { boxCentre.x - kBoxW / 2, boxCentre.y - kBoxH / 2 - kCaptionH - kGap, kBoxW, kHeight };
}

void WordToggle::mouseDown (const juce::MouseEvent&)
{
    // A complete gesture, so the host sees begin / value / end and records it.
    attachment.setValueAsCompleteGesture (param.convertFrom0to1 (isOn() ? 0.0f : 1.0f));
}

void WordToggle::paint (juce::Graphics& g)
{
    const auto& p = theme::palette();
    const float w = (float) getWidth();
    const bool on = isOn();
    const auto lineInk = hovering ? p.bright : p.ink;

    theme::drawTracked (g, captionText, { 0.0f, 0.0f, w, (float) kCaptionH },
                        juce::Justification::centred, theme::Face::semibold, 10.5f, 0.14f, lineInk);

    const auto box = juce::Rectangle<float> (0.0f, (float) (kCaptionH + kGap), w, (float) kBoxH)
                         .reduced (0.5f);
    if (on)
    {
        g.setColour (accent == Accent::blue ? p.blue : p.red);
        g.fillRoundedRectangle (box, 4.0f);
    }
    g.setColour (lineInk);
    g.drawRoundedRectangle (box, 4.0f, 1.0f);

    // The word is the state, so it is the one thing drawn at full weight.
    theme::drawTracked (g, on ? onText : offText, box, juce::Justification::centred,
                        theme::Face::semibold, 9.5f, 0.18f, on ? p.paper : lineInk);
}

// --- HeaderAction ------------------------------------------------------------

namespace
{
    // Shalal's numbers: the glyph box is 26 px in a 44 px button, the label
    // 8.5 px small caps (which its sheet renders at 1.28x), and a disabled
    // button fades whole to one alpha.
    constexpr float kActionLabelPx = 8.5f * 1.28f;
    constexpr float kActionDimAlpha = 0.35f;
}

HeaderAction::HeaderAction (Glyph glyphToDraw, juce::String labelText)
    : glyph (glyphToDraw), label (std::move (labelText))
{
    setMouseCursor (juce::MouseCursor::PointingHandCursor);
}

void HeaderAction::pulse()
{
    pulseAnim = 1.0f;
    repaint();
}

void HeaderAction::flash()
{
    flashTicks = 6;
    pulse();
}

void HeaderAction::tick()
{
    if (flashTicks > 0)
    {
        --flashTicks;
        repaint();
    }
}

void HeaderAction::drawGlyph (juce::Graphics& g, Glyph glyph, juce::Rectangle<float> box,
                              juce::Colour colour, float strokeWidth)
{
    // Ported line for line from Shalal's GlyphButton::drawGlyph: the same
    // shapes at the same fractions of the box, so the two headers match.
    juce::Path p;
    const auto c = box.getCentre();
    const float s = juce::jmin (box.getWidth(), box.getHeight());
    const juce::PathStrokeType stroke (strokeWidth, juce::PathStrokeType::curved, juce::PathStrokeType::rounded);
    g.setColour (colour);

    switch (glyph)
    {
        case Glyph::die:
        {
            // Isometric cube: top rhombus and two side faces. Every pip is
            // placed in its face's own coordinates (fractions along the
            // face's two edges), so at any size the pips sit inside the face
            // they belong to instead of at offsets guessed for one size.
            const float h = s * 0.46f, wv = s * 0.4f, dy = s * 0.23f;
            const juce::Point<float> top (c.x, c.y - h), left (c.x - wv, c.y - h + dy), right (c.x + wv, c.y - h + dy),
                                     mid (c.x, c.y - h + 2.0f * dy), bl (c.x - wv, c.y + h - dy), br (c.x + wv, c.y + h - dy),
                                     bottom (c.x, c.y + h);
            p.startNewSubPath (top); p.lineTo (right); p.lineTo (br); p.lineTo (bottom); p.lineTo (bl); p.lineTo (left); p.closeSubPath();
            p.startNewSubPath (left); p.lineTo (mid); p.lineTo (right);
            p.startNewSubPath (mid); p.lineTo (bottom);
            g.strokePath (p, stroke);
            const float pipR = s * 0.055f;
            auto pip = [&] (juce::Point<float> origin, juce::Point<float> e1, juce::Point<float> e2, float u, float v)
            {
                const auto at = origin + e1 * u + e2 * v;
                g.fillEllipse (at.x - pipR, at.y - pipR, pipR * 2.0f, pipR * 2.0f);
            };
            pip (left, top - left, mid - left, 0.5f, 0.5f);                    // top face: 1
            for (const float t : { 0.3f, 0.7f })                               // left face: 2, on the diagonal
                pip (left, mid - left, bl - left, t, t);
            // Right face: 3 pips, but in a triangle rather than a die's
            // diagonal, which at glyph size read as a single stroke.
            pip (mid, right - mid, bottom - mid, 0.3f, 0.28f);
            pip (mid, right - mid, bottom - mid, 0.7f, 0.28f);
            pip (mid, right - mid, bottom - mid, 0.5f, 0.72f);
            break;
        }
        case Glyph::wavOut:
        {
            // A filing tray with an arrow dropping into it.
            p.startNewSubPath (c.x, c.y - s * 0.42f); p.lineTo (c.x, c.y + s * 0.08f);
            p.startNewSubPath (c.x - s * 0.16f, c.y - s * 0.1f); p.lineTo (c.x, c.y + s * 0.1f); p.lineTo (c.x + s * 0.16f, c.y - s * 0.1f);
            p.startNewSubPath (c.x - s * 0.42f, c.y + s * 0.1f); p.lineTo (c.x - s * 0.42f, c.y + s * 0.4f);
            p.lineTo (c.x + s * 0.42f, c.y + s * 0.4f); p.lineTo (c.x + s * 0.42f, c.y + s * 0.1f);
            g.strokePath (p, stroke);
            break;
        }
        case Glyph::trash:
        {
            p.startNewSubPath (c.x - s * 0.38f, c.y - s * 0.25f); p.lineTo (c.x + s * 0.38f, c.y - s * 0.25f);
            p.startNewSubPath (c.x - s * 0.28f, c.y - s * 0.25f); p.lineTo (c.x - s * 0.2f, c.y + s * 0.42f);
            p.lineTo (c.x + s * 0.2f, c.y + s * 0.42f); p.lineTo (c.x + s * 0.28f, c.y - s * 0.25f);
            p.startNewSubPath (c.x - s * 0.12f, c.y - s * 0.25f); p.lineTo (c.x - s * 0.09f, c.y - s * 0.4f);
            p.lineTo (c.x + s * 0.09f, c.y - s * 0.4f); p.lineTo (c.x + s * 0.12f, c.y - s * 0.25f);
            p.startNewSubPath (c.x - s * 0.08f, c.y - s * 0.1f); p.lineTo (c.x - s * 0.05f, c.y + s * 0.28f);
            p.startNewSubPath (c.x + s * 0.08f, c.y - s * 0.1f); p.lineTo (c.x + s * 0.05f, c.y + s * 0.28f);
            g.strokePath (p, stroke);
            break;
        }
    }
}

void HeaderAction::paint (juce::Graphics& g)
{
    const auto& p = theme::palette();
    const float w = (float) getWidth();
    const float boxH = juce::jmin (26.0f, (float) getHeight() - 14.0f);
    juce::Rectangle<float> box ((w - boxH) * 0.5f, 2.0f, boxH, boxH);
    if (pulseAnim > 0.01f)
    {
        box = box.expanded (boxH * 0.12f * pulseAnim);
        pulseAnim *= 0.75f;
        repaint();
    }

    // Disabled fades whole (one transparency layer, so crossing strokes do
    // not darken) to the one unavailable alpha.
    const bool dimmed = ! isEnabled();
    if (dimmed)
        g.beginTransparencyLayer (kActionDimAlpha);

    const float alpha = hovering ? 1.0f : 0.8f;
    const auto ink = (flashTicks > 0 ? p.red : (hovering ? p.bright : p.ink)).withAlpha (alpha);
    drawGlyph (g, glyph, box.reduced (1.0f), ink, 1.1f);
    theme::drawTracked (g, label, { 0.0f, (float) getHeight() - 12.0f, w, 11.0f },
                        juce::Justification::centred, theme::Face::semibold, kActionLabelPx, 0.14f, ink);

    if (dimmed)
        g.endTransparencyLayer();
}

void HeaderAction::mouseDrag (const juce::MouseEvent& e)
{
    // A few pixels of travel turn a click into a drag, once per gesture. The
    // editor starts the OS drag from here, so nothing after it in this
    // gesture can be relied on to arrive.
    if (dragStarted || ! isEnabled() || onDragStart == nullptr || e.getDistanceFromDragStart() <= 5)
        return;
    dragStarted = true;
    onDragStart();
}

void HeaderAction::mouseUp (const juce::MouseEvent& e)
{
    if (dragStarted || ! isEnabled() || ! getLocalBounds().contains (e.getPosition()))
        return;
    pulse();
    if (onClick)
        onClick();
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
