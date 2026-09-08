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
- **The Claude Design canvas existed all along and the first UI was thrown
  away.** An earlier session could not reach it and derived an interface from
  the written spec in plan section 2.7: filled brass knobs, a readout strip, a
  horizontal output slider. The real canvas (`Dybbuk UI v3.dc.html`, project
  `a3466a02-e100-482f-a5dc-0d9ad16fd4c4`) is a 900 x 620 engraved plate with
  line-art knobs, serif and script type, and trims on both edges. The derived
  spec is kept at `docs/design/inputs/04-ui-derived-from-text-spec.md` as a
  record of what was guessed. Reaching the canvas needs `/design-login`, which
  does not work over Remote Control.
- **The canvas added an IN trim.** A second level control on the left edge,
  ahead of Strength: IN sets what reaches the plugin, Strength sets how hard
  that hits the loop. New parameter `in`, hint 17, ahead of Bypass.
- **Three typefaces are embedded** (EB Garamond, Frank Ruhl Libre for the
  Hebrew, Pinyon Script for the patch name), about 1 MB of binary data, so the
  plate looks the same on a machine that has never seen them.
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

- Parameter count: 22, in Push bank order, all automatable (an IN trim came
  with the v3 canvas; Chaos, Crust, Tones Fold and Colour came with the
  wildness pass)
- Formats: VST3 / AU / Standalone; pluginval strictness 10 and `auval` pass
- `EngineTest`: 34 scenarios plus `render` (125 checks, 0 failures; 5.4 s
  without the 30 minute soak, 11.6 s with it)
- `ProcessorTest`: state, readouts, presets, bypass (0 failures)
- Five factory presets ship as code tables and are proven to sound
- UI: the v3 Claude Design canvas, both sheets, engraved line-art knobs, the
  lamp (now flickering with the chaos), fourteen knobs, two engraved trims, a
  dice, and metered IN and OUT trims on the edges
- Known issues: one open ear question on the tap rebalance, below; the
  listening gates are the user's call

## Changed after the first playthrough (2026-09-06)

Three things Roy found by playing it that no measurement had flagged, because
every test was checking that the behaviour existed rather than that it was
wanted.

1. **A high squeal past about a 0.2 s delay that Clear could not remove.** The
   clock bleed. Once the chip clock falls into the audio band a steady pulse
   train is not "ticking", it is a pitch: at a 1 s delay the fs/2 square sat at
   2.75 kHz and -44 dBFS, and it was there with no input at all, because the
   clock generates it rather than the buffer. The fs/2 square is now off, the
   level is 15 dB lower, and what remains is gated by the loop's own content so
   it rides the repeats. Measured: -95.8 dBFS at the clock frequency with an
   empty loop, 13 dB louder while the loop is ringing. `EngineTest bleed` now
   tests for the absence of the tone rather than its presence, which is what it
   should have tested all along.
2. **Time Mod was unusable past about an eighth of its travel.** It ran to two
   octaves of clock FM. It now runs to a quarter of an octave, three semitones,
   so the whole knob sits where the metallic edge lives. That is very close to
   the 13 % Roy asked for.
3. **A stereo source was being collapsed.** The plan says "mono sum at input",
   and the input to the delay still is, because the chip is mono on the
   hardware and that is the sound. But the dry path had no reason to lose its
   image and now keeps both channels. `ProcessorTest stereoDry` proves a left
   only source stays on the left.

The noise floor at the longest Time reads 14 dB lower than it did (-60 dBFS
rather than -46) because most of what the old measurement was picking up was
the squeal, not hiss.

## The dice, and the limiter question (2026-09-07)

### Should there be a limiter? No, and here is the measurement

The mono path is bounded by construction: everything the loop can do goes
through the saturator's tanh, which asymptotes below full scale. The one path
that escapes it is the stereo side, because that is a difference taken AFTER
the loop.

Over a 30 minute soak with every parameter sweeping, **10 samples out of
86,400,000 exceeded full scale**, and the worst was 1.0157 -- 0.14 dB over,
0.000012 per cent of the time. A limiter would be a second, less musical
nonlinearity sitting in front of the one that IS the sound, doing nothing for
hours at a stretch.

