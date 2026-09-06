#include "VerticalFader.h"

namespace
{
    constexpr float kFloorDb = -60.0f;
    constexpr float kTopDb = 6.0f;
    constexpr float kFallDbPerTick = 24.0f / 30.0f;
    constexpr float kPeakFallDbPerTick = 12.0f / 30.0f;
    constexpr float kRepaintDb = 0.25f;

    float dbToNorm (float db) { return juce::jlimit (0.0f, 1.0f, (db - kFloorDb) / (kTopDb - kFloorDb)); }

    // The hatch that fills the meter and the handle: 45 degree ruling, which
    // is how the canvas draws every filled area.
    void hatch (juce::Graphics& g, juce::Rectangle<float> area, juce::Colour colour, float spacing,
                float thickness)
    {
        juce::Graphics::ScopedSaveState save (g);
        g.reduceClipRegion (area.getSmallestIntegerContainer());
        g.setColour (colour);
        const float span = area.getWidth() + area.getHeight();
        for (float d = 0.0f; d < span; d += spacing)
            g.drawLine (area.getX() + d, area.getY(), area.getX(), area.getY() + d, thickness);
    }
}

VerticalFader::VerticalFader (juce::RangedAudioParameter& parameterToUse, juce::String caption)
    : param (parameterToUse),
      attachment (parameterToUse,
                  [this] (float newValue)
                  {
                      normValue = param.convertTo0to1 (newValue);
                      repaint();
                  }),
      captionText (std::move (caption))
{
    attachment.sendInitialUpdate();
    setMouseCursor (juce::MouseCursor::UpDownResizeCursor);
}

juce::Rectangle<float> VerticalFader::trackArea() const
{
    // Caption, "+6", track, "-Inf", value: the track takes what is left.
    const float top = 30.0f;
    const float bottom = (float) getHeight() - 26.0f;
    return { (float) getWidth() * 0.5f - 17.0f, top, 34.0f, juce::jmax (10.0f, bottom - top) };
}

void VerticalFader::setLevel (float linearPeak) noexcept
{
    const float db = juce::Decibels::gainToDecibels (linearPeak, kFloorDb);
    if (db > meterDb)
        meterDb = db;
    if (db > peakDb)
        peakDb = db;
}

void VerticalFader::tick()
{
    const float before = meterDb;
    meterDb = juce::jmax (kFloorDb, meterDb - kFallDbPerTick);
    peakDb = juce::jmax (kFloorDb, peakDb - kPeakFallDbPerTick);

    if (std::abs (before - meterDb) > kRepaintDb || std::abs (lastPaintedDb - meterDb) > kRepaintDb)
    {
        lastPaintedDb = meterDb;
        repaint();
    }
}

