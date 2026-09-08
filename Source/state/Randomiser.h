#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

// The dice.
//
// Randomising every parameter independently over its full range produces
// garbage almost every time, because the interesting settings of this plugin
// are CORRELATED: a long Time wants a darker Filter, a hot Decay wants some
// Absorb under it or it just screams, Time Mod is only musical when the Tones
// pitch it rides is somewhere sensible, and a Blend of 8 per cent means you
// cannot hear any of it. A uniform roll finds those combinations about as often
// as it finds any other, which is to say almost never.
//
// So the die is rolled twice. The first roll picks a CHARACTER -- a coherent
// region of the space with a name -- and the second fills in each parameter
// inside that character's own range. The result is a patch that sounds like
// something rather than like an accident, and the player still has the whole
// range by hand afterwards.
//
// Two things it will never touch, both on purpose:
//   Bypass, which is performance state and not preset content (CLAUDE.md), so a
//     roll of the dice must never take the plugin in or out of circuit.
//   In and Out, because a level control is the one thing that can hurt someone
//     wearing headphones, and a random one is not a musical idea.
// Time Sync is also left alone: whether you are working in note divisions is a
// workflow choice, not a sound.
class Randomiser
{
public:
    // Every parameter is set as its own complete gesture, so a host that
    // records automation sees a normal edit and its undo works.
    static void randomise (juce::AudioProcessorValueTreeState& apvts, juce::Random& rng);

    // The name of the character that was rolled, for the readout. Valid until
    // the next call.
    static const char* lastCharacterName() noexcept;

    static int numCharacters() noexcept;

    // For the test harness: roll a specific character rather than a random one,
    // so every one of them can be proven to make sound and stay bounded.
    static void randomiseCharacter (juce::AudioProcessorValueTreeState& apvts, juce::Random& rng,
                                    int characterIndex);
};
