#include "BrassKnob.h"

namespace
{
    constexpr float kDragPixelsPerRange = 220.0f; // vertical pixels for the whole range
    constexpr float kFineFactor = 8.0f;           // shift held: 1760 px for the whole range
    constexpr float kWheelPerNotch = 0.5f;
    constexpr float kKnobGlide = 0.35f;
    constexpr float kKnobGlideEps = 0.002f;
    constexpr float kModEase = 0.5f;
    constexpr float kModRepaintEps = 0.003f;
    constexpr float kArcStartDeg = -135.0f;
    constexpr float kArcSweepDeg = 270.0f;

    float angleFor (float norm) noexcept
    {
        return juce::degreesToRadians (kArcStartDeg + kArcSweepDeg * juce::jlimit (0.0f, 1.0f, norm));
    }

    void strokeArc (juce::Graphics& g, juce::Point<float> centre, float radius, float from, float to,
                    float thickness)
    {
        juce::Path path;
        path.addCentredArc (centre.x, centre.y, radius, radius, 0.0f, from, to, true);
        g.strokePath (path, juce::PathStrokeType (thickness, juce::PathStrokeType::curved,
                                                  juce::PathStrokeType::rounded));
    }
}

BrassKnob::BrassKnob (juce::RangedAudioParameter& parameterToUse, juce::String label,
                      juce::String hint, Size size)
    : param (parameterToUse),
      attachment (parameterToUse,
                  [this] (float newValue)
                  {
                      normValue = param.convertTo0to1 (newValue);
                      if (shownValue < 0.0f || dragging)
                          shownValue = normValue;
                      repaint();
                  }),
      labelText (std::move (label)),
      hintText (std::move (hint)),
      knobSize (size)
{
    attachment.sendInitialUpdate();
    setMouseCursor (juce::MouseCursor::UpDownResizeCursor);
}

Readout BrassKnob::readout() const
{
    if (readoutProvider)
        return readoutProvider();

    Readout r;
    r.name = labelText;
    r.value = param.getText (param.getValue(), 32);
    // The label carries the unit for parameters whose string is a bare number.
    // Anything that already spells out a unit or a word is left alone, which is
    // what stops "0.31 s dB" and "-Inf dB".
    const auto unit = param.getLabel();
    if (unit.isNotEmpty() && ! r.value.containsAnyOf ("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ"))
        r.value += " " + unit;
    r.hint = hintText;
    r.asideColour = theme::palette().faded;
    return r;
}

void BrassKnob::setRunawayZone (float normStart) noexcept
{
    runawayFrom = normStart;
    repaint();
}

void BrassKnob::setDetents (int count) noexcept
{
    if (count == detents)
        return;
    detents = count;
    repaint();
}

void BrassKnob::setModulation (bool active, float liveNorm, float spreadNorm) noexcept
{
    modActive = active;
    modLive = juce::jlimit (0.0f, 1.0f, liveNorm);
    modSpread = juce::jlimit (0.0f, 1.0f, spreadNorm);
    if (modShownLive < 0.0f) // first sight: adopt, do not sweep in from zero
        modShownLive = modLive;
}

void BrassKnob::tick()
{
    bool needsRepaint = false;

    if (shownValue >= 0.0f && std::abs (shownValue - normValue) > kKnobGlideEps)
    {
        shownValue += (normValue - shownValue) * kKnobGlide;
        needsRepaint = true;
    }

    if (modShownLive < 0.0f)
        modShownLive = normValue;
    const float liveTarget = modActive ? modLive : normValue;
    const float spreadTarget = modActive ? modSpread : 0.0f;
    if (std::abs (modShownLive - liveTarget) > kModRepaintEps
        || std::abs (modShownSpread - spreadTarget) > kModRepaintEps)
    {
        modShownLive += (liveTarget - modShownLive) * kModEase;
        modShownSpread += (spreadTarget - modShownSpread) * kModEase;
        needsRepaint = true;
    }

    if (needsRepaint)
        repaint();
}

