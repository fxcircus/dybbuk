#include "Parameters.h"

#include "dsp/BurstEngine.h"
#include "dsp/TimeMap.h"

namespace params
{
namespace
{
    juce::NormalisableRange<float> skewedRange (float min, float max, float midpoint)
    {
        juce::NormalisableRange<float> range (min, max);
        range.setSkewForCentre (midpoint);
        return range;
    }

    // Musician-friendly readouts, not mathematical ones. NB juce::String (v, 0)
    // means FULL precision, not zero decimals: that is how raw floats leak
    // into the UI and into host automation lanes. Use roundToInt instead.
    //   ms  -> seconds with two decimals from 100 ms up, integer ms below
    //   Hz  -> integers from 100 Hz up, two decimals below
    //   %   -> integers
    //   else, e.g. dB -> displayDecimals (default 1)
    std::unique_ptr<juce::AudioParameterFloat> floatParam (int versionHint,
                                                           const char* paramID,
                                                           const char* name,
                                                           juce::NormalisableRange<float> range,
                                                           float defaultValue,
                                                           const char* unit,
                                                           int displayDecimals = 1)
    {
        const juce::String unitStr (unit);
        const bool msUnit = unitStr == "ms";
        return std::make_unique<juce::AudioParameterFloat> (
            juce::ParameterID { paramID, versionHint }, name, range, defaultValue,
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

    // A percentage whose bottom end deserves a word rather than a zero
    // ("Off" for Fills, "Still" for Chaos, "Never" for Fade).
    std::unique_ptr<juce::AudioParameterFloat> percentWithWord (int versionHint,
                                                                const char* paramID,
                                                                const char* name,
                                                                float defaultValue,
                                                                const char* zeroWord)
    {
        const juce::String word (zeroWord);
        return std::make_unique<juce::AudioParameterFloat> (
            juce::ParameterID { paramID, versionHint }, name,
            juce::NormalisableRange<float> (0.0f, 100.0f, 1.0f), defaultValue,
            juce::AudioParameterFloatAttributes()
                .withLabel ("")
                .withStringFromValueFunction ([word] (float v, int)
                {
                    return v < 0.5f ? word : juce::String (juce::roundToInt (v)) + " %";
                }));
    }

    // The two trims share one range and one readout, so the pair reads as a
    // pair. The bottom of the fader is silence, not -60 dB of hiss.
    std::unique_ptr<juce::AudioParameterFloat> trimParam (int versionHint, const char* paramID,
                                                          const char* name)
    {
        return std::make_unique<juce::AudioParameterFloat> (
            juce::ParameterID { paramID, versionHint }, name, skewedRange (kOutFloorDb, 6.0f, -12.0f), 0.0f,
            juce::AudioParameterFloatAttributes()
                .withLabel ("dB")
                .withStringFromValueFunction ([] (float v, int)
                {
                    return v <= kOutFloorDb + 0.05f ? juce::String ("-Inf") : juce::String (v, 1);
                }));
    }

} // namespace

juce::String stepReadout (double seconds)
{
    const double ms = seconds * 1000.0;
    return ms >= 100.0 ? juce::String (seconds, 2) + " s"
                       : juce::String (juce::roundToInt (ms)) + " ms";
}

juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;

    // Sync is built first so Step's readout can see it, but added at its own
    // slot so declaration order still matches the hint order.
    auto stepSync = std::make_unique<juce::AudioParameterBool> (
        juce::ParameterID { id::stepsync, 12 }, "Sync", false,
        juce::AudioParameterBoolAttributes().withStringFromValueFunction (
            [] (bool v, int) { return juce::String (v ? "Sync" : "Free"); }));
    // Captured by value into Step's readout below, NOT held in a file static:
    // a host with two Dybbuk instances would otherwise have the first one's
    // Step readout following the second one's Sync switch. Both parameters
    // are owned by the same APVTS, so the pointer outlives the lambda.
    auto* syncRaw = stepSync.get();

    // 1. Step. The knob is 0..1; what it means is a step time, and while Sync
    // is on it reads as a note division (Push shows the host's string, so a
    // synced Step must not read "0.31 s" there). The default is a quarter
    // second: a sixteenth at 60, an eighth at 120.
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { id::step, 1 }, "Step",
        juce::NormalisableRange<float> (0.0f, 1.0f), knob01ForStepSeconds (0.250),
        juce::AudioParameterFloatAttributes()
            .withLabel ("")
            .withStringFromValueFunction ([syncRaw] (float v, int)
            {
                if (syncRaw != nullptr && syncRaw->get())
                    return juce::String (timemap::kDivisions[timemap::divisionIndexForTime01 (v)].name);
                return stepReadout (stepSecondsForKnob01 (v));
            })
            .withValueFromStringFunction ([] (const juce::String& text)
            {
                const double n = text.retainCharacters ("0123456789.-").getDoubleValue();
                const double sec = text.containsIgnoreCase ("ms") ? n * 0.001
                                   : (text.containsIgnoreCase ("s") ? n : n * 0.001);
                return knob01ForStepSeconds (sec);
            })));

