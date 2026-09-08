#include "Lamp.h"

namespace
{
    constexpr float kEnvelopeRate = 0.05f;   // as the canvas has it
    constexpr float kFlickerDecay = 0.86f;
    constexpr float kRunawayMixRate = 0.15f;
    constexpr float kDipDecay = 0.12f;
    constexpr float kRepaintEps = 0.004f;
}

Lamp::Lamp()
{
    setInterceptsMouseClicks (false, false);
}

void Lamp::setAgitation (float agitate01, float speed01) noexcept
{
    agitation = juce::jlimit (0.0f, 1.0f, agitate01);
    speed = juce::jlimit (0.0f, 1.0f, speed01);
}

void Lamp::tick()
{
    // Amplitude and the odd guttering drop come from the canvas; what is being
    // measured is the engine's own loop energy rather than a guess from the
    // knob positions.
    // Chaos adds to both the depth of the flicker and the rate of the
    // guttering drops, so a loop that is driving itself LOOKS like it is:
    // the ember gets restless before the sound does anything you could name.
    const float amp = (runaway ? 0.09f : 0.038f)
                      * (0.45f + 0.55f * agitation + 0.9f * chaos) * (0.5f + speed);
    flicker = flicker * kFlickerDecay + (rng.nextFloat() - 0.5f) * amp;
    if (rng.nextFloat() < (runaway ? 0.02f : 0.006f) + 0.03f * chaos)
        flicker -= 0.35f;

    const float wanted = bypassed ? 0.03f : target;
    envelope += (wanted - envelope) * kEnvelopeRate;
    runawayMix += ((runaway ? 1.0f : 0.0f) - runawayMix) * kRunawayMixRate;
    dip = juce::jmax (0.0f, dip - kDipDecay);

    live = juce::jlimit (0.05f, 1.0f, envelope * (0.85f + flicker * 2.0f) * (1.0f - dip));

    if (std::abs (live - lastPainted) > kRepaintEps)
    {
        lastPainted = live;
        repaint();
    }
}

void Lamp::paint (juce::Graphics& g)
{
    const auto& p = theme::palette();
    const auto b = getLocalBounds().toFloat();
    const auto c = b.getCentre();

    // Sixteen rays, the fixture around the glass.
    g.setColour (p.ink.withAlpha (0.5f));
    for (int i = 0; i < 16; ++i)
    {
        const float a = juce::degreesToRadians (22.5f * (float) i);
        const juce::Point<float> dir (std::sin (a), -std::cos (a));
        g.drawLine ({ c + dir * 36.0f, c + dir * 42.0f }, 1.0f);
    }

    // The glow sits under the glass and grows with the loop.
    const float glowRadius = 24.0f + 30.0f * live;
    const float glowAlpha = ((runaway ? 0.35f : 0.22f) + 0.4f * live) * 0.5f;
    for (int ring = 3; ring >= 1; --ring)
    {
        const float r = glowRadius * (float) ring / 3.0f;
        g.setColour (p.red.withAlpha (glowAlpha / (float) (ring * 2)));
        g.fillEllipse (c.x - r, c.y - r, r * 2.0f, r * 2.0f);
    }

    g.setColour (p.ink);
    g.drawEllipse (c.x - 32.0f, c.y - 32.0f, 64.0f, 64.0f, 1.5f);

    // The glass itself: a hatched disc that swells and brightens.
    const float scale = 0.72f + 0.42f * live;
    const float r = 20.0f * scale;
    const float alpha = 0.55f + 0.45f * live;
    const juce::Rectangle<float> glass (c.x - r, c.y - r, r * 2.0f, r * 2.0f);

    {
        juce::Graphics::ScopedSaveState save (g);
        juce::Path clip;
        clip.addEllipse (glass);
        g.reduceClipRegion (clip);

        g.setColour (p.red.withAlpha (alpha));
        const float spacing = 5.0f;
        for (float d = -r * 2.0f; d < r * 4.0f; d += spacing)
        {
            g.drawLine (glass.getX() + d, glass.getY(), glass.getX() + d - r * 2.0f,
                        glass.getY() + r * 2.0f, 1.6f);
            g.drawLine (glass.getX() + d - r * 2.0f, glass.getY(), glass.getX() + d,
                        glass.getY() + r * 2.0f, 1.6f);
        }
    }

    g.setColour (p.red.withAlpha (juce::jmin (1.0f, alpha + 0.2f)));
    g.drawEllipse (glass, 1.0f);
}
