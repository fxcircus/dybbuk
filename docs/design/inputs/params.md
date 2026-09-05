# Dybbuk parameter layout and state model

Scope: plan sections 2.6 and 2.7 (theme), Phase 4 Clear plumbing and presets, Phase 5 Push ordering. Everything below is written against the template as it exists in `Source/` today plus the idioms in `../teder` and `../infinite_sustainer`. File paths named here are proposals; nothing was created.

Two findings from reading the siblings that shape the design:

1. **AU orders parameters by `(versionHint, hash-of-id)`, not declaration order** (`juce_audio_plugin_client_AU_1.mm:2293-2296`, verified in `build/_deps/juce-src`). The template's `floatParam` gives every parameter hint 1, so the AU build's Push bank 1 would be hash order. Every parameter gets its own ascending hint (the Infinite Sustainer `++hint` pattern). VST3 uses declaration order, so the two must agree: declare in hint order.
2. **The sibling PresetManagers write the theme into preset files and overwrite it on load.** `saveCurrent` does `apvts.copyState()` which carries `darkMode`; the shipped IS preset files literally contain `darkMode="0"`, and `loadPreset` does `apvts.replaceState (incoming)` with no preservation. Neither teder nor IS fixes this. The fix is in section 4.

---

## 1. Ordered parameter table

Push 3 bank 1 = rows 1 to 8, bank 2 = rows 9 to 16, bank 3 = Bypass alone. IDs are the stable strings; hints are the `ParameterID` version hints (never renumber an existing id; Bypass takes 1000 so anything appended later still sorts before it in AU while staying declared last for VST3).

| # | id | Name | Type | Range / mapping | Default | Unit / label | Readout min / mid / max | Tooltip |
|---|---|---|---|---|---|---|---|---|
| 1 | `time` | Time | float, hint 1 | 0..1 linear knob; delay = 27.52 ms * 130.81^t (exp fs_chip, see 3) | 0.45 | none (baked) | "28 ms" / "0.31 s" / "3.60 s"; synced: "1/32" / "1/8D" / "1 bar" | How long sound takes to come back around; long settings get dark and crusty, and sweeping it bends the pitch of what is already in the loop. |
| 2 | `decay` | Decay | float, hint 2 | 0..1.15 linear (unity sits at 87 % of travel; the top 13 % is the red runaway zone) | 0.45 | none | "0.00" / "0.58" / "1.15 runaway" | How many times the sound goes around; past 1.00 the loop feeds itself and runs away. |
| 3 | `filter` | Filter | float, hint 3 | logRange 20..18000 Hz | 8000 | Hz (baked) | "20 Hz" / "600 Hz" / "18000 Hz" | Darkens the loop with every pass; full right lets everything through. |
| 4 | `resonance` | Resonance | float, hint 4 | 0..100 linear; engine maps to SVF damping, self-osc from `kResonanceSelfOscAt = 90` | 15 | % | "0" / "50" / "100" (+ "%") | Sharpens the filter's edge into a whistle, and past 90 percent it sings on its own. |
| 5 | `absorb` | Absorb | float, hint 5 | 0..100 linear | 20 | % | "0" / "50" / "100" | Pulls the wet signal into the earth: quieter and older sounding the further you turn it. |
| 6 | `blend` | Blend | float, hint 6 | 0..100 linear, equal power in engine | 50 | % | "0" / "50" / "100" | Dry to wet balance, equal power in the middle. |
| 7 | `agitate` | Agitate | float, hint 7 | 0..100 linear; macro scaling the three hero routes | 0 | % | "0" / "50" / "100" | How hard the internal modulation shakes the loop: the agitation sweeps the filter, the loop itself wobbles the time, and your playing pushes the decay. |
| 8 | `agitspeed` | Agit Speed | float, hint 8 | logRange 0.016..1000 Hz | 0.35 | none (baked) | "62.5 s" / "4.00 Hz" / "1000 Hz" | How fast the agitation cycles, from a minute per breath up to audio rate. |
| 9 | `strength` | Strength | float, hint 9 | 0..40 dB linear; drive tied to gain in engine | 0.0 | dB (host label) | "0.0" / "20.0" / "40.0" | Input gain into the loop; more than a little starts to drive. |
| 10 | `out` | Out | float, hint 10 | skewedRange(-60, 6, centre -12); 0 dB lands at 81 % of travel; floor = -Inf | 0.0 | dB (host label) | "-Inf" / "-12.0" / "6.0" | Output level after the blend, with the meter behind it. |
| 11 | `timemod` | Time Mod | float, hint 11 | 0..100 linear; `kTimeModMaxOctaves = 2.0` peak FM at 100 | 0 | % | "0" / "50" / "100" | Audio rate wobble on the time from the sub tone, for metallic clangs and ring mod colours. |
| 12 | `timesync` | Time Sync | bool, hint 12 | off / on | off | "Free" / "Sync" | | Locks the Time knob to note values at the host tempo; long divisions cap at 3.6 seconds. |
| 13 | `agitmode` | Agit Mode | choice, hint 13 | {"Loop", "Gate"}; Gate = one AD cycle per input onset above `kGateThresholdDb = -30` | Loop | | | Loop cycles the agitation on its own; Gate fires one cycle each time you play a note. |
| 14 | `toneslevel` | Tones Level | float, hint 14 | 0..100 linear | 0 | % | "Off" / "50" / "100" | Adds the internal drone tone into the loop; off leaves the pure effect. |
| 15 | `tonespitch` | Tones Pitch | float, hint 15 | logRange 32.703..2093 Hz (C1..C7) | 110 | note name (baked) | "C1" / "C4" / "C7"; off-grid shows "A2 +12" | Pitch of the drone and of the sub tone that feeds Time Mod. |
| 16 | `spread` | Spread | float, hint 16 | 0..100 linear | 0 | % | "Mono" / "50" / "100" | Widens the wet signal across the stereo field; zero is the true mono of the hardware. |
| 17 | `bypass` | Bypass | bool, hint 1000, declared LAST | off / on (1 = bypassed) | off | | | Takes Dybbuk out of circuit with a short crossfade; the loop keeps running underneath. |