void BrassKnob::paint (juce::Graphics& g)
{
    const auto& p = theme::palette();
    const float d = (float) diameter();
    const juce::Point<float> centre ((float) getWidth() * 0.5f, (float) kKnobTopInset + d * 0.5f);
    const float bodyRadius = d * 0.5f - 6.0f;
    const float trackRadius = bodyRadius + 4.5f;
    const float thickness = juce::jmax (2.0f, d * 0.045f);
    const float shown = shownValue < 0.0f ? normValue : shownValue;

    // 1. Body.
    {
        juce::ColourGradient gradient (p.knobBody, centre.x, centre.y - bodyRadius * 0.6f,
                                       p.knobBody.darker (0.25f), centre.x, centre.y + bodyRadius,
                                       true);
        g.setGradientFill (gradient);
        g.fillEllipse (centre.x - bodyRadius, centre.y - bodyRadius, bodyRadius * 2.0f, bodyRadius * 2.0f);
        g.setColour (p.outline);
        g.drawEllipse (centre.x - bodyRadius, centre.y - bodyRadius, bodyRadius * 2.0f,
                       bodyRadius * 2.0f, 1.0f);
    }

    // 2. Track.
    g.setColour (hovering || dragging ? p.knobRing.interpolatedWith (p.ink, 0.18f) : p.knobRing);
    strokeArc (g, centre, trackRadius, angleFor (0.0f), angleFor (1.0f), thickness);

    // 3. The runaway zone on Decay: past unity the loop feeds itself, and the
    // ring says so before you hear it.
    if (runawayFrom >= 0.0f)
    {
        g.setColour (p.runaway.withAlpha (0.35f));
        strokeArc (g, centre, trackRadius, angleFor (runawayFrom), angleFor (1.0f), thickness);

        const float a = angleFor (runawayFrom) - juce::MathConstants<float>::halfPi;
        const juce::Point<float> dir (std::cos (a), std::sin (a));
        g.setColour (p.runaway.withAlpha (0.7f));
        g.drawLine ({ centre + dir * (bodyRadius + 2.0f),
                      centre + dir * (trackRadius + thickness + 2.0f) }, 1.5f);
    }

    // 4. Detents, when Time is locked to note values.
    if (detents > 1)
    {
        g.setColour (p.faded.withAlpha (0.5f));
        for (int i = 0; i < detents; ++i)
        {
            const float a = angleFor ((float) i / (float) (detents - 1))
                            - juce::MathConstants<float>::halfPi;
            const juce::Point<float> dir (std::cos (a), std::sin (a));
            g.drawLine ({ centre + dir * (trackRadius + thickness + 1.0f),
                          centre + dir * (trackRadius + thickness + 4.0f) }, 1.0f);
        }
    }

    // 5. Value arc, red past the runaway line.
    if (shown > 0.001f)
    {
        const float redFrom = runawayFrom >= 0.0f ? juce::jmin (shown, runawayFrom) : shown;
        g.setColour (dragging ? p.accent.brighter (0.12f) : p.accent);
        strokeArc (g, centre, trackRadius, angleFor (0.0f), angleFor (redFrom), thickness);

        if (runawayFrom >= 0.0f && shown > runawayFrom)
        {
            g.setColour (p.runaway);
            strokeArc (g, centre, trackRadius, angleFor (runawayFrom), angleFor (shown), thickness);
        }
    }

    // 6 and 7. Where modulation has actually taken the value. The pointer stays
    // on what you set; this shows what you hear.
    if (modActive)
    {
        const float modRadius = trackRadius + thickness + 2.5f;
        const float modThickness = thickness * 0.55f;

        if (modShownSpread > 0.002f)
        {
            g.setColour (p.modulated.withAlpha (0.35f));
            strokeArc (g, centre, modRadius, angleFor (modShownLive - modShownSpread),
                       angleFor (modShownLive + modShownSpread), modThickness);
        }

        g.setColour (p.modulated);
        if (std::abs (modShownLive - shown) > 0.002f)
            strokeArc (g, centre, modRadius, angleFor (juce::jmin (shown, modShownLive)),
                       angleFor (juce::jmax (shown, modShownLive)), modThickness);

        const float a = angleFor (modShownLive) - juce::MathConstants<float>::halfPi;
        const juce::Point<float> dot = centre + juce::Point<float> (std::cos (a), std::sin (a)) * modRadius;
        g.fillEllipse (dot.x - thickness * 0.6f, dot.y - thickness * 0.6f, thickness * 1.2f,
                       thickness * 1.2f);
    }

    // 8 and 9. Pointer and hub.
    {
        const float a = angleFor (shown) - juce::MathConstants<float>::halfPi;
        const juce::Point<float> dir (std::cos (a), std::sin (a));
        g.setColour (p.knobPointer);
        g.drawLine ({ centre + dir * (bodyRadius * 0.50f), centre + dir * (bodyRadius * 0.90f) },
                    juce::jmax (2.0f, d * 0.026f) + (dragging ? 0.6f : 0.0f));
        g.setColour (p.outline);
        const float hub = d * 0.03f;
        g.fillEllipse (centre.x - hub, centre.y - hub, hub * 2.0f, hub * 2.0f);
    }

    // 10 and 11. Caption and value.
    const auto r = readout();
    theme::drawCaps (g, labelText, { 0.0f, d + 24.0f, (float) getWidth(), 14.0f },
                     juce::Justification::centred, 10.0f, theme::Weight::semibold, 0.12f,
                     hovering || dragging ? p.ink : p.faded);

    g.setFont (theme::font (11.0f));
    g.setColour (p.faded);
    const juce::Rectangle<float> valueRow (0.0f, d + 40.0f, (float) getWidth(), 12.0f);
    if (r.aside.isNotEmpty())
    {
        const auto valueWidth = theme::textWidth (theme::font (11.0f), r.value + " ");
        const auto asideWidth = theme::textWidth (theme::font (11.0f), r.aside);
        const float startX = valueRow.getCentreX() - (valueWidth + asideWidth) * 0.5f;
        g.drawText (r.value, valueRow.withX (startX), juce::Justification::centredLeft, false);
        g.setColour (r.asideColour);
        g.drawText (r.aside, valueRow.withX (startX + valueWidth), juce::Justification::centredLeft, false);
    }
    else
    {
        g.drawText (r.value, valueRow, juce::Justification::centred, false);
    }
}