What was worth having is the claim "this cannot output above full scale" being
true. `outputCeiling` is the same soft clip family already in the file with a
knee at 0.95, so nothing a player hears can reach it: the soak now measures
**0 samples over, stereo peak 0.9987**. It is a guard, not a limiter, and the
comment says so.

One caveat, recorded rather than hidden: it is applied per channel after the
side is added, so Spread's exact L + R = wet identity holds only below the knee.
Above it the channels clip independently. That is the right trade -- a mono sum
that is bit-exact and over full scale is worse than one that is neither.

### The dice

A pipped die beside the theme mark, which rolls to a new face on every click.

Randomising eighteen parameters independently over their full ranges produces
garbage almost every time, because the interesting settings here are
CORRELATED: a long Time wants a darker Filter, a hot Decay wants some Absorb
under it, Time Mod is only musical when the Tones pitch it rides is somewhere
sensible, and a Blend of 8 per cent means you cannot hear any of it.

So the die is rolled twice. The first roll picks a **character** -- Space, Tape,
Metal, Possessed or Ruin -- and the second fills in each parameter inside that
character's own range. Rates and cutoffs are picked log-uniformly, because a
linear roll over a range that crosses decades lands at the top nearly every
time, and Tones Pitch snaps to a semitone because it is both a drone and the FM
grid.

Decay tops out at 1.25 against a ceiling of 1.45: a roll can land in the runaway
zone but never parks at the far end of it. That part of the knob is for the
player to find.

**Three things it never touches.** Bypass, because that is performance state and
a roll of the dice must not take the plugin in or out of circuit. In and Out,
because a level control is the one thing that can hurt someone wearing
headphones. Time Sync, because whether you are working in note divisions is a
workflow choice rather than a sound.

`ProcessorTest diceIsMusical` is the gate, and it is deliberately not a range
check: it rolls every character twelve times, **renders each patch**, and
requires all sixty to be audible, bounded and finite. A roll that produces
silence counts as a failure in exactly the same way as one that clips. Measured
across the five characters, rolls land between -22.4 and -9.2 dBFS.

Each parameter is set as its own complete gesture, so a host's undo and its
automation recording both see an ordinary edit, and the preset goes dirty
through the listener that was already there.

`ProcessorTest render` writes one roll of each character to WAV.

## Stage 3 of the wildness pass (2026-09-07)

Stages 1 and 2 made the loop pushable and gave the modulation a reach. This one
closes the hardware's own generative loop, and gives the plugin the only two
sources of brightness it has ever had.

**The chaos reaches the generator.** On the Strega, CV2 -- the Time/Filter
circuit's own DC feedback -- modulates Activation and Tonic: the level and the
PITCH of the oscillator that feeds the delay. That is a loop through the sound
generator, and it is why the hardware plays itself. Here Interference reached
only the clock, so it could repitch what was already stored but could never
cause a new event at a new pitch. `dstTonesPitch` and `dstTonesLevel` are
destinations now, and `EngineTest chaosloop` measures what that buys: with
nothing played into it at any point, **36 distinct pitch plateaux over 83
seconds**, where a loop that can only smear what it already holds produces one.

**The drone became a voice.** `kTonesFullLevel` 0.35 -> 0.9, and the injection
now goes through the same input clipper the played signal does and is scaled by
the same Strength -- so Strength can drive the drone into the chip's write
nonlinearity, which is where a drone gets its harmonics. The follower is
deliberately still fed the pre-drone signal: with the drone in it, Follower to
Strength would close a real latch and Gate mode would fire on a continuous tone.

**Tones Fold.** Nothing in the plugin could add high frequency -- five stages of
lowpass remove it and one tanh puts a little back, so every control's net
direction was darker and pushing anything converged on mud. A wavefolder on the
drone measures a spectral centroid of 114 Hz at Fold 0 rising to 835 Hz at full,
with the worst non-harmonic bin 57.7 dB under the fundamental at the top of the
pitch range. Written as a blend FROM the triangle rather than as a sine of the
folded ramp, so Fold 0 is bit-identical: a plain `sin(pi/2 * t)` has about 15 dB
less third harmonic than the triangle it would have replaced, which would have
made "default 0" a quiet character change.