Notes on the table:

- **Clear is not in the table.** It is a button and a mailbox (section 4). Double-click on the Decay knob triggers the same mailbox. Tooltip for the button: "Empties the loop instantly."
- **Time Mod source (design assumption, please confirm):** the hardware normals Time Mod to the oscillator's sub-harmonic output, so Time Mod = depth of the Tones sub-oscillator into fs_chip at audio rate. The sub runs whenever Time Mod > 0 even with Tones Level at 0, which is why Tones Pitch is useful from day one and why it is in bank 2. The Interference to Time route is a separate hero route scaled by Agitate.
- Agit Speed shows a period below 1 Hz, following the Infinite Sustainer Sweep Rate precedent ("a four second swell is a musical thought, 0.25 Hz is not"). Filter shows integers all the way down because its floor is 20 Hz; the two-decimals-below-100 rule exists for LFO ranges. Both are deliberate departures from the generic `floatParam` unit branches and are written as custom string functions (section 2).
- Defaults are starting proposals. Per CLAUDE.md they belong to the user; nothing downstream should retune them as a side effect.
- `performanceParams()` = `{ id::bypass }`. It is excluded from dirty tracking and preserved across preset loads (section 4). It is not "forced off on load" as the template's `finishLoad` currently does; that would take a bypassed plugin back into circuit.

Layout skeleton (`Source/Parameters.cpp`), with the hint discipline made explicit:

```cpp
juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;
    int hint = 0; // AU sorts by (hint, id-hash); VST3 by declaration. Keep both identical.

    // Time Sync is constructed FIRST so Time's readout can read it, but ADDED at slot 12.
    auto timeSync = std::make_unique<juce::AudioParameterBool> (
        juce::ParameterID { id::timesync, 12 }, "Time Sync", false,
        juce::AudioParameterBoolAttributes().withStringFromValueFunction (
            [] (bool v, int) { return juce::String (v ? "Sync" : "Free"); }));
    auto* syncRaw = timeSync.get(); // owned by the processor for its lifetime, like the lambda that reads it

    layout.add (timeParam (++hint, syncRaw));                                            // 1
    layout.add (decayParam (++hint));                                                    // 2
    layout.add (filterParam (++hint));                                                   // 3
    layout.add (floatParam (++hint, id::resonance, "Resonance", { 0.0f, 100.0f, 1.0f }, 15.0f, "%")); // 4
    layout.add (floatParam (++hint, id::absorb,    "Absorb",    { 0.0f, 100.0f, 1.0f }, 20.0f, "%")); // 5
    layout.add (floatParam (++hint, id::blend,     "Blend",     { 0.0f, 100.0f, 1.0f }, 50.0f, "%")); // 6
    layout.add (floatParam (++hint, id::agitate,   "Agitate",   { 0.0f, 100.0f, 1.0f },  0.0f, "%")); // 7
    layout.add (agitSpeedParam (++hint));                                                // 8
    layout.add (floatParam (++hint, id::strength,  "Strength",  { 0.0f, 40.0f }, 0.0f, "dB"));         // 9
    layout.add (outParam (++hint));                                                      // 10
    layout.add (floatParam (++hint, id::timemod,   "Time Mod",  { 0.0f, 100.0f, 1.0f },  0.0f, "%")); // 11
    jassert (hint + 1 == 12); ++hint; layout.add (std::move (timeSync));                 // 12
    layout.add (std::make_unique<juce::AudioParameterChoice> (
        juce::ParameterID { id::agitmode, ++hint }, "Agit Mode", juce::StringArray { "Loop", "Gate" }, 0)); // 13
    layout.add (wordAtZeroPercent (++hint, id::toneslevel, "Tones Level", 0.0f, "Off"));  // 14
    layout.add (tonesPitchParam (++hint));                                               // 15
    layout.add (wordAtZeroPercent (++hint, id::spread, "Spread", 0.0f, "Mono"));          // 16

    // LAST. Hint 1000 so any parameter added later (hint 17, 18...) still sorts before it in AU.
    layout.add (std::make_unique<juce::AudioParameterBool> (
        juce::ParameterID { id::bypass, 1000 }, "Bypass", false));
    return layout;
}
```

