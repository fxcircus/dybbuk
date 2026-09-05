# 02. Modulation system

Source: `inputs/mod_1_stability.md` (judge winner) with grafts from
`inputs/mod_0_generative.md`, adapted to the loop that already existed. Plan
section 2.5 and Phase 3.

## 1. How it reaches the loop

The design this came from assumed it could hand the loop a per-sample ratio
array and a bundle of ramped gains. The loop was already built and measured by
then, so the integration is different and simpler: modulation offsets ride in
`TimeFilterLoop::Params` and are applied **after** the knob smoothers.

That split matters. A knob still glides over 30 ms so turning it cannot zipper,
while modulation reaches the coefficients undamped at control rate. Smoothing
modulation with the same filter would have made a 5 Hz filter sweep into a
1 dB wobble.

Time is the exception and stays audio rate: it goes through the existing
per-sample `modOct` array into `ChipClock`, because a Time modulation held for
32 samples is a staircase, and the metallic FM character is exactly what that
staircase destroys.

```
engine block
  -> sub-blocks that end on a control tick every 32 samples (counter persists
     across host blocks, so nothing depends on the host's buffer size)
  -> at each tick: drift.tick(), interference.tick(loopEnv, lastSample),
     matrix.tick(agitationMean, follower01, wander, drift)
  -> per sample: strength, follower, agitation, and modOct for Time
  -> loop.process(mono, wet, sub, paramsWithOffsets, modOct)
```

## 2. Sources

| Source | Rate | Range | What it is |
|---|---|---|---|
| Agitation | per sample | 0 to 1 | looping or gated attack/decay ramp, 0.016 Hz to 1 kHz |
| Input Follower | per sample | 0 to 1 | peak follower on the post-Strength signal, 5 ms attack, 100 ms release |
| Interference | tick plus per-sample ramp | -1 to 1 | Lorenz chaos driven by the loop's own energy, plus crackle |
| Drift | tick plus per-sample ramp | -1 to 1 | filtered random, 0.05 to 2 Hz |

**Agitation** floors its shortest segment at 0.5 ms, so a 99/1 saw at 1 kHz
never becomes a one-sample cliff: a cliff into Time is a pitch snap, which is
a click. Control destinations receive the **mean over the tick**, accumulated
per sample across sub-blocks and host blocks, not a point sample. A 1 kHz
generator point-sampled at 1.5 kHz aliases into garbage on the filter; its
mean converges to the shape's average instead. In Gate mode a retrigger
relaunches the attack from the current output rather than from zero, so
playing over a still-open cycle does not step the filter.

**The follower** doubles as the onset detector for Gate mode: above -30 dBFS,
above 1.8 times a 300 ms lagging baseline (so it fires on attacks rather than
on sustain), with 20 ms of hold-off so one pick cannot count twice. The
baseline updates after the test, or a slow swell would outrun itself.

**Interference** is the plan's magic ingredient and the thing it says to budget
tuning time for. A Lorenz system whose rho is driven by the loop's energy
across the chaos threshold at 24.74: a quiet loop settles to a fixed point and
a hot one thrashes. Three details make it musical rather than merely random:

- The output is `tanh((y - x) * scale)`, which is `dx/dt` over sigma. It is
  exactly zero at any fixed point, so a quiet loop produces no modulation at
  all. Taking `x` directly would park on one lobe and detune the delay by a
  constant.
- The loop's own last sample is injected into the state (`x += 0.15 * energy *
  sample`), which is what makes the source correlated with what you played
  rather than independent of it.
- A `heat` integrator over 6 seconds adds to rho, so a loop that has been hot
  for a while gets wilder still. It is the only long-timescale memory in the
  system, and it is why the texture goes somewhere over minutes.

On top of the wander sits a sparse crackle whose density grows with the square
of the energy, rising over 0.3 ms so it never steps the clock.

**Drift** is always on at a hidden depth of 0.006 octaves (about 7 cents),
outside the matrix and outside the Agitate macro. This is the plan's promise
that the same settings never land in the same place twice.

## 3. Matrix

Four sources by seven destinations, bipolar depths, each destination scaled in
its own units: Time in octaves (full scale 2), Filter in octaves (4),
Resonance, Decay, Absorb and Blend linear (0.5), Strength in dB (20).

Three hero routes are wired by default, the first of which is the hardware's
normalled connection:

| Route | Depth | Effect at Agitate 1 |
|---|---|---|
| Agitation to Filter | 0.75 | cutoff opens up to 3 octaves over the cycle |
| Interference to Time | 0.50 | up to 1 octave of chaotic wander |
| Follower to Decay | 0.30 | Decay rises up to 0.15 while you play, so digging in near unity tips it into runaway and it relaxes when you stop |

Unipolar sources with a positive depth only push a destination upward from the
knob, which is the hardware idiom: set Filter dark and let the agitation open
it.

The **Agitate** macro multiplies every cell, with a 1.5 power curve so the
lower half stays subtle. **Time Mod** is deliberately not macro-scaled: it is
its own depth on the Time column, so clock FM works whether or not Agitate is
up. When the Tones oscillator arrives in Phase 4 it becomes the Time Mod
source, as on the hardware where Time Mod is normalled to the sub-harmonics
output.

Control destinations are one-pole smoothed at control rate (Filter and
Resonance 30 Hz, Decay 40 Hz, the rest 20 Hz) so a tick cannot step a gain.

## 4. The one feedback path

Loop energy drives Interference, which modulates Time, which changes the loop
energy. It is bounded at every stage: the energy is a normalised 0 to 1 with a
20 ms rise and a 400 ms fall, rho is clamped to its range, the Lorenz state is
clamped to a hard bound and reset if it ever goes non-finite, the wander is a
tanh, the total Time modulation is clamped to 2 octaves, and the loop envelope
input is sanitised at the boundary before anything reads it.

## 5. Measured

`EngineTest generative` is the plan's pass/fail for the whole concept, and it
is measured in two halves because the two claims need different conditions.
Non-repetition is tested with Agitate at 0, where the only things moving are
Interference into Time, the drift trim and the chip's own noise: with the
agitation running, its cycle is a deliberate periodic driver and would show up
as exactly the correlation the test looks for.

| Claim | Measured |
|---|---|
| Self-oscillates from nothing, no input ever | -11.5 dBFS over 20 to 55 s |
| Never repeats | correlation 0.89 at 2 s falling to -0.28 at 40 s, no rebound above 0.27 |
| Chaotic, not merely noisy | a 1e-5 nudge at t=0 changes the output at 50 s by 141 % |
| Reproducible when seeded | identical hashes |
| Evolves | energy spread 0.79 across twelve 5 s windows |
| Agitation period | within 0.02 % at 0.5, 4 and 40 Hz |
| Follower | 4.98 ms attack, 100.0 ms release |
| Interference at rest | RMS 0.0004, versus 0.25 warm and 0.31 hot |
| Drift | two runs differ, pitch unmoved to 0.0 cents |

## 6. What this surfaced about Absorb

The milestone cannot run at the default Absorb of 20 %. Absorb takes up to
4 dB per iteration out of the feedback and the loop has only 1.2 dB of margin
at Decay 1.15, so even a fifth of the knob damps self-oscillation completely:
-9.1 dBFS at Absorb 0 against -48.4 dBFS at Absorb 0.2 (`EngineTest probe`).

That is Absorb doing what the manual describes, diminishing the signal into the
earth. But it means the runaway zone at the top of Decay only exists at low
Absorb, which is worth a listen before deciding whether `kAbsorbFbMaxDb` should
come down. It is one constant.