**Colour.** `TptSvf` computed the bandpass output on every sample and threw it
away. Mixing it in lets the loop lock onto the cutoff instead of collapsing onto
the lowest surviving mode, which is why every runaway used to arrive as the same
dark hum whatever Filter was set to. Measured: the runaway's spectral centroid
moves from 270 Hz to 2300 Hz across the control, and it is bit-identical at 0.
It is also the only highpass anywhere in the plugin -- the loop had none except
the saturator's 10 Hz DC blocker.

**Two engraved trims** in the empty band between the hero row (boxes end at
y 278) and the lamp (top y 324): FOLD at x 76..380 and COLOUR at x 520..824,
both at y 284..318 and 6 px clear of each. They are trims rather than knobs
because the two knob rows are full and because these are controls you aim once
and play against, not ones you ride.

**The presets were re-voiced once**, against a settled loop, and a fifth was
added. Bat Cave sat at Decay 0.92 calling itself "just under runaway" while
being 2.15 dB below unity against a ceiling it could not have reached anyway; it
is at 1.18 and it sustains (-16.3 dBFS at 1 s against -17.3 at 4 s, where it
used to lose 7 dB over the same interval). **Possession** is the new one: no
input dependency at all, and it grows rather than decays (-22.0 dBFS at 1 s
against -18.6 at 4 s).

Bat Cave's `tonespitch 55` with no `toneslevel` was left exactly as it was.
Three separate agents called it "the pitch of a switched-off oscillator" and
proposed adding a drone; that is wrong. `Tones::sub` runs regardless of the
drone's level and IS the Time Mod modulator, so 55 Hz sets that preset's FM grid
to 27.5 Hz, which is the metallic edge its name asks for.

### What was measured and NOT acted on

`chaosloop` originally asserted that the level moves, and it does not: the 200 ms
spread is 0.8 dB and stays 0.8 dB whether the Interference-to-Blend depth is
0.25, 0.6 or 1.0. A self-oscillating loop's amplitude is regulated by the
saturator -- that is what stops it running away -- so everything downstream of it
can only move the level a little. Making that number bigger would have meant
letting one route dominate the output gain, which reads as a fault rather than a
gesture. The check now measures the spectral centroid, which does move (424 to
621 Hz across the run), and the finding is recorded here rather than tuned away.

## Stage 2 of the wildness pass (2026-09-07)

Stage 1 took the ceilings off the loop. This one gives the modulation system a
reach and a pair of hands, and unhooks the chip's destruction from the delay
time. Two new parameters, four new knobs.

**Chaos gets its own control.** Every route was multiplied by one macro, so at
Agitate 0 -- the shipped default -- the entire modulation content of the plugin
was Drift's 7 cents of Time trim, and the only way to unmute the chaotic source
was to simultaneously impose a periodic triangle on the cutoff. The matrix now
has per-SOURCE gains: Agitate scales the periodic side, `chaos` scales the whole
Interference row, Drift stays outside both. There is finally a setting at which
the instrument haunts itself without an LFO on top.

**The matrix reaches something.** Three of twenty-eight cells were non-zero and
`setDepth` had no caller anywhere, so Resonance, Absorb, Blend and Strength were
dead destinations for the life of the plugin. Now wired: Agitation to Time (wow,
vibrato, then clang), Follower to Filter and to Strength (play harder, the loop
opens and drives harder -- into a clipper ahead of the loop, so it cannot
destabilise anything), Interference to Filter, Resonance and Absorb.

**A rate knob finally bends pitch.** The only periodic Time modulator was the
Tones sub-harmonic, whose floor is 16.35 Hz, so tape wow, vibrato and chorus
were impossible at every setting -- including in the preset named "Wow and
Flutter", whose wobble was actually un-steerable chaos. Agitation now has a
per-sample path to the clock. `EngineTest agitfm` measures 760 cents of bend at
0.8 Hz and 1729 at 5 Hz, and a sideband grid 1.77 times the carrier at 275 Hz.

That scenario also documents something worth knowing: **this delay's FM has
nulls.** One clock drives the write and the read, so what repitches buffered
content is the difference between the modulator now and one delay-time ago --
and when the delay is a whole number of modulator periods that difference is
zero and the modulation cancels. At Time 0.25 the nulls are 10.8 Hz apart, and
300 Hz sits close enough to one that the grid collapses from 1.77 to 0.28.