`floatParam` keeps the template body and gains the `int versionHint` first argument (teder has it as a trailing default; leading is safer because it cannot be forgotten). Add `logRange (min, max)` from Infinite Sustainer verbatim (a true exponential; `setSkewForCentre` is a power law with an additive offset and crushes the slow end of a rate control).

---

## 2. String-from-value rules for the special cases

All time mapping lives in a header-only `Source/dsp/TimeMap.h` so `Parameters.cpp` (readouts), the engine (fs_chip) and `Tests/EngineTest.cpp` share one source of truth. `Parameters.cpp` is not in `ENGINE_SOURCES` and must stay out of it, which is why this is a header.

```cpp
// Source/dsp/TimeMap.h  (header-only, no JUCE dependency beyond <cmath>)
namespace timemap
{
    inline constexpr int    kMemorySamples = 5504;                          // ~44 kbit at 8 bits, 3 stages of 1834/1835
    inline constexpr double kFsChipMaxHz   = 200000.0;                      // shortest, cleanest
    inline constexpr double kDelayMinSec   = kMemorySamples / kFsChipMaxHz; // 0.02752 s
    inline constexpr double kDelayMaxSec   = 3.6;                           // the hard ceiling, sync clamps here
    inline constexpr double kFsChipMinHz   = kMemorySamples / kDelayMaxSec; // 1528.9 Hz
    inline constexpr double kTimeSmoothMs  = 20.0;                          // one-pole on log2(fs_chip), free AND sync

    inline double delaySecondsForTime01 (double t)
    {
        t = t < 0.0 ? 0.0 : (t > 1.0 ? 1.0 : t);
        return kDelayMinSec * std::pow (kDelayMaxSec / kDelayMinSec, t);     // 27.52 ms * 130.81^t
    }
    inline double time01ForDelaySeconds (double s)
    {
        s = s < kDelayMinSec ? kDelayMinSec : (s > kDelayMaxSec ? kDelayMaxSec : s);
        return std::log (s / kDelayMinSec) / std::log (kDelayMaxSec / kDelayMinSec);
    }
    inline double fsChipForDelaySeconds (double s) { return kMemorySamples / s; }
}
```

Shared formatter used by Time (free) and by the editor readout strip, identical to the `floatParam` ms branch so both agree:

```cpp
inline juce::String timeReadout (double seconds)
{
    const double ms = seconds * 1000.0;
    return ms >= 100.0 ? juce::String (seconds, 2) + " s"
                       : juce::String (juce::roundToInt (ms)) + " ms";
}
```

### Time (host readout switches on Time Sync)

```cpp
std::unique_ptr<juce::AudioParameterFloat> timeParam (int hint, juce::AudioParameterBool* sync)
{
    return std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { id::time, hint }, "Time",
        juce::NormalisableRange<float> (0.0f, 1.0f), 0.45f,
        juce::AudioParameterFloatAttributes()
            .withLabel ("")                                           // unit is baked in
            .withStringFromValueFunction ([sync] (float v, int)
            {
                // sync->get() is a relaxed atomic read; safe from any thread the host calls this on.
                if (sync != nullptr && sync->get())
                    return juce::String (timemap::kDivisions[timemap::divisionIndexForTime01 (v)].name);
                return timeReadout (timemap::delaySecondsForTime01 (v));
            })
            .withValueFromStringFunction ([] (const juce::String& text)
            {
                // Typing "250 ms", "0.5 s" or a bare number of ms into the editor's box.
                const double n = text.retainCharacters ("0123456789.-").getDoubleValue();
                const double sec = text.containsIgnoreCase ("ms") ? n * 0.001
                                 : text.containsIgnoreCase ("s")  ? n : n * 0.001;
                return (float) timemap::time01ForDelaySeconds (sec);
            }));
}
```

Why the host readout is coupled to sync: Push 3's encoder display shows the host's parameter string. A synced Time reading "0.31 s" on Push while the plugin is playing a 1/8 would be wrong in the one place the plan cares about. The host may cache the text until Time next moves; the editor's own readout strip is live regardless (it reads `uiDelaySeconds`, section 6).

### Decay

```cpp
std::unique_ptr<juce::AudioParameterFloat> decayParam (int hint)
{
    return std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { id::decay, hint }, "Decay",
        juce::NormalisableRange<float> (0.0f, 1.15f), 0.45f,
        juce::AudioParameterFloatAttributes()
            .withLabel ("")
            .withStringFromValueFunction ([] (float v, int)
            {
                const juce::String num (v, 2);                         // "0.45", "1.08"
                return v > 1.0f + 0.005f ? num + " runaway" : num;     // 1.00 itself is not runaway
            }));
}
```

### Out

