#include "ModeToggle.h"

namespace
{
    // Teder's thumb: 45 degree ruling at the spacing its sheet uses, which
    // is a touch finer than the fader's so the word over it stays legible.
    constexpr float kHatchSpacing = 4.5f;
    constexpr float kHatchThickness = 0.9f;
    constexpr float kGlide = 0.4f; // of the remaining distance per tick

    void hatch (juce::Graphics& g, juce::Rectangle<float> area, juce::Colour colour)
    {
        juce::Graphics::ScopedSaveState save (g);
        g.reduceClipRegion (area.getSmallestIntegerContainer());
        g.setColour (colour);
        const float span = area.getWidth() + area.getHeight();
        for (float d = 0.0f; d < span; d += kHatchSpacing)
            g.drawLine (area.getX() + d, area.getY(), area.getX(), area.getY() + d, kHatchThickness);
    }
}

ModeToggle::ModeToggle (juce::RangedAudioParameter& parameterToUse, juce::StringArray optionLabels)
    : param (parameterToUse),
      attachment (parameterToUse,
                  [this] (float newValue)
                  {
                      selected = juce::roundToInt (param.convertTo0to1 (newValue)
                                                   * (float) (labels.size() - 1));
                      repaint();
                  }),
      labels (std::move (optionLabels))
{
    attachment.sendInitialUpdate();
    highlight = (float) selected;
    setMouseCursor (juce::MouseCursor::PointingHandCursor);
}

void ModeToggle::tick()
{
    const auto target = (float) selected;
    if (std::abs (target - highlight) < 0.01f)
    {
        if (highlight != target)
        {
            highlight = target; // land exactly, so the thumb never sits a hair off its cell
            repaint();
        }
        return;
    }
    highlight += (target - highlight) * kGlide;
    repaint();
}

void ModeToggle::paint (juce::Graphics& g)
{
    const auto& p = theme::palette();
    const float w = (float) getWidth();
    const float h = (float) getHeight();
    const int n = labels.size();
    const float segW = w / (float) juce::jmax (1, n);
    const juce::Rectangle<float> bar (0.6f, 0.6f, w - 1.2f, h - 1.2f);
    const auto lineInk = hovering ? p.bright : p.ink;

    // The bar and its cell dividers.
    g.setColour (p.paper);
    g.fillRoundedRectangle (bar, 4.0f);
    g.setColour (lineInk);
    g.drawRoundedRectangle (bar, 4.0f, 1.2f);
    g.setColour (p.ink.withAlpha (0.35f));
    for (int i = 1; i < n; ++i)
        g.fillRect (segW * (float) i, 4.0f, 1.0f, h - 8.0f);

    // The selected cell: the hatched thumb, gliding.
    const juce::Rectangle<float> sel (segW * highlight + 2.5f, 3.0f, segW - 5.0f, h - 6.0f);
    hatch (g, sel, p.ink.withAlpha (0.22f));
    g.setColour (lineInk);
    g.drawRoundedRectangle (sel, 3.0f, 1.1f);

    // Labels live INSIDE their cells, the active one in ink, the rest faded.
    for (int i = 0; i < n; ++i)
        theme::drawTracked (g, labels[i], { segW * (float) i, 0.0f, segW, h },
                            juce::Justification::centred, theme::Face::semibold, 8.5f, 0.12f,
                            i == selected ? p.ink : p.faded);
}

void ModeToggle::setIndexFromX (float x)
{
    const int n = labels.size();
    const float segW = (float) getWidth() / (float) juce::jmax (1, n);
    const int nearest = juce::jlimit (0, n - 1, (int) (x / segW));
    if (nearest != selected)
        attachment.setValueAsCompleteGesture (param.convertFrom0to1 ((float) nearest / (float) (n - 1)));
}

void ModeToggle::mouseDown (const juce::MouseEvent& e) { setIndexFromX (e.position.x); }
void ModeToggle::mouseDrag (const juce::MouseEvent& e) { setIndexFromX (e.position.x); }
