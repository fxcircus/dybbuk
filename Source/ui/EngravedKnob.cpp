#include "EngravedKnob.h"

namespace
{
    // Geometry straight from the canvas: the face sits in a box 30 px larger
    // than itself so the legends and the limit marks have room.
    constexpr float kPad = 30.0f;
    constexpr float kA0 = -135.0f;   // travel starts at eight o'clock
    constexpr float kSweep = 270.0f;
    constexpr int kIndexTicks = 11;
    constexpr float kIndexStep = 27.0f; // 270 / 10

    constexpr float kDragPixelsPerRange = 220.0f;
    constexpr float kFinePixelsPerRange = 900.0f; // shift held, as the canvas has it
    constexpr float kWheelPerNotch = 0.5f;
    constexpr float kGlide = 0.35f, kGlideEps = 0.002f;
    constexpr float kModEase = 0.4f, kModEps = 0.003f;

    float angleFor (float norm) noexcept { return kA0 + kSweep * juce::jlimit (0.0f, 1.0f, norm); }

    // Zero degrees points up and angles run clockwise, matching the canvas.
    juce::Point<float> polar (juce::Point<float> centre, float radius, float degrees) noexcept
    {
        const float r = juce::degreesToRadians (degrees);
        return { centre.x + radius * std::sin (r), centre.y - radius * std::cos (r) };
    }
}

EngravedKnob::Spec EngravedKnob::heroSpec() { return { 104, 12.0f, 13.0f, 9.0f, true, false }; }
EngravedKnob::Spec EngravedKnob::midSpec() { return { 62, 10.5f, 11.5f, 8.5f, true, false }; }
EngravedKnob::Spec EngravedKnob::macroSpec() { return { 78, 11.0f, 12.0f, 9.0f, true, true }; }
EngravedKnob::Spec EngravedKnob::speedSpec() { return { 78, 9.0f, 11.0f, 9.0f, false, false }; }

juce::Rectangle<int> EngravedKnob::boundsFor (const Spec& spec, juce::Point<int> faceCentre)
{
    const int svg = spec.size + (int) kPad;
    const int height = svg + (int) (spec.labelPx + spec.valuePx + 12.0f);
    return { faceCentre.x - svg / 2, faceCentre.y - svg / 2, svg, height };
}

EngravedKnob::EngravedKnob (juce::RangedAudioParameter& parameterToUse, juce::String label,
                            Spec specToUse)
    : param (parameterToUse),
      attachment (parameterToUse,
                  [this] (float newValue)
                  {
                      normValue = param.convertTo0to1 (newValue);
                      if (shownValue < 0.0f || dragging)
                          shownValue = normValue;
                      if (onValueChange)
                          onValueChange();
                      repaint();
                  }),
      labelText (std::move (label)),
      spec (specToUse)
{
    attachment.sendInitialUpdate();
    setMouseCursor (juce::MouseCursor::UpDownResizeCursor);
}

void EngravedKnob::setLegends (juce::String minLabel, juce::String maxLabel)
{
    minLegend = std::move (minLabel);
    maxLegend = std::move (maxLabel);
    repaint();
}

void EngravedKnob::setDangerFrom (float normStart) noexcept
{
    dangerFrom = normStart;
    repaint();
}

void EngravedKnob::setDetents (int count) noexcept
{
    if (count == detents)
        return;
    detents = count;
    repaint();
}

void EngravedKnob::setModulation (bool active, float liveNorm) noexcept
{
    modActive = active;
    modLive = juce::jlimit (0.0f, 1.0f, liveNorm);
    if (modShown < 0.0f)
        modShown = modLive;
}

void EngravedKnob::setValueTextProvider (std::function<juce::String()> provider)
{
    valueProvider = std::move (provider);
    repaint();
}

bool EngravedKnob::isHot() const noexcept
{
    return dangerFrom >= 0.0f && (shownValue < 0.0f ? normValue : shownValue) > dangerFrom;
}

juce::Point<float> EngravedKnob::faceCentre() const noexcept
{
    const float svg = (float) spec.size + kPad;
    return { svg * 0.5f, svg * 0.5f };
}

juce::String EngravedKnob::valueText() const
{
    if (valueProvider)
        return valueProvider();
    return param.getText (param.getValue(), 32);
}

void EngravedKnob::tick()
{
    bool needsRepaint = false;

    if (shownValue >= 0.0f && std::abs (shownValue - normValue) > kGlideEps)
    {
        shownValue += (normValue - shownValue) * kGlide;
        needsRepaint = true;
    }

    if (modShown < 0.0f)
        modShown = normValue;
    const float target = modActive ? modLive : normValue;
    if (std::abs (modShown - target) > kModEps)
    {
        modShown += (target - modShown) * kModEase;
        needsRepaint = true;
    }

    if (needsRepaint)
        repaint();
}

