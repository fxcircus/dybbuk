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

// --- ExportStamp -------------------------------------------------------------

ExportStamp::ExportStamp()
{
    setMouseCursor (juce::MouseCursor::DraggingHandCursor);
}

void ExportStamp::mouseDrag (const juce::MouseEvent& e)
{
    // A few pixels of travel turn a click into a drag, once per gesture. The
    // editor starts the OS drag from here, so nothing after it in this
    // gesture can be relied on to arrive.
    if (dragged || e.getDistanceFromDragStart() < 5)
        return;
    dragged = true;
    if (onDragStart)
        onDragStart();
}

void ExportStamp::mouseUp (const juce::MouseEvent& e)
{
    if (! dragged && onClick && getLocalBounds().contains (e.getPosition()))
        onClick();
    dragged = false;
}

void ExportStamp::paint (juce::Graphics& g)
{
    const auto& p = theme::palette();
    const auto b = getLocalBounds().toFloat();
    const auto lineInk = ! isEnabled() ? p.faded.withAlpha (0.45f) : (hovering ? p.ink : p.faded);

    const juce::Rectangle<float> ring (b.getCentreX() - 13.0f, b.getY(), 26.0f, 26.0f);
    g.setColour (lineInk);
    g.drawEllipse (ring, 1.0f);

    // A tray with a wave lifting out of it, at the clear stamp's proportions.
    const float cx = ring.getCentreX(), cy = ring.getCentreY();
    juce::Path tray;
    tray.startNewSubPath (cx - 5.2f, cy + 1.6f);
    tray.lineTo (cx - 5.2f, cy + 5.0f);
    tray.lineTo (cx + 5.2f, cy + 5.0f);
    tray.lineTo (cx + 5.2f, cy + 1.6f);
    g.strokePath (tray, juce::PathStrokeType (1.1f));

    juce::Path wave;
    wave.startNewSubPath (cx - 5.0f, cy - 1.2f);
    wave.lineTo (cx - 3.6f, cy - 4.2f);
    wave.lineTo (cx - 2.2f, cy + 1.0f);
    wave.lineTo (cx - 0.8f, cy - 5.6f);
    wave.lineTo (cx + 0.8f, cy + 1.6f);
    wave.lineTo (cx + 2.2f, cy - 3.4f);
    wave.lineTo (cx + 3.6f, cy);
    wave.lineTo (cx + 5.0f, cy - 1.8f);
    g.strokePath (wave, juce::PathStrokeType (1.0f));

    theme::drawTracked (g, "DRAG OUT", { -6.0f, ring.getBottom() + 3.0f, b.getWidth() + 12.0f, 10.0f },
                        juce::Justification::centred, theme::Face::semibold, 7.0f, 0.16f, lineInk);
}

// --- EngravedTrim ------------------------------------------------------------

EngravedTrim::EngravedTrim (juce::RangedAudioParameter& parameterToUse, juce::String caption)
    : param (parameterToUse),
      attachment (parameterToUse,
                  [this] (float newValue)
                  {
                      normValue = param.convertTo0to1 (newValue);
                      repaint();
                  }),
      captionText (std::move (caption))
{
    setMouseCursor (juce::MouseCursor::LeftRightResizeCursor);
    attachment.sendInitialUpdate();
}

void EngravedTrim::setValueTextProvider (std::function<juce::String()> provider)
{
    valueProvider = std::move (provider);
    repaint();
}

float EngravedTrim::normFromX (float x) const noexcept
{
    const float left = kRailLeft, right = (float) getWidth() - kRailRight;
    return juce::jlimit (0.0f, 1.0f, (x - left) / juce::jmax (1.0f, right - left));
}

void EngravedTrim::mouseDown (const juce::MouseEvent& e)
{
    dragging = true;
    attachment.beginGesture();
    attachment.setValueAsPartOfGesture (param.convertFrom0to1 (normFromX ((float) e.position.x)));
}

void EngravedTrim::mouseDrag (const juce::MouseEvent& e)
{
    if (! dragging)
        return;
    // Shift is a fine adjust, as it is on every knob on the plate.
    const float n = e.mods.isShiftDown()
                        ? juce::jlimit (0.0f, 1.0f,
                                        normValue + 0.25f * (normFromX ((float) e.position.x) - normValue))
                        : normFromX ((float) e.position.x);
    attachment.setValueAsPartOfGesture (param.convertFrom0to1 (n));
}

