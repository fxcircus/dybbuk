#include "PresetHeader.h"

PresetHeader::PresetHeader (PresetManager& manager) : presets (manager)
{
    setMouseCursor (juce::MouseCursor::PointingHandCursor);
    shownName = presets.getCurrentName();
}

juce::Rectangle<float> PresetHeader::prevArrow() const
{
    return { 0.0f, 0.0f, 18.0f, (float) getHeight() };
}

juce::Rectangle<float> PresetHeader::nextArrow() const
{
    return { (float) getWidth() - 18.0f, 0.0f, 18.0f, (float) getHeight() };
}

void PresetHeader::tick()
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

void PresetHeader::paint (juce::Graphics& g)
{
    const auto& p = theme::palette();
    const auto b = getLocalBounds().toFloat();

    auto arrow = [&g, &p] (juce::Rectangle<float> area, bool pointsLeft, bool lit)
    {
        const auto c = area.getCentre();
        juce::Path path;
        if (pointsLeft)
        {
            path.startNewSubPath (c.x + 2.0f, c.y - 5.0f);
            path.lineTo (c.x + 2.0f, c.y + 5.0f);
            path.lineTo (c.x - 6.0f, c.y);
        }
        else
        {
            path.startNewSubPath (c.x - 2.0f, c.y - 5.0f);
            path.lineTo (c.x - 2.0f, c.y + 5.0f);
            path.lineTo (c.x + 6.0f, c.y);
        }
        path.closeSubPath();
        g.setColour (lit ? p.bright : p.ink);
        g.fillPath (path);
    };

    arrow (prevArrow(), true, hoverZone == 0);
    arrow (nextArrow(), false, hoverZone == 2);

    const juce::Rectangle<float> plate (22.0f, b.getCentreY() - 13.5f, b.getWidth() - 44.0f, 27.0f);
    g.setColour (p.ink.withAlpha (0.06f));
    g.fillRect (plate);

    auto name = shownName;
    if (shownDirty)
        name += " *";

    g.setFont (theme::font (theme::Face::script, 19.0f));
    g.setColour (hoverZone == 1 ? p.bright : p.ink);
    g.drawText (name, plate, juce::Justification::centred, true);
}

void PresetHeader::mouseMove (const juce::MouseEvent& e)
{
    const int zone = prevArrow().contains (e.position) ? 0
                                                       : (nextArrow().contains (e.position) ? 2 : 1);
    if (zone != hoverZone)
    {
        hoverZone = zone;
        repaint();
    }
}

void PresetHeader::mouseExit (const juce::MouseEvent&)
{
    hoverZone = -1;
    repaint();
}

void PresetHeader::mouseDown (const juce::MouseEvent& e)
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

void PresetHeader::showList()
{
    const auto list = presets.getPresets();
    juce::PopupMenu menu;
    const auto current = presets.getCurrentName();

    bool separated = false;
    for (int i = 0; i < list.size(); ++i)
    {
        const auto& info = list.getReference (i);
        if (! info.factory && ! separated)
        {
            menu.addSeparator();
            separated = true;
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
