# Dybbuk — technical design

`dybbuk-plan.md` is the source spec: what we are emulating and why. This file
is the entry point to how, and `docs/PROGRESS.md` is what actually shipped.
When they disagree, PROGRESS wins.

## 0. Reading guide

| File | Answers |
|---|---|
| `design/01-core.md` | the PT2399 model, the loop, filters, saturator, Clear, every tuning constant |
| `design/02-modulation.md` | Agitation, Interference, the follower, drift, the mod matrix (Phase 3) |
| `design/03-params-state-engine.md` | the parameter table, readouts, Time Sync, state, presets, the engine facade |
| `design/04-ui.md` | canvas coordinates, theme tokens, knob and ember specs, the readout strip |
| `design/05-verification.md` | the scenario suite and the manual gates |
| `design/inputs/` | the raw independent designs and the judge verdicts they were merged from |

## 1. Signal flow

```
in L+R -> mono sum -> STRENGTH (gain into a soft clip)
       -> loop sum node <---------------------------------+
       -> chip 1 -> chip 2 -> chip 3  (series, one clock)  |
       -> tap sum (weights normalised to 1)                |
       -> FILTER (TPT SVF, bounded resonance state)        |
       -> ABSORB (high shelf cut, then attenuation)        |
       -> loop saturator (asymmetric tanh) + DC blocker    |
       -> Clear gain ------------------- DECAY (0..1.15) --+
       -> wet
wet + dry -> equal power BLEND -> OUT -> L+R
```

Per host sample, inside one chip stage: input MFB, write guard, zero or more
chip ticks (write resampling onto the chip's clock grid, converter distortion,
noise-shaped quantisation, memory write and read, DAC pole), read resampling,
reconstruction filter tracking the clock, hiss, clock bleed, output MFB.

Per block: the processor snapshots parameters, resolves Time Sync from the
transport, and crossfades bypass. The engine chunks anything longer than the
block size it was prepared for.

## 2. Files

| Path | Role |
|---|---|
| `Source/dsp/ChipConstants.h` | every tunable constant and the Time to fs_chip mapping |
| `Source/dsp/TimeMap.h` | note divisions and the sync conversions |
| `Source/dsp/Rng.h`, `OnePole.h`, `TptSvf.h`, `LoopSaturator.h` | small header-only building blocks |
| `Source/dsp/ChipClock.{h,cpp}` | the variable clock: one phase accumulator, one control-rate frame |
| `Source/dsp/PTCore.h` | the silicon: memory, converter distortion, quantiser, DAC pole |
| `Source/dsp/PTStage.{h,cpp}` | one chip with its analog board |
| `Source/dsp/TimeFilterLoop.{h,cpp}` | the feedback loop and the Clear machine |
| `Source/dsp/DybbukEngine.{h,cpp}` | mono sum, Strength, blend, out, the UI atomics |
| `Source/Parameters.{h,cpp}` | the layout, ranges and readouts |
| `Source/PluginProcessor.{h,cpp}` | parameter snapshot, Time Sync, bypass crossfade, state |
| `Source/state/PresetManager.{h,cpp}`, `FactoryPresets.{h,cpp}` | presets |
| `Source/ui/Theme.{h,cpp}`, `Source/PluginEditor.{h,cpp}` | the editor (interim; Phase 5 builds the designed one) |
| `Tests/EngineTest.cpp` | 15 DSP scenarios plus `render` |
| `Tests/ProcessorTest.cpp` | state, readouts, presets, bypass |
| `Tests/UISnapshot.cpp` | the editor to PNGs, both themes and every preset |

## 3. Cross-cutting rules

- **Level convention.** 1.0 is 0 dBFS and the chip's converter full scale.
  Nominal loop level is 0.3 to 0.7 peak; the THD calibration is pinned to 0.5.
- **Control rate.** Slow coefficients refresh every 16 samples on a counter
  that persists across block boundaries, so smoothing never depends on how the
  host chops up time. Only the clock's phase increment runs at true audio
  rate, because that is where the FM character lives.
- **Smoothing.** Time is a 20 ms ramp in log fs (a constant-rate pitch glide).
  Decay, Filter, Resonance and Absorb are 30 ms. Bypass is a 20 ms crossfade.
- **Threading.** Parameters are cached atomics read once per block into a
  plain struct. UI commands travel as atomic counters (Clear, Time snap).
  Engine state reaches the UI only through atomics the editor polls.
- **Non-finite containment.** Two defences, because one is not enough: a
  per-sample guard at the loop sum node catches NaN, inf and absurd values,
  and a per-block `isfinite` check on the feedback state flushes a loop that
  was poisoned some other way.
