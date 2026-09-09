#include "Lamp.h"

namespace
{
    // The ember. The envelope rate is quicker than the old lamp's so the
    // breathing while listening actually shows; the flicker is the canvas's
    // guttering, kept small so the tick pulse reads over it.
    constexpr float kEnvelopeRate = 0.12f;
    constexpr float kFlickerDecay = 0.86f;
    constexpr float kFlickerAmp = 0.03f;
    constexpr float kPulseDecay = 0.78f;     // ~150 ms at 30 Hz: a fast attack and a short tail
    constexpr float kFlareRise = 0.5f, kFlareDecay = 0.8f;
    constexpr float kDipDecay = 0.12f;
    constexpr float kBreathPeriodTicks = 2.5f * 30.0f;
    constexpr float kWarmthRate = 0.2f;
    constexpr float kRepaintEps = 0.004f;

    // The ring. Slots ease into place; the collapse after a clear is quick
    // enough to read as one gesture with the ember's dip.
    constexpr float kSlotEase = 0.3f;
    constexpr float kCollapseDecay = 0.85f;
    constexpr float kJitterPx = 1.6f;

    // Geometry for the 128 px box: housing ring, fixture rays, the pip ring.
    constexpr float kHousingR = 30.0f;
    constexpr float kRayInnerR = 34.0f, kRayOuterR = 40.0f;
    constexpr float kRingR = 50.0f;

    juce::Point<float> onRing (juce::Point<float> c, float radius, float slot, float slots) noexcept
    {
        // Clockwise from twelve o'clock, over the whole circle.
        const float a = juce::MathConstants<float>::twoPi * slot / juce::jmax (1.0f, slots);
        return { c.x + radius * std::sin (a), c.y - radius * std::cos (a) };
    }

    float pipRadius (float level01, bool sounding) noexcept
    {
        // Size follows the peak of the material on a square root, so a quiet
        // step is still a visible pip and a loud one does not swamp the ring.
        return 2.8f + 3.0f * std::sqrt (juce::jlimit (0.0f, 1.0f, level01)) + (sounding ? 1.6f : 0.0f);
    }
}

Lamp::Lamp()
{
    setInterceptsMouseClicks (false, false);
}

void Lamp::setPattern (int stepCount, int currentStep, int ticks) noexcept
{
    const int newCount = juce::jlimit (0, kMaxPips, stepCount);

    // The ring emptied: whatever did it (a clear, a fade to nothing), the pips
    // that were there fall inward. Captured here rather than in flash() so the
    // order the editor polls things in cannot lose the picture.
    if (newCount == 0 && count > 0)
    {
        ghostCount = count;
        ghostLevel = level;
        ghostGain = gain;
        collapse = 1.0f;
    }

    if (newCount != count || currentStep != current)
        ringDirty = true;
    count = newCount;
    current = currentStep;

    if (ticks != lastTicks)
    {
        lastTicks = ticks;
        pulse = 1.0f;
    }
}

void Lamp::setStep (int index, float level01, float gain01) noexcept
{
    if (index < 0 || index >= kMaxPips)
        return;
    const float l = juce::jlimit (0.0f, 1.0f, level01);
    const float gn = juce::jlimit (0.0f, 1.0f, gain01);
    const auto i = (size_t) index;
    if (std::abs (l - level[i]) > kRepaintEps || std::abs (gn - gain[i]) > kRepaintEps)
        ringDirty = true;
    level[i] = l;
    gain[i] = gn;
}

void Lamp::setCeiling (int maxSteps, bool holdWhenFull) noexcept
{
    ceiling = juce::jlimit (1, kMaxPips, maxSteps);
    hold = holdWhenFull;
}

void Lamp::flash() noexcept
{
    dip = 1.0f;
}

