#include "FactoryPresets.h"

#include "../Parameters.h"

namespace
{
    namespace id = params::id;

    // Starting points for the pattern maker, in raw units. Time is the knob
    // position (0..1), written through knob01ForStepSeconds so the table says
    // the time it means, or as a detent index over 13 for a synced one.
    // Absent parameters are restored to their defaults by PresetManager, so
    // anything a preset does not name is deliberate: none of these set
    // Threshold (you set that to your instrument and your room), In or Out
    // (levels), Freeze (you decide when to hold) or Bypass (performance
    // state).
    //
    // Ordered plainest first and strangest last, because that is how the list
    // is read. Every mode has at least one; Golem has four because it is the
    // base player and each one is a different argument.

    // Deadpan: no fills, no chaos, nothing choked, nothing forgotten. The
    // last eight things you played, in order, forever. The one to learn the
    // gate on: what you hear is your own playing and the threshold.
    const PresetValue deadpan[] = {
        { id::step, params::knob01ForStepSeconds (0.250) },
        { id::steps, 8.0f },          { id::blend, 50.0f },    { id::fills, 0.0f },
        { id::chaos, 0.0f },          { id::direction, 0.0f }, { id::length, 100.0f },
        { id::feedback, 100.0f }
    };

    // Footfall: the same thing on the host's grid, with the ring cut off each
    // note so only the knock is left, and the pattern starting again from its
    // first step on every bar line. Decay 45 leaves about 56 ms of a step
    // sounding at 120 BPM.
    const PresetValue footfall[] = {
        { id::step, 2.0f / 13.0f },   // the 1/16 detent
        { id::stepsync, 1.0f },       { id::barreset, 1.0f },  { id::steps, 8.0f },
        { id::blend, 55.0f },         { id::fills, 25.0f },    { id::chaos, 15.0f },
        { id::length, 45.0f },        { id::feedback, 85.0f }, { id::spread, 50.0f }
    };

    // Unsay: Mirror over a reversed order, so every note swells up to its own
    // attack and the phrase comes back last one first. Fills is named at zero
    // because the order is the whole promise here and the default would
    // scramble it the moment you freeze.
    const PresetValue unsay[] = {
        { id::mode, 6.0f },
        { id::step, params::knob01ForStepSeconds (0.300) },
        { id::steps, 6.0f },          { id::blend, 60.0f },    { id::fills, 0.0f },
        { id::chaos, 10.0f },         { id::direction, 1.0f }, { id::length, 50.0f },
        { id::feedback, 100.0f }
    };

    // Overhang: Wraith, with Decay at four ticks, which is exactly what the
    // haunt slots hold, so the oldest moment is under the floor before the
    // next one needs its slot. The step is longer than a plucked note on
    // purpose: the gap in each step is where the ghosts sound alone.
    const PresetValue overhang[] = {
        { id::mode, 1.0f },
        { id::step, params::knob01ForStepSeconds (0.360) },
        { id::steps, 6.0f },          { id::blend, 55.0f },    { id::fills, 0.0f },
        { id::chaos, 10.0f },         { id::length, 45.0f },   { id::feedback, 100.0f },
        { id::glue, 25.0f },          { id::spread, 40.0f }
    };

    // Stack: Legion with Pitch read as the interval, a fourth rather than a
    // fifth so it does not double the strongest overtone of a guitar string.
    // Glue is structural here, not colour: three voices stack about 3 dB
    // hotter than one.
    const PresetValue stack[] = {
        { id::mode, 3.0f },
        { id::step, params::knob01ForStepSeconds (0.300) },
        { id::steps, 6.0f },          { id::blend, 65.0f },    { id::chaos, 0.0f },
        { id::length, 65.0f },        { id::feedback, 100.0f }, { id::pitch, 5.0f },
        { id::glue, 30.0f }
    };

    // Drag: Trance at 4.8x slower, which at a 0.6 s step spreads the first
    // 125 ms of each note across the whole step. Attacks held, not whole
    // notes slowed. Feedback 75 costs a step 2.5 dB a play, so the wash sinks
    // instead of piling up.
    const PresetValue drag[] = {
        { id::mode, 2.0f },
        { id::step, params::knob01ForStepSeconds (0.600) },
        { id::steps, 6.0f },          { id::blend, 60.0f },    { id::fills, 0.0f },
        { id::chaos, 15.0f },         { id::length, 75.0f },   { id::feedback, 75.0f }
    };