void BrassKnob::mouseEnter (const juce::MouseEvent&)
{
    hovering = true;
    if (onHover)
        onHover (*this);
    repaint();
}

void BrassKnob::mouseExit (const juce::MouseEvent&)
{
    hovering = false;
    if (! dragging) // no flicker when a long throw leaves the rectangle
        repaint();
}

void BrassKnob::mouseDown (const juce::MouseEvent& e)
{
    lastDragY = e.position.y;
    dragging = true;
    attachment.beginGesture();
    if (onHover)
        onHover (*this); // a click also claims the strip
    repaint();
}

void BrassKnob::mouseDrag (const juce::MouseEvent& e)
{
    // Incremental, never distance-from-start, so pressing or releasing shift
    // mid-drag cannot make the value jump.
    const float pixels = kDragPixelsPerRange * (e.mods.isShiftDown() ? kFineFactor : 1.0f);
    float norm = juce::jlimit (0.0f, 1.0f, normValue + (lastDragY - e.position.y) / pixels);
    lastDragY = e.position.y;

    if (detents > 1)
        norm = std::round (norm * (float) (detents - 1)) / (float) (detents - 1);

    attachment.setValueAsPartOfGesture (param.convertFrom0to1 (norm));
}

void BrassKnob::mouseUp (const juce::MouseEvent&)
{
    if (dragging)
    {
        dragging = false;
        attachment.endGesture();
    }
    repaint();
}

void BrassKnob::mouseDoubleClick (const juce::MouseEvent&)
{
    if (onDoubleClick && onDoubleClick())
        return;
    attachment.setValueAsCompleteGesture (param.convertFrom0to1 (param.getDefaultValue()));
}

void BrassKnob::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel)
{
    if (dragging) // a nested gesture asserts in debug builds
        return;

    const float step = kWheelPerNotch * wheel.deltaY * (e.mods.isShiftDown() ? 1.0f / kFineFactor : 1.0f);
    float norm = juce::jlimit (0.0f, 1.0f, normValue + step);
    if (detents > 1)
        norm = std::round (norm * (float) (detents - 1)) / (float) (detents - 1);
    attachment.setValueAsCompleteGesture (param.convertFrom0to1 (norm));
}