```cpp
inline constexpr float kOutFloorDb = -60.0f; // the last tick renders -Inf and the engine mutes

std::unique_ptr<juce::AudioParameterFloat> outParam (int hint)
{
    return std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { id::out, hint }, "Out",
        skewedRange (kOutFloorDb, 6.0f, -12.0f), 0.0f,
        juce::AudioParameterFloatAttributes()
            .withLabel ("dB")                                          // host appends: "-Inf dB" is acceptable (IS Dry Level precedent)
            .withStringFromValueFunction ([] (float v, int)
            {
                return v <= kOutFloorDb + 0.05f ? juce::String ("-Inf") : juce::String (v, 1);
            }));
}
```

### Agit Speed

```cpp
std::unique_ptr<juce::AudioParameterFloat> agitSpeedParam (int hint)
{
    return std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { id::agitspeed, hint }, "Agit Speed",
        logRange (0.016f, 1000.0f), 0.35f,
        juce::AudioParameterFloatAttributes()
            .withLabel ("")
            .withStringFromValueFunction ([] (float v, int)
            {
                if (v < 1.0f)    return juce::String (1.0f / v, 1) + " s";          // "62.5 s", "2.9 s"
                if (v < 100.0f)  return juce::String (v, 2) + " Hz";                // "4.00 Hz"
                return juce::String (juce::roundToInt (v)) + " Hz";                  // "262 Hz", "1000 Hz"
            }));
}
```

### The three smaller ones (for completeness)

```cpp
// Filter: integers throughout (floor is 20 Hz, no LFO region).
[] (float v, int) { return juce::String (juce::roundToInt (v)) + " Hz"; }

// Tones Pitch: note name, cents only when off grid by more than 5.
[] (float v, int)
{
    const double midi = 69.0 + 12.0 * std::log2 (v / 440.0);
    const int note = juce::roundToInt (midi);
    const int cents = juce::roundToInt ((midi - note) * 100.0);
    auto name = juce::MidiMessage::getMidiNoteName (note, true, true, 4);   // "A2"
    return std::abs (cents) > 5 ? name + (cents > 0 ? " +" : " ") + juce::String (cents) : name;
}

// wordAtZeroPercent: "%" rule plus a word at the bottom ("Off" for Tones Level, "Mono" for Spread).
[word] (float v, int) { return v < 0.5f ? juce::String (word) : juce::String (juce::roundToInt (v)); }
```

**Editor readout strip rule** (CLAUDE.md: never append a label to a string that already carries a unit or word): `text.containsAnyOf ("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ") ? text : text + " " + param.getLabel()`. That handles "-Inf", "1.08 runaway", "Off", "Mono", "A2 +12", "0.31 s", "1/8" and still yields "6.0 dB" and "20 %".

---

## 3. Time Sync

### Note-value list (14 entries, ascending duration, quarter note = 1 beat)

| index | name | beats | index | name | beats |
|---|---|---|---|---|---|
| 0 | 1/32 | 0.125 | 7 | 1/8D | 0.75 |
| 1 | 1/16T | 1/6 | 8 | 1/4 | 1 |
| 2 | 1/16 | 0.25 | 9 | 1/2T | 4/3 |
| 3 | 1/8T | 1/3 | 10 | 1/4D | 1.5 |
| 4 | 1/16D | 0.375 | 11 | 1/2 | 2 |
| 5 | 1/8 | 0.5 | 12 | 1/2D | 3 |
| 6 | 1/4T | 2/3 | 13 | 1 bar | beatsPerBar (host time signature, `numerator * 4 / denominator`, IS convention) |

"2 bars" is left out on purpose: it exceeds 3.6 s at every tempo below 133 BPM, so it would mostly display a lie. Parked in IDEAS if wanted.

```cpp
// Source/dsp/TimeMap.h (continued)
struct Division { const char* name; double beats; bool isBars; };
inline constexpr int kDivisionCount = 14;
inline constexpr Division kDivisions[kDivisionCount] = {
    { "1/32", 0.125, false }, { "1/16T", 1.0 / 6.0, false }, { "1/16", 0.25, false },
    { "1/8T", 1.0 / 3.0, false }, { "1/16D", 0.375, false }, { "1/8", 0.5, false },
    { "1/4T", 2.0 / 3.0, false }, { "1/8D", 0.75, false }, { "1/4", 1.0, false },
    { "1/2T", 4.0 / 3.0, false }, { "1/4D", 1.5, false }, { "1/2", 2.0, false },
    { "1/2D", 3.0, false }, { "1 bar", 1.0, true } };

// The Time knob keeps its 0..1 range; sync quantises it to 14 detents (the editor draws them).
inline int divisionIndexForTime01 (float t)
{
    const int i = (int) std::lround (t * (kDivisionCount - 1));
    return i < 0 ? 0 : (i >= kDivisionCount ? kDivisionCount - 1 : i);
}

struct SyncedDelay { double seconds; bool clamped; };
inline SyncedDelay syncedDelaySeconds (int index, double bpm, double beatsPerBar)
{
    const auto& d = kDivisions[index];
    const double beats = d.isBars ? d.beats * beatsPerBar : d.beats;
    const double raw = beats * 60.0 / bpm;                       // 1/8 at 120 = 0.25 s; 1 bar at 60 = 4.0 s
    const double s = raw < kDelayMinSec ? kDelayMinSec : (raw > kDelayMaxSec ? kDelayMaxSec : raw);
    return { s, s != raw };
}
// fs_chip target = kMemorySamples / seconds, i.e. the same fsChipForDelaySeconds() free mode uses.
```

