# Dybbuk - JUCE Audio Effect Plan

Dybbuk is an Ableton audio effect (VST3/AU) that emulates the sound and behavior of the Make Noise
Strega's Time/Filter Experiment section, for processing guitar and synths. Personal use. Push 3
integration comes in a later phase.

---

## 1. What we are emulating (research summary)

### 1.1 Signal topology (from the official manual + Sound on Sound review)

The Strega is two halves. The left half supplies "substances" (internal oscillator + external
input through a driven preamp). The right half is the Time/Filter Experiment: a lo-fi
multi-stage delay inside a filtered feedback loop, blended with the dry signal at the output.
We are building the right half, with the input drive stage, and a small optional slice of the
left half for character.

Hardware signal flow, reconstructed:

```
ext in -> Strength (gain + drive) -> [sum with internal Tones] -> DELAY CORE -> FILTER -> ABSORB split
                                                                     ^                        |
                                                                     |---- DECAY (feedback) ---
                                                                                              |
                                                            BLEND (dry/wet) <--- wet out <----
```

Manual definitions that matter:
- Time: "the rate at which substances are dripped into the Filter." Coarse + fine.
- Decay: "sets the iterations of the Time/Filter experiment. Ranges from a handful to infinity."
  This is the feedback amount. Self-oscillation is intended behavior.
- Filter: sits inside the loop, before Decay. Full CW passes everything, full CCW passes
  almost nothing. Behaves like a lowpass with resonance (resonance is a touch-mod destination
  on hardware).
- Absorb: post-filter split. Determines how much of the filtered signal is "diminished into
  the earth" versus presented for blending. In practice it darkens/ages the wet signal and
  interacts with Filter to set the "age and quality of the tape" (manual, Wow and Flutter patch).
- Blend: dry/wet, CV controllable.
- Time Modulation input is NORMALLED to the oscillator's sub-harmonics output. The delay clock
  receives audio-rate FM by default. This produces the metallic clangs and ring-mod-like
  sidebands. The combo knob sets modulation depth.
- CV2 output: "DC Voltage Feedback signal from the Time/Filter circuit." The loop itself is a
  modulation source. This is the "Interference" that modulates Activation and Tonic elsewhere,
  making the instrument self-influencing and generative.

### 1.2 The delay core: PT2399 behavior (ElectroSmash analysis + SoS)

The delay is built on PT2399 chips (community consensus: three delay lines, SoS heard a
"multitap, maybe three-step repeat"). PT2399 facts that define the sound:

- Delta-sigma 1-bit converter with 44Kbit RAM. Delay time is set by varying the internal
  VCO clock: 22MHz clock = 31ms delay, 2MHz = 342ms. Longer times push the clock lower and
  fidelity collapses. Strega deliberately runs it far past spec for very long times.
- Changing Time changes the sample clock of a fixed-size memory. Consequences:
  - Sweeping Time repitches whatever is in the buffer (tape-like smear, not crossfade).
  - Bandwidth and SNR drop as Time increases. THD: 0.13% at 31ms, 1% at 342ms, 3%+ beyond.
  - Noise floor rises with Time (approx -90dBFS at short times, much worse when overclocked low).
- Fixed analog anti-alias filtering around the chip: 3-pole MFB lowpass at ~8.8kHz on input,
  reconstruction filter + MFB on output (~2.8-5.8kHz region). Net frequency response is flat
  to ~1kHz then rolls off. The wet path is dark by design.
- At long times the clock frequency falls toward the audio band and clock noise becomes
  audible: the "ticking, burbling" that Strega exposes instead of hiding. SoS calls this one of
  its signature characteristics.
- Input clipping above ~1.3Vrms: the chip itself saturates, in addition to loop saturation.
- Mono. The whole wet path is mono on hardware. (We will offer optional stereo spread later,
  default true-to-hardware mono wet.)

### 1.3 Modulation system

