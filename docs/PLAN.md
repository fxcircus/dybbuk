# Dybbuk — plan

## What it is

An Ableton audio effect (VST3 / AU / Standalone) that emulates the Time/Filter
Experiment section of the Make Noise Strega: a lo-fi multi-stage PT2399 delay
inside a filtered feedback loop, blended with the dry signal. For guitar and
synths, personal use. The full research and rationale live in
`dybbuk-plan.md`; the technical design is `docs/DESIGN.md`.

What makes it worth building is the delay core. The PT2399 is a fixed-size
memory read at a variable clock, so Time does not move a read tap: it changes
the sample rate of everything already stored. Sweeping Time repitches the
buffer, long settings collapse the bandwidth and expose the clock as audible
ticking, and audio-rate modulation of the clock produces metallic sidebands.
Modelled that way, all of it falls out for free.

## Parameters

The list is the plugin's public API. The first eight are Push 3 bank 1.

| ID | Name | Range | Default | Notes |
|---|---|---|---|---|
| `time` | Time | 0..1 (27.5 ms to 3.67 s) | 0.45 | exponential in chip clock; reads as a note division while synced |
| `decay` | Decay | 0..1.15 | 0.45 | past 1.00 the loop regenerates; readout says "runaway" |
| `filter` | Filter | 20..18000 Hz | 8000 | in-loop lowpass |
| `resonance` | Resonance | 0..100 | 15 | self-oscillates at the top |
| `absorb` | Absorb | 0..100 | 20 | attenuates and darkens together |
| `blend` | Blend | 0..100 | 50 | equal power |
| `agitate` | Agitate | 0..100 | 0 | macro over the three hero mod routes (Phase 3) |
| `agitspeed` | Agit Speed | 0.016..1000 Hz | 0.35 | shows a period below 1 Hz |
| `strength` | Strength | 0..40 dB | 0 | gain into a soft clip |
| `out` | Out | -60..+6 dB | 0 | bottom of the range is silence |
| `timemod` | Time Mod | 0..100 | 0 | audio-rate FM depth (Phase 3) |
| `timesync` | Time Sync | off / on | off | 14 divisions, clamped to the memory ceiling |
| `agitmode` | Agit Mode | Loop / Gate | Loop | Phase 3 |
| `toneslevel` | Tones Level | 0..100 | 0 | internal drone, off by default (Phase 4) |
| `tonespitch` | Tones Pitch | 32.7..2093 Hz | 110 | reads as a note name |
| `spread` | Spread | 0..100 | 0 | stereo widening, mono by default (Phase 4) |
| `bypass` | Bypass | off / on | off | declared last, 1 = bypassed |

Clear is not a parameter: it is a momentary command through an atomic mailbox,
so a session recall can never empty the loop on load.

## Phases

### Phase 0 — Skeleton
Builds VST3 + AU + Standalone, passes audio, `build.sh` runs clean.

**Gate:** zero `Source/` warnings; pluginval strictness 10; `auval`; appears in
the DAW. **Passed** except the in-DAW check.

### Phase 1 — DSP core
The variable-clock chip, three stages in series, the filtered feedback loop,
Clear. Headless.

**Gate:** `EngineTest` scenarios prove the five known tells with numbers; delay
time, THD, noise floor and bandwidth match the PT2399 measurements the plan
cites; sample rate and block size provably do not change the sound; CPU
measured. **Passed** (55 checks). Remaining: the listening pass on
`EngineTest render`.

### Phase 2 — Parameters and state
Full parameter set, Time Sync, presets, the state model.

**Gate:** every parameter automatable and in Push bank order; session save and
reload restores everything; a preset never changes the theme or the bypass
state; pluginval strictness 10. **Passed** (`ProcessorTest`, 0 failures).

### Phase 3 — Modulation
Agitation generator, input follower, Interference, drift, the three hero
routes and the Agitate macro, audio-rate Time modulation.

**Gate:** with no input, Interference plus Decay high produces an evolving
texture that never exactly repeats, measured by autocorrelation across windows
rather than by ear alone. That is the pass/fail for the whole concept.

### Phase 4 — Playability, character and presets
Standalone playtest. Optional Tones injection and stereo spread, both off by
default. Preset browser.

**Gate:** played for real through the standalone; the four factory presets
sound like their descriptions; a full session is drivable without the mouse.

### Phase 5 — UI
The designed interface from `docs/design/04-ui.md`: brass knob components, two
theme token sets, the ember animated from loop energy, the readout strip, the
full-width Out slider with its meter.

**Gate:** both themes rendered and reviewed; every control reachable; window
scales and stays aspect locked.

### Phase 6 — Validation matrix
- Sample rates 44.1 / 48 / 96 / 192 kHz (engine level: **passed**)
- Buffer sizes 1 / 17 / 128 / 512 / 4096 (engine level: **passed**)
- Mono to stereo
- Offline render matches realtime
- Automation across a bounce
- State persistence; copy-paste the device between tracks
- Host bypass mid-sound, no clicks or stuck audio
- 30-minute stability soak

## Open questions

The listening calls are in `docs/DESIGN.md` section 4: feedback topology, FM
law, the bit and noise curve, the write guard, wet makeup, and whether
Echo-Verb decays too fast. Each is one constant.