void VerticalFader::paint (juce::Graphics& g)
{
    const auto& p = theme::palette();
    const bool active = hovering || dragging;
    const auto lineInk = active ? p.bright : p.ink;
    const auto track = trackArea();
    const float w = (float) getWidth();

    theme::drawTracked (g, captionText, { 0.0f, 2.0f, w, 13.0f }, juce::Justification::centred,
                        theme::Face::semibold, 9.5f, 0.14f, lineInk);

    g.setFont (theme::font (theme::Face::text, 7.0f));
    g.setColour (p.faded);
    g.drawText ("+6", juce::Rectangle<float> (0.0f, 16.0f, w, 10.0f), juce::Justification::centred, false);
    g.drawText (juce::String::fromUTF8 ("\xe2\x88\x92\xe2\x88\x9e"),
                juce::Rectangle<float> (0.0f, track.getBottom() + 1.0f, w, 10.0f),
                juce::Justification::centred, false);

    // End caps and the rail.
    const float railX = track.getCentreX() - 1.0f;
    g.setColour (p.ink);
    g.drawLine (track.getCentreX() - 4.0f, track.getY(), track.getCentreX() + 4.0f, track.getY(), 1.0f);
    g.drawLine (track.getCentreX() - 4.0f, track.getBottom(), track.getCentreX() + 4.0f,
                track.getBottom(), 1.0f);
    g.fillRect (railX - 2.0f, track.getY(), 6.0f, track.getHeight());

    // The meter climbs the rail itself.
    const float meterNorm = dbToNorm (meterDb);
    if (meterNorm > 0.005f)
    {
        const juce::Rectangle<float> fill (railX - 2.0f, track.getBottom() - track.getHeight() * meterNorm,
                                           6.0f, track.getHeight() * meterNorm);
        g.setColour (p.paper);
        g.fillRect (fill);
        hatch (g, fill, p.red, 4.0f, 2.6f);
    }

    if (peakDb > kFloorDb + 0.5f)
    {
        const float y = track.getBottom() - track.getHeight() * dbToNorm (peakDb);
        g.setColour (p.red);
        g.fillRect (track.getCentreX() - 6.0f, y - 1.0f, 12.0f, 2.0f);
    }

    // The handle: a hatched block with a centre line, drawn over the rail.
    const float handleY = track.getBottom() - track.getHeight() * normValue;
    const juce::Rectangle<float> handle (track.getCentreX() - 10.0f, handleY - 5.5f, 20.0f, 11.0f);
    g.setColour (p.paper);
    g.fillRect (handle);
    hatch (g, handle.reduced (1.0f), p.ink.withAlpha (0.5f), 4.0f, 1.0f);
    g.setColour (lineInk);
    g.drawRect (handle, 1.5f);
    g.drawLine (handle.getX(), handle.getCentreY(), handle.getRight(), handle.getCentreY(), 1.0f);

    g.setFont (theme::font (theme::Face::text, 10.5f));
    g.setColour (active ? p.bright : p.faded);
    const float db = param.convertFrom0to1 (param.getValue());
    const auto value = db <= kFloorDb + 0.05f
                           ? juce::String::fromUTF8 ("\xe2\x88\x92Inf dB")
                           : (db > 0.0f ? "+" : "") + juce::String (db, 1) + " dB";
    g.drawText (value,
                juce::Rectangle<float> (0.0f, (float) getHeight() - 14.0f, w, 13.0f),
                juce::Justification::centred, false);
}

void VerticalFader::mouseEnter (const juce::MouseEvent&)
{
    hovering = true;
    repaint();
}

void VerticalFader::mouseExit (const juce::MouseEvent&)
{
    hovering = false;
    if (! dragging)
        repaint();
}

void VerticalFader::mouseDown (const juce::MouseEvent& e)
{
    dragging = true;
    dragStartValue = normValue;
    dragStartY = e.position.y;
    attachment.beginGesture();
    applyDrag (e);
}

void VerticalFader::applyDrag (const juce::MouseEvent& e)
{
    const auto track = trackArea();
    float norm;

    if (e.mods.isShiftDown())
    {
        // Fine: relative to where the drag started, five times the throw.
        norm = dragStartValue + (dragStartY - e.position.y) / juce::jmax (1.0f, track.getHeight() * 5.0f);
    }
    else
    {
        norm = 1.0f - (e.position.y - track.getY()) / juce::jmax (1.0f, track.getHeight());
    }

    attachment.setValueAsPartOfGesture (param.convertFrom0to1 (juce::jlimit (0.0f, 1.0f, norm)));
}

void VerticalFader::mouseDrag (const juce::MouseEvent& e) { applyDrag (e); }

void VerticalFader::mouseUp (const juce::MouseEvent&)
{
    if (dragging)
    {
        dragging = false;
        attachment.endGesture();
    }
    repaint();
}

void VerticalFader::mouseDoubleClick (const juce::MouseEvent&)
{
    attachment.setValueAsCompleteGesture (param.convertFrom0to1 (param.getDefaultValue()));
}