**Time Mod got a taper instead of an amputation.** The first playthrough cut it
8x, from 2.0 octaves to 0.25, linearly -- which deleted the destroyed-pitch
region rather than relocating it, and left the knob topping out at exactly the
point the original complaint was about. `timeModOctaves` is the old linear curve
plus a quartic tail: 0.0328 octaves at 13 % of the knob against the old 0.0325,
so every position tuned by ear is unchanged to within one per cent, and the top
30 % is the region that never existed. 15 semitones at the top, and the sideband
grid goes from 0.24 of the carrier to 16.7.

**Two bugs on the Time column.** Time Mod's depth was added into the
Interference cell and then multiplied by the chaos sample, so the knob was
secretly a second chaos-depth control whose smear rode loop energy -- the
metallic grid got LESS defined the harder you played. The Tones term was also
added after `timeOctave` had clamped, so half the excursion escaped the bound
its own constant claimed to enforce. There are now four independent terms,
summed and clamped once. **`EngineTest generative` had to be re-pointed at the
Chaos control as part of this**, because 100 % of the chaos it was measuring
came through that bug; left alone it would have gone on passing while testing a
modulator that had just been switched off.

**Crust.** Every degradation axis -- converter drive, bit depth, hiss,
bandwidth -- was a monotone function of the clock, and the clock is the Time
knob, so destruction was a side effect of choosing a long delay and never a
choice. At a 57 ms slapback Crust now takes THD from 2.8 % to 9.8 %, the floor
from -88.9 to -56.0 dBFS, and 2 kHz down 43.6 dB relative to 200 Hz. At zero it
is bit-identical, which is what keeps `thd` and `noise` a calibration rather
than a coincidence.

**The clock got room, and the bleed got a clamp in the same commit.**
`kFsChipHardMax` / `kFsChipHardMin` 250000 / 750 -> 600000 / 500, stated as an
invariant: log2(3) = 1.585 octaves of headroom in BOTH directions against a
1.5 octave modulation clamp, so the modulator is compressed by its own clamp
before the clock is ever flat-topped. The bleed exponent was unclamped and
`kInvLogBleed` normalises against 1500 Hz, so widening the floor would have
quietly re-opened the pitched squeal removed by ear in 6917f69 -- fs/2 at a 1 s
delay is 2751 Hz, which is the tone that was reported. `bleed` still measures
-86.6 dBFS on an empty loop.

**The filter's sweep stopped squaring off.** Upward modulation is compressed
against a fixed 12 kHz ceiling rather than clipped at 0.45 x sr, so the
modulator keeps its shape as the depth runs out and the same patch sweeps
identically at 44.1, 48 and 96 kHz.

**Modulation may now push Decay past where the knob stops** (`kDecayModHeadroom`
1.15), so a hard hit can surge the loop past its own ceiling and let it settle
back. The saturator bounds the result regardless of gain.

Control rate doubled to 3 kHz with `kInvControlBlock` now DERIVED rather than
written out again as a literal -- as a hand-written 1/32 it was a landmine, since
changing the block alone would have broken both the source ramp interpolation
and the agitation mean with no compile error. Interference's speed ceiling went
from about 13 Hz to about 32, so the chaos has a flutter register and not only a
drunken bend. The follower's full scale went to 0.65 so a slam has somewhere to
go now that it drives three destinations.

**Four knobs on the plate**, a documented deviation from the v3 canvas: CHAOS
and CRUST beside the IN trim, TONES and PITCH beside OUT, in the two 208 px
windows either side of AGITATE and SPEED. Two of them are not new features --
Tones Level and Tones Pitch have been working DSP with no control since Phase 4,
and Tones Pitch silently sets the sideband spacing of the TIME MOD knob that was
already on the plate. **The lamp now flickers with the chaos**, from an atomic
the engine has published since Phase 3 and nothing ever read: a self-playing
instrument you cannot watch is one a player concludes is doing nothing.

Every new control has a scenario whose job is to prove it is worth turning, not
that it exists: `crust`, `timemod`, `agitfm` and `routes`. That is the failure
mode of the first playthrough written into the harness.

### Defaults changed, with Roy's sign-off

