#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include <cmath>

// Single source of truth for the parameter layout. IDs are stable strings:
// changing one breaks saved sessions and presets.
//
// Order matters twice over. VST3 presents parameters in declaration order and
// AU sorts them by (version hint, hash of id), so the two only agree if every
// parameter carries its own ascending hint declared in the same order. The
// first eight are the Push 3 bank 1: Threshold, Time, Steps, Blend, Freeze,
// Fills, Chaos, Direction.
namespace params
{
namespace id
{
    inline constexpr auto step      = "step";
    inline constexpr auto steps     = "steps";
    inline constexpr auto threshold = "threshold";
    inline constexpr auto blend     = "blend";
    inline constexpr auto freeze    = "freeze";
    inline constexpr auto fills     = "fills";
    inline constexpr auto chaos     = "chaos";
    inline constexpr auto direction = "direction";
    inline constexpr auto length    = "length";
    inline constexpr auto fade      = "fade";
    inline constexpr auto pitch     = "pitch";
    inline constexpr auto stepsync  = "stepsync";
    inline constexpr auto input     = "in";
    inline constexpr auto out       = "out";
    inline constexpr auto bypass    = "bypass";
} // namespace id

inline constexpr float kOutFloorDb = -60.0f; // the bottom of the Out fader reads "-Inf" and mutes

// The Step knob is 0..1 and means a step time. Free, it is an exponential
// sweep over two decades, so the middle of the travel is 200 ms and a
// sixteenth at any sane tempo sits near it. Synced, the same 0..1 is
// quantised to the note divisions in dsp/TimeMap.h.
inline constexpr double kStepMinSeconds = 0.050;   // under this it is chop, not rhythm (Roy)
inline constexpr double kStepMaxSeconds = 2.000;

inline double stepSecondsForKnob01 (float t) noexcept
{
    const double u = t < 0.0f ? 0.0 : (t > 1.0f ? 1.0 : (double) t);
    return kStepMinSeconds * std::pow (kStepMaxSeconds / kStepMinSeconds, u);
}

inline float knob01ForStepSeconds (double seconds) noexcept
{
    const double s = seconds < kStepMinSeconds ? kStepMinSeconds
                     : (seconds > kStepMaxSeconds ? kStepMaxSeconds : seconds);
    return (float) (std::log (s / kStepMinSeconds) / std::log (kStepMaxSeconds / kStepMinSeconds));
}

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

// Shared by the Step parameter's host readout and the editor's readout strip,
// so the two can never drift apart. Seconds with two decimals from 100 ms up,
// integer milliseconds below.
juce::String stepReadout (double seconds);

juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();
} // namespace params
