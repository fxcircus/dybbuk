#include "PresetBar.h"

PresetBar::PresetBar (PresetManager& manager) : presets (manager)
{
    setMouseCursor (juce::MouseCursor::PointingHandCursor);
    shownName = presets.getCurrentName();
}

juce::Rectangle<float> PresetBar::prevArrow() const
{
    return { 0.0f, 0.0f, 18.0f, (float) getHeight() };
}

juce::Rectangle<float> PresetBar::nextArrow() const
{
    return { (float) getWidth() - 18.0f, 0.0f, 18.0f, (float) getHeight() };
}

Readout PresetBar::readout() const
{
    Readout r;
    r.name = "preset";
    r.value = shownName;
    r.hint = "Click the name for the list, arrows to step through.";
    r.asideColour = theme::palette().faded;
    if (shownDirty)
    {
        r.aside = "edited";
        r.asideColour = theme::palette().accent;
    }
    return r;
}

void PresetBar::tick()
{
    const auto name = presets.getCurrentName();
    const bool dirty = presets.isDirty();
    if (name != shownName || dirty != shownDirty)
    {
        shownName = name;
        shownDirty = dirty;
        repaint();
    }
}

void PresetBar::paint (juce::Graphics& g)
{
    const auto& p = theme::palette();
    const auto bounds = getLocalBounds().toFloat();

    g.setColour (p.panel);
    g.fillRoundedRectangle (bounds, 3.0f);
    g.setColour (p.outline.withAlpha (0.5f));
    g.drawRoundedRectangle (bounds.reduced (0.5f), 3.0f, 1.0f);

    auto drawArrow = [&g, &p] (juce::Rectangle<float> area, bool pointsLeft, bool lit)
    {
        const auto c = area.getCentre();
        juce::Path path;
        const float w = 3.5f, h = 5.0f;
        if (pointsLeft)
        {
            path.startNewSubPath (c.x + w * 0.5f, c.y - h);
            path.lineTo (c.x - w * 0.5f, c.y);
            path.lineTo (c.x + w * 0.5f, c.y + h);
        }
        else
        {
            path.startNewSubPath (c.x - w * 0.5f, c.y - h);
            path.lineTo (c.x + w * 0.5f, c.y);
            path.lineTo (c.x - w * 0.5f, c.y + h);
        }
        g.setColour (lit ? p.ink : p.faded);
        g.strokePath (path, juce::PathStrokeType (1.4f, juce::PathStrokeType::curved,
                                                  juce::PathStrokeType::rounded));
    };

    drawArrow (prevArrow(), true, hoverZone == 0);
    drawArrow (nextArrow(), false, hoverZone == 2);

    auto name = shownName;
    if (shownDirty)
        name += " *";

    g.setFont (theme::font (12.0f));
    g.setColour (hoverZone == 1 ? p.ink : p.readout);
    g.drawText (name, bounds.reduced (20.0f, 0.0f), juce::Justification::centred, true);
}

void PresetBar::mouseEnter (const juce::MouseEvent& e)
{
    if (onHover)
        onHover (*this);
    mouseMove (e);
}

void PresetBar::mouseExit (const juce::MouseEvent&)
{
    hoverZone = -1;
    repaint();
}

void PresetBar::mouseMove (const juce::MouseEvent& e)
{
    const int zone = prevArrow().contains (e.position) ? 0
                                                       : (nextArrow().contains (e.position) ? 2 : 1);
    if (zone != hoverZone)
    {
        hoverZone = zone;
        repaint();
    }
}

void PresetBar::mouseDown (const juce::MouseEvent& e)
{
    if (prevArrow().contains (e.position))
        presets.stepPreset (-1);
    else if (nextArrow().contains (e.position))
        presets.stepPreset (1);
    else
        showList();

    tick();
    if (onPresetChanged)
        onPresetChanged();
}

void PresetBar::showList()
{
    const auto list = presets.getPresets();
    juce::PopupMenu menu;
    const auto current = presets.getCurrentName();

    bool addedSeparator = false;
    for (int i = 0; i < list.size(); ++i)
    {
        const auto& info = list.getReference (i);
        if (! info.factory && ! addedSeparator)
        {
            menu.addSeparator();
            addedSeparator = true;
        }
        menu.addItem (i + 1, info.name, true, info.name == current);
    }

    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this),
                        [this, list] (int result)
                        {
                            if (result <= 0 || result > list.size())
                                return;
                            presets.loadPreset (list.getReference (result - 1));
                            tick();
                            if (onPresetChanged)
                                onPresetChanged();
                        });
}
