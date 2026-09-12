# Dybbuk — the Burst direction

**Written 2026-09-09.** Roy scrapped the Strega Time/Filter emulation as the
concept (the UI shell, themes, knobs, faders, the lamp, the start-over
command and the state plumbing all stay). The new concept is the micro-looper
channel of the Chase Bliss BAD MOOD, specifically its **Burst** mode: an
instant rhythmic pattern maker driven by an audio gate.

This file records what the hardware actually does (from the BAD MOOD manual,
BAD1.1, and its MIDI manual), where Roy's brief differs from it on purpose,
and the model the first test was built against. `docs/PLAN.md` is superseded
for everything below the UI; the phase gates for the new engine live at the
bottom of this file until they have earned a place in PLAN.md.

## What the hardware does (manual, verbatim where it matters)

**The micro-looper channel** is an "always listening looper ... It is
continuously recording when bypassed, and then you turn it on and see what
you get." Loop length is not set by hand: "Instead of manually setting the
length like a typical looper, it's set by the CLOCK position", where CLOCK is
the sample rate of the whole pedal (a fixed-size memory, so half the sample
rate is twice the loop and an octave down, the same trick as the PT2399 we
just left). A HALF dip switch halves the loop. It has three states,
Recording, Playing, Overdubbing, and "no stop command and is never really
off". Hold the footswitch to overdub; a FADE hidden option makes loops fade
while overdubbing "for slowly evolving loops or the ability to treat the
Micro-Looper Channel like a delay". Bypassing it again acts as REPLACE: "it
starts to erase the existing loop and record the input audio in its place".

**Burst mode** ("Loop sequencer"):

> Burst mode takes your loops and turns them into rhythmic patterns of up to
> 8 steps. These patterns dynamically react to your instrument when in
> playback, to create randomizing "fills" when you play along.
>
> Burst mode turns a micro-loop into a sequence. Wherever a "unique" sound is
> detected in the loop, Burst creates a step. It then cycles through those
> slices of audio to create a pattern.
>
> The strange part of Burst is that your overdubs are not directly recorded
> into the sequence, they are recorded into the micro-loop that it's built
> from. This means that what you hear and where you're being recorded are
> disconnected.
>
> It's a playful and unpredictable process, but the steady pace of the
> sequencer ensures everything always feels rhythmically connected.

Controls in Burst: **LENGTH** "Sets the speed of the pattern / size of each
step." **MODIFY** "Sets the sensitivity of the envelope detector. When a sound
is louder than the threshold while in playback, the pattern will be
scrambled." So the "fills" are a scramble of step order triggered by the
input gate, not new material.

Stable sequencing advice from the manual: record the loop in another mode
first and then switch to Burst, or "play short, muted notes, so that the
different sounds in the loop are neatly separated". That tells us the slicer
is an onset detector on the loop contents, and that it struggles with
legato material; the manual diagram shows the micro-loop cut at eight
onsets, then the sequence 1..8 repeating.

Clocking: the pedal can sync to MIDI clock with a division from 32nd note to
whole note; below 60 BPM it has to lower its internal sample rate to get
there, "resulting in the speed and pitch of existing audio being shifted".
The step clock is otherwise free-running from the LENGTH knob. Tap tempo
exists over MIDI.

Neighbouring modes, for the port-over discussion: **Mask** (a threshold gate
on the loop's loud parts, "changed in a way of your choosing"; MODIFY at zero
plays the pure micro-loop), **Radio** (five reinterpretations of the same
loop: Tape speed/direction, Ambient time-stretch, Orchestral voices,
Shoegaze frozen moments, Dance rotating half/double/normal speed). Global:
**GLUE** end-of-chain saturator, **CROSS** amplitude+pitch interference
driven by the input or the other channel, **SPREAD** stereo, **TRAILS**,
**LATCH** for the hold functions, ramping/bounce of any knob.

## Where the brief differs from the hardware, on purpose

Roy's brief is a gated *step recorder* rather than a loop that is sliced
after the fact:

1. **Wait for signal.** Nothing is captured until the input gate opens.
   The hardware always records and slices when engaged; ours listens.
2. **Each gated event becomes a step as it happens.** The hardware slices a
   finished loop into up to 8 steps at once. Ours appends a step per onset,
   so the pattern grows while you play and the sequencer can be running
   already.
3. **Play over the sequence.** With overdub on, each new captured event adds
   a step to the pattern. The hardware's overdubs go into the loop and
   only reach the sequence through re-slicing; ours go straight into the
   sequence, which is the "what you hear and where you're recorded" gap
   closed deliberately.