### Where it runs

The **processor** resolves sync each block (the engine never sees an `AudioPlayHead`, the IS pattern, which is what lets EngineTest drive sync by writing two doubles):

```cpp
// processBlock, once per block
double beatsPerBar = 4.0;
if (auto* ph = getPlayHead())
    if (const auto pos = ph->getPosition())
    {
        const double bpm = pos->getBpm().orFallback (lastKnownBpm);   // standalone reports a position with bpm unset
        if (std::isfinite (bpm) && bpm > 1.0 && bpm < 999.0) lastKnownBpm = bpm;   // lastKnownBpm starts at 120
        const auto sig = pos->getTimeSignature().orFallback (juce::AudioPlayHead::TimeSignature{});
        if (sig.numerator > 0 && sig.denominator > 0) beatsPerBar = sig.numerator * 4.0 / sig.denominator;
        // ppq / isPlaying also snapshotted into Params for the later Agitation host-sync
    }

const float time01 = pTime->load();
if (pTimeSync->load() >= 0.5f)
{
    const auto sd = timemap::syncedDelaySeconds (timemap::divisionIndexForTime01 (time01), lastKnownBpm, beatsPerBar);
    p.delayTargetSec = (float) sd.seconds;  p.timeSynced = true;  p.syncClamped = sd.clamped;
}
else
{
    p.delayTargetSec = (float) timemap::delaySecondsForTime01 (time01);  p.timeSynced = false;  p.syncClamped = false;
}
```

The **engine** owns the one smoothing path: `delayTargetSec` becomes `log2 (kMemorySamples / delayTargetSec)` and is smoothed by a one-pole with `kTimeSmoothMs = 20` at control rate (every `kControlInterval = 32` samples), with the resulting read/write phase increment linearly interpolated across the 32 samples so there is no division in the inner loop (plan section 6). Time Mod adds to the same log2 value per sample. Free and sync differ only in the number they write into `delayTargetSec`, so switching divisions smears pitch exactly like turning the knob.

### Readouts when clamped

- Host string (no BPM available in `Parameters.cpp`): the division name only, "1 bar".
- Editor readout strip (has `uiDelaySeconds` and `uiSyncClamped` from the processor): unclamped shows "1/8"; clamped shows the division and the ceiling it hit, "1 bar (3.60 s max)" or, for the floor, "1/32 (28 ms min)". The knob's detent stays on the division so the player sees what they asked for and what they got.

---

## 4. Extra state, theme, Clear mailbox, state version

### The audit: everything that affects sound or appearance

| Item | Category | Where it lives | In presets? | Survives preset load? |
|---|---|---|---|---|
| 17 parameters | APVTS | `PARAM` children | yes (bypass excepted) | n/a |
| `bypass` | performance parameter | APVTS | never written to disk as content (it is saved, but ignored: the live value is restored after load) | yes, preserved |
| `theme` (int, 0 = brass default, 1 = alternate) | editor-only property | `apvts.state` root property | **no**, stripped at save | **yes**, preserved |
| `uiScale` (float, 1.0 default; editor window scale) | editor-only property | `apvts.state` root property | no | yes |
| `stateVersion` (int) | format marker | root property, stamped in `stampExtraState` so presets carry it too (today only sessions do) | yes | replaced by the incoming file's value, then migrated |
| `presetName`, `presetDate` | PresetManager bookkeeping | root properties | name yes, date re-derived from the file's `created` | manager rewrites |

Explicitly **not** state: the Clear request (momentary), the ember value (derived), loop buffer contents (a reloaded session starts with an empty loop, as the hardware would after power-up), smoother positions, noise and chaos seeds (the plan wants runs that are never bit-identical), Agit Angle (constant 50/50), hero route depths, Drift depth and clock-bleed level (named constants in the engine: `kRouteAgitToFilterOct = 3.0`, `kRouteInterferenceToTimeOct = 0.35`, `kRouteFollowerToDecay = 0.25`, `kDriftDepthCents = 6`, `kCrustLevelDb = -38`; tune by ear, promote to hidden params only if the user asks).

