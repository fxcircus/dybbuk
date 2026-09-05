# 03. Parameters, state, and the engine facade

Source: `inputs/params.md`, adjusted where it met the shipped core. This
records what is actually in the code.

## 1. Ordered parameter table

Order matters twice. VST3 presents parameters in declaration order; AU sorts
them by (version hint, hash of id). The two only agree if every parameter
carries its own ascending hint declared in the same order, which is what
`Source/Parameters.cpp` does. The first eight are Push 3 bank 1.

| # | id | Name | Type | Range | Default | Readout examples |
|---|---|---|---|---|---|---|
| 1 | `time` | Time | float | 0..1 (27.5 ms to 3.67 s, exponential) | 0.45 | "28 ms", "0.25 s", "3.67 s"; synced: "1/8" |
| 2 | `decay` | Decay | float | 0..1.15 | 0.45 | "0.45", "1.08 runaway" |
| 3 | `filter` | Filter | float | 20..18000 Hz, log | 8000 | "20", "8000" (label Hz) |
| 4 | `resonance` | Resonance | float | 0..100 | 15 | "15" (label %) |
| 5 | `absorb` | Absorb | float | 0..100 | 20 | "20" |
| 6 | `blend` | Blend | float | 0..100 | 50 | "50" |
| 7 | `agitate` | Agitate | float | 0..100 | 0 | "0" |
| 8 | `agitspeed` | Agit Speed | float | 0.016..1000 Hz, log | 0.35 | "12.5 s", "4.00 Hz", "262 Hz" |
| 9 | `strength` | Strength | float | 0..40 dB | 0 | "0.0" (label dB) |
| 10 | `out` | Out | float | -60..+6 dB, skewed to -12 | 0 | "-Inf", "0.0" |
| 11 | `timemod` | Time Mod | float | 0..100 | 0 | "0" |
| 12 | `timesync` | Time Sync | bool | Free / Sync | off | "Free" |
| 13 | `agitmode` | Agit Mode | choice | Loop / Gate | Loop | "Loop" |
| 14 | `toneslevel` | Tones Level | float | 0..100 | 0 | "Off", "50 %" |
| 15 | `tonespitch` | Tones Pitch | float | 32.7..2093 Hz, log | 110 | "A2", "A2 +12" |
| 16 | `spread` | Spread | float | 0..100 | 0 | "Mono", "50 %" |
| 17 | `bypass` | Bypass | bool, hint 1000, declared LAST | off / on (1 = bypassed) | off | |

Readout rules follow CLAUDE.md. Three deliberate departures from the generic
helper, each because the generic rule reads wrong here:

- **Agit Speed shows a period below 1 Hz** ("12.5 s"). A twelve second swell is
  a musical thought; 0.08 Hz is arithmetic.
- **Filter shows integers all the way down.** The two-decimals-below-100 rule
  exists for LFO rates, and Filter's floor is 20 Hz.
- **Time bakes its unit** and reads as a note division while Sync is on,
  because Push shows the host's parameter string and a synced Time reading
  "0.31 s" there would be wrong in the one place the plan cares about.

Parameters 7, 8, 11, 13, 14, 15 and 16 exist and are automatable but are not
read by the engine yet (Phase 3 and 4). They are declared now so nothing ever
has to be reordered, which would break every saved session.

## 2. Time Sync

Fourteen divisions from 1/32 to 1 bar, in `Source/dsp/TimeMap.h`. "2 bars" is
absent on purpose: it exceeds the 3.67 s ceiling below 131 BPM, so it would
mostly display a value the engine cannot deliver.

The processor resolves free versus synced into a single Time value each block,
so the engine never sees an `AudioPlayHead` and both paths go through the same
20 ms log-domain smoother. Changing a sync division therefore smears the pitch
exactly like turning the knob, which is the hardware behaviour the plan asks
for. Tempo falls back to the last one actually reported (the standalone
reports a position with every field unset), and a division longer than the
memory clamps, with the editor showing "(capped)".

## 3. State

| Item | Category | In presets | Survives a preset load |
|---|---|---|---|
| 17 parameters | APVTS | yes, except bypass | n/a |
| `bypass` | performance parameter | ignored on load | yes, preserved |
| `theme` (int, 0 = brass) | editor property on the tree root | no, stripped at save | yes, preserved |
| `uiScale` (float) | editor property | no | yes |
| `stateVersion` | format marker, stamped by `stampExtraState` | yes | migrated |
| `presetName`, `presetDate` | PresetManager bookkeeping | name yes | rewritten |

Explicitly not state: the Clear request (momentary), the loop's buffer contents
(a reloaded session starts empty, as the hardware would after power up),
smoother positions, and the noise and chaos seeds (the plan wants runs that are
never bit-identical).

Four template behaviours were wrong for this plugin and are fixed:

1. `saveCurrent` wrote the theme into every preset file, so loading someone
   else's preset changed your colours. Editor properties are now stripped.
2. `loadPreset` replaced the whole tree, so the theme came back from the file.
   Editor properties are now lifted out and put back.
3. `finishLoad` forced performance parameters to zero, which took a bypassed
   plugin back into circuit whenever you browsed presets. Bypass is now
   preserved across a load.
4. A preset saved before a parameter existed left that parameter at whatever
   the last patch put on the knob. `restoreMissingParameterDefaults` now sets
   it to its default, so a preset sounds the same every time it is loaded.

The preset root tag is `DybbukPreset` (the template still had the Infinite
Sustainer tag) and `migrateState` runs on every incoming tree, session or
preset, before it replaces the live one.

## 4. Clear

Not a parameter, not automated, not saved: a Clear in a session recall would
empty the loop on load. The editor calls `DybbukProcessor::requestClear()`,
which increments an atomic counter in the engine. The audio thread compares it
at the next block boundary, fades the wet out over 6 ms, wipes every buffer and
filter state, then fades back in, and publishes `uiClearsServed` so the button
can flash on the acknowledgement rather than on the click.

## 5. Factory presets

Four tables in `Source/state/FactoryPresets.cpp`, keyed by parameter id: a typo
fails to compile, the values are reviewable in a diff, and there is no
binary-data target to keep in sync across three build targets. They ship as
plain arrays rather than `std::initializer_list` members, which dangle once the
full expression ends.

Echo-Verb, Wow and Flutter, Bat Cave and Breathing, each with a one-line note
saying what to listen for when tuning it. `ProcessorTest` proves every table
applies, reads back, makes sound, and stays bounded.

## 6. The engine facade

`DybbukEngine` (Source/dsp/DybbukEngine.h) owns the loop and everything around
it. `Params` is a plain struct snapshotted once per block, in engine units:

```
strengthDb, time01, decay, filterHz, resonance01, absorb01, blend01, outDb, bypass
```

Published to the UI as atomics, polled by the editor's timer:

| Atomic | Meaning |
|---|---|
| `uiOutputLevel` | peak of the block, linear |
| `uiLoopEnergy` | 0 to 1, log mapped over 60 dB, peak with a 50 ms release, for the ember |
| `uiDelaySeconds` | the smoothed delay the loop is actually running |
| `uiClearsServed` | acknowledgement counter |

The engine also carries `requestClear()`, `requestTimeSnap()` (called on preset
load so Time jumps rather than gliding the whole range) and `seedForTests()`.
