#include "Randomiser.h"

#include "../Parameters.h"
#include "../dsp/BurstEngine.h"

namespace
{
    namespace id = params::id;

    struct Span
    {
        float lo, hi;
        bool fixed() const noexcept { return hi <= lo; }
    };

    // One coherent region of the space, with a name. The ranges are deliberately
    // narrower than the parameters themselves: the dice is a starting point, not
    // a tour of the extremes. Threshold in particular stays between -40 and
    // -20 dB, where a picked note at -6 dBFS is always heard and its tail is
    // always let go -- the ends of that knob are for the player to find.
    struct Character
    {
        const char* name;
        Span stepSeconds;   // log-uniform
        Span steps;         // integer, inclusive
        Span threshold, blend, fills, chaos, length, fade;
        float direction[BurstEngine::kDirectionCount]; // relative weights, Forward..Drunk
    };

    const Character kCharacters[] = {
        // Behaves. A steady pattern of the last few things you played, mostly
        // forwards, with a fill now and then when you play over it.
        { "Steady",
          { 0.120f, 0.500f }, { 4.0f, 8.0f },
          { -40.0f, -20.0f }, { 40.0f, 65.0f }, { 0.0f, 25.0f }, { 0.0f, 10.0f },
          { 70.0f, 100.0f },  { 0.0f, 10.0f },
          { 0.70f, 0.10f, 0.20f, 0.00f, 0.00f } },

        // Short steps, choked, with chaos doing the ratchets: a stutter edit
        // played live.
        { "Stutter",
          { 0.030f, 0.120f }, { 2.0f, 6.0f },
          { -40.0f, -20.0f }, { 50.0f, 80.0f }, { 20.0f, 50.0f }, { 20.0f, 55.0f },
          { 30.0f, 70.0f },   { 0.0f, 10.0f },
          { 0.40f, 0.20f, 0.20f, 0.20f, 0.00f } },

        // A long pattern that forgets. Every step loses level each time round,
        // so it evolves like a delay instead of piling up, and the walk through
        // it is drunk more often than not.
        { "Erosion",
          { 0.150f, 0.600f }, { 10.0f, 16.0f },
          { -40.0f, -20.0f }, { 45.0f, 70.0f }, { 10.0f, 40.0f }, { 5.0f, 30.0f },
          { 60.0f, 100.0f },  { 15.0f, 45.0f },
          { 0.20f, 0.10f, 0.10f, 0.20f, 0.40f } },

        // There and back. Pendulum or reverse, never forwards: the turnaround
        // is the rhythm.
        { "Pendulum",
          { 0.100f, 0.400f }, { 3.0f, 8.0f },
          { -40.0f, -20.0f }, { 40.0f, 65.0f }, { 10.0f, 35.0f }, { 0.0f, 20.0f },
          { 50.0f, 100.0f },  { 0.0f, 20.0f },
          { 0.00f, 0.30f, 0.70f, 0.00f, 0.00f } },

        // Everything at once. Fills deep, chaos high, the order random or drunk,
        // and the length anywhere: the roll for when the pattern should not be
        // recognisable as what you played.
        { "Havoc",
          { 0.040f, 0.300f }, { 6.0f, 16.0f },
          { -40.0f, -20.0f }, { 55.0f, 90.0f }, { 40.0f, 100.0f }, { 45.0f, 90.0f },
          { 20.0f, 100.0f },  { 0.0f, 30.0f },
          { 0.10f, 0.10f, 0.10f, 0.40f, 0.30f } },
    };

    constexpr int kCharacterCount = (int) (sizeof (kCharacters) / sizeof (kCharacters[0]));
    int lastRolled = 0;

    float pick (const Span& s, juce::Random& rng)
    {
        return s.fixed() ? s.lo : s.lo + rng.nextFloat() * (s.hi - s.lo);
    }

    // Log-uniform, for the spans that cross decades. Picking a rate or a time
    // linearly puts almost every roll at the top of the range.
    float pickLog (const Span& s, juce::Random& rng)
    {
        if (s.fixed() || s.lo <= 0.0f)
            return s.lo;
        return s.lo * std::pow (s.hi / s.lo, rng.nextFloat());
    }

    int pickInt (const Span& s, juce::Random& rng)
    {
        const int lo = juce::roundToInt (s.lo), hi = juce::roundToInt (s.hi);
        return hi <= lo ? lo : lo + rng.nextInt (hi - lo + 1);
    }

    int pickWeighted (const float* weights, int count, juce::Random& rng)
    {
        float total = 0.0f;
        for (int i = 0; i < count; ++i)
            total += weights[i];
        if (total <= 0.0f)
            return 0;

        float r = rng.nextFloat() * total;
        for (int i = 0; i < count; ++i)
        {
            r -= weights[i];
            if (r < 0.0f)
                return i;
        }
        return count - 1;
    }

    void setParam (juce::AudioProcessorValueTreeState& apvts, const char* paramId, float value)
    {
        if (auto* p = apvts.getParameter (paramId))
        {
            // A complete gesture each, so a host's undo and its automation
            // recording both see an ordinary edit.
            p->beginChangeGesture();
            p->setValueNotifyingHost (p->convertTo0to1 (value));
            p->endChangeGesture();
        }
    }
}

int Randomiser::numCharacters() noexcept { return kCharacterCount; }

const char* Randomiser::lastCharacterName() noexcept { return kCharacters[lastRolled].name; }

void Randomiser::randomise (juce::AudioProcessorValueTreeState& apvts, juce::Random& rng)
{
    randomiseCharacter (apvts, rng, rng.nextInt (kCharacterCount));
}

void Randomiser::randomiseCharacter (juce::AudioProcessorValueTreeState& apvts, juce::Random& rng,
                                     int characterIndex)
{
    lastRolled = juce::jlimit (0, kCharacterCount - 1, characterIndex);
    const Character& c = kCharacters[lastRolled];

    // Step is written as the free knob position. If Sync is on the same
    // position lands on a division in the same part of the travel, which is
    // the right musical neighbourhood, and the switch itself is not touched.
    setParam (apvts, id::step, params::knob01ForStepSeconds ((double) pickLog (c.stepSeconds, rng)));
    setParam (apvts, id::steps, (float) pickInt (c.steps, rng));
    setParam (apvts, id::threshold, std::round (pick (c.threshold, rng)));
    setParam (apvts, id::blend, std::round (pick (c.blend, rng)));
    setParam (apvts, id::fills, std::round (pick (c.fills, rng)));
    setParam (apvts, id::chaos, std::round (pick (c.chaos, rng)));
    setParam (apvts, id::length, std::round (pick (c.length, rng)));
    setParam (apvts, id::fade, std::round (pick (c.fade, rng)));
    setParam (apvts, id::direction, (float) pickWeighted (c.direction, BurstEngine::kDirectionCount, rng));

    // in, out, stepsync, freeze and bypass are deliberately untouched. See the header.
}