- Agitation Function Generator: looping or gated Attack/Decay function. Speed from ~1 minute
  per cycle up to audio rate (~1kHz). Angle skews rise vs fall (ramp <-> triangle <-> saw).
  Output 0 to 6V equivalent, normalled to Filter cutoff on hardware.
- Interference: a chaotic signal derived from the Time/Filter loop state. Sounds like
  shortwave crackle/static but is correlated with the audio, since it IS the audio loop.
  Because it feeds back into things that feed the loop, the system is generative and
  non-repeating.
- Envelope follower on the external input (CV1 on hardware): input dynamics as a mod source.
- Self-noise everywhere: SoS notes the same knob settings never land in the same place twice.
  The emulation must inject continuous low-level noise inside the loop or it will sound
  sterile and deterministic.

### 1.4 Optional character source: Strega Tones

The internal oscillator (triangle core, "Tones" knob morphs triangle -> folded -> saw-like ->
pulse+sub -> jagged square, plus sub-harmonic dividers) constantly leaks into the delay via
the Activation Constant control. For a pure effect we do not need the full synth voice, but a
minimal "Tones injection" (sine/triangle drone + sub divider, level knob, pitch knob) buys a
lot of authentic drone character for little code. Phase 4, optional.

---

## 2. DSP architecture

All processing mono in the core (matching hardware), stereo I/O with mono sum at input and
equal wet to both outputs. Internal chain, per block:

```
in L+R -> mono sum -> STRENGTH (gain 0..+40dB into soft-clip drive)
       -> env follower tap (mod source "Input")
       -> [+ Tones injection, Phase 4]
       -> loop sum node <------------------------------------+
       -> input MFB LPF (fixed ~8.8kHz, 3-pole, Q~0.9)       |
       -> PT CORE (variable-clock delay, see 2.1)            |
       -> reconstruction LPF (tracks clock, see 2.1)         |
       -> FILTER (SVF lowpass, cutoff = Filter knob,         |
                  resonance param, self-osc allowed)         |
       -> ABSORB (tilt/darken + attenuate, see 2.3)          |
       -> loop saturator (tanh-ish + DC blocker) -- DECAY ---+
       -> wet out
wet/dry BLEND (equal-power) -> output LEVEL -> L+R
```

### 2.1 PT core: variable-clock delay line

This is the heart. Do NOT implement as a fractional read tap on a 44.1/48k buffer. Implement
as a virtual chip with its own sample rate:

- Fixed-length internal buffer of N samples (start with N = 5504, close to 44Kbit at 8 bits;
  exact number is a tunable "memory size" constant).
- Chip clock fs_chip is the Time parameter. Map Time knob 0..1 exponentially to
  fs_chip from ~200kHz (short, clean, ~30ms) down to ~1.5kHz (very long, destroyed).
  Delay seconds = N / fs_chip. That gives roughly 27ms to 3.6s, matching Strega's wide range.
- Host-rate to chip-rate conversion: maintain a phase accumulator; write into the chip buffer
  via linear interpolation resampling at ratio fs_chip/fs_host; read out the oldest sample and
  resample back. Keep interpolation deliberately cheap (linear, or 4-point at most). The
  imperfection IS the sound.
- Because the buffer length is fixed in samples, modulating fs_chip repitches buffered content
  automatically. This gives correct tape-smear on Time sweeps and correct audio-rate FM
  clanging for free. No extra work needed.
- Quantization: quantize the stored sample to emulate delta-sigma grunge. Simplest effective
  model: add TPDF dither + quantize to ~8-10 bits, with effective bit depth dropping as
  fs_chip drops (e.g. 10 bits at max clock down to 6 bits at min). Tune by ear against demos.
- Reconstruction filter: one-pole or two-pole lowpass at min(0.45 * fs_chip, 8kHz). This is
  what collapses bandwidth automatically at long times.
- Chip noise: white noise added inside the core, level inversely proportional to fs_chip.
  Target roughly -90dB wet at shortest Time rising to clearly audible hiss at longest.