4. **Free and hosted clocks.** The hardware step clock is a knob or MIDI
   clock. Ours has a free mode (step time in ms) and a transport mode (a
   note division locked to the host's PPQ, so the pattern lands on the
   grid and survives a transport relocate).

Kept from the hardware: the steady step clock is what makes it musical
("everything always feels rhythmically connected"); the input gate during
playback drives *fills* (a scramble of the order); the step count ceiling;
the sample-rate-as-length idea is worth keeping as a later "Clock" control
because the delay core's variable-rate memory is already written.

## The model the first test is built against (B0, shipped 2026-09-09)

`Source/dsp/BurstEngine.h/.cpp`, headless, proven by `EngineTest burst`
(20 checks) and audible in `EngineTest render` as `dybbuk_burst.wav`.

- **Gate.** A peak follower (instant attack, 20 ms release) on the mono
  input against a threshold in dB. The gate opens when the peak crosses the
  threshold and closes 6 dB under it, so a decaying tail cannot chatter it.
  A re-attack inside an open gate splits the step: the peak jumping 2.5x
  over a baseline that rises in 30 ms and falls in 80 ms. That asymmetry is
  load-bearing: a symmetric baseline let every note's own attack split
  itself as soon as the 20 ms hold-off expired.
- **Pre-roll.** Detection lags the transient, so the last 4 ms of input
  are prepended to every step. Fades (2 ms) are applied on playback only;
  the material is never touched.
- **Steps.** A pool of 17 mono slices, each allocated for 2 s in `prepare`
  (about 6.5 MB at 48 kHz, 26 MB at 192 kHz). One spare over the ceiling
  of 16 means capture never writes into a slice the sequencer may be
  reading. A held note past 2 s commits as-is.
- **Freeze.** Off, every gated event becomes a step, appended as its gate
  closes. On, the gate stays shut and the pattern is frozen: that is "play
  over it". (Shipped as a Record arm first; Roy pointed out that armed is
  the normal state and bypass is how audio stops, so the switch is Freeze.) The first design had a
  play-over switch that still captured one step while off, which turned
  out to be useless in the test, so the switch became a plain arm.
- **Sequencer.** A steady step clock starts on the first commit, so the
  pattern's phase is the moment the first thing you played ended. Every
  tick moves to the next step and plays it from its start. Material longer
  than the step is faded out at the boundary; shorter material leaves a gap.
  Free mode only so far; the step length is read at tick time.
- **Ceiling.** Past `maxSteps` the oldest step is dropped, so an armed
  pattern is the last N things you played. If the dropped step was the one
  sounding, it goes quiet until the next tick.
- **Start over.** The same mailbox pattern as before: the pattern empties,
  the engine goes back to listening, `uiClearsServed` acknowledges.
- **Published to the UI.** Step count, current step, gate state, a
  per-step peak level, input and output peaks. Enough for the lamp to
  become the pattern.

