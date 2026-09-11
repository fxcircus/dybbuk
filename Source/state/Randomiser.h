#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

// The dice.
//
// Randomising every parameter independently over its full range produces
// garbage almost every time, because the interesting settings of this plugin
// are CORRELATED: a short Step wants a short Length or it smears, a deep Fade
// wants a long pattern or it empties before the next note, a Threshold rolled
// up to 0 dB hears nothing at all, and a Blend of 8 per cent means you cannot
// hear any of it. A uniform roll finds the good combinations about as often as
// it finds any other, which is to say almost never.
//
// So the die is rolled twice. The first roll picks a CHARACTER -- a coherent
// region of the space with a name -- and the second fills in each parameter
// inside that character's own range. The result is a patch that sounds like
// something rather than like an accident, and the player still has the whole
// range by hand afterwards.
//
// Things it will never touch, all on purpose:
//   Bypass, which is performance state and not preset content (CLAUDE.md), so a
//     roll of the dice must never take the plugin in or out of circuit.
//   In and Out, because a level control is the one thing that can hurt someone
//     wearing headphones, and a random one is not a musical idea.
//   Sync, because whether you are working in note divisions is a workflow
//     choice, not a sound.
//   Freeze, because it is the player's hand on the pattern: a dice that could
//     freeze or unfreeze would be a dice that erases things.
//   Threshold, because it is set to the instrument and the room, not to the
//     patch: a roll that deafened the gate would look like a broken plugin.
//   Spread, because the stereo image is a mix decision too.
//   Blend, because it is the mix you set for the room: a roll at 8 % would
//     make the pattern inaudible and one at 100 % would silence your playing.
class Randomiser
{
public:
    // What a roll may touch: one bit per knob, so the player can keep the
    // ones that are set (Shalal's RANDOM settings). The pattern knobs are on
    // by default; the ones that are set to the instrument and the room
    // (Threshold, Blend, Spread) and the workflow switches (Sync, Bar) are
    // there but off, as is Feedback, whose roll rewrites how long a pattern
    // lives. In, Out, Freeze and Bypass have no bit at all: a level control
    // can hurt someone in headphones, and the other two are performance
    // state a dice must never flip.
    enum Field : unsigned int
    {
        fieldTime = 1u << 0,  fieldSteps = 1u << 1,     fieldFills = 1u << 2,  fieldChaos = 1u << 3,
        fieldDecay = 1u << 4, fieldFeedback = 1u << 5,  fieldDirection = 1u << 6, fieldPitch = 1u << 7,
        fieldGlue = 1u << 8,  fieldMode = 1u << 9,
        fieldThreshold = 1u << 10, fieldBlend = 1u << 11, fieldSpread = 1u << 12,
        fieldSync = 1u << 13, fieldBar = 1u << 14,
        fieldAll = (1u << 15) - 1u,
        fieldDefault = fieldAll & ~(fieldFeedback | fieldThreshold | fieldBlend | fieldSpread | fieldSync | fieldBar)
    };
    static constexpr int kFieldCount = 15;
    static const char* fieldName (int index) noexcept;   // in bit order, for a menu

    // Every parameter is set as its own complete gesture, so a host that
    // records automation sees a normal edit and its undo works. Only the
    // fields in the mask are touched.
    static void randomise (juce::AudioProcessorValueTreeState& apvts, juce::Random& rng, unsigned int mask = fieldAll);

    // The name of the character that was rolled, for the readout. Valid until
    // the next call.
    static const char* lastCharacterName() noexcept;

    static int numCharacters() noexcept;

    // For the test harness: roll a specific character rather than a random one,
    // so every one of them can be proven to make sound and stay bounded.
    static void randomiseCharacter (juce::AudioProcessorValueTreeState& apvts, juce::Random& rng,
                                    int characterIndex, unsigned int mask = fieldAll);
};
