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
    constexpr float kFrostRate = 0.12f;      // Freeze sets in over about a quarter second
    constexpr float kRepaintEps = 0.004f;

    // The ring. The ceiling eases into place; a new limb is most of the way
    // grown in a quarter second; the collapse after a clear is quick enough
    // to read as one gesture with the ember's dip.
    constexpr float kSlotEase = 0.3f;
    constexpr float kGrowEase = 0.32f;
    constexpr float kCollapseDecay = 0.85f;
    constexpr float kJitterPx = 1.6f;

    // Geometry for the 128 px box: housing ring, fixture rays.
    constexpr float kHousingR = 30.0f;
    constexpr float kRayInnerR = 34.0f, kRayOuterR = 40.0f;

    // The tentacles. They root in the white band inside the housing, so the
    // shoulder is seen pushing through the ring, and reach out toward the
    // old ring radius; a loud step reaches further, a spent one withers.
    constexpr float kTentacleRoot = kHousingR - 6.0f;
    constexpr float kTentacleMin = 11.0f, kTentacleMax = 24.0f;
    constexpr float kWaveAmp = 2.6f;
    constexpr float kWritheRate = 0.055f;            // radians per tick at rest
    constexpr int kSpineSegments = 12;

    // The bearings. A mode change eases over about half a second, slow
    // enough that the limbs are seen to stretch or split rather than swap.
    constexpr float kModeEase = 0.1f;
    constexpr float kTranceStretch = 0.3f;           // Trance: reach, and the wobble's width
    constexpr float kTranceSlow = 0.5f;              // ... and how much slower it writhes
    constexpr float kLegionFan = 0.75f;              // Legion: the side bulbs' angle off the heading, radians
    constexpr float kTrailAgePerTick = 0.6f;         // Wraith: a ghost is mostly gone four ticks on
    constexpr float kTrailAgePerFrame = 0.985f;      // ... and fades on its own once the pattern stops
    constexpr float kTrailDrift = 0.35f;             // ... drifting back a third of a slot as it goes
    constexpr float kTremorPx = 2.2f;                // Tremor: how far the ember shakes with the gate open
    constexpr float kTrembleAmp = 1.5f;              // Rattle: the tremble's width at a limb's tip, in px
    constexpr float kTrembleSounding = 1.2f;         // ... and how much more the sounding one has
    constexpr float kTrembleCycles = 4.5f;           // ... waves along a limb
    constexpr float kTrembleRate = 2.3f;             // ... radians per frame: a buzz, not a wave
    constexpr float kMirrorCurl = 0.55f;             // Mirror: how far the tip has turned back, radians
    constexpr float kMoteInnerR = 35.0f, kMoteOuterR = 60.0f;   // Miasma: the band the haze drifts in
    constexpr float kMoteBob = 2.5f;                 // ... how far a mote wanders in and out
    constexpr float kMiasmaThin = 0.35f;             // ... how translucent the limbs go

    // Slot i of the ceiling, clockwise from twelve o'clock over the whole
    // circle: a limb's place depends on the ceiling, not on how many there are.
    juce::Point<float> rayDir (float slot, float slots) noexcept
    {
        const float a = juce::MathConstants<float>::twoPi * slot / juce::jmax (1.0f, slots);
        return { std::sin (a), -std::cos (a) };
    }
}

