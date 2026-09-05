#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

// Single source of truth for the parameter layout. IDs are stable strings:
// changing one breaks saved sessions and presets.
//
// Order matters twice over. VST3 presents parameters in declaration order and
// AU sorts them by (version hint, hash of id), so the two only agree if every
// parameter carries its own ascending hint declared in the same order. The
// first eight are the Push 3 bank 1 the plan asks for: Time, Decay, Filter,
// Resonance, Absorb, Blend, Agitate, Agit Speed.
namespace params
{
namespace id
{
    inline constexpr auto time       = "time";
    inline constexpr auto decay      = "decay";
    inline constexpr auto filter     = "filter";
    inline constexpr auto resonance  = "resonance";
    inline constexpr auto absorb     = "absorb";
    inline constexpr auto blend      = "blend";
    inline constexpr auto agitate    = "agitate";
    inline constexpr auto agitspeed  = "agitspeed";
    inline constexpr auto strength   = "strength";
    inline constexpr auto out        = "out";
    inline constexpr auto timemod    = "timemod";
    inline constexpr auto timesync   = "timesync";
    inline constexpr auto agitmode   = "agitmode";
    inline constexpr auto toneslevel = "toneslevel";
    inline constexpr auto tonespitch = "tonespitch";
    inline constexpr auto spread     = "spread";
    inline constexpr auto bypass     = "bypass";
} // namespace id

inline constexpr float kOutFloorDb = -60.0f; // the bottom of the Out fader reads "-Inf" and mutes

// Performance state rather than preset content. Excluded from dirty tracking,
// and its live value is preserved across preset loads: a preset must never
// take the plugin in or out of circuit.
inline const juce::StringArray& performanceParams()
{
    static const juce::StringArray ids { id::bypass };
    return ids;
}

// Editor-side properties on the APVTS root. They are stripped from preset
// files and preserved across preset loads, so loading someone's preset never
// changes your theme or window size.
inline const juce::StringArray& editorOnlyProperties()
{
    static const juce::StringArray props { "theme", "uiScale" };
    return props;
}

// Shared by the Time parameter's host readout and the editor's readout strip,
// so the two can never drift apart.
juce::String timeReadout (double seconds);

juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();
} // namespace params
