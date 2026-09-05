#include "Ember.h"

namespace
{
    constexpr float kUiHz = 30.0f;
    constexpr float kEmberAttack = 0.35f, kEmberRelease = 0.08f; // quick to light, slow to die
    constexpr float kFlickerBase = 0.06f, kFlickerHot = 0.10f, kFlickerSmooth = 0.30f;
    constexpr float kBreathHz = 0.25f, kBreathDepth = 0.03f;
    constexpr float kRunawayPulseHz = 1.2f, kRunawayPulseDepth = 0.08f, kRunawayMixRate = 0.15f;
    constexpr float kFlashDecay = 0.12f;
    constexpr float kEmberRepaintEps = 0.004f;
}

Ember::Ember()
{
    setInterceptsMouseClicks (false, false);
}

void Ember::tick()
{
    shown += (target - shown) * (target > shown ? kEmberAttack : kEmberRelease);

    n1 += (rng.nextFloat() * 2.0f - 1.0f - n1) * kFlickerSmooth;
    n2 += (n1 - n2) * kFlickerSmooth;

    phase += 1.0f / kUiHz;
    runawayMix += ((runaway ? 1.0f : 0.0f) - runawayMix) * kRunawayMixRate;
    flashDip = juce::jmax (0.0f, flashDip - kFlashDecay);

    const float flicker = n2 * (kFlickerBase + (kFlickerHot - kFlickerBase) * shown);
    const float breath = kBreathDepth
                         * std::sin (juce::MathConstants<float>::twoPi * kBreathHz * phase);
    const float pulse = runawayMix * kRunawayPulseDepth
                        * std::sin (juce::MathConstants<float>::twoPi * kRunawayPulseHz * phase);

    live = juce::jlimit (0.0f, 1.0f, shown * (1.0f + flicker + breath + pulse) * (1.0f - flashDip));

    if (std::abs (live - lastPainted) > kEmberRepaintEps)
    {
        lastPainted = live;
        repaint();
    }
}

void Ember::paint (juce::Graphics& g)
{
    const auto& p = theme::palette();
    const auto bounds = getLocalBounds().toFloat();
    const auto centre = bounds.getCentre();

    // Runaway shifts the coal from brass toward red rather than switching, so
    // the transition reads as heat rather than as a state change.
    const auto core = p.emberCore.interpolatedWith (p.runaway, runawayMix * 0.75f);
    const auto glow = p.emberGlow.interpolatedWith (p.runaway, runawayMix * 0.6f);

    // The ash bed: always visible, so an empty loop still looks like something
    // that could catch rather than a hole in the panel.
    g.setColour (p.outline.withAlpha (0.35f));
    g.drawEllipse (centre.x - 22.0f, centre.y - 22.0f, 44.0f, 44.0f, 1.0f);

    if (live > 0.002f)
    {
        // Outer haze, then a tighter glow, then the coal itself.
        const float haze = 22.0f + 36.0f * live;
        g.setColour (glow.withAlpha (0.10f * live));
        g.fillEllipse (centre.x - haze, centre.y - haze, haze * 2.0f, haze * 2.0f);

        const float inner = 14.0f + 16.0f * live;
        g.setColour (glow.withAlpha (0.30f * live));
        g.fillEllipse (centre.x - inner, centre.y - inner, inner * 2.0f, inner * 2.0f);

        const float coal = 5.0f + 9.0f * live;
        g.setColour (core.withAlpha (juce::jlimit (0.25f, 1.0f, 0.4f + 0.6f * live)));
        g.fillEllipse (centre.x - coal, centre.y - coal, coal * 2.0f, coal * 2.0f);
    }
    else
    {
        g.setColour (p.outline.withAlpha (0.5f));
        g.fillEllipse (centre.x - 4.0f, centre.y - 4.0f, 8.0f, 8.0f);
    }
}