    // Undertow: the long pattern that forgets, an octave down. Pitch -12
    // halves the play rate, so the choke takes 150 ms of material and spends
    // the full step on it. Blend sits under what you are playing, because a
    // bass line made of your own picking belongs below the picking.
    const PresetValue undertow[] = {
        { id::mode, 0.0f },
        { id::step, params::knob01ForStepSeconds (0.300) },
        { id::steps, 16.0f },         { id::blend, 45.0f },    { id::fills, 0.0f },
        { id::chaos, 8.0f },          { id::direction, 3.0f }, { id::length, 100.0f },   // Drunk
        { id::feedback, 65.0f },      { id::pitch, -12.0f }
    };

    // Lean: Tremor, where Fills is the ratchet's density. Until you lean on a
    // note this is the plain sequencer; the moment you do, the pattern stops
    // where it stands and shakes that step three times over until you let go.
    const PresetValue lean[] = {
        { id::mode, 4.0f },
        { id::step, params::knob01ForStepSeconds (0.200) },
        { id::steps, 6.0f },          { id::blend, 60.0f },    { id::fills, 60.0f },
        { id::chaos, 10.0f },         { id::length, 70.0f },   { id::feedback, 100.0f },
        { id::glue, 25.0f }
    };

    // Drill: Rattle on three steps, the head of each note looped about
    // twenty five times a second, thrown there and back across the stereo
    // field. Three steps and a pendulum make the turnaround the rhythm.
    const PresetValue drill[] = {
        { id::mode, 5.0f },
        { id::step, params::knob01ForStepSeconds (0.180) },
        { id::steps, 3.0f },          { id::blend, 45.0f },    { id::chaos, 20.0f },
        { id::direction, 2.0f },      { id::length, 62.0f },   { id::feedback, 100.0f },
        { id::glue, 30.0f },          { id::spread, 65.0f }
    };

    // Dust: Miasma at a small grain, so each note returns as a dry crackle of
    // itself rather than a wash. Low under your playing, and thinning over a
    // couple of minutes.
    const PresetValue dust[] = {
        { id::mode, 7.0f },
        { id::step, params::knob01ForStepSeconds (0.250) },
        { id::steps, 8.0f },          { id::blend, 35.0f },    { id::chaos, 20.0f },
        { id::length, 18.0f },        { id::feedback, 80.0f }
    };

    // Misfire: most steps go wrong and none go off the clock. Past half depth
    // Chaos stacks two and three events on the same step, so this is skips,
    // ratchets, reversals, sour intervals and ghosts, all of it still landing
    // exactly on the tick.
    const PresetValue misfire[] = {
        { id::mode, 0.0f },
        { id::step, params::knob01ForStepSeconds (0.160) },
        { id::steps, 8.0f },          { id::blend, 70.0f },    { id::chaos, 65.0f },
        { id::direction, 4.0f },      { id::length, 70.0f },   { id::feedback, 90.0f },   // Random
        { id::glue, 40.0f },          { id::spread, 55.0f }
    };

    template <int N>
    constexpr int countOf (const PresetValue (&)[N]) { return N; }

    const FactoryPreset kPresets[] = {
        { "Deadpan", "The last eight things you played, in order.", deadpan, countOf (deadpan) },
        { "Footfall", "Sixteenths on the host's grid, cut to their strikes.", footfall, countOf (footfall) },
        { "Unsay", "Six steps backwards, last one first.", unsay, countOf (unsay) },
        { "Overhang", "Each step leaves its last moment behind.", overhang, countOf (overhang) },
        { "Stack", "Every step sung by three voices a fourth apart.", stack, countOf (stack) },
        { "Drag", "The phrase again at a fifth of the speed.", drag, countOf (drag) },
        { "Undertow", "Sixteen steps an octave down, wandering and dying.", undertow, countOf (undertow) },
        { "Lean", "The step under your hand, shaken in threes.", lean, countOf (lean) },
        { "Drill", "Three steps of buzz, thrown side to side.", drill, countOf (drill) },
        { "Dust", "What you played, ground down to grains.", dust, countOf (dust) },
        { "Misfire", "Most steps go wrong, none go off the clock.", misfire, countOf (misfire) },
    };
}

int numFactoryPresets() { return (int) (sizeof (kPresets) / sizeof (kPresets[0])); }

const FactoryPreset& factoryPreset (int index)
{
    return kPresets[index < 0 ? 0 : (index >= numFactoryPresets() ? numFactoryPresets() - 1 : index)];
}
