#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

// Single source of truth for the parameter layout. IDs are stable — changing
// one breaks saved sessions and presets, so pick them deliberately up front.
//
// The parameter list is the plugin's public API. Decide it early: adding is
// cheap, renaming and reordering is not.
namespace params
{
namespace id
{
    inline constexpr auto drive  = "drive";
    inline constexpr auto tone   = "tone";
    inline constexpr auto mix    = "mix";
    inline constexpr auto bypass = "bypass";
} // namespace id

// Parameters that are performance state rather than preset content (freeze
// buttons, momentary triggers). PresetManager skips these when marking the
// state dirty and forces them off on load.
inline const juce::StringArray& performanceParams()
{
    static const juce::StringArray ids { id::bypass };
    return ids;
}

juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();
} // namespace params