- **Filter 8000 -> 2000 Hz, range top 18000 -> 12000.** Each chip stage carries
  a fixed 4.5 kHz output filter, so `bandwidth` measures 8 kHz at -25.6 dB
  through one stage and `probe` returned the same self-oscillation level at
  Filter 4 k, 8 k and 18 k. The top fifth of the knob was provably inaudible and
  the old default sat inside it, which is why AGITATE read as cosmetic.
- **Agitate 0 -> 25 %, Chaos ships at 20 %.** A fresh instance now modulates
  itself. Chaos is gated by loop energy, so it stays quiet until you play into
  it.
- **Agitation to Filter is bipolar-centred**, a deliberate departure from the
  hardware's unipolar 0-6 V normal, recorded in DESIGN.md section 4a. The source
  is a unipolar ramp whose mean is exactly 0.5, so half the route was permanent
  DC brightening pinned against the cutoff ceiling before the sweep began.

## Stage 1 of the wildness pass (2026-09-07)

Roy's read after living with it: "everything works technically but this plugin
is meant to be much more experimental than it currently is ... you can't really
push it very far and get musical results."

That was two complaints, and the second one was the structural one. Every
extremity control in the build was **subtractive**: Absorb attenuated, long Time
damped, Resonance turned out to be a level-dependent compressor, the runaway
settled into a sine, and Strength hard-clipped and stopped. Nothing anywhere in
the signal path got louder, brighter or more unstable as you pushed it, so the
plugin's only answer to being pushed was to get quieter and darker.

This stage moves no defaults and adds no parameters. It takes the ceilings off.

1. **Resonance was a compressor.** `kSvfSatLimit` is an absolute clamp on the
   filter's bandpass integrator, so the resonant gain depended on how hot the
   signal was. Measured at 0.5: +37.9 dB at an amplitude of 0.001 and **-1.5 dB
   at 0.5**, which is roughly where the loop runs — so the whole knob was worth
   5.8 dB there, and turning Resonance up during a runaway *reduced* the loop
   gain. At 2.0 the same measurement reads +6.6 dB and the knob is worth 12.75
   dB. The bound moves off the integrator and onto the loop saturator, which is
   where `TimeFilterLoop`'s own comment always said it was.
2. **Resonance was also a switch.** `k` first went negative at res 0.961, so
   with the parameter stepped in integer percent the filter was an active
   oscillator at 97, 98, 99 and 100 and nothing below. Curve 1.5 -> 2.0, start
   0.92 -> 0.78, overdrive 0.03 -> 0.08: the oscillation region is now about
   thirteen steps wide and the filter free-rings to 0.75 at 8 kHz with Decay at
   zero. It is a voice you can play into and sit just underneath.
3. **The runaway zone was 1.21 dB of excess gain**, which is why it always
   settled as a sine (measured crest 1.41 at every setting). `kDecayMax` 1.15 ->
   1.45 with `kSatDrive` 1.0 -> 1.2 in the same commit, because the saturator's
   asymptote falling from 1.05 to 0.875 is what pays for the bigger budget. The
   equilibrium now runs peak 0.71, RMS -4.9 dBFS, **crest 1.25** — 3 dB louder
   and audibly squarer. `EngineTest sustain` prints the curve, and it is what
   chose 1.45: the step gain has flattened to 0.70 dB by the top.
4. **Decay's range is now two linear segments joined at unity**, so every
   position below 1.0 is bit-identical to what it was, including the default,
   and the entire extra ceiling is spent on the red zone. `ProcessorTest`
   asserts both halves of that. The editor reads the hatching threshold off the
   range instead of computing `1 / kDecayMax`, which with a non-linear range
   would have started the red a fifth of a turn early.
5. **Absorb was four attenuators and nothing additive.** `kAbsorbOutMaxDb`
   18 -> 6 and `kAbsorbFbMaxDb` 4 -> 2, with `kAbsorbShelfMax` 0.7 -> 0.9 so the
   character moves into the per-iteration shelf where it compounds over repeats
   instead of taking 18 dB off the wet in one go against an Out fader that
   stops at +6.