void EngravedKnob::paint (juce::Graphics& g)
{
    const auto& p = theme::palette();
    const float S = (float) spec.size;
    const auto c = faceCentre();
    const float outerR = S * 0.5f;
    const float innerR = S * 0.5f - 4.0f;
    const float shown = shownValue < 0.0f ? normValue : shownValue;
    const float av = angleFor (shown);
    const float dangerAngle = dangerFrom >= 0.0f ? angleFor (dangerFrom) : 1000.0f;
    const bool hot = dangerFrom >= 0.0f && av > dangerAngle;
    const bool active = hovering || dragging;

    const auto lineInk = hot ? p.red : (active ? p.bright : p.ink);

    // Eleven index ticks around the travel, red past the danger mark.
    for (int t = 0; t < kIndexTicks; ++t)
    {
        const float a = kA0 + kIndexStep * (float) t;
        const bool red = dangerFrom >= 0.0f && a > dangerAngle;
        g.setColour (red ? p.red : p.ink);
        g.drawLine ({ polar (c, outerR - 4.0f, a), polar (c, outerR, a) }, 1.0f);
    }

    // The danger zone is hatched solid rather than merely coloured, so it
    // reads as a marked region on the plate even in the light sheet.
    if (dangerFrom >= 0.0f)
    {
        g.setColour (p.red);
        for (float a = dangerAngle; a < 135.0f; a += 6.0f)
            g.drawLine ({ polar (c, outerR - 4.0f, a), polar (c, outerR, a) }, 1.0f);
    }

    // Detents, when Time is locked to note values.
    if (detents > 1)
    {
        g.setColour (p.faded);
        for (int i = 0; i < detents; ++i)
        {
            const float a = angleFor ((float) i / (float) (detents - 1));
            g.drawLine ({ polar (c, outerR + 1.5f, a), polar (c, outerR + 4.0f, a) }, 1.0f);
        }
    }

    // The knurled rim: short ticks all the way round the inner circle.
    const int mill = juce::jmax (16, juce::roundToInt (56.0f * S / 104.0f));
    g.setColour (p.ink.withAlpha (0.7f));
    for (int t = 0; t < mill; ++t)
    {
        const float a = 360.0f * (float) t / (float) mill;
        g.drawLine ({ polar (c, innerR - 3.5f, a), polar (c, innerR - 1.5f, a) }, 0.8f);
    }

    // Limit marks: dashed spokes at each end of the travel.
    if (spec.legends)
    {
        const float dashes[] = { 2.0f, 2.0f };
        g.setColour (lineInk);
        for (float a : { kA0, kA0 + kSweep })
        {
            const auto from = polar (c, innerR - 1.0f, a);
            const auto to = polar (c, S * 0.62f, a);
            g.drawDashedLine ({ from, to }, dashes, 2, 1.0f);
        }
    }

    // The face.
    g.setColour (lineInk);
    g.drawEllipse (c.x - innerR, c.y - innerR, innerR * 2.0f, innerR * 2.0f, 1.5f);

    // Where modulation has actually taken the value. The needle stays on what
    // you set; this arc is what the engine is doing to it. The canvas computes
    // this geometry but does not draw it; it is drawn here because the engine
    // publishes the real thing.
    if (modActive && std::abs (modShown - shown) > 0.004f)
    {
        const float arcR = innerR * 0.42f;
        const float aLo = angleFor (juce::jmin (shown, modShown));
        const float aHi = angleFor (juce::jmax (shown, modShown));
        juce::Path arc;
        arc.addCentredArc (c.x, c.y, arcR, arcR, 0.0f, juce::degreesToRadians (aLo),
                           juce::degreesToRadians (aHi), true);
        g.setColour (p.red.withAlpha (0.75f));
        g.strokePath (arc, juce::PathStrokeType (1.0f));
        for (float a : { aLo, aHi })
            g.drawLine ({ polar (c, arcR - 4.0f, a), polar (c, arcR + 2.0f, a) }, 1.0f);
    }

    // Needle and arrowhead.
    {
        g.setColour (lineInk);
        g.drawLine ({ polar (c, innerR * 0.1f, av), polar (c, innerR * 0.62f, av) },
                    spec.redNeedle ? 2.4f : 2.0f);

        const auto apex = polar (c, innerR * 0.88f, av);
        const auto base = polar (c, innerR * 0.55f, av);
        const float rad = juce::degreesToRadians (av);
        const juce::Point<float> side (std::cos (rad) * S * 0.07f, std::sin (rad) * S * 0.07f);

        juce::Path head;
        head.startNewSubPath (apex);
        head.lineTo (base + side);
        head.lineTo (base - side);
        head.closeSubPath();
        g.setColour (spec.redNeedle ? p.red : lineInk);
        g.fillPath (head);
    }

    // Legends beside the face, then the caption and the value below it.
    const float legendY = c.y + outerR + 4.0f - spec.legendPx;
    if (spec.legends && (minLegend.isNotEmpty() || maxLegend.isNotEmpty()))
    {
        g.setFont (theme::font (theme::Face::text, spec.legendPx));
        g.setColour (p.faded);
        g.drawText (minLegend, juce::Rectangle<float> (c.x - outerR - 2.0f, legendY, 44.0f, spec.legendPx + 4.0f),
                    juce::Justification::centredLeft, false);
        g.setColour (dangerFrom >= 0.0f ? p.red : p.faded);
        g.drawText (maxLegend, juce::Rectangle<float> (c.x + outerR + 2.0f - 44.0f, legendY, 44.0f, spec.legendPx + 4.0f),
                    juce::Justification::centredRight, false);
    }

    const float svg = S + kPad;
    theme::drawTracked (g, labelText, { 0.0f, svg + 1.0f, (float) getWidth(), spec.labelPx + 4.0f },
                        juce::Justification::centred, theme::Face::semibold, spec.labelPx, 0.14f,
                        accented ? p.red : (active ? p.bright : p.ink));

    g.setFont (theme::font (theme::Face::text, spec.valuePx));
    g.setColour (hot ? p.red : (active ? p.bright : p.faded));
    g.drawText (valueText(),
                juce::Rectangle<float> (0.0f, svg + spec.labelPx + 4.0f, (float) getWidth(),
                                        spec.valuePx + 4.0f),
                juce::Justification::centred, false);
}

