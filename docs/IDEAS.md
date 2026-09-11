# Dybbuk — ideas and backlog

Park ideas here instead of derailing the current phase. Ranked by musical
payoff against implementation cost. Things are marked DONE with a date rather
than deleted: knowing what was considered and rejected is useful later.

## Mode candidates from the survey (2026-09-10)

A survey of what MOOD MKII, Habit, Blooper, Microcosm, Chroma Console,
Tensor, Particle 2 and the step-effect plugins (Effectrix, Stutter Edit,
Glitch, Beat Repeat) do with a captured fragment. The behaviours that recur
everywhere: reverse, stutter the current instant, tape stop, freeze, pitch
by octaves, time-stretch, granular smear, filter sweep, gate/chop, shuffle
the order, stepped speed, layered overdub. We already have reverse (order
and per step), freeze (Wraith), stretch (Trance), octaves (Legion), hold
(Tremor), shuffle (Fills), stepped pitch. Candidates as new players for
every step, ranked by distinctness against what exists, cost, and fun:

1. **Faint** (tape stop; Blooper Stopper, Effectrix Vinyl, Glitch Tape
   Stop): each step decelerates to a halt within its step, pitch falling
   with it. Decay = how fast it stops. Cheap (a rate ramp on the voice).
2. **Rattle** (Blooper Stutter, Tensor Rand, Effectrix Scratchloop): a
   tiny slice of the step, 10 to 60 ms, repeated for the whole step, a
   buzz roll. Unlike Tremor it needs no input. Decay = slice length.
3. **Mirror**: every step's material backwards, swelling attacks. Direction
   reverses the order and Chaos reverses a step now and then; this is all
   of them, all the time. No knob.
4. **Miasma** (Microcosm Haze, Particle density): each step as a cloud of
   grains from random points in its material, a blur rather than Trance's
   ordered stretch. Decay = density. Pairs with Wraith.
5. **Levitate** (Microcosm Arp): each step as a rising arpeggio of itself,
   sub-steps at 0, +P, +2P... Pitch = the interval, Fills = notes per step.
6. **Revenant** (Habit's Scan): steps the ring has dropped are kept in a
   graveyard and come back at random in place of a live one. Needs the slot
   pool to hold the dead; the most on-theme idea here.
7. **Flicker** (Blooper Dropper, Glitch Gater): each step chopped by a
   sub-step gate with random holes. Fills = hole density. Medium.

Not modes but worth having globally: a **filter** (Blooper Filter, Effectrix
Filter; the plugin has no tone control at all, and `TptSvf.h` is still in
the tree), and **tape ageing** (Blooper Stability, Chroma Texture: wow,
flutter, dropouts on the material), which is the Crust idea from the delay
days. Not worth it: Mosaic (Legion covers it), Swapper (Freeze covers it),
Habit's Collect (that is overdubbing, which the ring already is).

## Parked by the Burst pivot (2026-09-09)

- **Radio mode** is now B5 in `docs/BURST.md` (six stations proposed, with
  a viability and fun call each). **Mask mode** (a threshold-driven disguise
  of the loud parts) stays parked.
- Everything below this line concerns the delay engine, which B2 removes.

## The wildness pass

`docs/WILDNESS-PLAN.md` holds stages 2 to 4 of the pass that answers Roy's
"you can't push it very far" note, plus the four decisions that belong to him.
Stage 1 shipped on 2026-09-07 and is written up in `docs/PROGRESS.md`. Several
items that used to live in this file are now folded into that plan: the Crust
trim, host-synced Agitation, a modulation matrix the player can reach, and the
Push 3 touch gesture. Per-chip clock detune was investigated and **refuted** —
the stages are in series, so generations 2 and 3 really are darker and dirtier,
and the runaway is already multi-mode.

## Finish line (before new features)

- **The listening gates.** Everything measurable passes; what is left needs
  ears. `EngineTest render` writes seven wav files, and the two that decide
  things are `dybbuk_timesweep.wav` (the plan says to stop and tune here if
  the sweep does not smear like tape) and `dybbuk_generative.wav` (40 s of the
  loop playing itself with no input).
- **Play it in Ableton.** Live only rescans plugins at startup, so this means
  quitting and reopening Live.
- **The six listening calls in `docs/DESIGN.md` section 4.** Each one is a
  single constant: feedback topology, FM law, the bit and noise curve, the
  write guard, wet makeup, and whether Echo-Verb decays too fast.
- **Absorb versus the runaway zone.** The default Absorb of 20 % makes
  self-oscillation impossible at any Decay, because Absorb removes up to 4 dB
  per iteration and the loop has 1.2 dB of margin at Decay 1.15. Measured
  -9.1 dBFS at Absorb 0 against -48.4 dBFS at Absorb 0.2 (`EngineTest probe`).
  Decide by ear whether `kAbsorbFbMaxDb` should come down.

## High payoff, moderate effort

- **Per-chip clock detune** (`kStageDetune`, about 1 %). Three real chips would
  never share a clock exactly, and the detune would give the three-step repeat
  a chorus, make the bleed tones beat against each other, and soften the tap
  comb nulls. Rejected for now only because it needs per-stage phase
  accumulators, which breaks the single shared clock frame the whole core is
  built around. Worth revisiting if the repeats sound too identical.
- **Crust trim.** The clock-bleed level is a hidden constant
  (`kBleedMaxAmp`, -40 dBFS at the bottom of the range). If the ticking wants
  to be a performance control, it is already a one-line parameter.
- **Host-synced Agitation.** `Agitation` has the phase accessor for it; the
  hook is a `resyncPhase (ppq / beatsPerCycle)` call once per block.
- **A full modulation matrix in the UI.** The engine already has four sources
  by seven destinations with per-cell depths; v1 only exposes three hero routes
  and one macro. The UI question is harder than the DSP one.

## High payoff, bigger lift

- **Push 3 touch gestures** (plan Phase 5). Hold a pad to open a mod route at
  full depth, release to close. A MIDI note handler gating matrix cells, which
  is the plugin analogue of the Touch Bridges idea.
- **A second delay voice** for true stereo, rather than the difference-based
  widener. Doubles the CPU, which at 0.3 % of a core is not the objection; the
  objection is that hardware-true mono is the sound.

## Smaller niceties

- Preset rename and delete from the header, rather than only through the
  manager API that `ProcessorTest` drives.
- A "2 bars" sync division, which currently sits outside the memory's 3.67 s
  ceiling below 131 BPM and would mostly display a value the engine cannot
  deliver.
- Undo for a Clear that was not meant.

## Considered and rejected

- **Oversampling** (plan section 3). The aliasing from the resampling core is
  the sound. Revisit only if the loop saturator alone starts to sound digital.
- **Stepping Time on a preset load.** Rejected in favour of snapping, because
  gliding across the whole range on a patch change chirps everything already in
  the buffer.
- **Giving the wet output the 8 dB that tap normalisation costs**
  (`kWetMakeup`). It would lift the noise floor by the same 8 dB and cost the
  plan's most quotable spec, "-90 dBFS at short times". Level is Blend and
  Out's job.