6. **Strength was a hard clipper wearing a comment that said "asymptotic".**
   `pt::fastTanh` clamps its argument to +-3 and returns exactly 1.0 there, so
   `softClip` was pinned flat above |x| = 1.6 and the top 25 dB of a 40 dB knob
   only changed the duty cycle of an already-square wave. `std::tanh` in that
   one place, knee 0.7 -> 0.45. THD on a -30 dBFS source now reads 0.1 / 0.2 /
   0.6 / 2.6 / 18.7 % across the knob. `fastTanh` is untouched everywhere else,
   because the chip's THD calibration is pinned to its 8/27 cubic.
7. **A live bypass bug.** `driven` was forced to zero while bypassed so the loop
   would run on silence, but the Tones drone was added unconditionally two lines
   later — so a bypassed plugin was still being fed a full oscillator, and
   un-bypassing dumped a circulating drone the player never played.
8. **`kFeedbackFromTapSum`'s comment described an A/B the code cannot perform.**
   Line 174 picks one node and line 207 sends the same node to the wet, so
   flipping it would delete the three-step repeat from the *output* too. Comment
   corrected; the flag is not flipped. The tap rebalance takes what it was
   reaching for.

Heard as an A/B against renders from the previous commit, the shape is right:
the patches that were being damped got much louder and the polite ones did not
move. Wow and Flutter **+12.9 dB** in the tail, Bat Cave +6.7, runaway +6.4,
generative +2.9 — and the clean short delay +0.0 dB.

Three new scenarios exist because these were all choices that needed a number:
`sustain` (the equilibrium curve against Decay), `resonance` (gain at cutoff
against level, and the filter's own free ring) and `strength` (whether the top
of the drive knob still changes the texture). `probe` gained the Absorb-against-
Decay rows and `runaway` gained a spectral print, because "mud or a sine" is a
spectral claim and the only number it produced before was a level.

## Headroom, measured

Both figures improved while the loop got hotter, which was the point of moving
`kSatDrive` and `kDecayMax` in the same commit: the roof comes down as the floor
goes up.

Over the 30 minute soak with EVERY parameter sweeping -- including Chaos, Crust,
Colour, Tones Fold and Tones Pitch, and therefore including the closed feedback
path from loop energy through the chaos to the drone's pitch and back -- the
mono peak is **0.84** (was 0.98 before the pass) and the stereo peak with Spread
at 60 % is **1.05** (was 1.35). No creep over thirty minutes, no DC.

The new parameters had to be added to that sweep, not just measured once: a
headroom figure that covers a plugin which no longer exists is worse than no
figure, and the drone feedback path in particular needs thirty minutes rather
than three.

The side component is still the one output path the loop saturator does not
bound, because it is a difference taken after the loop. The mono sum is
untouched by construction: L + R still sums back to exactly the wet at any
width.

## Closed: Absorb versus the runaway zone

**Answered, and it was not one constant.** The finding was right that Absorb
vetoed the runaway zone, and wrong that lowering `kAbsorbFbMaxDb` alone would
fix it: even at 1.0 dB the required Decay for unity at full Absorb sits above
1.15, so the veto survives. The budget was the problem, not the tax.

`kDecayMax` 1.15 -> 1.45 (a 3.23 dB zone rather than 1.21 dB) and
`kAbsorbFbMaxDb` 4.0 -> 2.0 together, so Absorb costs a fifth of the budget
where it used to cost four fifths. Measured with the new `probe` rows, 30 s of
silence at Decay 1.45, Time 0.30, Filter 2 kHz:

| Absorb | before | now |
|---|---|---|
| 0 % | -9.1 dBFS | -4.9 dBFS |
| 20 % (the default) | -48.3 dBFS | -8.0 dBFS |
| 50 % | — | -13.0 dBFS |
| 100 % | — | -19.9 dBFS |

The red zone is now reachable at every Absorb setting including 100 %, which is
what the hatching on the plate has been promising since Phase 5.

## Open finding: how far the tap rebalance should go

`kTapWeightRaw` moved from { 1, 0.85, 0.7 } to { 1, 0.7, 0.5 }, which lifts the
first arrival from -8.13 dB to -6.85 dB at no noise-floor cost (it changes the
mix, not the gain, which is why this and not `kWetMakeup`). The design called
for { 1, 0.5, 0.35 } and +2.79 dB, and that was measured and rejected: it costs
the plan's tell 1. `EngineTest repitch` ramps Time under a 450 Hz tone and
tracks the summed wet's pitch, and the boundary is sharp — 325 Hz at
{ 1, 0.7, 0.5 }, 400 Hz at { 1, 0.6, 0.4 }. The shortest tap is the one that
repitches least, so once it dominates the mix the tape smear is what you stop
hearing. Whether 1.5 dB of wet level is worth a shallower smear is an ear
question.

