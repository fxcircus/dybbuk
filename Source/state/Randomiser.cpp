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
        Span fills, chaos, length, feedback;
        float direction[BurstEngine::kDirectionCount]; // relative weights, Forward, Reverse, Pendulum, Drunk, Random
        Span pitch;         // semitones, integer; half the rolls stay at 0
        Span glue;          // the drive, percent
        float mode[BurstEngine::kModeCount]; // relative weights, Golem, Wraith, Trance, Legion, Tremor, Rattle, Mirror, Miasma
    };

    const Character kCharacters[] = {
        // Behaves. A steady pattern of the last few things you played, mostly
        // forwards, with a fill now and then when you play over it.
        { "Steady",
          { 0.120f, 0.500f }, { 4.0f, 8.0f },
          { 0.0f, 25.0f }, { 0.0f, 10.0f },
          { 70.0f, 100.0f },  { 90.0f, 100.0f },
          { 0.70f, 0.10f, 0.20f, 0.00f, 0.00f },
          { 0.0f, 0.0f },
          { 0.0f, 15.0f },
          { 0.70f, 0.05f, 0.10f, 0.10f, 0.05f, 0.05f, 0.05f, 0.05f } },

        // Short steps, choked, with chaos doing the ratchets: a stutter edit
        // played live.
        { "Stutter",
          { 0.050f, 0.120f }, { 2.0f, 6.0f },
          { 20.0f, 50.0f }, { 20.0f, 55.0f },
          { 30.0f, 70.0f },   { 90.0f, 100.0f },
          { 0.40f, 0.20f, 0.20f, 0.00f, 0.20f },
          { -12.0f, 12.0f },
          { 10.0f, 50.0f },
          { 0.40f, 0.05f, 0.10f, 0.15f, 0.30f, 0.20f, 0.05f, 0.05f } },

        // A long pattern that forgets. Every step loses level each time round,
        // so it evolves like a delay instead of piling up, and the walk through
        // it is drunk more often than not.
        { "Erosion",
          { 0.150f, 0.600f }, { 10.0f, 16.0f },
          { 10.0f, 40.0f }, { 5.0f, 30.0f },
          { 60.0f, 100.0f },  { 55.0f, 85.0f },
          { 0.20f, 0.10f, 0.10f, 0.40f, 0.20f },
          { -12.0f, 0.0f },
          { 20.0f, 60.0f },
          { 0.30f, 0.30f, 0.30f, 0.10f, 0.00f, 0.05f, 0.10f, 0.25f } },

        // There and back. Pendulum or reverse, never forwards: the turnaround
        // is the rhythm.
        { "Pendulum",
          { 0.100f, 0.400f }, { 3.0f, 8.0f },
          { 10.0f, 35.0f }, { 0.0f, 20.0f },
          { 50.0f, 100.0f },  { 80.0f, 100.0f },
          { 0.00f, 0.30f, 0.70f, 0.00f, 0.00f },
          { -7.0f, 7.0f },
          { 0.0f, 25.0f },
          { 0.35f, 0.15f, 0.15f, 0.35f, 0.00f, 0.10f, 0.10f, 0.10f } },

        // Everything at once. Fills deep, chaos high, the order random or drunk,
        // and the length anywhere: the roll for when the pattern should not be
        // recognisable as what you played.
        { "Havoc",
          { 0.050f, 0.300f }, { 6.0f, 16.0f },
          { 40.0f, 100.0f }, { 45.0f, 90.0f },
          { 20.0f, 100.0f },  { 70.0f, 100.0f },
          { 0.10f, 0.10f, 0.10f, 0.30f, 0.40f },
          { 0.0f, 12.0f },
          { 40.0f, 100.0f },
          { 0.20f, 0.20f, 0.20f, 0.20f, 0.20f, 0.15f, 0.15f, 0.15f } },
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

const char* Randomiser::fieldName (int index) noexcept
{
    static const char* const names[kFieldCount] = { "Time", "Steps", "Fills", "Chaos", "Decay",
                                                     "Feedback", "Direction", "Pitch", "Glue", "Mode" };
    return index >= 0 && index < kFieldCount ? names[index] : "";
}

void Randomiser::randomise (juce::AudioProcessorValueTreeState& apvts, juce::Random& rng, unsigned int mask)
{
    randomiseCharacter (apvts, rng, rng.nextInt (kCharacterCount), mask);
}

void Randomiser::randomiseCharacter (juce::AudioProcessorValueTreeState& apvts, juce::Random& rng,
                                     int characterIndex, unsigned int mask)
{
    lastRolled = juce::jlimit (0, kCharacterCount - 1, characterIndex);
    const Character& c = kCharacters[lastRolled];

    // Step is written as the free knob position. If Sync is on the same
    // position lands on a division in the same part of the travel, which is
    // the right musical neighbourhood, and the switch itself is not touched.
    // Every field is drawn whether or not it is set, so the sequence of draws
    // (and a seed's outcome) does not depend on the mask.
    const float vTime = params::knob01ForStepSeconds ((double) pickLog (c.stepSeconds, rng));
    if (mask & fieldTime) setParam (apvts, id::step, vTime);
    const float vSteps = (float) pickInt (c.steps, rng);
    if (mask & fieldSteps) setParam (apvts, id::steps, vSteps);
    const float vFills = std::round (pick (c.fills, rng));
    if (mask & fieldFills) setParam (apvts, id::fills, vFills);
    const float vChaos = std::round (pick (c.chaos, rng));
    if (mask & fieldChaos) setParam (apvts, id::chaos, vChaos);
    const float vDecay = std::round (pick (c.length, rng));
    if (mask & fieldDecay) setParam (apvts, id::length, vDecay);
    const float vFeedback = std::round (pick (c.feedback, rng));
    if (mask & fieldFeedback) setParam (apvts, id::feedback, vFeedback);
    const float vDirection = (float) pickWeighted (c.direction, BurstEngine::kDirectionCount, rng);
    if (mask & fieldDirection) setParam (apvts, id::direction, vDirection);
    const float vPitch = rng.nextFloat() < 0.5f ? 0.0f : (float) pickInt (c.pitch, rng);
    if (mask & fieldPitch) setParam (apvts, id::pitch, vPitch);
    const float vGlue = std::round (pick (c.glue, rng));
    if (mask & fieldGlue) setParam (apvts, id::glue, vGlue);
    const float vMode = (float) pickWeighted (c.mode, BurstEngine::kModeCount, rng);
    if (mask & fieldMode) setParam (apvts, id::mode, vMode);

    // threshold, blend, spread, in, out, stepsync, barreset, freeze and bypass
    // are deliberately untouched: they are set to the instrument and the room, not
    // to the patch.
    // See the header.
}
