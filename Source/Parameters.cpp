#include "Parameters.h"

#include "dsp/TimeMap.h"

namespace params
{
namespace
{
    // A true exponential range. setSkewForCentre is a power law with an
    // additive offset, which crushes the slow end of a rate control; for
    // anything spanning decades (Filter, Agit Speed, Tones Pitch) this is the
    // one that feels right under the finger.
    juce::NormalisableRange<float> logRange (float min, float max)
    {
        return { min, max,
                 [] (float lo, float hi, float t) { return lo * std::pow (hi / lo, t); },
                 [] (float lo, float hi, float v) { return std::log (v / lo) / std::log (hi / lo); },
                 [] (float lo, float hi, float v) { return juce::jlimit (lo, hi, v); } };
    }

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
    // ("Off" for Tones Level, "Mono" for Spread).
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

} // namespace

juce::String timeReadout (double seconds)
{
    const double ms = seconds * 1000.0;
    return ms >= 100.0 ? juce::String (seconds, 2) + " s"
                       : juce::String (juce::roundToInt (ms)) + " ms";
}

juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;

    // Time Sync is built first so Time's readout can see it, but added at its
    // own slot so declaration order still matches the hint order.
    auto timeSync = std::make_unique<juce::AudioParameterBool> (
        juce::ParameterID { id::timesync, 12 }, "Time Sync", false,
        juce::AudioParameterBoolAttributes().withStringFromValueFunction (
            [] (bool v, int) { return juce::String (v ? "Sync" : "Free"); }));
    // Captured by value into Time's readout below, NOT held in a file static:
    // a host with two Dybbuk instances would otherwise have the first one's
    // Time readout following the second one's Sync switch. Both parameters
    // are owned by the same APVTS, so the pointer outlives the lambda.
    auto* syncRaw = timeSync.get();

    // 1. Time. The knob is 0..1; what it means is a delay time, and while
    // Sync is on it reads as a note division (Push shows the host's string,
    // so a synced Time must not read "0.31 s" there).
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { id::time, 1 }, "Time",
        juce::NormalisableRange<float> (0.0f, 1.0f), 0.45f,
        juce::AudioParameterFloatAttributes()
            .withLabel ("")
            .withStringFromValueFunction ([syncRaw] (float v, int)
            {
                if (syncRaw != nullptr && syncRaw->get())
                    return juce::String (timemap::kDivisions[timemap::divisionIndexForTime01 (v)].name);
                return timeReadout ((double) pt::delaySecondsForTime01 (v));
            })
            .withValueFromStringFunction ([] (const juce::String& text)
            {
                const double n = text.retainCharacters ("0123456789.-").getDoubleValue();
                const double sec = text.containsIgnoreCase ("ms") ? n * 0.001
                                   : (text.containsIgnoreCase ("s") ? n : n * 0.001);
                return pt::time01ForDelaySeconds ((float) sec);
            })));

    // 2. Decay. Unity sits at 87 % of travel; the top of the range is the
    // runaway zone and says so.
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { id::decay, 2 }, "Decay",
        juce::NormalisableRange<float> (0.0f, 1.15f), 0.45f,
        juce::AudioParameterFloatAttributes()
            .withLabel ("")
            .withStringFromValueFunction ([] (float v, int)
            {
                const juce::String num (v, 2);
                return v > 1.005f ? num + " runaway" : num;
            })));

    // 3. Filter: integers all the way down, because its floor is 20 Hz. The
    // two-decimals-below-100 rule exists for LFO rates, not for cutoffs.
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { id::filter, 3 }, "Filter", logRange (20.0f, 18000.0f), 8000.0f,
        juce::AudioParameterFloatAttributes()
            .withLabel ("Hz")
            .withStringFromValueFunction ([] (float v, int) { return juce::String (juce::roundToInt (v)); })));

    layout.add (floatParam (4, id::resonance, "Resonance", { 0.0f, 100.0f, 1.0f }, 15.0f, "%"));
    layout.add (floatParam (5, id::absorb, "Absorb", { 0.0f, 100.0f, 1.0f }, 20.0f, "%"));
    layout.add (floatParam (6, id::blend, "Blend", { 0.0f, 100.0f, 1.0f }, 50.0f, "%"));
    layout.add (floatParam (7, id::agitate, "Agitate", { 0.0f, 100.0f, 1.0f }, 0.0f, "%"));

    // 8. Agit Speed. Below 1 Hz a period is the musical quantity: a twelve
    // second swell is a thought, 0.08 Hz is arithmetic.
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { id::agitspeed, 8 }, "Agit Speed", logRange (0.016f, 1000.0f), 0.35f,
        juce::AudioParameterFloatAttributes()
            .withLabel ("")
            .withStringFromValueFunction ([] (float v, int)
            {
                if (v < 1.0f)
                    return juce::String (1.0f / v, 1) + " s";
                if (v < 100.0f)
                    return juce::String (v, 2) + " Hz";
                return juce::String (juce::roundToInt (v)) + " Hz";
            })));

    layout.add (floatParam (9, id::strength, "Strength", { 0.0f, 40.0f }, 0.0f, "dB"));

    // 10. Out. The bottom of the fader is silence, not -60 dB of hiss.
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { id::out, 10 }, "Out", skewedRange (kOutFloorDb, 6.0f, -12.0f), 0.0f,
        juce::AudioParameterFloatAttributes()
            .withLabel ("dB")
            .withStringFromValueFunction ([] (float v, int)
            {
                return v <= kOutFloorDb + 0.05f ? juce::String ("-Inf") : juce::String (v, 1);
            })));

    layout.add (floatParam (11, id::timemod, "Time Mod", { 0.0f, 100.0f, 1.0f }, 0.0f, "%"));
    layout.add (std::move (timeSync)); // 12
    layout.add (std::make_unique<juce::AudioParameterChoice> (
        juce::ParameterID { id::agitmode, 13 }, "Agit Mode", juce::StringArray { "Loop", "Gate" }, 0));
    layout.add (percentWithWord (14, id::toneslevel, "Tones Level", 0.0f, "Off"));

    // 15. Tones Pitch reads as a note name: this is a drone, not a filter.
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { id::tonespitch, 15 }, "Tones Pitch", logRange (32.703f, 2093.0f), 110.0f,
        juce::AudioParameterFloatAttributes()
            .withLabel ("")
            .withStringFromValueFunction ([] (float v, int)
            {
                const double midi = 69.0 + 12.0 * std::log2 ((double) v / 440.0);
                const int noteNumber = juce::roundToInt (midi);
                const int cents = juce::roundToInt ((midi - noteNumber) * 100.0);
                const auto name = juce::MidiMessage::getMidiNoteName (noteNumber, true, true, 4);
                return std::abs (cents) > 5
                           ? name + (cents > 0 ? " +" : " ") + juce::String (cents)
                           : name;
            })));

    layout.add (percentWithWord (16, id::spread, "Spread", 0.0f, "Mono"));

    // LAST, and hint 1000 so anything added later still sorts before it in AU
    // while staying declared last for VST3. 1 means bypassed, which is the
    // polarity the hosts expect.
    layout.add (std::make_unique<juce::AudioParameterBool> (
        juce::ParameterID { id::bypass, 1000 }, "Bypass", false));

    return layout;
}
} // namespace params
