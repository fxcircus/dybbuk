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
- **Four template preset behaviours were wrong for a delay and are fixed.**
  The template wrote the theme into every preset file and restored it on load
  (so loading a preset changed your colours), forced performance parameters to
  zero on load (so browsing presets took a bypassed plugin back into circuit),
  left parameters absent from an older preset at whatever the knob happened to
  hold, and still used the Infinite Sustainer preset tag. All four are fixed in
  `PresetManager`, and `ProcessorTest` covers each one.
- **Decay 1.0 is not infinity.** The chips and filters lose about 0.23 dB per
  iteration, so unity Decay decays slowly and the true infinity point sits
  near 1.03. This is physically honest and the runaway ceiling at 1.15 is
  unaffected; the EngineTest scenario asserts the decay rates rather than
  demanding infinite sustain at exactly 1.0.

## Current state

- Parameter count: 17, in Push bank order, all automatable; Agitate, Agit
  Speed, Agit Mode and Time Mod now drive the engine
- Formats: VST3 / AU / Standalone; pluginval strictness 10 and `auval` pass
- `EngineTest`: 23 scenarios plus `render` and `probe` (85 checks, 0 failures, 1.2 s)
- `ProcessorTest`: state, readouts, presets, bypass (0 failures)
- Four factory presets ship as code tables and are proven to sound
- UI: the designed interface, both themes, ten knobs with live modulation
  arcs, the ember, the readout strip and the metered Out fader
- Known issues: none open; the listening gates are the user's call

## Open finding: Absorb versus the runaway zone

The default Absorb of 20 % makes self-oscillation impossible at any Decay.
Absorb removes up to 4 dB per iteration from the feedback and the loop has only
1.2 dB of margin at Decay 1.15, so a fifth of the knob is enough to damp it
completely: -9.1 dBFS at Absorb 0 against -48.4 dBFS at Absorb 0.2 (run
`EngineTest probe`). That is the manual's "diminished into the earth" working
as described, but it means the red zone at the top of Decay does nothing in the
default patch. Whether `kAbsorbFbMaxDb` should come down is an ear question,
and it is one constant.

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
- [x] Full parameter set wired, `DybbukEngine` swapped in for `ExampleEngine`
- [x] All parameters automatable, first eight in Push bank 1 order
- [x] Session save/reload restores every parameter plus theme and window scale
- [x] A preset never changes the theme and never takes the plugin in or out of circuit
- [x] Time Sync resolves from the host transport, clamps, and smears like the knob
- [x] pluginval strictness 10 and `auval` pass

### Phase 3 — Modulation
- [x] Agitation, Input Follower, Interference, Drift
- [x] Audio-rate Time modulation path (Time only; everything else on a 32-sample tick)
- [x] Three hero routes and the Agitate macro
- [x] **Generative milestone passed.** No input at any point: self-oscillates
      at -11.5 dBFS, correlation falls from 0.89 at 2 s to -0.28 at 40 s with
      no rebound, a 1e-5 nudge changes the output 141 % a minute later, energy
      spread 0.79 across twelve 5 s windows
- [ ] Listened to (`EngineTest render` writes dybbuk_generative.wav, 40 s of it)

### Phase 5 — UI
- [x] Both themes rendered and reviewed (brass default, parchment alternate)
- [x] Ten knobs on the designed 720 x 576 canvas, sized large / medium-large / medium
- [x] Decay's runaway zone drawn in red on the ring, and named in the readout
- [x] Live modulation arcs: the pointer is what you set, the dot is what you hear
- [x] Time shows detents and note names while synced, and says when a division is capped
- [x] The ember, animated from loop energy, hotter and redder in runaway
- [x] Readout strip instead of tooltips, sticky to the last control touched
- [x] Full-width Out fader with the meter behind it and a peak hold
- [x] Shift-drag fine adjust, double-click Decay to Clear
- [x] Window scales, aspect locked; pluginval Editor and Editor Automation pass

### Phase 4 — Playability, character and presets
- [x] Optional Tones injection: triangle drone plus a sub-harmonic, off by default
- [x] Time Mod is normalled to the Tones sub, as on the hardware, so it gives
      discrete ring-mod sidebands rather than the chaotic FM it had before
- [x] Optional stereo Spread: the side is a difference, so the mono sum is
      bit-identical and Spread 0 is exactly the hardware's mono
- [x] Preset save / load / rename / delete (`ProcessorTest`)
- [x] Four factory presets, each proven to apply, sound and stay bounded
- [ ] Played through the standalone build

### Phase 6 — Validation matrix
- [x] Sample rates 44.1 / 48 / 96 / 192 kHz (engine level)
- [x] Buffer sizes 1 / 17 / 128 / 512 / 4096 (engine level)
- [ ] Mono to stereo
- [ ] Offline render matches realtime
- [ ] Automation across a bounce
- [ ] State persistence; device copy-paste
- [ ] Host bypass mid-sound
- [ ] 30-minute soak
