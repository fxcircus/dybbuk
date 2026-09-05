#include "FactoryPresets.h"

#include "../Parameters.h"

namespace
{
    namespace id = params::id;

    // Echo-Verb: a dark room, not discrete repeats. If you can count the
    // echoes, raise Absorb before touching Time.
    const PresetValue echoVerb[] = {
        { id::time, 0.30f },      { id::decay, 0.78f },    { id::filter, 1800.0f },
        { id::resonance, 20.0f }, { id::absorb, 35.0f },   { id::blend, 45.0f },
        { id::agitate, 15.0f },   { id::agitspeed, 0.12f }, { id::strength, 6.0f },
        { id::out, 0.0f },        { id::timemod, 8.0f }
    };

    // Wow and Flutter: aged tape. The wobble comes from the loop modulating
    // its own Time, not from Time Mod, which would clang.
    const PresetValue wowFlutter[] = {
        { id::time, 0.50f },      { id::decay, 0.55f },    { id::filter, 3500.0f },
        { id::resonance, 10.0f }, { id::absorb, 70.0f },   { id::blend, 50.0f },
        { id::agitate, 45.0f },   { id::agitspeed, 0.45f }, { id::strength, 4.0f },
        { id::out, 0.0f },        { id::timemod, 0.0f }
    };

    // Bat Cave: just under runaway, a resonant low filter chirped by the
    // agitation, with enough Time Mod for a metallic edge.
    const PresetValue batCave[] = {
        { id::time, 0.72f },      { id::decay, 0.92f },    { id::filter, 900.0f },
        { id::resonance, 65.0f }, { id::absorb, 30.0f },   { id::blend, 60.0f },
        { id::agitate, 70.0f },   { id::agitspeed, 6.5f }, { id::strength, 10.0f },
        { id::out, -2.0f },       { id::timemod, 35.0f },  { id::tonespitch, 55.0f }
    };

    // Breathing: a twelve second filter swell with the loop held near unity.
    // Play one note and leave it alone.
    const PresetValue breathing[] = {
        { id::time, 0.55f },      { id::decay, 0.85f },    { id::filter, 2400.0f },
        { id::resonance, 35.0f }, { id::absorb, 45.0f },   { id::blend, 55.0f },
        { id::agitate, 85.0f },   { id::agitspeed, 0.08f }, { id::strength, 6.0f },
        { id::out, 0.0f },        { id::timemod, 0.0f }
    };

    template <int N>
    constexpr int countOf (const PresetValue (&)[N]) { return N; }

    const FactoryPreset kPresets[] = {
        { "Echo-Verb", "A dark room, not discrete repeats.", echoVerb, countOf (echoVerb) },
        { "Wow and Flutter", "Aged tape, wobbling from the loop's own modulation.", wowFlutter, countOf (wowFlutter) },
        { "Bat Cave", "Just under runaway, resonant and chirping.", batCave, countOf (batCave) },
        { "Breathing", "A twelve second filter swell near unity.", breathing, countOf (breathing) },
    };
}

int numFactoryPresets() { return (int) (sizeof (kPresets) / sizeof (kPresets[0])); }

const FactoryPreset& factoryPreset (int index)
{
    return kPresets[index < 0 ? 0 : (index >= numFactoryPresets() ? numFactoryPresets() - 1 : index)];
}