- Clock bleed: when fs_chip < ~20kHz, inject a soft-clipped pulse train at fs_chip (and a
  subharmonic at fs_chip/2) at low level into the core output, level rising as clock falls.
  This is the ticking/burbling signature. Make its level a hidden tunable, exposed later as
  a "Crust" trim if wanted.
- Three stages: run three PT cores in SERIES, each with N/3 memory, sharing the same fs_chip.
  Tap the output of each stage and sum taps (equal or slightly decaying weights) into the
  filter. This reproduces the "three-step multitap" repeat SoS heard. Build stage count as a
  compile-time constant so we can A/B 1 vs 3 stages.

### 2.2 Filter (in-loop)

- TPT/zdf state variable filter, lowpass output. Cutoff mapped 20Hz..18kHz, matching
  "full CW passes everything, full CCW almost nothing."
- Resonance 0..self-oscillation. Default modest. Expose as its own knob (hardware buries it
  as a touch destination, we get to be nicer to ourselves).
- Nonlinearity in the resonance path (tanh on the feedback state) so self-osc sounds analog.

### 2.3 Absorb

Model as two things happening together, post-filter:
- A wet attenuator (how much survives to blend and to decay).
- A progressive darkening/tilt: as Absorb increases, add a gentle high shelf cut and a small
  amount of extra saturation, so Filter + Absorb together set "tape age" as described in the
  manual's Wow and Flutter patch. Exact curve tuned by ear.

### 2.4 Decay (feedback)

- Feedback gain 0..~1.15. Above 1.0 the loop regenerates; the loop saturator bounds it.
- Loop saturator: soft clip (tanh or cubic) with slight asymmetry + DC blocker at ~10Hz.
- With three series PT stages the loop delay is the full chain, matching hardware behavior
  where Decay sets "iterations of the experiment."

### 2.5 Modulation sources (all internal, routed via mod matrix)

1. Agitation: looping/one-shot AD envelope. Speed 0.016Hz..1kHz (log). Angle morphs
   rise/fall ratio continuously (1/99 .. 50/50 .. 99/1). Modes: Loop (default), gated by
   input transient (envelope follower crossing threshold), host-sync later.
2. Input Follower: envelope follower on the post-Strength signal (attack ~5ms,
   release ~100ms, both tunable internally).
3. Interference: chaotic source derived from loop state. Implementation: take the loop
   signal's rectified envelope, feed it as the drive parameter of a logistic map or a
   cheap chaotic oscillator (e.g. Lorenz integrated at control rate), then lightly slew.
   Output correlates with the audio, gets wilder as the loop gets hotter. This is the magic
   ingredient; budget real tuning time here.
4. Noise/Drift: very slow filtered random (0.05..2Hz) for analog drift on Time. Always on at
   tiny depth (hidden trim), also available as an assignable source.

Mod destinations: Time, Filter cutoff, Resonance, Decay, Absorb, Blend, Strength.
Matrix: 4 sources x 7 destinations, each cell a bipolar depth. UI can start as just
3 fixed "hero" routes with depth knobs (Agitation->Filter which is the hardware normal,
Interference->Time, Follower->Decay) and grow into a full matrix later.

Time modulation must be applied at audio rate (per-sample smoothed) to fs_chip, otherwise
the clang/FM character disappears. All other destinations control-rate (per 32 samples) with
smoothing is fine.

### 2.6 Parameter set (v1)

| Param        | Range                | Notes                                          |
|--------------|----------------------|------------------------------------------------|
| Strength     | 0..+40dB + drive     | drive amount tied to gain, one knob            |
| Time         | 0..1 (27ms..3.6s)    | exp mapping of fs_chip; shift-drag = fine      |
| Time Sync    | off/on               | on: Time snaps to note values (1/16..1/2 etc.) |
| Time Mod     | 0..1                 | depth of audio-rate FM from mod bus            |
| Decay        | 0..1.15              | feedback; double-click = Clear                 |
| Filter       | 20Hz..18kHz          | in-loop LPF cutoff                             |
| Resonance    | 0..self-osc          |                                                |
| Absorb       | 0..1                 | wet attenuation + darkening                    |
| Blend        | 0..1                 | equal power dry/wet                            |
| Agitate      | 0..1                 | macro, scales all internal mod route depths    |
| Agit Speed   | 0.016Hz..1kHz        |                                                |
| Agit Mode    | loop/gate            | toggle                                         |
| Out          | -inf..+6dB           | bottom slider with integrated meter            |
| Clear        | momentary            | flushes delay buffers instantly, not automated |