void Lamp::tick()
{
    // What the ember wants to be. Listening, it breathes; playing, it sits
    // on the sounding step's level and jumps on every tick; the gate on top
    // of either is a flare.
    float wanted;
    if (bypassed)
    {
        wanted = 0.03f;
    }
    else if (count == 0 && ! gate)
    {
        breath += juce::MathConstants<float>::twoPi / kBreathPeriodTicks;
        if (breath > juce::MathConstants<float>::twoPi)
            breath -= juce::MathConstants<float>::twoPi;
        wanted = 0.10f + 0.08f * (0.5f + 0.5f * std::sin (breath));
    }
    else
    {
        const float sounding = current >= 0 && current < count
                                   ? level[(size_t) current] * gain[(size_t) current]
                                   : 0.0f;
        wanted = 0.28f + 0.32f * sounding;
    }

    flicker = flicker * kFlickerDecay + (rng.nextFloat() - 0.5f) * kFlickerAmp;
    envelope += (wanted - envelope) * kEnvelopeRate;
    pulse *= kPulseDecay;
    flare = gate && ! bypassed ? flare + (1.0f - flare) * kFlareRise : flare * kFlareDecay;
    dip = juce::jmax (0.0f, dip - kDipDecay);
    warmth += ((fill && ! bypassed ? 1.0f : 0.0f) - warmth) * kWarmthRate;

    const float lit = bypassed ? 0.0f : 0.4f * pulse + 0.45f * flare;
    live = juce::jlimit (0.03f, 1.0f, (envelope * (0.9f + flicker) + lit) * (1.0f - dip));

    // The ring's layout: one more slot while a step is being written, unless
    // the pattern is full and holding, in which case nothing will be added.
    const bool writing = gate && ! bypassed && (count < ceiling || ! hold);
    const float wantedSlots = (float) juce::jmax (1, count + (writing && count < ceiling ? 1 : 0));
    if (std::abs (wantedSlots - shownSlots) > 0.002f)
    {
        // Ease most of the way, then land exactly so the pips never sit a
        // hair off their slots for want of a last step.
        shownSlots += (wantedSlots - shownSlots) * kSlotEase;
        if (std::abs (wantedSlots - shownSlots) <= 0.002f)
            shownSlots = wantedSlots;
        ringDirty = true;
    }

    if (warmth > 0.01f)
    {
        // A fill shakes the pips, a little more the deeper into it we are.
        for (auto& j : jitter)
            j = (rng.nextFloat() - 0.5f) * 2.0f * kJitterPx * warmth;
        ringDirty = true;
    }

    if (collapse > 0.0f)
    {
        collapse *= kCollapseDecay;
        if (collapse < 0.02f)
            collapse = 0.0f;
        ringDirty = true;
    }

    if (ringDirty || std::abs (live - lastPainted) > kRepaintEps)
    {
        lastPainted = live;
        ringDirty = false;
        repaint();
    }
}

juce::Colour Lamp::pipColour (const theme::Palette& p) const noexcept
{
    // A fill warms the red toward amber for as long as it runs.
    return warmth > 0.01f ? p.red.interpolatedWith (juce::Colour (0xffe6a23c), 0.5f * warmth) : p.red;
}