void EngravedKnob::setAccent (bool on) noexcept
{
    if (accented == on)
        return;
    accented = on;
    repaint();
}

void EngravedKnob::mouseEnter (const juce::MouseEvent&)
{
    hovering = true;
    repaint();
}

void EngravedKnob::mouseExit (const juce::MouseEvent&)
{
    hovering = false;
    if (! dragging) // no flicker when a long throw leaves the rectangle
        repaint();
}

void EngravedKnob::mouseDown (const juce::MouseEvent& e)
{
    lastDragY = e.position.y;
    dragNorm = normValue;
    dragging = true;
    attachment.beginGesture();
    repaint();
}

void EngravedKnob::mouseDrag (const juce::MouseEvent& e)
{
    // Incremental, never distance-from-start, so pressing or releasing shift
    // mid-drag cannot make the value jump.
    // The hand's position accumulates on its own; a detented knob rounds
    // that to a detent. Rounding the VALUE each event, as this once did,
    // meant a knob with few detents never moved: each small move rounded
    // straight back to where it was.
    const float pixels = e.mods.isShiftDown() ? kFinePixelsPerRange : kDragPixelsPerRange;
    dragNorm = juce::jlimit (0.0f, 1.0f, dragNorm + (lastDragY - e.position.y) / pixels);
    lastDragY = e.position.y;

    float norm = dragNorm;
    if (detents > 1)
        norm = std::round (norm * (float) (detents - 1)) / (float) (detents - 1);

    attachment.setValueAsPartOfGesture (param.convertFrom0to1 (norm));
}

void EngravedKnob::mouseUp (const juce::MouseEvent&)
{
    if (dragging)
    {
        dragging = false;
        attachment.endGesture();
    }
    repaint();
}

void EngravedKnob::mouseDoubleClick (const juce::MouseEvent&)
{
    float target = param.getDefaultValue();
    if (detents > 1)
        target = std::round (target * (float) (detents - 1)) / (float) (detents - 1);

    attachment.setValueAsCompleteGesture (param.convertFrom0to1 (target));

    if (onDoubleClick)
        onDoubleClick();
}

void EngravedKnob::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel)
{
    if (dragging) // a nested gesture asserts in debug builds
        return;

    float norm;
    if (detents > 1)
    {
        // One notch, one detent, whichever way the wheel went.
        const float d = 1.0f / (float) (detents - 1);
        const float current = std::round (normValue * (float) (detents - 1)) / (float) (detents - 1);
        norm = juce::jlimit (0.0f, 1.0f, current + (wheel.deltaY > 0.0f ? d : (wheel.deltaY < 0.0f ? -d : 0.0f)));
    }
    else
    {
        const float step = kWheelPerNotch * wheel.deltaY * (e.mods.isShiftDown() ? 0.25f : 1.0f);
        norm = juce::jlimit (0.0f, 1.0f, normValue + step);
    }
    attachment.setValueAsCompleteGesture (param.convertFrom0to1 (norm));
}