    // 2. Steps: the pattern ceiling. The hardware stops at 8; the engine
    // allows 16, and the default is the hardware's.
    layout.add (std::make_unique<juce::AudioParameterInt> (
        juce::ParameterID { id::steps, 2 }, "Steps", 1, BurstEngine::kMaxSteps, 8));

    // 3. Threshold: the gate's open level, the hardware's Sensitivity. It
    // closes 6 dB under this, so a decaying tail cannot chatter it.
    layout.add (floatParam (3, id::threshold, "Threshold", { -60.0f, 0.0f }, -30.0f, "dB"));

    layout.add (floatParam (4, id::blend, "Blend", { 0.0f, 100.0f, 1.0f }, 50.0f, "%"));

    // 5. Record, the arm. On, every gated event becomes a step; off, the
    // pattern is frozen and you play over it, with the gate driving Fills.
    layout.add (std::make_unique<juce::AudioParameterBool> (
        juce::ParameterID { id::record, 5 }, "Record", true,
        juce::AudioParameterBoolAttributes().withStringFromValueFunction (
            [] (bool v, int) { return juce::String (v ? "Armed" : "Off"); })));

    layout.add (percentWithWord (6, id::fills, "Fills", 30.0f, "Off"));
    layout.add (percentWithWord (7, id::chaos, "Chaos", 0.0f, "Still"));

    // 8. Direction. The choice order is BurstEngine::Direction's, so the index
    // is the enum.
    layout.add (std::make_unique<juce::AudioParameterChoice> (
        juce::ParameterID { id::direction, 8 }, "Direction",
        juce::StringArray { "Forward", "Reverse", "Pendulum", "Random", "Drunk" }, 0));

    // 9. Length: the choke. The floor is 5 % rather than 0 so a fully
    // shortened step is still a click and not silence.
    layout.add (floatParam (9, id::length, "Length", { 5.0f, 100.0f, 1.0f }, 100.0f, "%"));

    layout.add (percentWithWord (10, id::fade, "Fade", 0.0f, "Never"));

    // 11. Full: what an armed pattern does at its ceiling.
    layout.add (std::make_unique<juce::AudioParameterChoice> (
        juce::ParameterID { id::full, 11 }, "Full", juce::StringArray { "Replace", "Hold" }, 0));

    layout.add (std::move (stepSync)); // 12

    // 13, 14. The trims, one on each edge of the window.
    layout.add (trimParam (13, id::input, "In"));
    layout.add (trimParam (14, id::out, "Out"));

    // LAST, and hint 1000 so anything added later still sorts before it in AU
    // while staying declared last for VST3. 1 means bypassed, which is the
    // polarity the hosts expect.
    layout.add (std::make_unique<juce::AudioParameterBool> (
        juce::ParameterID { id::bypass, 1000 }, "Bypass", false));

    return layout;
}
} // namespace params
