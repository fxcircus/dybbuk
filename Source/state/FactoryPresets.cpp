#include "FactoryPresets.h"

#include "../Parameters.h"

namespace
{
    namespace id = params::id;

    // Starting points for the Burst direction, in raw units: Step is the knob
    // position (0..1), written through knob01ForStepSeconds so the table
    // says the time it means. Absent parameters are restored to their
    // defaults by PresetManager, so anything a preset does not name is
    // deliberate: none of these set Freeze (you decide when to hold) and
    // nothing here sets Bypass, which is performance state.

    // Sixteenths: the pattern on the host's grid. Play short muted notes and
    // they land as sixteenths whatever you played them as.
    const PresetValue sixteenths[] = {
        { id::step, 2.0f / 13.0f },   // the 1/16 detent
        { id::stepsync, 1.0f },       { id::steps, 8.0f },     { id::blend, 50.0f },
        { id::fills, 20.0f },         { id::chaos, 0.0f },     { id::direction, 0.0f },
        { id::length, 100.0f }
    };

    // Stutter: four short steps, half choked, with chaos doing the ratchets.
    // If it reads as a tremolo, play fewer notes.
    const PresetValue stutter[] = {
        { id::step, params::knob01ForStepSeconds (0.060) },
        { id::steps, 4.0f },          { id::blend, 60.0f },    { id::fills, 30.0f },
        { id::chaos, 40.0f },         { id::direction, 0.0f }, { id::length, 50.0f },
        { id::fade, 0.0f }
    };

    // Erosion: the long pattern that forgets. Sixteen steps, each a little
    // quieter every time round, the oldest replaced as you keep playing, and
    // a drunk walk through them so the same thing rarely comes back twice.
    const PresetValue erosion[] = {
        { id::step, params::knob01ForStepSeconds (0.200) },
        { id::steps, 16.0f },         { id::blend, 55.0f },    { id::fills, 25.0f },
        { id::chaos, 10.0f },         { id::direction, 3.0f }, { id::length, 100.0f },   // Drunk
        { id::fade, 35.0f }
    };

    // Pendulum: six steps played there and back. The turnaround is the
    // rhythm; leave the ends of the pattern for your longest notes.
    const PresetValue pendulum[] = {
        { id::step, params::knob01ForStepSeconds (0.180) },
        { id::steps, 6.0f },          { id::blend, 50.0f },    { id::fills, 15.0f },
        { id::chaos, 0.0f },          { id::direction, 2.0f }, { id::length, 85.0f },
        { id::fade, 0.0f }
    };

    // Deadpan: no fills, no chaos, no fade, nothing choked. The last eight
    // things you played, in order, forever. The one to learn the gate on.
    const PresetValue deadpan[] = {
        { id::step, params::knob01ForStepSeconds (0.250) },
        { id::steps, 8.0f },          { id::blend, 50.0f },    { id::fills, 0.0f },
        { id::chaos, 0.0f },          { id::direction, 0.0f }, { id::length, 100.0f },
        { id::fade, 0.0f }
    };

    template <int N>
    constexpr int countOf (const PresetValue (&)[N]) { return N; }

    const FactoryPreset kPresets[] = {
        { "Sixteenths", "The pattern on the host's grid.", sixteenths, countOf (sixteenths) },
        { "Stutter", "Four short steps, half choked, ratcheting.", stutter, countOf (stutter) },
        { "Erosion", "Sixteen steps that forget as you play.", erosion, countOf (erosion) },
        { "Pendulum", "Six steps, there and back.", pendulum, countOf (pendulum) },
        { "Deadpan", "The last eight things you played, in order.", deadpan, countOf (deadpan) },
    };
}

int numFactoryPresets() { return (int) (sizeof (kPresets) / sizeof (kPresets[0])); }

const FactoryPreset& factoryPreset (int index)
{
    return kPresets[index < 0 ? 0 : (index >= numFactoryPresets() ? numFactoryPresets() - 1 : index)];
}