Internal (no UI, tunable constants or hidden params):
- Time Fine trim (dropped; fine adjust is shift-drag in the editor)
- Agit Angle (fixed 50/50; revisit if missed)
- Individual mod route depths (Agitation->Filter, Interference->Time, Follower->Decay);
  the Agitate macro scales all three
- Drift depth, clock-bleed/Crust level

Time Sync implementation: derive target fs_chip from host BPM and the selected note value,
but run it through the SAME smoothing path as free mode, so switching divisions still smears
pitch instead of jumping cleanly. Clamp synced values to the 3.6s max delay; readout shows
the clamped value when a division exceeds it.

All parameters via APVTS, all automatable, generous smoothing (Time especially: smooth
fs_chip with ~20ms one-pole so knob sweeps warble instead of zipper).

### 2.7 UI (settled in Claude Design, ahead of DSP)

The UI was designed in parallel via Claude Design and is considered layout-final:

- Header: Dybbuk wordmark + Hebrew, preset selector with prev/next arrows, theme toggle
  top-right (pattern borrowed from Teder). Two theme token sets: dark-brass default + one
  alternate. Theme choice is editor-side state, non-automatable, persisted in plugin state.
- Hero row (large knobs): Time, Decay, Filter, Blend. Decay's top-of-range (past unity) is
  styled red as the "runaway" zone; readout shows "1.15 runaway".
- Second row (medium knobs): Time Mod, Strength, [loop/gate toggle], Resonance, Absorb.
- Bottom strip: Agitate (medium-large), activity ember (loop-energy indicator, the
  centerpiece), Speed, Clear button (understated utility near the ember).