void Lamp::paint (juce::Graphics& g)
{
    const auto& p = theme::palette();
    const auto b = getLocalBounds().toFloat();
    const auto c = b.getCentre();
    const float dim = bypassed ? 0.35f : 1.0f;
    const bool listening = count == 0 && collapse <= 0.0f;

    // The fixture: sixteen rays around the housing, fainter while there is
    // nothing to hold.
    g.setColour (p.ink.withAlpha ((listening ? 0.28f : 0.42f) * dim));
    for (int i = 0; i < 16; ++i)
    {
        const float a = juce::degreesToRadians (22.5f * (float) i);
        const juce::Point<float> dir (std::sin (a), -std::cos (a));
        g.drawLine ({ c + dir * kRayInnerR, c + dir * kRayOuterR }, 1.0f);
    }

    // The guide circle the pips sit on: an engraved hairline, there even
    // when the ring is empty so the listening state has a shape.
    g.setColour (p.ink.withAlpha ((listening ? 0.16f : 0.22f) * dim));
    g.drawEllipse (c.x - kRingR, c.y - kRingR, kRingR * 2.0f, kRingR * 2.0f, 0.8f);

    // The glow sits under the glass and grows with the ember.
    const float glowRadius = 22.0f + 26.0f * live;
    const float glowAlpha = (0.22f + 0.4f * live) * 0.5f * dim;
    for (int ring = 3; ring >= 1; --ring)
    {
        const float r = glowRadius * (float) ring / 3.0f;
        g.setColour (p.red.withAlpha (glowAlpha / (float) (ring * 2)));
        g.fillEllipse (c.x - r, c.y - r, r * 2.0f, r * 2.0f);
    }

    g.setColour (p.ink.withAlpha (dim));
    g.drawEllipse (c.x - kHousingR, c.y - kHousingR, kHousingR * 2.0f, kHousingR * 2.0f, 1.5f);

    // The glass itself: a hatched disc that swells and brightens.
    const float scale = 0.72f + 0.42f * live;
    const float r = 20.0f * scale;
    const float alpha = (0.55f + 0.45f * live) * dim;
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

    // The pips. Each is an engraved ring with red inside it: the ink says the
    // slot is taken, the red says how much is left of what was put there.
    const auto red = pipColour (p);
    auto drawPip = [&] (juce::Point<float> at, float lv, float gn, bool sounding, float fade)
    {
        const float pr = pipRadius (lv, sounding);
        const juce::Rectangle<float> disc (at.x - pr, at.y - pr, pr * 2.0f, pr * 2.0f);

        if (sounding)
        {
            // A halo, so the sounding step reads from across the room.
            g.setColour (red.withAlpha (0.35f * fade * dim));
            g.drawEllipse (disc.expanded (3.0f), 1.0f);
            g.setColour (red.withAlpha (fade * dim));
        }
        else
        {
            // Size already says how loud; the red says how much is left.
            g.setColour (red.withAlpha ((0.55f + 0.45f * lv) * gn * fade * dim));
        }
        g.fillEllipse (disc);

        // The ink outline fades with the step, but never below what a stroke
        // on the plate needs to be seen: a nearly spent step is a hollow pip.
        g.setColour (p.ink.withAlpha ((0.4f + 0.6f * gn) * fade * dim));
        g.drawEllipse (disc, sounding ? 1.2f : 1.0f);
    };

    for (int i = 0; i < count; ++i)
    {
        const auto idx = (size_t) i;
        auto at = onRing (c, kRingR + jitter[idx], (float) i, shownSlots);
        drawPip (at, level[idx], gain[idx], i == current, 1.0f);
    }

    // The pip being written: an outline at the slot the new step will take,
    // with the gate's own flare inside it.
    const bool writing = gate && ! bypassed && (count < ceiling || ! hold);
    if (writing)
    {
        const int slot = count < ceiling ? count : 0;
        const auto at = onRing (c, kRingR, (float) slot, shownSlots);
        const float wr = pipRadius (0.5f, false) + 1.5f;
        g.setColour (red.withAlpha (0.5f * flare));
        g.fillEllipse (at.x - wr * 0.6f, at.y - wr * 0.6f, wr * 1.2f, wr * 1.2f);
        g.setColour (p.ink.withAlpha (0.9f));
        g.drawEllipse (at.x - wr, at.y - wr, wr * 2.0f, wr * 2.0f, 1.0f);
    }

    // The ring as it was, falling into the ember after a clear.
    if (collapse > 0.0f && ghostCount > 0)
    {
        const float ghostR = kRingR * std::sqrt (collapse);
        for (int i = 0; i < ghostCount; ++i)
        {
            const auto idx = (size_t) i;
            const auto at = onRing (c, ghostR, (float) i, (float) ghostCount);
            drawPip (at, ghostLevel[idx], ghostGain[idx], false, collapse);
        }
    }
}