- **Theme.** An integer property on the APVTS root, default 0 (dark brass).
  Stripped from preset files, preserved across preset loads, restored from a
  session.

## 4. Open questions for the user

These are listening calls. Each is a compile-time constant or a number, so
answering them is a rebuild, not a rewrite.

1. **Feedback topology. ANSWERED, but not as asked.** The `kFeedbackFromTapSum`
   A/B cannot be performed: `TimeFilterLoop.cpp` picks the node once and sends
   the same node to the wet output, so flipping the flag deletes the three-step
   repeat from the output as well and `threestep` fails. The comment is
   corrected and the flag is not flipped. What the question was reaching for was
   a shallower comb, and the tap rebalance to { 1, 0.7, 0.5 } delivers part of
   it; how much further to go is the open ear question in PROGRESS.
2. **FM law.** Exponential in octaves (shipped) versus linear in clock rate.
   The hardware's VCO is current controlled and the Strega's CV conditioning
   is unmeasured. `kFmLawLinear`. STILL OPEN.
3. **Bit and noise calibration.** 11 bits down to 8, hiss from -86 to -48 dBFS.
   The only anchors are the plan's "-90 dBFS at short times" and "clearly
   audible hiss". STILL OPEN, and deliberately untouched: leaving it still is
   what would let a future Crust control prove it is orthogonal to Time.
4. **Write guard poles.** One, by default. Whether an overclocked chip folds
   8 kHz down to 200 Hz is exactly the long-Time character question.
   `kGuardPoles` takes 0, 1 or 2. STILL OPEN.
5. **Wet makeup. ANSWERED.** `kWetMakeup` stays 1.0. The tap rebalance buys the
   level back by changing the mix rather than the gain, so it costs nothing at
   the noise floor: `EngineTest noise` reads -92.8 dBFS at 27.5 ms, unchanged.
6. **Preset tuning.** Echo-Verb currently decays to nothing by 4 s, which may
   be shorter than "a dark dwelling reverb" wants. STILL OPEN, and it should be
   judged against a settled loop rather than re-voiced twice.

## 4a. Deliberate departures from the hardware

Recorded here rather than in the fidelity sections, because each one is a
choice to exceed the Strega rather than a claim about it.

- **The resonance state limit.** `kSvfSatLimit` 2.0 rather than 0.5. A real
  filter's resonance does compress with level; this one compressed so hard that
  the knob was worth 5.8 dB where the loop actually runs, which is not analog
  character, it is a control that stops working when you use it.
- **Agitation to Filter is bipolar**, centred on the knob, where the hardware's
  Agitation CV is a unipolar 0 to 6 V that can only open the filter. The
  hardware's filter is not sitting behind three cascaded fixed 4.5 kHz MFBs and
  an 8 kHz reconstruction cap; this one is, and the harness prints the proof
  that the upward half is inaudible here. Centring also removes a DC offset
  equal to half the route's depth, since the source's mean is exactly 0.5.
- **Agitation to Time.** A patch, not a normal: the hardware's Time modulation
  input is normalled to the oscillator's sub-harmonics, and this is the function
  generator instead. Without it there is no periodic Time modulator below
  16.35 Hz anywhere in the plugin, so tape wow and vibrato are unreachable.
- **Crust.** The hardware has one destruction axis and it is the Time knob.
  This separates them, so a short delay can be destroyed and a long one kept
  clean.
- **The fast Interference register.** The hardware's CV2 is a slow-ish control
  voltage; `kIntfSpeedMax` 45 takes the chaos up to about 32 Hz so it has a
  flutter register as well as a drunken bend.
- **Modulation above the Decay knob's ceiling** (`kDecayModHeadroom`). No
  hardware equivalent: it exists so a played transient can surge the loop past
  where the knob stops and let it settle back.

## 5. What was rejected, and why

Three PT core designs and two modulation designs were written independently
and scored by four judge lenses. The core here is the chip-fidelity design
(the only one that models THD growing with Time against the ElectroSmash
table), with grafts from the other two: the robustness design's wrapped phase
accumulator and per-sample input guard, and the musicality design's persistent
control-rate counter, post-reconstruction hiss and relative Clear criterion.

The judges' fatal findings against the winning design, all fixed here: a
control cadence that restarted every block (so smoothing times depended on
block size), an unsnapped clock smoother (a start-up chirp after every
prepare), an unwrapped phase accumulator, and missing non-finite containment.

Rejected outright: per-chip clock detune (`kStageDetune`), which is musically
attractive but needs per-stage phase accumulators and breaks the single shared
clock frame. Parked in `docs/IDEAS.md`.
