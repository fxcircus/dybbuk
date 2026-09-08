#include "Randomiser.h"

#include "../Parameters.h"

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
    // a tour of the extremes. Decay in particular tops out at 1.25 against a
    // ceiling of 1.45, so a roll can land in the runaway zone but never parks at
    // the far end of it -- that part of the knob is for the player to find.
    struct Character
    {
        const char* name;
        Span time, decay, filter, resonance, absorb, blend;
        Span agitate, agitSpeed, strength, timeMod;
        Span chaos, crust, tonesLevel, tonesPitch, tonesFold, colour, spread;
        float gateChance; // how often Agit Mode comes up Gate
    };

    const Character kCharacters[] = {
        // A dark room. Long-ish, damped, barely moving: the setting you would
        // reach for if you wanted the plugin to behave.
        { "Space",
          { 0.25f, 0.60f }, { 0.60f, 0.95f }, { 400.0f, 2500.0f }, { 10.0f, 45.0f },
          { 25.0f, 60.0f }, { 40.0f, 70.0f },
          { 5.0f, 35.0f },  { 0.03f, 0.5f },  { 0.0f, 8.0f },      { 0.0f, 10.0f },
          { 0.0f, 25.0f },  { 0.0f, 25.0f },  { 0.0f, 0.0f },      { 110.0f, 110.0f },
          { 0.0f, 20.0f },  { 0.0f, 15.0f },  { 0.0f, 40.0f },
          0.10f },

        // Aged tape. Crust up, and the Agitation bending the clock at a rate you
        // can hear as wow rather than as vibrato.
        { "Tape",
          { 0.30f, 0.70f }, { 0.50f, 0.85f }, { 800.0f, 3500.0f }, { 5.0f, 30.0f },
          { 35.0f, 75.0f }, { 40.0f, 65.0f },
          { 20.0f, 60.0f }, { 0.4f, 6.0f },   { 2.0f, 12.0f },     { 0.0f, 15.0f },
          { 5.0f, 30.0f },  { 30.0f, 85.0f }, { 0.0f, 0.0f },      { 110.0f, 110.0f },
          { 0.0f, 30.0f },  { 0.0f, 20.0f },  { 10.0f, 50.0f },
          0.15f },

        // Short, ringing and FM'd. Tones Pitch matters here even though the
        // drone is off, because the sub-harmonic is what Time Mod rides: it sets
        // the sideband grid, so a roll of the dice tunes the clang.
        { "Metal",
          { 0.02f, 0.30f }, { 0.55f, 1.00f }, { 1000.0f, 6000.0f }, { 30.0f, 75.0f },
          { 0.0f, 30.0f },  { 50.0f, 85.0f },
          { 10.0f, 50.0f }, { 2.0f, 300.0f }, { 4.0f, 18.0f },      { 25.0f, 75.0f },
          { 10.0f, 45.0f }, { 0.0f, 40.0f },  { 0.0f, 0.0f },       { 55.0f, 880.0f },
          { 0.0f, 40.0f },  { 20.0f, 60.0f }, { 0.0f, 45.0f },
          0.25f },

        // Plays itself. The drone is on, the chaos is driving it, and Decay sits
        // at or over unity: unplug the input and leave it alone.
        { "Possessed",
          { 0.35f, 0.75f }, { 0.95f, 1.25f }, { 600.0f, 2500.0f }, { 40.0f, 80.0f },
          { 5.0f, 35.0f },  { 70.0f, 100.0f },
          { 10.0f, 45.0f }, { 0.03f, 0.6f },  { 0.0f, 8.0f },      { 0.0f, 20.0f },
          { 45.0f, 95.0f }, { 0.0f, 45.0f },  { 35.0f, 80.0f },    { 41.2f, 220.0f },
          { 20.0f, 70.0f }, { 10.0f, 45.0f }, { 20.0f, 60.0f },
          0.05f },

        // Destroyed and hot. Crust high, Strength high, and enough Decay that
        // the repeats fight back.
        { "Ruin",
          { 0.10f, 0.55f }, { 0.80f, 1.20f }, { 300.0f, 2000.0f }, { 25.0f, 70.0f },
          { 10.0f, 45.0f }, { 60.0f, 95.0f },
          { 25.0f, 75.0f }, { 0.2f, 40.0f },  { 12.0f, 30.0f },    { 5.0f, 45.0f },
          { 25.0f, 70.0f }, { 55.0f, 100.0f }, { 0.0f, 40.0f },    { 41.2f, 165.0f },
          { 30.0f, 100.0f }, { 5.0f, 40.0f }, { 10.0f, 55.0f },
          0.30f },
    };

    constexpr int kCharacterCount = (int) (sizeof (kCharacters) / sizeof (kCharacters[0]));
    int lastRolled = 0;

    float pick (const Span& s, juce::Random& rng)
    {
        return s.fixed() ? s.lo : s.lo + rng.nextFloat() * (s.hi - s.lo);
    }

    // Log-uniform, for the spans that cross decades. Picking a rate or a cutoff
    // linearly puts almost every roll at the top of the range.
    float pickLog (const Span& s, juce::Random& rng)
    {
        if (s.fixed() || s.lo <= 0.0f)
            return s.lo;
        return s.lo * std::pow (s.hi / s.lo, rng.nextFloat());
    }

    // Tones Pitch snapped to a semitone, because it is a drone and it is also
    // the FM grid: an arbitrary 137.4 Hz is in tune with nothing.
    float pickSemitone (const Span& s, juce::Random& rng)
    {
        if (s.fixed())
            return s.lo;
        const float semitones = 12.0f * std::log2 (s.hi / s.lo);
        const int step = rng.nextInt (juce::jmax (1, (int) semitones + 1));
        return s.lo * std::pow (2.0f, (float) step / 12.0f);
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

    setParam (apvts, id::time, pick (c.time, rng));
    setParam (apvts, id::decay, pick (c.decay, rng));
    setParam (apvts, id::filter, pickLog (c.filter, rng));
    setParam (apvts, id::resonance, std::round (pick (c.resonance, rng)));
    setParam (apvts, id::absorb, std::round (pick (c.absorb, rng)));
    setParam (apvts, id::blend, std::round (pick (c.blend, rng)));
    setParam (apvts, id::agitate, std::round (pick (c.agitate, rng)));
    setParam (apvts, id::agitspeed, pickLog (c.agitSpeed, rng));
    setParam (apvts, id::strength, pick (c.strength, rng));
    setParam (apvts, id::timemod, std::round (pick (c.timeMod, rng)));
    setParam (apvts, id::chaos, std::round (pick (c.chaos, rng)));
    setParam (apvts, id::crust, std::round (pick (c.crust, rng)));
    setParam (apvts, id::toneslevel, std::round (pick (c.tonesLevel, rng)));
    setParam (apvts, id::tonespitch, pickSemitone (c.tonesPitch, rng));
    setParam (apvts, id::tonesfold, std::round (pick (c.tonesFold, rng)));
    setParam (apvts, id::colour, std::round (pick (c.colour, rng)));
    setParam (apvts, id::spread, std::round (pick (c.spread, rng)));

    if (auto* mode = apvts.getParameter (id::agitmode))
    {
        mode->beginChangeGesture();
        mode->setValueNotifyingHost (rng.nextFloat() < c.gateChance ? 1.0f : 0.0f);
        mode->endChangeGesture();
    }

    // in, out, timesync and bypass are deliberately untouched. See the header.
}
