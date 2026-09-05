#include "Parameters.h"

namespace params
{
namespace
{
    // Skewed ranges put the musically useful region in the middle of the
    // control instead of crushing it into one end.
    juce::NormalisableRange<float> skewedRange (float min, float max, float midpoint)
    {
        juce::NormalisableRange<float> range (min, max);
        range.setSkewForCentre (midpoint);
        return range;
    }

    // Musician-friendly readouts, not mathematic ones (658.289 ms is a knob
    // position, 0.66 s is a musical time). NB juce::String (v, 0) means FULL
    // precision, not zero decimals — passing 0 here is how raw floats leak
    // into the UI and host automation lanes.
    //   ms  -> seconds with two decimals from 100 ms up ("0.66 s"), integer
    //          milliseconds below ("45 ms"); the unit is baked into the
    //          string, so the label is left empty (a host would otherwise
    //          append a stale "ms" after a seconds readout)
    //   Hz  -> integers from 100 Hz up, two decimals below (LFO-rate ranges)
    //   %   -> integers
    //   else, e.g. dB and unitless -> displayDecimals (default 1)
    std::unique_ptr<juce::AudioParameterFloat> floatParam (const char* paramID,
                                                           const char* name,
                                                           juce::NormalisableRange<float> range,
                                                           float defaultValue,
                                                           const char* unit,
                                                           int displayDecimals = 1)
    {
        const juce::String unitStr (unit);
        const bool msUnit = unitStr == "ms";
        return std::make_unique<juce::AudioParameterFloat> (
            juce::ParameterID { paramID, 1 }, name, range, defaultValue,
            juce::AudioParameterFloatAttributes()
                .withLabel (msUnit ? "" : unit)
                .withStringFromValueFunction ([displayDecimals, unitStr, msUnit] (float value, int)
                {
                    if (msUnit)
                        return value >= 100.0f
                                   ? juce::String (value * 0.001f, 2) + " s"
                                   : juce::String (juce::roundToInt (value)) + " ms";
                    if (unitStr == "Hz")
                        return value >= 100.0f ? juce::String (juce::roundToInt (value))
                                               : juce::String (value, 2);
                    if (unitStr == "%")
                        return juce::String (juce::roundToInt (value));
                    return juce::String (value, juce::jmax (1, displayDecimals));
                }));
    }
} // namespace

juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;

    layout.add (floatParam (id::drive, "Drive", { 0.0f, 24.0f }, 0.0f, "dB"));
    layout.add (floatParam (id::tone, "Tone",
                            skewedRange (200.0f, 20000.0f, 2000.0f), 20000.0f, "Hz", 0));
    layout.add (floatParam (id::mix, "Mix", { 0.0f, 100.0f }, 100.0f, "%", 0));

    layout.add (std::make_unique<juce::AudioParameterBool> (
        juce::ParameterID { id::bypass, 1 }, "Bypass", false));

    return layout;
}
} // namespace params
