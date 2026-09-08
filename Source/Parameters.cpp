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

    // 2. Decay. Unity still sits at 87 % of travel, exactly where it did when
    // the range topped out at 1.15, because the range is two linear segments
    // joined there rather than one: everything below unity is bit-identical to
    // what it was, including the default, and the whole of the extra ceiling
    // is spent on the runaway zone above it. A plain linear 0..1.45 would have
    // quietly compressed the most-used part of the knob to buy a bigger red
    // zone. The sub-unity slope is written as 1/kDecayUnityNorm rather than as
    // a literal, so the pair cannot drift apart.
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { id::decay, 2 }, "Decay",
        juce::NormalisableRange<float> (
            0.0f, pt::kDecayMax,
            [] (float lo, float hi, float t)
            {
                juce::ignoreUnused (lo);
                return t <= pt::kDecayUnityNorm
                           ? t / pt::kDecayUnityNorm
                           : 1.0f + (hi - 1.0f) * (t - pt::kDecayUnityNorm) / (1.0f - pt::kDecayUnityNorm);
            },
            [] (float lo, float hi, float v)
            {
                juce::ignoreUnused (lo);
                return v <= 1.0f
                           ? v * pt::kDecayUnityNorm
                           : pt::kDecayUnityNorm
                                 + (1.0f - pt::kDecayUnityNorm) * (v - 1.0f) / (hi - 1.0f);
            },
            [] (float lo, float hi, float v) { return juce::jlimit (lo, hi, v); }),
        0.45f,
        juce::AudioParameterFloatAttributes()
            .withLabel ("")
            .withStringFromValueFunction ([] (float v, int)
            {
                const juce::String num (v, 2);
                return v > 1.005f ? num + " runaway" : num;
            })));

    // 3. Filter: integers all the way down, because its floor is 20 Hz. The
    // two-decimals-below-100 rule exists for LFO rates, not for cutoffs.
    //
    // The top is 12 kHz, not 18 kHz, and the default is 2 kHz, not 8 kHz. Each
    // of the three chip stages carries a fixed 4.5 kHz output filter, so
    // `EngineTest bandwidth` measures 8 kHz at -25.6 dB through ONE stage and
    // `probe` returns the same self-oscillation level at Filter 4 k, 8 k and
    // 18 k: the top fifth of the old knob was provably inaudible, and the old
    // default sat inside it. At 2 kHz on a 20..12000 range the knob sits at
    // 72 % of travel with live range on both sides, which is what makes the
    // filter modulation routes audible on a fresh instance instead of leaving
    // them pinned against a ceiling in dead air.
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { id::filter, 3 }, "Filter", logRange (20.0f, 12000.0f), 2000.0f,
        juce::AudioParameterFloatAttributes()
            .withLabel ("Hz")
            .withStringFromValueFunction ([] (float v, int) { return juce::String (juce::roundToInt (v)); })));

    layout.add (floatParam (4, id::resonance, "Resonance", { 0.0f, 100.0f, 1.0f }, 15.0f, "%"));
    layout.add (floatParam (5, id::absorb, "Absorb", { 0.0f, 100.0f, 1.0f }, 20.0f, "%"));
    layout.add (floatParam (6, id::blend, "Blend", { 0.0f, 100.0f, 1.0f }, 50.0f, "%"));
    // 7. Agitate, the macro over the periodic modulation. It defaulted to 0,
    // and since every route is multiplied by it the entire modulation content
    // of a fresh instance was Drift's 7 cents of Time trim -- so the plugin's
    // first impression was a static delay. 25 % gives macro 0.125 under the
    // 1.5-power curve: a slow filter breath, present but not showy.
    layout.add (floatParam (7, id::agitate, "Agitate", { 0.0f, 100.0f, 1.0f }, 25.0f, "%"));

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

    // 17. In. The design puts a trim fader on each edge of the window: this
    // one sets what reaches the plugin, Strength sets how hard that hits the
    // loop. Same range and readout as Out, so the pair reads as a pair.
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { id::input, 17 }, "In", skewedRange (kOutFloorDb, 6.0f, -12.0f), 0.0f,
        juce::AudioParameterFloatAttributes()
            .withLabel ("dB")
            .withStringFromValueFunction ([] (float v, int)
            {
                return v <= kOutFloorDb + 0.05f ? juce::String ("-Inf") : juce::String (v, 1);
            })));

    // 18. Chaos. Interference -- the loop's own state, fed back as a chaotic
    // control signal -- used to ride the Agitate macro with everything else, so
    // there was no setting at which you could have the haunted, self-driven
    // behaviour without also imposing a periodic triangle on the cutoff. It is
    // gated by the loop's own energy, so it stays quiet until you play into it.
    layout.add (percentWithWord (18, id::chaos, "Chaos", 20.0f, "Still"));

    // 19. Crust. How destroyed the chip is, independent of how long the delay
    // is. Every degradation axis used to be a function of Time alone, so a
    // short bit-crushed slapback was unreachable at any setting.
    layout.add (percentWithWord (19, id::crust, "Crust", 0.0f, "Clean"));

    // 20. Tones Fold. The drone was a bare triangle forever, and nothing in the
    // plugin could add high frequency, so "wilder" always arrived darker.
    layout.add (percentWithWord (20, id::tonesfold, "Tones Fold", 0.0f, "Pure"));

    // 21. Colour. How much bandpass is mixed into the loop's lowpass. At 0 it
    // is exactly the filter it has always been; above it the loop can lock onto
    // the cutoff instead of collapsing to the lowest surviving mode, which is
    // the only route to a bright runaway and the only highpass in the plugin.
    layout.add (percentWithWord (21, id::colour, "Colour", 0.0f, "Dark"));

    // LAST, and hint 1000 so anything added later still sorts before it in AU
    // while staying declared last for VST3. 1 means bypassed, which is the
    // polarity the hosts expect.
    layout.add (std::make_unique<juce::AudioParameterBool> (
        juce::ParameterID { id::bypass, 1000 }, "Bypass", false));

    return layout;
}
} // namespace params