## Measured (48 kHz unless stated)

| Behaviour | Measured | Plan or design target |
|---|---|---|
| Delay time accuracy | 100.19 ms versus 100.18 expected | within 2 % |
| THD at 31 ms | 0.145 % | 0.13 % (ElectroSmash) |
| THD at 342 ms | 0.995 % | 1 % |
| THD at 1 s | 1.82 % | 3 % plus (low end of the bracket) |
| Noise floor at 27.5 ms | -92.7 dBFS | about -90 dBFS |
| Noise floor at 3.67 s | -60.1 dBFS | clearly audible hiss |
| Bandwidth at short Time | -0.3 dB at 1 kHz, -25 dB at 8 kHz | flat to about 1 kHz then rolls off |
| Bandwidth at 1 s | -7.9 dB from 1 k to 2 k | collapses with Time |
| Clock bleed, empty loop | -86.6 dBFS at 750 Hz | inaudible with nothing in the delay |
| Clock bleed, loop ringing | -81.2 dBFS at 1500 Hz against -95.4 empty | rides the repeats |
| Runaway at Decay 1.45 | peak 0.711, RMS -4.9 dBFS, crest 1.25 | bounded, never digital clipping |
| Runaway at Decay 1.15 | peak 0.550, RMS -8.4 dBFS, crest 1.45 | for comparison: the old ceiling |
| Resonance at the loop's working level | 12.75 dB across the knob | was 5.80 dB, and lossy at the top |
| Filter's own free ring, Res 100 % | peak 0.75 at 8 kHz, Decay 0 | a voice you can play, still bounded |
| Strength THD, -30 dBFS source | 0.1 / 0.2 / 0.6 / 2.6 / 18.7 % at 0/10/20/30/40 dB | keeps biting to the top |
| CPU, full engine | 0.24 % of one core | under a few per cent |
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

### Phase 5 — UI (rebuilt from the v3 canvas)
- [x] Both sheets rendered and reviewed (dark default, light parchment)
- [x] Ten engraved knobs on the 900 x 620 plate, at the canvas's sizes
- [x] Decay's danger zone: red index ticks, hatched between them, whole face red past unity
- [x] Live modulation arcs inside the face (the canvas computes these and does not draw them)
- [x] Time grows detents and reads note names while synced, and says when a division is capped
- [x] The lamp, driven by real loop energy with the canvas's flicker on top
- [x] IN and OUT trims on the edges, hatched meters climbing solid ink rails, peak holds
- [x] Shift-drag fine adjust, double-click Decay to Clear
- [x] Three typefaces embedded; verified the Hebrew wordmark renders
- [x] Switching sheets cross-fades the whole plate over 350 ms, reviewed mid-dissolve
- [x] Window scales, aspect locked; pluginval Editor and Editor Automation pass
- [x] Fixed a shutdown crash the fonts introduced: a static Typeface::Ptr
      released its font after JUCE had torn down, which throws on a dead mutex
      and would have taken a host down on unload

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
- [x] Sample rates 44.1 / 48 / 96 / 192 kHz: delay time within 0.03 %, floor within 0.1 dB
- [x] Buffer sizes 1 / 17 / 128 / 512 / 4096: bit-identical output
- [x] Mono to stereo: both outputs carry signal and match with Spread off
- [x] Offline render matches realtime (block-size invariance proves it: the
      engine has no wall-clock dependency anywhere)
- [x] Automation across a bounce: `soak` sweeps every parameter continuously
      for 30 minutes, including bypass in and out every five
- [x] State persistence and device copy-paste (`ProcessorTest` round trips the
      full state blob, which is what a copy-paste is)
- [x] Host bypass mid-sound: crossfades to dry, loop survives, no click
- [x] 30-minute soak: finite throughout, mono peak 0.98, level -16.2 to
      -16.4 dBFS, no DC accumulation
- [ ] The same matrix inside Ableton Live 12 (quit and reopen to rescan)