Lamp::Lamp()
{
    setInterceptsMouseClicks (false, false);
    grow.fill (1.0f);

    // The haze is laid out once, all over the ring and at every pace, half
    // of it drifting against the clock, so it never settles into a pattern.
    juce::Random seed (0x4d1a5); // the same cloud in every window
    for (auto& m : motes)
    {
        m.angle = seed.nextFloat() * juce::MathConstants<float>::twoPi;
        m.radius = kMoteInnerR + (kMoteOuterR - kMoteInnerR) * seed.nextFloat();
        m.pace = (0.004f + 0.010f * seed.nextFloat()) * (seed.nextBool() ? 1.0f : -1.0f);
        m.bob = seed.nextFloat() * juce::MathConstants<float>::twoPi;
        m.size = 0.8f + 0.8f * seed.nextFloat();
    }
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

    // A step joined: it starts as nothing at its own slot and grows. (The
    // commit counter catches the joins this cannot: a replacement, or a
    // join the count showed a frame before the counter did.)
    if (newCount > count)
    {
        for (int i = count; i < newCount; ++i)
            grow[(size_t) i] = 0.0f;
        joinedThisFrame = true;
    }

    if (newCount != count || currentStep != current)
        ringDirty = true;
    count = newCount;
    current = currentStep;

    if (ticks != lastTicks)
    {
        lastTicks = ticks;
        pulse = 1.0f;
        ticked = true;
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
    // The first ceiling is where the limbs start, not somewhere to ease from.
    if (! ceilingSeen)
    {
        ceilingSeen = true;
        shownCeiling = (float) ceiling;
    }
}

void Lamp::setCommits (int commits) noexcept
{
    if (commits == lastCommits)
        return;
    lastCommits = commits;
    if (joinedThisFrame || count <= 0)
        return;

    // A join the count did not show: the newest limb grows all the same, and
    // if the ring was full it is the oldest that went, so the rest stand a
    // slot on from where they belong and slide back, as a ring buffer does.
    grow[(size_t) (count - 1)] = 0.0f;
    if (count >= ceiling)
        shift = 1.0f;
    ringDirty = true;
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
    {
        const float wantedFrost = frozen && ! bypassed ? 1.0f : 0.0f;
        const float before = frost;
        frost += (wantedFrost - frost) * kFrostRate;
        if (std::abs (frost - wantedFrost) < 0.005f)
            frost = wantedFrost;
        if (std::abs (frost - before) > 0.001f)
            ringDirty = true;
    }

    const float lit = bypassed ? 0.0f : 0.4f * pulse + 0.45f * flare;
    live = juce::jlimit (0.03f, 1.0f, (envelope * (0.9f + flicker) + lit) * (1.0f - dip));

    // The bearing crossfades; every weight moves, so the outgoing mode lets
    // go at the same pace the incoming one takes hold.
    for (int m = 0; m < kModeCount; ++m)
    {
        auto& mix = modeMix[(size_t) m];
        const float wantedMix = m == modeWanted ? 1.0f : 0.0f;
        if (mix == wantedMix)
            continue;
        mix += (wantedMix - mix) * kModeEase;
        if (std::abs (mix - wantedMix) < 0.005f)
            mix = wantedMix;
        ringDirty = true;
    }
    const float tranceMix = modeMix[(size_t) trance];
    const float wraithMix = modeMix[(size_t) wraith];
    const float tremorMix = modeMix[(size_t) tremor];
    const float rattleMix = modeMix[(size_t) rattle];
    const float mirrorMix = modeMix[(size_t) mirror];
    const float miasmaMix = modeMix[(size_t) miasma];

    // Wraith: on every tick the limb that has just started sounding leaves a
    // ghost of itself; the older ghosts step back a generation.
    if (ticked)
    {
        ticked = false;
        if (wraithMix > 0.01f && current >= 0 && current < count && ! bypassed)
        {
            for (auto& t : trails)
                t.age *= kTrailAgePerTick;
            auto& t = trails[(size_t) nextTrail];
            t.slot = current;
            t.slots = shownCeiling;
            t.level = level[(size_t) current];
            t.gain = gain[(size_t) current];
            t.writhe = writhe;
            t.age = 1.0f;
            nextTrail = (nextTrail + 1) % kTrails;
            ringDirty = true;
        }
    }
    for (auto& t : trails)
    {
        if (t.age <= 0.0f)
            continue;
        t.age = t.age * kTrailAgePerFrame - (wraithMix < 0.01f ? 0.05f : 0.0f);
        if (t.age < 0.02f)
            t.age = 0.0f;
        ringDirty = true;
    }

    // Tremor: a tight tremor, new every frame, hardest while the gate is open
    // and a smaller one on every tick; the sounding limb twitches with it.
    if (tremorMix > 0.01f && ! bypassed)
    {
        const float shake = tremorMix * (1.0f - frost) * juce::jmin (1.0f, flare + 0.5f * pulse);
        tremorX = (rng.nextFloat() - 0.5f) * 2.0f * kTremorPx * shake;
        tremorY = (rng.nextFloat() - 0.5f) * 2.0f * kTremorPx * shake;
        twitch = (rng.nextFloat() - 0.5f) * 2.0f * tremorMix * (1.0f - frost);
        if (count > 0 || shake > 0.01f)
            ringDirty = true;
    }
    else if (tremorX != 0.0f || tremorY != 0.0f || twitch != 0.0f)
    {
        tremorX = tremorY = twitch = 0.0f;
        ringDirty = true;
    }

    // Rattle: the tremble buzzes along every limb, its phase jumping most
    // of a cycle a frame so it reads as a vibration rather than a wave, and
    // the ember's hatching jitters a pixel with it. Still once frozen.
    if (rattleMix > 0.01f && ! bypassed && frost < 1.0f)
    {
        tremble += kTrembleRate;
        if (tremble > juce::MathConstants<float>::twoPi * 64.0f)
            tremble -= juce::MathConstants<float>::twoPi * 64.0f;
        hatchJitterX = (float) (rng.nextInt (3) - 1) * rattleMix;
        hatchJitterY = (float) (rng.nextInt (3) - 1) * rattleMix;
        ringDirty = true;
    }
    else if (hatchJitterX != 0.0f || hatchJitterY != 0.0f)
    {
        hatchJitterX = hatchJitterY = 0.0f;
        ringDirty = true;
    }

    // Miasma: the haze drifts, each mote at its own pace around the ring
    // and wandering in and out a little, never still while it shows.
    if (miasmaMix > 0.01f && ! bypassed)
    {
        const float still = 1.0f - 0.85f * frost; // the frost slows the cloud almost to a stop
        for (auto& m : motes)
        {
            m.angle += m.pace * still;
            if (m.angle > juce::MathConstants<float>::twoPi)
                m.angle -= juce::MathConstants<float>::twoPi;
            else if (m.angle < 0.0f)
                m.angle += juce::MathConstants<float>::twoPi;
            m.bob += 0.03f * still;
        }
        ringDirty = true;
    }

    // The ring's layout follows the Steps knob. Ease most of the way, then
    // land exactly so the limbs never sit a hair off their slots for want of
    // a last step; the same for the shift after a replacement.
    joinedThisFrame = false;
    const float wantedCeiling = (float) ceiling;
    if (std::abs (wantedCeiling - shownCeiling) > 0.002f)
    {
        shownCeiling += (wantedCeiling - shownCeiling) * kSlotEase;
        if (std::abs (wantedCeiling - shownCeiling) <= 0.002f)
            shownCeiling = wantedCeiling;
        ringDirty = true;
    }
    if (shift > 0.0f)
    {
        shift *= 1.0f - kSlotEase;
        if (shift <= 0.002f)
            shift = 0.0f;
        ringDirty = true;
    }

    // A limb that has just joined grows out of the housing.
    for (int i = 0; i < count; ++i)
    {
        auto& gw = grow[(size_t) i];
        if (gw >= 1.0f)
            continue;
        gw += (1.0f - gw) * kGrowEase;
        if (gw > 0.995f)
            gw = 1.0f;
        ringDirty = true;
    }

    if (warmth > 0.01f)
    {
        // A fill makes the tentacles thrash, a little more the deeper into it.
        for (auto& j : jitter)
            j = (rng.nextFloat() - 0.5f) * 2.0f * kJitterPx * warmth;
        ringDirty = true;
    }

    // The tentacles are never still while there are any: slow at rest,
    // quicker on the tick, thrashing during a fill, and frozen solid once
    // the frost has set.
    if ((count > 0 || collapse > 0.0f || (gate && ! bypassed)) && frost < 1.0f)
    {
        // Stretched (Trance), the limbs row slower as well as wider; reflected
        // (Mirror), they row the other way.
        writhe += kWritheRate * (1.0f + 1.5f * pulse + 3.0f * warmth) * (1.0f - frost)
                  * (1.0f - kTranceSlow * tranceMix) * (1.0f - 2.0f * mirrorMix);
        if (writhe > juce::MathConstants<float>::twoPi * 64.0f)
            writhe -= juce::MathConstants<float>::twoPi * 64.0f;
        else if (writhe < -juce::MathConstants<float>::twoPi * 64.0f)
            writhe += juce::MathConstants<float>::twoPi * 64.0f;
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

juce::Colour Lamp::emberColour (const theme::Palette& p) const noexcept
{
    // Frozen, the whole creature goes cold: the same blue as the button.
    return frost > 0.001f ? p.red.interpolatedWith (p.blue, frost) : p.red;
}

juce::Colour Lamp::pipColour (const theme::Palette& p) const noexcept
{
    // A fill warms the red toward amber for as long as it runs; not while frozen.
    const auto base = emberColour (p);
    const float warm = warmth * (1.0f - frost);
    return warm > 0.01f ? base.interpolatedWith (juce::Colour (0xffe6a23c), 0.5f * warm) : base;
}

void Lamp::paint (juce::Graphics& g)
{
    const auto& p = theme::palette();
    const auto b = getLocalBounds().toFloat();
    // The fixture is bolted down; the ember and its limbs shake with Tremor.
    const auto fixture = b.getCentre();
    const auto c = fixture + juce::Point<float> (tremorX, tremorY);
    const float dim = bypassed ? 0.35f : 1.0f;
    const bool listening = count == 0 && collapse <= 0.0f;
    const float tranceMix = modeMix[(size_t) trance];
    const float legionMix = modeMix[(size_t) legion];
    const float wraithMix = modeMix[(size_t) wraith];
    const float tremorMix = modeMix[(size_t) tremor];
    const float rattleMix = modeMix[(size_t) rattle];
    const float mirrorMix = modeMix[(size_t) mirror];
    const float miasmaMix = modeMix[(size_t) miasma];

    // The fixture: sixteen rays around the housing, fainter while there is
    // nothing to hold.
    g.setColour (p.ink.withAlpha ((listening ? 0.28f : 0.42f) * dim));
    for (int i = 0; i < 16; ++i)
    {
        const float a = juce::degreesToRadians (22.5f * (float) i);
        const juce::Point<float> dir (std::sin (a), -std::cos (a));
        g.drawLine ({ fixture + dir * kRayInnerR, fixture + dir * kRayOuterR }, 1.0f);
    }


    const auto ember = emberColour (p);

    // The glow sits under the glass and grows with the ember.
    const float glowRadius = 22.0f + 26.0f * live;
    const float glowAlpha = (0.22f + 0.4f * live) * 0.5f * dim;
    for (int ring = 3; ring >= 1; --ring)
    {
        const float r = glowRadius * (float) ring / 3.0f;
        g.setColour (ember.withAlpha (glowAlpha / (float) (ring * 2)));
        g.fillEllipse (c.x - r, c.y - r, r * 2.0f, r * 2.0f);
    }

    g.setColour (p.ink.withAlpha (dim));
    g.drawEllipse (fixture.x - kHousingR, fixture.y - kHousingR, kHousingR * 2.0f, kHousingR * 2.0f, 1.5f);

    // The glass itself: a hatched disc that swells and brightens. Seizing,
    // it also clenches and lets go a little with every frame.
    const float scale = 0.72f + 0.42f * live + 0.05f * tremorMix * twitch;
    const float r = 20.0f * scale;
    const float alpha = (0.55f + 0.45f * live) * dim;
    const juce::Rectangle<float> glass (c.x - r, c.y - r, r * 2.0f, r * 2.0f);

    {
        juce::Graphics::ScopedSaveState save (g);
        juce::Path clip;
        clip.addEllipse (glass);
        g.reduceClipRegion (clip);

        g.setColour (ember.withAlpha (alpha));
        const float spacing = 5.0f;
        // Rattling, the hatching jitters a pixel inside the glass; the
        // outline holds, so it is the light that buzzes, not the lamp.
        const float gx = glass.getX() + hatchJitterX, gy = glass.getY() + hatchJitterY;
        for (float d = -r * 2.0f; d < r * 4.0f; d += spacing)
        {
            g.drawLine (gx + d, gy, gx + d - r * 2.0f, gy + r * 2.0f, 1.6f);
            g.drawLine (gx + d - r * 2.0f, gy, gx + d, gy + r * 2.0f, 1.6f);
        }
    }

    g.setColour (ember.withAlpha (juce::jmin (1.0f, alpha + 0.2f)));
    g.drawEllipse (glass, 1.0f);

    // The tentacles. Each step is a limb growing out of the housing: its
    // reach says how loud the material is, its red says how much of it is
    // left, and it never quite holds still. The sounding step is the one
    // that lights up and lunges.
    const auto red = pipColour (p);
    auto drawTentacle = [&] (float slot, float slots, float lv, float gn, bool sounding, float fade,
                             float lengthScale, int phaseIndex, float writheAt, float growth)
    {
        // Seizing, the sounding limb jerks off its heading a little every frame.
        const float twist = sounding ? twitch * 0.12f * tremorMix : 0.0f;
        const auto ray = rayDir (slot, slots);
        const juce::Point<float> dir (ray.x * std::cos (twist) - ray.y * std::sin (twist),
                                      ray.x * std::sin (twist) + ray.y * std::cos (twist));
        const juce::Point<float> perp (-dir.y, dir.x);
        // In a trance, every limb is stretched: further out, and rowing wider.
        const float stretch = 1.0f + kTranceStretch * tranceMix;
        // A limb that is still joining is a shorter, thinner one with a
        // smaller bulb: it swells out of the housing rather than unfolding.
        const float reach = ((kTentacleMin + (kTentacleMax - kTentacleMin) * std::sqrt (juce::jlimit (0.0f, 1.0f, lv)))
                                 * (0.55f + 0.45f * gn) * lengthScale * stretch
                             + (sounding ? 4.0f * (0.6f + 0.4f * pulse) : 0.0f))
                            * growth;
        if (reach < 2.0f)
            return;

        // Each limb has its own phase and pace, so they do not row in unison.
        const float phase = (float) phaseIndex * 2.399f;
        const float pace = 0.7f + 0.5f * std::fmod ((float) phaseIndex * 0.618f, 1.0f);
        const float amp = (kWaveAmp + std::abs (jitter[(size_t) (phaseIndex % kMaxPips)]) * 2.0f
                           + (sounding ? 1.5f * pulse + 3.0f * tremorMix * std::abs (twitch) : 0.0f))
                          * (1.0f + 2.0f * kTranceStretch * tranceMix);
        // The bulb at the tip, and the neck that carries it: the neck is as
        // wide as the bulb's radius, so the limb swells into the ball rather
        // than touching it with a hair, and the two are one outline. Legion
        // shrinks the bulb, since it is about to be one of three.
        const float tipR = (2.2f + 1.7f * std::sqrt (juce::jlimit (0.0f, 1.0f, lv)) + (sounding ? 0.8f : 0.0f))
                           * (1.0f - 0.3f * legionMix) * (0.4f + 0.6f * growth);
        const float wRoot = (4.4f + 1.8f * lv + (sounding ? 0.8f : 0.0f)) * (0.6f + 0.4f * growth);
        const float wTip = tipR;

        // Rattling, a fine fast wave rides on the slow one, growing toward
        // the tip like it, hardest on the sounding limb.
        const float trembleAmp = rattleMix * kTrembleAmp * (sounding ? 1.0f + kTrembleSounding : 1.0f);
        // Reflected, the limb curls: its heading turns steadily along its
        // length, every limb the same way round, so the tip ends up facing
        // back toward the housing and the whole creature reads as spun.
        const float curl = kMirrorCurl * mirrorMix;

        juce::Point<float> spine[kSpineSegments + 1];
        float width[kSpineSegments + 1];
        for (int k = 0; k <= kSpineSegments; ++k)
        {
            const float t = (float) k / (float) kSpineSegments;
            // The wave grows toward the tip so the root stays anchored.
            const float wobble = amp * t * t * std::sin (juce::MathConstants<float>::twoPi * 1.15f * t + phase + writheAt * pace)
                                 + trembleAmp * t * std::sin (juce::MathConstants<float>::twoPi * kTrembleCycles * t + tremble + phase);
            // The curl bends the ray itself: the point at t sits where a
            // heading that has turned by curl * t * t by then would put it.
            const float turn = curl * t * t;
            const juce::Point<float> along (dir.x * std::cos (turn) - dir.y * std::sin (turn),
                                            dir.x * std::sin (turn) + dir.y * std::cos (turn));
            const juce::Point<float> across (-along.y, along.x);
            spine[k] = c + dir * kTentacleRoot + along * (reach * t) + across * wobble;
            width[k] = wRoot * (1.0f - t) + wTip * t;
        }

        // Where the neck meets the ball: the sides land on the circle and the
        // arc closes the front. Angles in JUCE's convention, clockwise from
        // twelve, with the limb's own heading measured the same way.
        const auto tip = spine[kSpineSegments];
        const auto tipDir = (spine[kSpineSegments] - spine[kSpineSegments - 1]);
        const float heading = std::atan2 (tipDir.x, -tipDir.y);
        const float neckAngle = std::asin (juce::jlimit (0.0f, 0.95f, (wTip * 0.5f) / tipR));
        const float back = juce::MathConstants<float>::pi - neckAngle;
        const juce::Point<float> tipPerp (-tipDir.y, tipDir.x);
        const float tipLen = juce::jmax (1.0e-3f, std::hypot (tipDir.x, tipDir.y));
        const auto unitDir = tipDir / tipLen;
        const auto unitPerp = tipPerp / tipLen;
        const auto joinPlus = tip - unitDir * (tipR * std::cos (neckAngle)) + unitPerp * (tipR * std::sin (neckAngle));
        const auto joinMinus = tip - unitDir * (tipR * std::cos (neckAngle)) - unitPerp * (tipR * std::sin (neckAngle));

        // The root is closed the same way, with a half circle about the root
        // point turned inward, so the limb has a rounded shoulder that pushes
        // out through the housing line rather than a flat cut on it.
        const float rootHeading = std::atan2 (dir.x, -dir.y);
        const float rootR = width[0] * 0.5f;

        juce::Path limb;
        limb.startNewSubPath (spine[0] + perp * rootR);
        for (int k = 1; k < kSpineSegments; ++k)
            limb.lineTo (spine[k] + perp * (width[k] * 0.5f));
        limb.lineTo (joinPlus);
        limb.addCentredArc (tip.x, tip.y, tipR, tipR, 0.0f, heading + back, heading - back, false);
        limb.lineTo (joinMinus);
        for (int k = kSpineSegments - 1; k >= 0; --k)
            limb.lineTo (spine[k] - perp * (width[k] * 0.5f));
        limb.addCentredArc (spine[0].x, spine[0].y, rootR, rootR, 0.0f,
                            rootHeading - juce::MathConstants<float>::halfPi,
                            rootHeading - 3.0f * juce::MathConstants<float>::halfPi, false);
        limb.closeSubPath();

        // The limb sits ON the housing, whatever its fade: an opaque paper
        // underlay first, so a spent, translucent limb never shows the ring
        // through itself.
        g.setColour (p.paper.withAlpha (fade));
        g.fillPath (limb);

        const auto fill = sounding ? red.withAlpha (fade * dim)
                                   : red.withAlpha ((0.45f + 0.55f * lv) * gn * fade * dim);
        if (sounding)
        {
            g.setColour (red.withAlpha (0.3f * fade * dim));
            g.strokePath (limb, juce::PathStrokeType (3.0f));
        }
        g.setColour (fill);
        g.fillPath (limb);

        // The ink outline fades with the step but never below what a stroke
        // on the plate needs: a spent limb is a hollow, withered one.
        const auto outline = p.ink.withAlpha ((0.45f + 0.55f * gn) * fade * dim);
        g.setColour (outline);
        g.strokePath (limb, juce::PathStrokeType (sounding ? 1.1f : 0.9f));

        // Legion: two more bulbs on short stalks either side of the tip, so
        // the limb ends in a fan of three. They grow out of the bulb as the
        // mode takes hold, so a switch is seen as a splitting.
        if (legionMix > 0.01f)
        {
            const float bulbR = tipR * (0.6f + 0.4f * (1.0f - legionMix));
            const float stalk = legionMix * tipR * 2.3f;
            const auto base = tip - unitDir * (tipR * 0.35f);
            for (const float sign : { -1.0f, 1.0f })
            {
                const float a = sign * kLegionFan;
                const juce::Point<float> fan (unitDir.x * std::cos (a) - unitDir.y * std::sin (a),
                                              unitDir.x * std::sin (a) + unitDir.y * std::cos (a));
                const auto end = base + fan * stalk;
                const juce::Rectangle<float> bulb (end.x - bulbR, end.y - bulbR, bulbR * 2.0f, bulbR * 2.0f);
                g.setColour (outline);
                g.drawLine ({ base, end }, bulbR + 1.6f);
                g.setColour (p.paper.withAlpha (fade));
                g.drawLine ({ base, end }, bulbR);
                g.fillEllipse (bulb);
                g.setColour (fill);
                g.drawLine ({ base, end }, bulbR);
                g.fillEllipse (bulb);
                g.setColour (outline);
                g.drawEllipse (bulb, sounding ? 1.1f : 0.9f);
            }
        }
    };

    // Wraith: the ghosts go under the living limbs. Each is the limb as it
    // lunged, a little further out than the limb now stands, drifting back
    // against the clock and fading as it ages, its wobble stopped where it
    // was: the sound left behind at the step.
    if (wraithMix > 0.01f)
        for (const auto& t : trails)
            if (t.age > 0.0f && t.slot >= 0)
                drawTentacle ((float) t.slot - kTrailDrift * (1.0f - t.age), t.slots, t.level, t.gain, false,
                              0.55f * t.age * wraithMix, 1.2f + 0.1f * (1.0f - t.age), t.slot + 3, t.writhe, 1.0f);

    // Each limb at its own slot of the ceiling. After a replacement the older
    // ones are still sliding back from a slot on; the newest grows in place.
    // In a miasma the limbs thin to a cloud: the ring shows through them.
    const float limbFade = 1.0f - kMiasmaThin * miasmaMix;
    for (int i = 0; i < count; ++i)
    {
        const auto idx = (size_t) i;
        const float slot = (float) i + (i < count - 1 ? shift : 0.0f);
        drawTentacle (slot, shownCeiling, level[idx], gain[idx], i == current, limbFade, 1.0f, i, writhe, grow[idx]);
    }

    // The limb being written: a nub pushing out of the housing at the slot
    // the new step will take (the last one's, if it is full and replacing),
    // growing with the gate's flare.
    const bool writing = gate && ! bypassed && (count < ceiling || ! hold);
    if (writing)
    {
        const int slot = count < ceiling ? count : count - 1;
        drawTentacle ((float) slot, shownCeiling, 0.5f, 0.6f, false, 0.35f + 0.65f * flare, 0.25f + 0.55f * flare,
                      slot + 7, writhe, 1.0f);
    }

    // The limbs as they were, drawn back into the ember after a clear.
    if (collapse > 0.0f && ghostCount > 0)
        for (int i = 0; i < ghostCount; ++i)
            drawTentacle ((float) i, shownCeiling, ghostLevel[(size_t) i], ghostGain[(size_t) i], false,
                          collapse, collapse, i, writhe, 1.0f);

    // Miasma: the haze, over everything, in the faded ink at low alpha: a
    // cloud of grains hanging about the ring, thickest about the limb that
    // is sounding, since that is where the grains are coming from.
    if (miasmaMix > 0.01f)
    {
        const bool haveSounding = current >= 0 && current < count;
        const float soundingAngle = haveSounding
                                        ? juce::MathConstants<float>::twoPi * ((float) current + (current < count - 1 ? shift : 0.0f))
                                              / juce::jmax (1.0f, shownCeiling)
                                        : 0.0f;
        for (const auto& m : motes)
        {
            float near = 0.45f;
            if (haveSounding)
            {
                const float cosD = std::cos (m.angle - soundingAngle);
                near = 0.25f + 0.75f * juce::jmax (0.0f, cosD) * juce::jmax (0.0f, cosD);
            }
            const float rr = m.radius + kMoteBob * std::sin (m.bob);
            const juce::Point<float> at (fixture.x + std::sin (m.angle) * rr, fixture.y - std::cos (m.angle) * rr);
            const float sz = m.size * (1.0f + 0.3f * pulse * near);
            g.setColour (p.faded.withAlpha ((0.22f + 0.5f * near) * miasmaMix * dim));
            g.fillEllipse (at.x - sz, at.y - sz, sz * 2.0f, sz * 2.0f);
        }
    }
}
