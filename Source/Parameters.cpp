#include "Parameters.h"

#include "dsp/BurstEngine.h"
#include "dsp/TimeMap.h"

namespace params
{
namespace
{
    // JUCE's integer parameter does not call itself discrete, so a host may
    // show Steps or Pitch as a continuous ramp. This one does; getNumSteps
    // already reports the range.
    struct DiscreteInt : public juce::AudioParameterInt
    {
        using juce::AudioParameterInt::AudioParameterInt;
        bool isDiscrete() const override { return true; }
    };

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
        juce::ParameterID { id::stepsync, 15 }, "Sync", false,
        juce::AudioParameterBoolAttributes().withStringFromValueFunction (
            [] (bool v, int) { return juce::String (v ? "Sync" : "Free"); }));
    // Captured by value into Step's readout below, NOT held in a file static:
    // a host with two Dybbuk instances would otherwise have the first one's
    // Step readout following the second one's Sync switch. Both parameters
    // are owned by the same APVTS, so the pointer outlives the lambda.
    auto* syncRaw = stepSync.get();

    // 1. Threshold: the gate's open level, the hardware's Sensitivity. It
    // closes 6 dB under this, so a decaying tail cannot chatter it. First,
    // because it is the first thing the signal meets (Roy, playing it).
    auto pThreshold = floatParam (14, id::threshold, "Threshold", { -60.0f, 0.0f }, -30.0f, "dB");

    // 2. Time. The knob is 0..1; what it means is a step time, and while Sync
    // is on it reads as a note division (Push shows the host's string, so a
    // synced Step must not read "0.31 s" there). The default is a quarter
    // second: a sixteenth at 60, an eighth at 120.
    auto pTime = std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { id::step, 3 }, "Time",
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
            }));

    // 3. Steps: the pattern ceiling. The hardware stops at 8; the engine
    // allows 16, and the default is the hardware's.
    auto pSteps = std::make_unique<DiscreteInt> (
        juce::ParameterID { id::steps, 4 }, "Steps", 1, BurstEngine::kMaxSteps, 8);

    auto pBlend = floatParam (5, id::blend, "Blend", { 0.0f, 100.0f, 1.0f }, 50.0f, "%");

    // 5. Freeze. Off (the normal state), every gated event becomes a step;
    // on, the pattern is frozen and you play over it. Bypass is the way to
    // stop audio passing, so this is not an arm: armed is the default.
    auto pFreeze = std::make_unique<juce::AudioParameterBool> (
        juce::ParameterID { id::freeze, 2 }, "Freeze", false,
        juce::AudioParameterBoolAttributes().withStringFromValueFunction (
            [] (bool v, int) { return juce::String (v ? "Frozen" : "Off"); }));

    auto pFills = percentWithWord (11, id::fills, "Fills", 30.0f, "Off");
    auto pChaos = percentWithWord (6, id::chaos, "Chaos", 0.0f, "Still");

    // 8. Direction. The choice order is BurstEngine::Direction's, so the index
    // is the enum.
    auto pDirection = std::make_unique<juce::AudioParameterChoice> (
        juce::ParameterID { id::direction, 7 }, "Direction",
        juce::StringArray { "Forward", "Reverse", "Pendulum", "Drunk", "Random" }, 0);

    // 9. Length: the choke. The floor is 5 % rather than 0 so a fully
    // shortened step is still a click and not silence.
    // 9. Decay: the choke, how much of each step its material may sound.
    // Was "Length", which read as the pattern's length beside Steps and Time.
    auto pDecay = floatParam (9, id::length, "Decay", { 5.0f, 100.0f, 1.0f }, 100.0f, "%");

    auto pFade = percentWithWord (10, id::fade, "Fade", 0.0f, "Never");

    // 11. Pitch: the hardware's CLOCK, but only the half of it that
    // repitches. Every step's material is resampled by this many semitones;
    // the step clock is Time's and does not move. (Hint 11 belonged to Full,
    // Replace / Hold at the ceiling, removed the same day it shipped: a full
    // pattern always replaces its oldest step and Freeze stops it taking
    // more.)
    auto pPitch = std::make_unique<DiscreteInt> (
        juce::ParameterID { id::pitch, 8 }, "Pitch", -12, 12, 0,
        juce::AudioParameterIntAttributes().withStringFromValueFunction ([] (int v, int)
        {
            return (v > 0 ? "+" : "") + juce::String (v) + " st";
        }));

    auto pSync = std::move (stepSync); // 12

    // 13, 14. The trims, one on each edge of the window.
    auto pIn = trimParam (17, id::input, "In");
    auto pOut = trimParam (18, id::out, "Out");

    // 15, 16. Glue and Spread, the end of the pattern's chain: the old loop's
    // saturator as a drive, and alternate steps sat left and right. Both off
    // by default, with a word at zero.
    auto pGlue = percentWithWord (12, id::glue, "Glue", 0.0f, "Clean");
    auto pSpread = percentWithWord (13, id::spread, "Spread", 0.0f, "Mono");

    // 17. Mode: what a step does with its material. Possess is the sequencer
    // as it is; the rest are other players for the same pattern (B5).
    auto pMode = std::make_unique<juce::AudioParameterChoice> (
        juce::ParameterID { id::mode, 1 }, "Mode",
        juce::StringArray { "Possess", "Linger", "Legion", "Haunt", "Seize" }, 0);

    // 18. Bar: synced, the pattern restarts from its first step on every bar
    // line. Off, it keeps its own phase on the grid.
    auto pBar = std::make_unique<juce::AudioParameterBool> (
        juce::ParameterID { id::barreset, 16 }, "Bar", false,
        juce::AudioParameterBoolAttributes().withStringFromValueFunction (
            [] (bool v, int) { return juce::String (v ? "On" : "Off"); }));

    // LAST, and hint 1000 so anything added later still sorts before it in AU
    // while staying declared last for VST3. 1 means bypassed, which is the
    // polarity the hosts expect.
    auto pBypass = std::make_unique<juce::AudioParameterBool> (
        juce::ParameterID { id::bypass, 1000 }, "Bypass", false);

    // Push 3 shows eight parameters a page, in this order; VST3 uses this
    // order and AU sorts by the hints above, which count the same way. Page
    // one is what you reach for while playing: the mode, the hand, the
    // clock, the mix, the disorder, the way it runs and the pitch. Page two
    // shapes the steps and the output and holds the setup switches. Page
    // three is the trims and Bypass. The plate's own order is a different
    // thing and stays as it is.
    std::unique_ptr<juce::RangedAudioParameter> ordered[] = {
        std::move (pMode),  std::move (pFreeze), std::move (pTime),      std::move (pSteps),
        std::move (pBlend), std::move (pChaos),  std::move (pDirection), std::move (pPitch),
        std::move (pDecay), std::move (pFade),   std::move (pFills),     std::move (pGlue),
        std::move (pSpread), std::move (pThreshold), std::move (pSync),  std::move (pBar),
        std::move (pIn),    std::move (pOut),    std::move (pBypass) };
    for (auto& prm : ordered)
        layout.add (std::move (prm));

    return layout;
}
} // namespace params