void EngravedTrim::mouseUp (const juce::MouseEvent&)
{
    if (! dragging)
        return;
    dragging = false;
    attachment.endGesture();
}

void EngravedTrim::mouseDoubleClick (const juce::MouseEvent&)
{
    attachment.setValueAsCompleteGesture (param.convertFrom0to1 (param.getDefaultValue()));
}

void EngravedTrim::paint (juce::Graphics& g)
{
    const auto& p = theme::palette();
    const float w = (float) getWidth();
    const float railY = 20.0f;
    const float left = kRailLeft, right = w - kRailRight;
    const bool hot = hovering || dragging;

    theme::drawTracked (g, captionText, { 0.0f, railY - 15.0f, w, 12.0f },
                        juce::Justification::centredLeft, theme::Face::semibold, 9.0f, 0.18f,
                        hot ? p.bright : p.ink);

    if (valueProvider)
        theme::drawTracked (g, valueProvider(), { 0.0f, railY - 15.0f, w, 12.0f },
                            juce::Justification::centredRight, theme::Face::semibold, 9.0f, 0.14f,
                            hot ? p.bright : p.faded);

    g.setColour ((hot ? p.bright : p.ink).withAlpha (0.75f));
    g.drawLine (left, railY, right, railY, 1.0f);
    g.drawLine (left, railY - 4.0f, left, railY + 5.0f, 1.0f);
    g.drawLine (right, railY - 4.0f, right, railY + 5.0f, 1.0f);

    // A quarter tick at the midpoint, so the travel can be read at a glance.
    g.setColour (p.ink.withAlpha (0.35f));
    const float mid = left + 0.5f * (right - left);
    g.drawLine (mid, railY - 2.0f, mid, railY + 3.0f, 1.0f);

    // The travelled part reads solid, the rest hairline: the same "how far in"
    // language the IN and OUT meters use.
    const float x = left + normValue * (right - left);
    g.setColour (hot ? p.bright : p.ink);
    if (x > left + 0.5f)
        g.drawLine (left, railY, x, railY, 1.8f);

    const auto carriage = diamondAt ({ x, railY }, 5.0f, 6.0f);
    g.setColour (p.paper);
    g.strokePath (carriage, juce::PathStrokeType (2.6f));
    g.setColour (hot ? p.bright : p.ink);
    g.fillPath (carriage);
}

// --- DiceButton --------------------------------------------------------------

DiceButton::DiceButton()
{
    setMouseCursor (juce::MouseCursor::PointingHandCursor);
}

void DiceButton::mouseDown (const juce::MouseEvent&)
{
    // Roll to a face that is not the one showing, so a click always looks like
    // a click even if the patch it lands on is close to the last.
    int next = face;
    while (next == face)
        next = 1 + rng.nextInt (6);
    face = next;
    repaint();

    if (onClick)
        onClick();
}

void DiceButton::paint (juce::Graphics& g)
{
    const auto& p = theme::palette();
    const auto bounds = getLocalBounds().toFloat().reduced (1.0f);
    const float side = juce::jmin (bounds.getWidth(), bounds.getHeight());
    const auto body = juce::Rectangle<float> (side, side).withCentre (bounds.getCentre());
    const auto ink = hovering ? p.bright : p.ink;

    // Line art, like every other mark on the plate: an engraved outline, not a
    // filled button.
    g.setColour (ink);
    g.drawRoundedRectangle (body, 3.5f, 1.2f);

    // Pip positions on a 3x3 grid inside the face.
    const float step = body.getWidth() * 0.28f;
    const float cx = body.getCentreX(), cy = body.getCentreY();
    const float r = juce::jmax (1.1f, body.getWidth() * 0.075f);

    auto pip = [&g, r] (float x, float y)
    {
        g.fillEllipse (x - r, y - r, 2.0f * r, 2.0f * r);
    };

    const bool corners = face >= 2;
    const bool middleRow = face >= 6;
    const bool centre = (face % 2) == 1;

    if (corners)
    {
        pip (cx - step, cy - step);
        pip (cx + step, cy + step);
    }
    if (face >= 4)
    {
        pip (cx + step, cy - step);
        pip (cx - step, cy + step);
    }
    if (middleRow)
    {
        pip (cx - step, cy);
        pip (cx + step, cy);
    }
    if (centre)
        pip (cx, cy);
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