Proven by the numbers: nothing plays until the first commit; four bursts
make four steps in the order played; adjacent onsets sit within 2 samples of
200 ms and the same step recurs to the sample; an 80 ms burst sounds for
79 ms; a 60 ms step leaves a silent gap; block sizes 1 / 128 / 512 hash
identically; 44.1 and 96 kHz keep the clock; the ceiling drops the oldest;
a long note is cut at the boundary with no click (largest sample step
0.022 against a sine's own 0.022); a louder re-attack splits, a held note
does not.

## B2 engine additions (2026-09-09, same day)

Roy's decisions: the pattern is not saved with the session but can be
dragged out as a WAV; bypassed means deaf; the ceiling is a Steps control
from 1 to 16; full-and-armed is a switch between replacing the oldest and
holding; the transport phase and the knob map were left to me.

- **Transport mode.** The processor resolves the step clock, never the
  engine: free mode hands over the knob in seconds; synced mode hands over
  the division in seconds plus the distance from the block start to the
  next grid line. The engine puts its tick there, every block, so a
  relocate or a tempo change lands within one block, and the first tick
  after a first commit waits for the grid. The pattern keeps its own step
  phase on that grid; a "reset on the bar" is a later option.
- **Steps 1..16** is the active length of the pattern, live: lowering it
  loops the first N steps, raising it brings the rest back. Only at a
  commit does the ceiling drop or refuse material (Replace / Hold).
- **Direction:** forward, reverse, pendulum, drunk, random (Random at the far end of the knob, Roy's call).
- **Decay** (was Length): a choke, the fraction of the step a slice may sound.
- **Feedback** (was Fade, with the opposite polarity): the level a step
  keeps every play, like a delay's feedback; Inf keeps every step, under
  it a step fades and, under -60 dB, leaves the pattern. Frozen, nothing
  is paid: Freeze holds the pattern whole, so an armed pattern evolves like a delay instead of
  piling up.
- **Fills:** disarmed, a gated onset scrambles the order for one cycle,
  depth being how many pairs are swapped. The hardware's one trick.
- **Chaos:** per tick, a chance of a skip, a ratchet (the slice again at
  the half step), a reverse, or a repeat. Seeded, so the tests are exact.
- **Deaf bypass:** the gate hears silence while bypassed; the sequencer
  keeps its place. The processor's 20 ms crossfade handles the audio.
- **Export.** `copyPattern` is a seqlock read of the pattern from the
  message thread (slices in the pattern are immutable until a mutation
  bumps the generation), and `renderPattern` plays one cycle offline with
  the same voice as the live sequencer. `EngineTest export` proves the
  render matches the live cycle sample for sample.
- **Levels:** In feeds the gate (so the trim is also sensitivity), Blend
  is equal power, Out's floor is silence.

`EngineTest`: 13 scenarios, 54 checks, 0.1 s.

## Proposed port-over plan (for Roy to edit)

What to keep from the repo: the plate, both themes, knobs, faders, trims,
the lamp, the dice, the preset header, the parameter and state plumbing, the
three test harnesses, the bypass crossfade, the output guard, the loop
saturator (as Glue), and the variable-rate read from the PT core (as Clock).
What to remove: the delay loop, Agitation, Interference, the mod matrix,
Tones, drift. Git keeps them.

### B2 — the engine in the plugin
- Parameters (Push bank 1 first): Threshold, Step, Steps, Record, Blend,
  Clock, Glue, Fills; then Direction, Length, Fade, Spread, In, Out, Sync,
  Bypass. Exact list to be agreed below.
- **Free and transport modes.** Free: Step in ms. Transport: a note
  division (1/32 to 1 bar, triplet and dotted) locked to the host's PPQ so
  steps land on the grid and survive a relocate; when the transport is
  stopped, free-run at the host's last BPM. Same Sync toggle and detent
  behaviour the Time knob already has.
- Start-over stays the mailbox it is. Bypass keeps the engine listening or
  not (decision below).
- **The lamp becomes the pattern:** up to N pips on a ring around the red
  dot, each lit by its step's level, the sounding one bright, the dot
  pulsing on each tick; breathing while listening, a flare while the gate
  is open. Rendered in UISnapshot, both themes, listening / playing / armed.
- Presets and the dice rewritten for the new parameter list; ProcessorTest
  readouts and round-trips updated.

### B3 — step manipulation (the hardware's Burst plus what a plugin can add)
- **Fills.** The hardware's one trick: gated input while disarmed scrambles
  the order for a cycle, then it settles back. A Fills depth from a
  two-step swap to a full shuffle.
- **Direction:** forward, reverse, pendulum, drunk, random (Random at the far end of the knob, Roy's call).
- **Length:** choke every step to a fraction of the step time, or let the
  material run past the boundary into the next step.
- **Fade:** the hardware's FADE, applied per cycle: older steps lose level
  each time round unless refreshed, so an armed pattern evolves like a
  delay instead of piling up.
- **Clock:** the hardware's headline control, a global sample rate in
  harmonised steps that repitches and slows every step together.
  **Shipped 2026-09-09 as Pitch**, Roy's name: semitones on the material
  only, the step clock stays Time's. A fresh fractional read in the
  voice, not the PT core's.
- Per-step chance, ratchet, reverse, and a swing amount, driven by a
  single Chaos macro so the dice has something to roll.

### B5 — shipped 2026-09-09, as five modes

Roy's names, in the plate's register, on Teder's segmented toggle:

| Mode | What a step does with its material | Knobs it reads |
|---|---|---|
| **GOLEM** | the sequencer as it was: the material, choked by Decay | everything, as before |
| **TRANCE** (Ambient) | played slower from its start at its own pitch, 1x to 8x, two Hann grains with a phase-aligned respawn (the WSOLA idea) so a note keeps its pitch; the step holds what fits. (First shipped as "stretch to fill the step", which left any note longer than the step untouched; Roy: "Linger seems to need Pitch to do anything".) | Decay = how many times slower; Pitch still pitch |
| **LEGION** (Orchestral) | three voices: unison, up and down by Pitch's interval (octaves at zero, since a few cents of chorus could not be heard); the extra two come and go | Pitch = the interval, not a transpose |
| **WRAITH** (Shoegaze) | the material plays whole, then its last moment is frozen as a held grain that keeps sounding under the steps that follow, up to four stacked, each fading 18 dB over Decay's share of eight ticks, and releasing over 250 ms if the mode moves on or the pattern empties | Decay = how long a haunting lasts |
| **TREMOR** (MKII Env) | while the input is over the threshold the current step is held and ratcheted, then the pattern carries on | Fills = how densely it ratchets (the scramble is off in this mode) |
| **RATTLE** (Blooper Stutter, Tensor Rand) | a slice of the step, 10 to 60 ms, looped for the whole step: a buzz roll | Decay = the slice |
| **MIRROR** | every step's material backwards; Chaos's reverse flips one forward now and then | none |
| **MIASMA** (Microcosm Haze, Particle density) | a cloud of grains from anywhere in the material, for the whole step; the grain player's scatter, no phase search | Decay = the grain, 10 to 80 ms |

Also in this pass: **Bar**, a toggle beside Sync: synced, the pattern
restarts from its first step on every bar line, even one that falls
between grid ticks. Off, it keeps its own phase.

Not built: Rotate (Dance), which is Pitch on a clock and better left to
Chaos; the dial's static between stations (a later option).

Proven: `EngineTest` bar, linger, legion, haunt, tremor, modesexport (a
step's material is trimmed to its audible length at commit, or Linger
stretched the gate's silent release and Haunt froze it).

### B5 — Radio: other players for the same pattern (the proposal, 2026-09-09)

**What the hardware does.** Radio mode "contains five distinct loopers
that take the same recording and interpret it into different genres
spread across various stations. You can scan freely between the stations,
introducing interference and combining the different loops." MODIFY is
the dial; each station has one clean spot where "the static parts", and
between two stations both play, through static. LENGTH means something
different at every station. The stations, in the manual's own words:

| Station | Manual | LENGTH |
|---|---|---|
| Tape | plays the loop in full, but lets you change its speed and/or direction | speed / direction |
| Ambient | keeps the pitch but slows it dramatically, "a cinematic blur" | playback speed |
| Orchestral | "a symphony of different voices that come in and out" | number of voices |
| Shoegaze | "a collection of frozen moments that last forever", stacked layers you navigate | moment selector |
| Dance | rotates steadily between half, double and normal speed, "club night at the circus" | rotation speed |

That is all anyone has written down. Reviews restate it; the walkthrough
videos have no transcripts we can reach. Two stations have ancestors in
the MOOD MKII manual, which is more specific: **Tape** there is speed and
direction "in harmonized steps (octaves)", half to 4x, forward or
reversed; **Stretch** (Ambient's parent) "chops your loop up into slices
and moving through them at a speed of your choosing", slice size on one
knob (small = "blurry and grainy", large = "repeating phrases"), speed
and direction on the other with noon as a freeze. MKII also had **Env**:
"whenever sound is detected at the input it will repeat the current slice
until the sound disappears". Orchestral, Shoegaze and Dance are new and
undocumented beyond the table.

**The translation.** Dybbuk has no loop to reinterpret; it has a pattern
of steps on a clock. So a station here is a different *player* for every
step. The sequencer, the pattern and the clock stay exactly as they are;
the station changes what a step does with its material during its step.
Every station keeps the rhythm, which is the whole point of Burst. Two
controls: **STATION** (detented: Off, then the stations) and **TUNE**
(the station's own parameter, the hardware's LENGTH). The dial's static
is a separate, later decision (below).

**Station by station**, with a viability call and a fun call:

1. **Tape.** Speed and direction. Already shipped as Pitch and Direction,
   and Steps plus Time cover "shorten the loop". Nothing to build. STATION
   Off is Tape.
2. **Stretch** (Ambient). Each step's material time-stretched to fill its
   step at its own pitch, granular: 30 to 60 ms Hann grains, two
   overlapping, a little random offset so the grains do not comb. TUNE
   = stretch factor from 1x to 8x, with the top of the knob a freeze of
   the step's first moment. Length's choke still applies on top. Cost:
   two extra reads per sample, trivial. Testable: a 40 ms burst at 4x
   sounds for 160 ms at the same measured frequency, the harness can
   assert both. **Fun: high.** A muted pick becomes a pad that still
   lands on the grid, and with Fade it dissolves. **Build first.**
3. **Choir** (Orchestral). One to four voices per step at harmonised
   intervals (unison, octave up, fifth, fourth down, octave down, each a
   Pitch-style read), a few cents of detune between them, and the voices
   "come in and out": each voice has a chance per cycle of joining or
   leaving, so the arrangement keeps changing. TUNE = number of voices.
   Cost: N reads. Testable by Goertzel at each interval. **Fun: high,**
   especially with Direction, where it becomes an arpeggiator playing
   your own notes. **Build second.**
4. **Halo** (Shoegaze). A moment of each step frozen into a drone that
   sustains for the whole step: a granular freeze (a 40 ms grain looped
   with a crossfade and a little random position) rather than a spectral
   one, which sounds close enough here and needs no FFT. The "stacked
   layers": the frozen moment does not stop at the step boundary but
   continues under the following steps, up to N layers, so a pattern
   turns into a chord that changes as steps arrive. TUNE = which moment
   of the step is frozen. Cost: up to N frozen reads. **Fun: high,** and
   the most "shoegaze" thing this plugin could do. **Build third**, after
   Stretch has proven the grain engine.
5. **Rotate** (Dance). Every N steps the whole pattern jumps between
   half, normal and double speed: Pitch stepping -12, 0, +12 on its own
   rotation clock. TUNE = steps per turn. Cost: nothing, it is Pitch
   automated. Testable in one scenario. **Fun: medium**; it is an octave
   LFO, but a cheap one, and on a dance pattern it is exactly the pedal's
   joke. **Build last, or fold into Chaos.**
6. **Stutter** (MKII's Env, a bonus). While the input is over the
   threshold, the current step repeats instead of advancing, then the
   pattern carries on where it would have been. Uses the gate that
   already drives Fills. Cost: nothing. **Fun: high live,** a hand on the
   pattern that needs no knob. **Worth a station.**

**The dial.** On the hardware MODIFY is continuous: static between
stations, two stations at once in the gaps. Here the honest version is a
STATION knob with detents and, later, a **Dial** option that crossfades
neighbouring stations with a band of static (noise plus a bit of the
Crust idea from B4) in between. Fun and on-brand, but it is a second
control and a UX question, so it is proposed as B5.2 rather than the
first cut.

**Where it goes on the plate.** Two knobs, STATION and TUNE, on the
bottom strip where Record used to sit, centred as a pair, or STATION as a
word rail and TUNE as a trim. Roy's call. The dybbuk should show the
station: Stretch lengthens the tentacles, Choir splits their tips, Halo
leaves a glow ring hanging after each step.

**Order:** Stretch, Choir, Halo, Stutter, Rotate, then the Dial.
**Gate per station:** an EngineTest scenario that proves the described
behaviour with numbers, the export rendering with the station applied,
and a snapshot of the dybbuk in that station.

### B4 — character and stereo. **Shipped 2026-09-09.**
- Glue (the saturator) as an end-of-chain stage. The Colour and Crust
  lo-fi ideas are not in it; Pitch's decimated read is the only lo-fi so far.
- Spread: alternate steps left and right.
- Radio and Mask stay in IDEAS.md until Burst is finished.

### Decisions that belong to Roy
1. **Does the pattern survive a session save?** The hardware forgets on
   power-off. Stamping up to 16 slices of audio into the state tree is
   possible (a few MB) but unusual for an effect; the default proposal is
   no, the pattern is performance state.
2. **Bypassed: listening or deaf?** The template guidance says silence
   for a looper (it collects nothing while out of circuit). Proposal: deaf.
3. **Ceiling 8 like the hardware, or 16?** The engine allows 16.
4. **Armed and full:** drop the oldest (as built) or stop adding. Roy
   first asked for a switch, then (same day) for it to go: always drop
   the oldest; Freeze is the way to stop adding. The engine keeps the
   Hold path for the test, the plugin does not expose it.
5. **Transport mode phase:** does step 1 realign to the bar, or does the
   pattern keep its own phase on the grid? Proposal: own phase, with a
   "reset on bar" as a later option.
6. **The knob map.** Fourteen knobs and two trims exist on the plate; the
   list above has sixteen controls plus IN, OUT and the switches. Which
   go on the plate and which become hidden or dice-only.

## Gates for the new engine

- **B0 — the basic test.** Headless `BurstEngine` and `EngineTest burst`.
  **Shipped 2026-09-09.**
- **B1 — port-over plan** agreed with Roy: the decisions above. **Done.**
- **B2 — the engine in the plugin.** Parameters, processor, presets, dice,
  the plate remapped, the dybbuk as the pattern, drag-out export, transport
  mode; build.sh, pluginval, auval, both harnesses, snapshots reviewed.
  **Shipped 2026-09-09.** Left for Roy: play it in Ableton, drag a pattern
  onto a track, listen to whether the gap and the instant start feel right.
- **B3 — step manipulation** and **B4 — character** as proposed above.
