# Dybbuk — progress tracker

**This file is the living truth.** `dybbuk-plan.md` is the source spec and
`docs/DESIGN.md` plus `docs/design/*.md` are the technical design; when they
disagree with what is written here, this wins.

## Scope changes since the plan

- **Design came out of a multi-agent round, not a single pass.** Three
  independent PT core designs and two modulation designs were written, scored
  by four judge lenses (DSP engineer, real-time systems, musician, plus a
  fidelity pass), then merged. `docs/design/inputs/` holds the raw designs and
  the verdict files, `docs/design/01-core.md` holds the merged core spec with
  every fatal flaw the judges found already fixed. Worth reading before
  changing the core: several of those flaws (block-size dependent smoothing,
  an unsnapped clock smoother, an unwrapped phase accumulator) are the kind
  that only show up as a click or a chirp months later.
- **The Claude Design UI canvas was never found on this machine.** Plan
  section 2.7 calls the layout final, but no export exists on disk or in the
  artifact gallery. `docs/design/04-ui.md` derives concrete coordinates,
  theme tokens and component specs from the written spec instead.
- **`kWetMakeup` defaults to 1.0, not the +8 dB the tap normalisation costs.**
  Tap weights sum to 1 so "Decay 1.15" really means 15 per cent over unity;
  giving the 8 dB back on the wet output would also lift the noise floor by
  8 dB and break the plan's "-90 dBFS at short times". Level is Blend and Out's
  job. Revisit after the first real playthrough.
- **Decay 1.0 is not infinity.** The chips and filters lose about 0.23 dB per
  iteration, so unity Decay decays slowly and the true infinity point sits
  near 1.03. This is physically honest and the runaway ceiling at 1.15 is
  unaffected; the EngineTest scenario asserts the decay rates rather than
  demanding infinite sustain at exactly 1.0.

## Current state

- Parameter count: 4 (still the template's placeholder set; Phase 2 replaces it)
- Formats: VST3 / AU / Standalone
- Test scenarios in `EngineTest`: 15 plus `render` (55 checks, 0 failures, 0.2 s)
- Engine: `DybbukEngine` built and measured, not yet wired to the processor
- Known issues: the plugin still runs the template's `ExampleEngine`; the
  editor is still the template placeholder

## Measured (48 kHz unless stated)

| Behaviour | Measured | Plan or design target |
|---|---|---|
| Delay time accuracy | 100.19 ms versus 100.18 expected | within 2 % |
| THD at 31 ms | 0.145 % | 0.13 % (ElectroSmash) |
| THD at 342 ms | 0.995 % | 1 % |
| THD at 1 s | 1.82 % | 3 % plus (low end of the bracket) |
| Noise floor at 27.5 ms | -92.7 dBFS | about -90 dBFS |
| Noise floor at 3.67 s | -46.5 dBFS | clearly audible hiss |
| Bandwidth at short Time | -0.3 dB at 1 kHz, -25 dB at 8 kHz | flat to about 1 kHz then rolls off |
| Bandwidth at 1 s | -7.9 dB from 1 k to 2 k | collapses with Time |
| Clock bleed at 3.67 s | -44.5 dBFS at 750 Hz | audible ticking and burbling |
| Runaway at Decay 1.15 | peak 0.567, RMS -7.9 dBFS, steady | bounded, never digital clipping |
| CPU, full engine | 0.18 % of one core | under a few per cent |
| Sample rates 44.1 to 192 kHz | delay within 0.03 %, floor within 0.1 dB | unchanged |
| Block sizes 1 to 4096 | bit-identical output | unchanged |

## Phase gates

### Phase 0 — Skeleton
- [x] `./build.sh` completes with zero warnings from `Source/`
- [x] pluginval strictness 10 passes (template engine)
- [x] `auval` passes (template engine)
- [ ] Appears in the DAW and passes audio unchanged (null test)

### Phase 1 — DSP core
- [x] `EngineTest` scenarios cover the core behaviours (15 scenarios, 55 checks)
- [x] Sample-rate and block-size invariance proven, not assumed
- [x] Non-finite input contained (`nan` scenario)
- [x] Clear flushes without a click
- [x] CPU measured: 0.18 % of one core at 48 kHz, block 128
- [ ] **Listened to.** `EngineTest render` writes five wav files. The plan's
      milestone is that Time sweeps sound like tape smear and long settings
      get crusty. Nothing downstream fixes a wrong core, so this gate is a
      real one.

### Phase 2 — Parameters and state
- [ ] Full parameter set wired, engine swapped in for `ExampleEngine`
- [ ] All parameters automatable in the host
- [ ] Session save/reload restores everything, parameters and extra state
- [ ] pluginval strictness 10 passes

### Phase 3 — Modulation
- [ ] Agitation, Input Follower, Interference, Drift
- [ ] Audio-rate Time modulation path
- [ ] Generative milestone: no input, Interference plus Decay high, evolving and never exactly repeating

### Phase 4 — Playability and presets
- [ ] Played through the standalone build
- [ ] Preset save / load / rename / delete
- [ ] Four factory presets (Echo-Verb, Wow and Flutter, Bat Cave, Breathing)

### Phase 5 — UI
- [ ] Both themes rendered and reviewed
- [ ] Readout strip on every control
- [ ] Window scales, aspect locked

### Phase 6 — Validation matrix
- [x] Sample rates 44.1 / 48 / 96 / 192 kHz (engine level)
- [x] Buffer sizes 1 / 17 / 128 / 512 / 4096 (engine level)
- [ ] Mono to stereo
- [ ] Offline render matches realtime
- [ ] Automation across a bounce
- [ ] State persistence; device copy-paste
- [ ] Host bypass mid-sound
- [ ] 30-minute soak
