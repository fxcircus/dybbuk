#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_core/juce_core.h>

// Message-thread only: writes a rendered pattern to disk. 32-bit float so the
// export is bit-for-bit what the sequencer would have played, with no dither
// decision made on the player's behalf.
namespace wavexport
{
    bool writeStereoFloat (const juce::File& dest, const juce::AudioBuffer<float>& buffer,
                           double sampleRate);
}