So `stampExtraState` / `applyExtraState` stamp nothing engine-side today. They stay wired (the template's constructor already hooks them) and carry the version marker, which keeps the door open for the moment something non-parameter arrives:

```cpp
void DybbukProcessor::stampExtraState (juce::ValueTree& state) const
{
    state.setProperty ("stateVersion", currentStateVersion, nullptr);
    // No engine-side extra state yet. theme / uiScale are editor properties that already
    // sit on apvts.state; PresetManager keeps them out of preset files and alive across loads.
}

void DybbukProcessor::applyExtraState (const juce::ValueTree& state)
{
    juce::ignoreUnused (state); // migrate() has already run; nothing to push to the engine
}
```

`getStateInformation` then no longer sets `stateVersion` itself; it calls `stampExtraState` (it already does) and the marker comes along.

### Theme: default dark-brass, one alternate, editor-side, persisted, preset-proof

- Property name `theme`, integer. Drop the template's `darkMode` bool entirely (no shipped Dybbuk sessions exist). `Theme.h` grows `setTheme (int)`, `currentTheme()`, `inline constexpr int kDefaultTheme = 0` (brass), `kThemeCount = 2`.
- Editor constructor: `theme::setTheme ((int) proc.apvts.state.getProperty ("theme", theme::kDefaultTheme));` before any Label caches colours (the template's existing trap note).
- Toggle: `theme::setTheme (next); proc.apvts.state.setProperty ("theme", next, nullptr);` exactly like teder line 191. Setting a property does not touch the parameter listener, so it never dirties the preset.
- `Tests/UISnapshot.cpp`: replace `setProperty ("darkMode", true)` with `setProperty ("theme", 1)` and rename the output to `editor_snapshot_alt.png`; add a third pass that sets a factory preset then flips the theme, to prove the theme survives a load.

The fix in `PresetManager` (template version), three places:

```cpp
// Parameters.h
inline const juce::StringArray& editorOnlyProperties()
{
    static const juce::StringArray props { "theme", "uiScale" };
    return props;
}

// PresetManager::saveCurrent: presets never carry the theme
auto state = apvts.copyState();
if (stampExtraState) stampExtraState (state);
for (const auto& prop : params::editorOnlyProperties()) state.removeProperty (prop, nullptr);
state.removeProperty ("presetDate", nullptr);            // re-derived from "created" on load
referenceState = state;

// PresetManager::loadFromXml (factor the file branch out of loadPreset, as IS did)
juce::NamedValueSet keep;                                // the live editor properties
for (const auto& prop : params::editorOnlyProperties())
    if (apvts.state.hasProperty (prop)) keep.set (prop, apvts.state[prop]);

auto* bypassParam = apvts.getParameter (params::id::bypass);
const float bypassNow = bypassParam->getValue();         // a preset never takes the plugin in or out of circuit

auto incoming = juce::ValueTree::fromXml (*stateXml);
migrate (incoming);                                      // section "state version" below
apvts.replaceState (incoming);
restoreMissingParameterDefaults (incoming);              // port from IS: absent VALUE = default, not "whatever the knob held"

for (const auto& prop : params::editorOnlyProperties())  // live value wins; a preset from an older build never imposes its theme
{
    if (keep.contains (prop)) apvts.state.setProperty (prop, keep[prop], nullptr);
    else                      apvts.state.removeProperty (prop, nullptr);
}
if (bypassParam->getValue() != bypassNow) bypassParam->setValueNotifyingHost (bypassNow);

referenceState = apvts.copyState();
if (applyExtraState) applyExtraState();
finishLoad (name);
```

And two template behaviours to remove: `finishLoad` must not force performance params to 0 (that un-bypasses), and `applyFactoryDefaults` must skip `performanceParams()` for the same reason. The factory branch does not replace the tree, so `theme` is untouched there.

`setStateInformation` (session reload) is the opposite case: the theme must come **from** the session, so it does not run the keep logic; it runs `migrate`, `replaceState`, `restoreMissingParameterDefaults`, `applyExtraState`, `refreshReferenceFromDisk`.

Also rename the preset root tag from the IS leftover `"SustainerPreset"` to `"DybbukPreset"` (the template still checks the old name) and keep the `.preset` extension.

### Clear: editor to processor mailbox

Not a parameter, not automated, not saved. One atomic counter in the engine, incremented by the editor (button, or double-click on Decay), serviced at the next block boundary. A counter rather than a bool so the engine can publish an acknowledgement the button can flash on, and so a second click during a clear restarts it instead of being swallowed.

```cpp
// TimeFilterLoop.h (the engine)
public:
    // MESSAGE THREAD (or any thread): one atomic store, never blocks.
    void requestClear() noexcept { clearRequests.fetch_add (1, std::memory_order_release); }
    std::atomic<int> uiClearsServed { 0 };       // editor compares against its own click count to flash the button

private:
    std::atomic<int> clearRequests { 0 };
    int clearsSeen = 0;                          // audio thread only
    enum class ClearPhase { idle, fadingOut, fadingIn } clearPhase = ClearPhase::idle;
    float clearGain = 1.0f, clearStep = 0.0f;
    static constexpr float kClearFadeMs = 4.0f;  // a hard zero mid-loop is a click at whatever level the wet was

// TimeFilterLoop::process, top of block
if (const int pending = clearRequests.load (std::memory_order_acquire); pending != clearsSeen)
{
    clearsSeen = pending;
    clearPhase = ClearPhase::fadingOut;          // restarting from fadingIn is fine
    clearStep = 1.0f / juce::jmax (1.0f, kClearFadeMs * 0.001f * (float) sr);
}
// per sample: wet *= clearGain; clearGain ramps down; when it reaches 0:
//   wipe(): std::fill on the three PTCore buffers (5504 floats total, ~22 KB, trivially cheap, no allocation),
//           reset SVF state, absorb shelf state, reconstruction filters, DC blocker, saturator memory;
//           leave fs_chip smoothing, Agitation phase, Interference state and all SmoothedValues untouched
//   uiClearsServed.store (clearsSeen); clearPhase = fadingIn; then ramp clearGain back to 1.
```

`DybbukProcessor::requestClear()` forwards to the engine. The processor also publishes `uiLoopEnergy` (section 6) so the ember visibly dies with the clear.

### State version policy

- `currentStateVersion = 1` for this layout. It rides on the root property `stateVersion` in both sessions and presets (via `stampExtraState`).
- Bump only when an existing id changes meaning, range or units, or a root property is renamed. Adding a parameter is not a bump (absent value = default via `restoreMissingParameterDefaults`); removing one is not a bump (stale `PARAM` nodes are ignored).
- `static void migrate (juce::ValueTree& tree)` in `PluginProcessor.cpp`, called before every `replaceState` (session and preset). Reads `stateVersion` (absent = 0 = pre-1 dev builds, treated as 1 since nothing shipped), applies `case` steps in order, writes `currentStateVersion`. Each bump gets a line in `docs/PROGRESS.md` and a `case`.
- `ParameterID` version hints are a different thing (AU ordering). They are never renumbered for an existing id, and a new parameter takes the next integer after 16 (Bypass holds 1000).

---

## 5. Factory presets

### How they ship: code tables, not files

The template's `PresetManager` is file-backed with one virtual factory entry ("Init" = parameter defaults). Infinite Sustainer embeds `.sustpreset` XML through `juce_add_binary_data`, which needs a binary-data target linked into the plugin, UISnapshot and a `PresetProbe`, and a preset renamed on disk silently vanishes (that is why PresetProbe exists). Dybbuk's four presets are 16 numbers each, so ship them as **tables in code keyed by `params::id::*` constants**: a typo in an id fails to compile, the values are reviewable in a diff, no CMake asset target, and the factory list is `"Init"` plus four `Info { name, {}, factory = true }` entries.

```cpp
// Source/state/FactoryPresets.h
struct FactoryPreset
{
    const char* name;
    std::initializer_list<std::pair<const char*, float>> values;   // ids not listed take their defaults
};
const juce::Array<FactoryPreset>& factoryPresets();

// PresetManager::loadPreset, factory-with-table branch:
applyFactoryDefaults();                                  // skips performanceParams()
for (const auto& [id, v] : preset.values)
    if (auto* p = apvts.getParameter (id)) p->setValueNotifyingHost (p->convertTo0to1 (v));
referenceState = apvts.copyState();                      // reset bell reverts to the table, theme untouched
if (applyExtraState) applyExtraState();
finishLoad (preset.name);
apvts.state.removeProperty ("presetDate", nullptr);
```

Listing order: "Init", then the four in the order below (fixed, they are a tour), then user presets A-Z. A user preset with the same name shadows the factory one (IS rule). Verification: UISnapshot loads each factory preset in turn and writes `editor_snapshot_<name>.png`, which also reviews every readout in the table; EngineTest gets a `presets` scenario that runs each value set through the engine for 4 s of a 220 Hz burst and prints wet RMS at 1 s and 3 s plus a runaway flag, so a table edit that silences or blows up a preset shows as a number.

### The four value sets

Derived from the manual's Patch Corner descriptions as summarised in the plan (Echo-Verb: short-ish Time, moderate Decay, Filter darkened; Wow and Flutter: Blend 50, Absorb or Filter high; Bat Cave: long, dark, resonant, agitated; Breathing: slow agitation on the filter). Starting points to tune by ear; readouts shown so the tables can be checked against the screenshots.

| id | Echo-Verb | Wow and Flutter | Bat Cave | Breathing |
|---|---|---|---|---|
| `time` | 0.30 ("0.12 s") | 0.50 ("0.31 s") | 0.72 ("0.92 s") | 0.55 ("0.40 s") |
| `decay` | 0.78 | 0.55 | 0.92 | 0.85 |
| `filter` | 1800 Hz | 3500 Hz | 900 Hz | 2400 Hz |
| `resonance` | 20 | 10 | 65 | 35 |
| `absorb` | 35 | 70 | 30 | 45 |
| `blend` | 45 | 50 | 60 | 55 |
| `agitate` | 15 | 45 | 70 | 85 |
| `agitspeed` | 0.12 Hz ("8.3 s") | 0.45 Hz ("2.2 s") | 6.5 Hz ("6.50 Hz") | 0.08 Hz ("12.5 s") |
| `strength` | 6.0 dB | 4.0 dB | 10.0 dB | 6.0 dB |
| `out` | 0.0 dB | 0.0 dB | -2.0 dB | 0.0 dB |
| `timemod` | 8 | 0 | 35 | 0 |
| `timesync` | Free | Free | Free | Free |
| `agitmode` | Loop | Loop | Loop | Loop |
| `toneslevel` | Off | Off | Off | Off |
| `tonespitch` | 110 Hz (A2) | 110 Hz (A2) | 55 Hz (A1, so the sub feeding Time Mod growls at 27.5 Hz) | 110 Hz (A2) |
| `spread` | Mono | Mono | Mono | Mono |
| `bypass` | not in table | | | |

Intent per preset, so the tuning session knows what to listen for: Echo-Verb should read as a dark room, not as discrete repeats (if repeats are audible, raise Absorb before touching Time). Wow and Flutter gets its wobble from the Interference to Time route opened by Agitate, not from Time Mod (which would clang); Absorb 70 is the "aged tape". Bat Cave sits just under runaway with a resonant low filter chirped at 6.5 Hz by the agitation, plus a touch of audio-rate Time Mod for the metallic edge. Breathing is a 12.5 s filter swell at high Agitate with the loop held near unity.

---

## 6. The per-block `Params` snapshot

`TimeFilterLoop::Params` (replaces `ExampleEngine::Params`; the engine never sees the APVTS or the play head). Units are engine units, converted once in `processBlock`.

```cpp
struct Params
{
    // Time. The processor has already resolved free vs sync into one number.
    float delayTargetSec = 0.2467f;   // timemap::delaySecondsForTime01 (0.45); engine smooths log2 (N / this)
    bool  timeSynced = false;         // for the UI publish only; the engine does not branch on it
    bool  syncClamped = false;        // same
    float timeMod01 = 0.0f;           // 0..1, times kTimeModMaxOctaves peak FM from the sub oscillator

    // The loop.
    float decay = 0.45f;              // 0..1.15 linear feedback gain, bounded by the loop saturator
    float filterHz = 8000.0f;         // in-loop SVF cutoff
    float resonance01 = 0.15f;        // 0..1 (knob / 100); self-osc from kResonanceSelfOscAt / 100
    float absorb01 = 0.20f;           // 0..1 attenuation + tilt
    float blend01 = 0.50f;            // 0..1, equal power

    // Modulation.
    float agitate01 = 0.0f;           // macro over the three hero routes
    float agitSpeedHz = 0.35f;        // 0.016..1000
    int   agitMode = 0;               // 0 loop, 1 gate

    // Gain staging.
    float strengthDb = 0.0f;          // 0..40; drive follows gain inside the engine
    float outGain = 1.0f;             // linear; 0.0 exactly when Out sits at the -Inf floor
    bool  outMuted = false;

    // Phase 4 options (present from day one, default off).
    float tonesLevel01 = 0.0f;
    float tonesPitchHz = 110.0f;
    float spread01 = 0.0f;

    // Host transport, snapshotted for Time Sync now and Agitation host-sync later.
    double bpm = 120.0;               // lastKnownBpm (never garbage, never "unset")
    double ppqPosition = 0.0;
    bool   ppqValid = false;
    bool   transportPlaying = false;
    float  beatsPerBar = 4.0f;

    bool bypass = false;              // engine keeps running; processor feeds it SILENCE while bypassed
};
```

Conversions in `processBlock` (one snapshot per block, cached `std::atomic<float>*` for all 17 ids):

- `p.resonance01 = pResonance->load() * 0.01f` and likewise absorb, blend, agitate, timeMod, tonesLevel, spread.
- `p.outMuted = pOut->load() <= kOutFloorDb + 0.05f; p.outGain = outMuted ? 0.0f : Decibels::decibelsToGain (pOut->load());` The engine smooths `outGain` (linear, 20 ms) so the fader never clicks at the floor.
- `p.agitMode = jlimit (0, 1, roundToInt (pAgitMode->load()))`.
- Bypass: the template's 20 ms `bypassMix` crossfade stays exactly as is. Decision for this plugin: while bypassed the engine sees silence (looper/freezer convention, and it means a runaway loop keeps evolving on its own and returns as it was rather than having eaten what you played while out of circuit).

Engine to editor atomics (polled at 30 Hz, tearing acceptable):

| atomic | type | feeds |
|---|---|---|
| `uiOutputLevel` | float peak, post Out | the OUT slider meter |
| `uiLoopEnergy` | float 0..1, log-mapped RMS at the loop sum node (`kEmberFloorDb = -60`, `kEmberCeilingDb = 0`) | the ember |
| `uiDelaySeconds` | float, the smoothed actual delay | Time readout strip (free: `timeReadout`; synced: division name, with the clamp note) |
| `uiSyncClamped` | bool | the clamp note |
| `uiClearsServed` | int | Clear button flash and Decay double-click feedback |

`docs/PLAN.md`'s empty parameter table should be filled from section 1, and the "Scope changes" block in `docs/PROGRESS.md` should record the two idiom departures up front: per-parameter version hints for AU ordering, and editor-only properties kept out of presets and preserved across loads.