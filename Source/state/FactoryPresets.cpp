#include "FactoryPresets.h"

#include "../Parameters.h"

namespace
{
    namespace id = params::id;

    // Re-voiced once, against a settled loop, after the wildness pass. Several
    // constants these were originally tuned against have moved: Absorb costs
    // 2 dB per iteration rather than 4 and 6 dB of wet rather than 18, the
    // runaway zone runs to 1.45 rather than 1.15, Resonance spreads its Q over
    // the whole knob instead of the last fifth, and Agitation sweeps the filter
    // around the knob rather than upward from it.
    //
    // Two rules held throughout. Absent parameters are restored to their
    // defaults by PresetManager, so anything a preset does not name is
    // deliberate. And nothing here sets Bypass, which is performance state.

    // Echo-Verb: a dark room, not discrete repeats. If you can count the
    // echoes, raise Absorb before touching Time.
    const PresetValue echoVerb[] = {
        { id::time, 0.30f },      { id::decay, 0.86f },     { id::filter, 1800.0f },
        { id::resonance, 30.0f }, { id::absorb, 35.0f },    { id::blend, 45.0f },
        { id::agitate, 15.0f },   { id::agitspeed, 0.12f }, { id::strength, 6.0f },
        { id::out, 0.0f },        { id::timemod, 8.0f },    { id::chaos, 12.0f }
    };

    // Wow and Flutter: aged tape. The wobble is the Agitation bending the
    // delay clock at a rate you set, which is what "wow and flutter" means --
    // before Agitation could reach the clock this preset's wobble was actually
    // chaos, at no rate anybody had chosen.
    const PresetValue wowFlutter[] = {
        { id::time, 0.50f },      { id::decay, 0.62f },     { id::filter, 2400.0f },
        { id::resonance, 18.0f }, { id::absorb, 55.0f },    { id::blend, 50.0f },
        { id::agitate, 30.0f },   { id::agitspeed, 1.6f },  { id::strength, 4.0f },
        { id::out, 0.0f },        { id::timemod, 0.0f },    { id::chaos, 10.0f },
        { id::crust, 30.0f }
    };

    // Bat Cave: over the line, not under it. The old table sat at Decay 0.92
    // and called itself "just under runaway" while being 2.15 dB below unity,
    // against a ceiling it could not have reached anyway. It is in the red now.
    //
    // tonespitch 55 Hz with no toneslevel is deliberate and is NOT an oversight:
    // Tones::sub runs regardless of the drone's level and is the Time Mod
    // modulator, so 55 Hz sets this preset's FM grid to 27.5 Hz, which is the
    // metallic edge the name asks for. Adding a drone here would be a different
    // preset.
    const PresetValue batCave[] = {
        { id::time, 0.72f },      { id::decay, 1.18f },     { id::filter, 900.0f },
        { id::resonance, 72.0f }, { id::absorb, 25.0f },    { id::blend, 60.0f },
        { id::agitate, 55.0f },   { id::agitspeed, 6.5f },  { id::strength, 10.0f },
        { id::out, -2.0f },       { id::timemod, 45.0f },   { id::tonespitch, 55.0f },
        { id::chaos, 35.0f },     { id::colour, 20.0f }
    };

    // Breathing: a twelve second swell with the loop held near unity. Play one
    // note and leave it alone.
    const PresetValue breathing[] = {
        { id::time, 0.55f },      { id::decay, 1.02f },     { id::filter, 1600.0f },
        { id::resonance, 45.0f }, { id::absorb, 40.0f },    { id::blend, 55.0f },
        { id::agitate, 85.0f },   { id::agitspeed, 0.08f }, { id::strength, 6.0f },
        { id::out, 0.0f },        { id::timemod, 0.0f },    { id::chaos, 25.0f }
    };

    // Possession: unplug the input. It plays itself.
    //
    // The one preset with no input dependency at all, and the reason the Tones
    // destinations exist: the loop's own energy drives the chaos, the chaos
    // moves the drone's pitch and level, the drone writes new material into the
    // delay, and that changes the energy. Measured by `EngineTest chaosloop` --
    // 36 distinct pitch plateaux over 83 seconds, where a loop that could only
    // smear what it already held would produce one.
    const PresetValue possession[] = {
        { id::time, 0.55f },       { id::decay, 1.05f },     { id::filter, 1400.0f },
        { id::resonance, 60.0f },  { id::absorb, 15.0f },    { id::blend, 100.0f },
        { id::agitate, 20.0f },    { id::agitspeed, 0.2f },  { id::strength, 0.0f },
        { id::out, -3.0f },        { id::timemod, 0.0f },    { id::chaos, 80.0f },
        { id::toneslevel, 60.0f }, { id::tonespitch, 65.4f }, { id::tonesfold, 40.0f },
        { id::colour, 30.0f },     { id::spread, 35.0f }
    };

    template <int N>
    constexpr int countOf (const PresetValue (&)[N]) { return N; }

    const FactoryPreset kPresets[] = {
        { "Echo-Verb", "A dark room, not discrete repeats.", echoVerb, countOf (echoVerb) },
        { "Wow and Flutter", "Aged tape, wobbling at a rate you set.", wowFlutter, countOf (wowFlutter) },
        { "Bat Cave", "Over the line, resonant and chirping.", batCave, countOf (batCave) },
        { "Breathing", "A twelve second swell near unity.", breathing, countOf (breathing) },
        { "Possession", "Unplug the input. It plays itself.", possession, countOf (possession) },
    };
}

int numFactoryPresets() { return (int) (sizeof (kPresets) / sizeof (kPresets[0])); }

const FactoryPreset& factoryPreset (int index)
{
    return kPresets[index < 0 ? 0 : (index >= numFactoryPresets() ? numFactoryPresets() - 1 : index)];
}
