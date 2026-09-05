#include "OutSlider.h"

namespace
{
    constexpr float kMeterFloorDb = -60.0f;
    constexpr float kMeterFallDbPerTick = 24.0f / 30.0f; // 24 dB per second at 30 Hz
    constexpr float kPeakFallDbPerTick = 12.0f / 30.0f;
    constexpr float kPeakHoldTicks = 45.0f;  // 1.5 s
    constexpr float kClipHoldTicks = 30.0f;
    constexpr float kMeterRepaintDb = 0.2f;

    float dbToNorm (float db) { return juce::jlimit (0.0f, 1.0f, (db - kMeterFloorDb) / (6.0f - kMeterFloorDb)); }
}

OutSlider::OutSlider (juce::RangedAudioParameter& parameterToUse, juce::String hint)
    : param (parameterToUse),
      attachment (parameterToUse,
                  [this] (float newValue)
                  {
                      normValue = param.convertTo0to1 (newValue);
                      repaint();
                  }),
      hintText (std::move (hint))
{
    attachment.sendInitialUpdate();
    setMouseCursor (juce::MouseCursor::LeftRightResizeCursor);
}

Readout OutSlider::readout() const
{
    Readout r;
    r.name = "out";
    r.value = param.getText (param.getValue(), 32);
    if (! r.value.containsAnyOf ("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ"))
        r.value += " dB";
    r.hint = hintText;
    r.asideColour = theme::palette().faded;

    if (clipHold > 0.0f)
    {
        r.aside = "clip";
        r.asideColour = theme::palette().runaway;
    }
    else if (peakHoldDb > kMeterFloorDb + 0.5f)
    {
        r.aside = "peak " + juce::String (peakHoldDb, 1) + " dB";
    }
    return r;
}

void OutSlider::setPeak (float linearPeak) noexcept
{
    const float db = juce::Decibels::gainToDecibels (linearPeak, kMeterFloorDb);
    if (db > meterDb)
        meterDb = db;
    if (db > peakHoldDb)
    {
        peakHoldDb = db;
        clipHold = linearPeak >= 1.0f ? kClipHoldTicks : clipHold;
    }
}

void OutSlider::tick()
{
    const float before = meterDb;
    meterDb = juce::jmax (kMeterFloorDb, meterDb - kMeterFallDbPerTick);

    static_cast<void> (kPeakHoldTicks);
    peakHoldDb = juce::jmax (kMeterFloorDb, peakHoldDb - kPeakFallDbPerTick);
    clipHold = juce::jmax (0.0f, clipHold - 1.0f);

    if (std::abs (before - meterDb) > kMeterRepaintDb)
        repaint();
}

void OutSlider::paint (juce::Graphics& g)
{
    const auto& p = theme::palette();
    const float x = railX(), w = railWidth();
    const float y = 15.0f, h = 10.0f;

    theme::drawCaps (g, "out", { 0.0f, y - 3.0f, 36.0f, 16.0f }, juce::Justification::centredLeft,
                     10.0f, theme::Weight::semibold, 0.12f, hovering || dragging ? p.ink : p.faded);

    // Meter behind the rail.
    g.setColour (p.panel);
    g.fillRoundedRectangle (x, y, w, h, 2.0f);

    const float meterNorm = dbToNorm (meterDb);
    if (meterNorm > 0.001f)
    {
        g.setColour (p.meterFill.withAlpha (0.75f));
        g.fillRoundedRectangle (x, y, w * meterNorm, h, 2.0f);
    }

    if (peakHoldDb > kMeterFloorDb + 0.5f)
    {
        const float px = x + w * dbToNorm (peakHoldDb);
        g.setColour (clipHold > 0.0f ? p.runaway : p.meterPeak);
        g.fillRect (px - 1.0f, y, 2.0f, h);
    }

    g.setColour (p.outline.withAlpha (0.6f));
    g.drawRoundedRectangle (x, y, w, h, 2.0f, 1.0f);

    // The handle sits on the same rail: what you set, over what you get.
    const float handleX = x + w * normValue;
    g.setColour (hovering || dragging ? p.ink : p.knobPointer);
    g.fillRoundedRectangle (handleX - 3.0f, y - 5.0f, 6.0f, h + 10.0f, 2.0f);

    const auto r = readout();
    g.setFont (theme::font (12.0f));
    g.setColour (p.readout);
    g.drawText (r.value, juce::Rectangle<float> (x + w + 8.0f, y - 5.0f, 52.0f, h + 10.0f),
                juce::Justification::centredLeft, false);
}

void OutSlider::mouseEnter (const juce::MouseEvent&)
{
    hovering = true;
    if (onHover)
        onHover (*this);
    repaint();
}

void OutSlider::mouseExit (const juce::MouseEvent&)
{
    hovering = false;
    if (! dragging)
        repaint();
}

void OutSlider::mouseDown (const juce::MouseEvent& e)
{
    dragging = true;
    attachment.beginGesture();
    if (onHover)
        onHover (*this);
    mouseDrag (e);
}

void OutSlider::mouseDrag (const juce::MouseEvent& e)
{
    const float norm = juce::jlimit (0.0f, 1.0f, (e.position.x - railX()) / juce::jmax (1.0f, railWidth()));
    attachment.setValueAsPartOfGesture (param.convertFrom0to1 (norm));
}

void OutSlider::mouseUp (const juce::MouseEvent&)
{
    if (dragging)
    {
        dragging = false;
        attachment.endGesture();
    }
    repaint();
}

void OutSlider::mouseDoubleClick (const juce::MouseEvent&)
{
    attachment.setValueAsCompleteGesture (param.convertFrom0to1 (param.getDefaultValue()));
}