- Sync toggle sits quietly next to the Time knob; synced readout shows note values.
- Full-width OUT slider with integrated meter pinned to the bottom (pattern borrowed from
  Infinite Sustainer's output slider).
- Fixed readout strip shows hovered/dragged param name + value. No floating tooltips.
- No em dashes anywhere in UI copy.

Deliverables to translate into JUCE: knob component spec (large/medium/small; default,
hover, drag, modulated states) into a custom LookAndFeel, the two theme token sets into a
theme struct, and the ember into a Component animated from an atomic loop-energy value
published by the processor.

---

## 3. Project setup

- JUCE 8, C++17 (same toolchain as Infinite Sustainer), CMake project.
- Formats: VST3 + AU. Test host: Ableton Live 12.
- Plugin type: audio effect, stereo in / stereo out, mono core.
- Repo layout:

```
dybbuk/
  CMakeLists.txt
  src/
    PluginProcessor.{h,cpp}
    PluginEditor.{h,cpp}
    dsp/
      PTCore.{h,cpp}        // variable-clock delay stage
      TimeFilterLoop.{h,cpp}// full loop: 3x PTCore, SVF, absorb, decay, saturator
      Strength.{h,cpp}      // input gain + drive + env follower
      Agitation.{h,cpp}     // AD function generator
      Interference.{h,cpp}  // chaos source
      ModMatrix.{h,cpp}
    params/Params.h         // APVTS layout, ranges, mappings
  tests/
    offline_render.cpp      // renders test signals through the chain to wav
```

- No external DSP dependencies. juce::dsp where convenient (SVF, oversampling if needed).
- Oversampling: not needed for v1. The aliasing from the resampling core is part of the
  sound. Revisit only if the loop saturator aliases unpleasantly at high Decay.

---

## 4. Build phases

### Phase 1: Core loop, fixed settings (get the sound)
- PTCore single stage: variable-clock buffer, quantization, reconstruction filter, noise.
- Wire: Strength -> PTCore -> SVF -> feedback -> Blend. Hardcode reasonable values.
- Offline render harness: run drum loop, guitar DI, and a sine through it, listen.
- Milestone: sweeping Time repitches the buffer with warble and gets crusty at long
  settings. If this milestone does not sound like the demos, stop and tune here. Nothing
  downstream fixes a wrong core.

### Phase 2: Full topology + parameters
- Three series stages with summed taps. A/B against single stage.
- Absorb block, loop saturator, DC blocker, clock-bleed injection.
- Full APVTS parameter set, smoothing, state save/restore.
- Milestone: recreate three reference behaviors by ear:
  1. "Echo-Verb" patch: short-ish Time, moderate Decay, Filter darkened = dark dwelling reverb.
  2. "Wow and Flutter": Blend 50%, Absorb or Filter high = aged tape character.
  3. Runaway: Decay past unity = musical self-oscillation that saturates, never digitally clips.

### Phase 3: Modulation system
- Agitation generator + Input Follower + Interference + Drift.
- Fixed hero routes with depth knobs. Audio-rate Time modulation path.
- Milestone: with no input, Interference + Decay high produces evolving self-playing
  texture that never exactly repeats. That is the pass/fail test for the whole concept.

### Phase 4: Character + UI implementation
- Optional Tones injection (drone osc + sub, pitch + level). Off by default.
- Optional stereo spread (short Haas offset or dual-mono with detuned fs_chip per side,
  tiny amount). Hardware-true mono remains default.
- Implement the settled Claude Design UI (section 2.7): custom LookAndFeel from the knob
  spec, theme token sets + toggle, ember animation, OUT slider/meter, readout strip,
  shift-drag fine adjust on Time, double-click Decay = Clear.
- Clear plumbing: momentary UI action -> thread-safe flag -> processor flushes all PTCore
  buffers and filter states at the next block boundary (no allocation, no locks).
- Presets: ship the manual's Patch Corner names as starting points (Echo-Verb,
  Wow and Flutter, Bat Cave, Breathing).

### Phase 5: Push 3 / Ableton integration
- Nothing special needed in the plugin: Ableton maps plugin parameters to Push encoders
  automatically. Do provide a curated parameter order in the APVTS layout so the first
  8 params on Push bank 1 are Time, Decay, Filter, Resonance, Absorb, Blend, Agitate,
  Agit Speed.
- Later ideas: MIDI-learnable momentary "touch" gestures (hold a pad = temporarily open a
  mod route at full depth, release = closes). This is the plugin analog of Touch Bridges
  and maps beautifully to Push pads. Implement as a MIDI note input handler that gates
  matrix routes.

---

## 5. Tuning references

- Make Noise official Strega demos and Cortini's launch livestream (target tonality).
- Manual Patch Corner pages 19-36 (behavioral test cases with knob positions).
- ElectroSmash PT2399 measurements (THD vs delay table, freq response) for calibrating
  quantization, noise, and filter curves at the "in-spec" end of the Time range.
- Known tells to verify by ear:
  1. Time sweep smears pitch (never crossfades).
  2. Long Time = dark, hissy, ticking/burbling clock noise.
  3. Decay past unity self-oscillates warmly, loop never hard-clips digitally.
  4. Same settings drift; output is never bit-identical across runs.
  5. Audio-rate Time Mod produces metallic ring-mod-like sidebands.

## 6. Risks

- The PTCore resampler tuning (interpolation quality, bit depth curve, noise curve) is where
  the entire character lives. Budget most iteration time there.
- Feedback loop stability at Decay > 1 with modulated Time: saturator placement and DC
  blocking must be verified with worst-case settings early (Phase 2 milestone 3).
- CPU: three resampling stages + SVF + chaos at audio rate is cheap (< a few % of one core),
  but audio-rate Time smoothing per sample must avoid divisions in the inner loop
  (precompute ratio increments per block).
