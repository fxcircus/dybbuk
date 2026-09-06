# 01. PT core and Time/Filter loop

Source: `inputs/pt_0_fidelity.md` (judge winner, tally 97.5 of a possible 120
across four lenses) with every fatal flaw the judges found fixed and the
grafts from `pt_1_robustness.md` and `pt_2_musicality.md` folded in. The
verdict files are in `inputs/`. Plan sections 2.1 to 2.4.

Level convention: 1.0 = 0 dBFS = the chip's converter full scale. Nominal
loop level 0.3 to 0.7 peak. All THD figures are calibrated at A_REF = 0.5.

## 1. Calibration backbone

PT2399: 44 Kbit RAM, 22 MHz clock = 31 ms, 2 MHz = 342 ms. A fixed memory
read at a variable clock. With N = 5504 words, 31 ms needs fs_chip = 177.5 kHz,
which brackets the plan's 200 kHz upper end.

```
fs_chip(t) = FS_CHIP_MAX * exp(-LOG_RANGE * t)      t = Time 0..1, smoothed, plus mod
LOG_RANGE  = ln(200000 / 1500) = 4.8929
u          = clamp((LOG_FS_MAX - ln fs_chip) * INV_LOG_RANGE, 0, 1)   // "crust", 0 in spec, 1 destroyed
delay(t)   = kTotalWords / fs_chip(t)               // 5502 words: 27.5 ms to 3.668 s
```

| delay | fs_chip | u | plan fact |
|---|---|---|---|
| 31 ms | 177.5 kHz | 0.024 | THD 0.13 %, noise near -90 dBFS |
| 114.6 ms | 48 kHz | 0.292 | chip slower than host from here down |
| 275 ms | 20 kHz | 0.471 | clock bleed onset |
| 342 ms | 16.1 kHz | 0.515 | THD 1 % |
| 1.0 s | 5.5 kHz | 0.734 | THD 3 % plus |
| 3.67 s | 1.5 kHz | 1.0 | destroyed |

## 2. Files under Source/dsp/

| File | Class | Role |
|---|---|---|
| `ChipConstants.h` | `namespace pt` | every constexpr tunable, `kStages`, `delaySecondsForTime01()`, `time01ForDelaySeconds()` |
| `Rng.h` | `struct Rng` | xorshift32, `white()` in [-1, 1), header only |
| `OnePole.h` | `struct OnePole` | `s += a * (x - s)`, header only |
| `TptSvf.h/.cpp` | `TptSvf` | Zavalishin TPT SVF with an exposed, tanh bounded bandpass state (JUCE's hides s1/s2) |
| `ChipClock.h/.cpp` | `ChipClock` | log fs smoothing, audio rate mod input, the one phase accumulator, a control rate `Frame` |
| `PTCore.h/.cpp` | `PTCore` | the silicon: memory, input clip, delta sigma THD, noise shaped TPDF quantiser, DAC pole |
| `PTStage.h/.cpp` | `PTStage` | chip on its board: input MFB, guard pole, core, interpolated read, reconstruction SVF, hiss, bleed, output MFB |
| `LoopSaturator.h/.cpp` | `LoopSaturator` | asymmetric tanh plus 10 Hz DC blocker |
| `TimeFilterLoop.h/.cpp` | `TimeFilterLoop` | kStages stages in series, tap sum, loop SVF, Absorb, saturator, Decay, Clear, UI atomics |
| `DybbukEngine.h/.cpp` | `DybbukEngine` | replaces ExampleEngine: mono sum, Strength, loop, equal power Blend, Out |

Allocation: none at runtime. Chip memory is `std::array<float, kStageWords>`,
1834 words per stage, 22 KB for three, independent of host sample rate (the
delay is N / fs_chip, so 44.1 k and 96 k give identical delay times).
`prepare` is repeat safe: recompute host rate coefficients, then `reset()`.
Host blocks larger than `maxBlock` are chunked by the engine.

## 3. ChipClock

```cpp
struct Frame {
  int nTicks; float invRatio, tickOffset, readFrac;   // per host sample
  float aDac, aGuard, recG, d, invD, biasComp, step, invStep,
        noiseAmp, bleedLvl, u, fsChip;                // refreshed every kCtrlInterval samples
};
void prepare (double sr);          // recompute host rate coefficients, then reset
void reset();                      // phase 0, countdown 0, refresh now; Time target kept
void snapTime();                   // setCurrentAndTargetValue: first block, prepare, preset load
void setTime01 (float t);          // once per block, target of the 20 ms ramp
const Frame& advance (float modOctaves);   // once per host sample
```

```
setTime01(t):  logFsSmooth.setTargetValue (LOG_FS_MAX - t * LOG_RANGE)   // linear ramp IN LOG fs
advance(modOct):
  logFs = logFsSmooth.getNextValue() + modOct * LN2       // mod bypasses the smoother: audio rate
  fs    = clamp (exp (logFs), FS_CHIP_HARD_MIN, FS_CHIP_HARD_MAX)
  ratio = fs * invSr
  phase += ratio                                          // phase kept in [0, 1)
  nTicks = (int) phase                                    // 0 .. 6
  phase -= (float) nTicks                                 // WRAP EVERY SAMPLE (fatal flaw 3)
  invRatio   = 1 / ratio                                  // the only division per host sample, shared by all stages
  tickOffset = 1 - (phase_before_increment)               // tick j lands at host fraction (tickOffset + j) * invRatio
  readFrac   = phase
  if (--ctrlCountdown <= 0) { ctrlCountdown = kCtrlInterval; refreshSlow (fs, logFs); }
```

`refreshSlow` (every 16 samples, persistent across block boundaries):

```
u        = clamp ((LOG_FS_MAX - logFs) * INV_LOG_RANGE, 0, 1)
fcRec    = min (REC_TRACK * fs, REC_FC_MAX)                  // min(0.45 fs_chip, 8 kHz)
aDac     = 1 - exp (-2pi * fcRec / fs)                       // one pole AT CHIP RATE
aGuard   = 1 - exp (-2pi * fcRec / sr)                       // write guard pole at host rate
recG     = tan (pi * fcRec / sr)
d        = SIGMA_DRIVE_MIN * exp (SIGMA_DRIVE_SLOPE * u); invD = 1 / d
biasComp = fastTanh (d * SIGMA_BIAS) * invD
bits     = BITS_MAX - (BITS_MAX - BITS_MIN) * u^BITS_CURVE
step     = exp2 (1 - bits); invStep = 1 / step               // INVARIANT: this pair is always recomputed together, never ramped apart
noiseAmp = 10^((NOISE_DB_SHORT + (NOISE_DB_LONG - NOISE_DB_SHORT) * u) / 20)
bleedLvl = fs >= BLEED_ONSET_HZ ? 0 : BLEED_MAX_AMP * pow (ln (BLEED_ONSET_HZ / fs) * INV_LOG_BLEED, BLEED_CURVE)
```

Why log domain smoothing: a linear ramp in log fs is a constant rate pitch
glide, which is what a clock sweep sounds like. 20 ms is the plan's "warble
instead of zipper". Stepping the slow fields at 3 kHz is inaudible because
Time itself is 20 ms smoothed; only the phase increment needs true audio rate,
which is where the FM character lives.

**Fatal flaw 2, fixed:** `logFsSmooth` is a Linear SmoothedValue whose current
value is 0 after construction, so an unsnapped first block ramps the clock up
from the 750 Hz hard minimum over 20 ms and time compresses everything written
during it into a tick about 27 ms later. `snapTime()` is called from `prepare`,
from the engine's first block after prepare, and from the preset load mailbox.

## 4. The tick (PTCore, one chip sample)

```
fastTanh(x): x = clamp (x, -3, 3); return x * (27 + x*x) / (27 + 9*x*x)    // Pade, cubic coeff 8/27 = 0.296

chipClip(x):                                    // the 1.3 Vrms input ceiling
  ax = |x|; if (ax <= CLIP_KNEE) return x
  return sign(x) * (CLIP_KNEE + (1 - CLIP_KNEE) * fastTanh ((ax - CLIP_KNEE) * INV_CLIP_SPAN))

tick(xin, f, rng):
  v = chipClip (xin)
  v = fastTanh (f.d * (v + SIGMA_BIAS)) * f.invD - f.biasComp   // delta sigma THD, unity small signal gain
  w = v - qErr                                                  // first order error feedback, NTF = 1 - z^-1
  dith = 0.5 * (rng.white() + rng.white()) * f.step             // TPDF, +-1 LSB
  q = f.step * roundToInt ((w + dith) * f.invStep)
  q = clamp (q, -1, 1)
  qErr = q - w
  y = memory[ptr]; memory[ptr] = q; if (++ptr == words) ptr = 0 // read oldest, overwrite: one pointer
  dacPrev = dacCur; dacCur += f.aDac * (y - dacCur)             // the DAC's analog pole, at chip rate
```

Calibration, verified independently by two judges:

- **THD.** For y = x - c d^2 x^3 with Pade c = 0.296, H3/H1 = c d^2 A^2 / 4.
  At A_REF 0.5, 0.13 % needs d = 0.265; 1 % at u = 0.515 needs d = 0.735, so
  the slope is ln(0.735 / 0.265) / 0.515 = 2.0. Result: 0.13 % at 31 ms, 1.0 %
  at 342 ms, 2.4 % at 1 s. SIGMA_BIAS 0.03 adds H2/H1 = 0.05 % at 31 ms rising
  to 2.5 % at u = 1, so even harmonics grow with Time as the chip's do. The
  nonlinearity sits in the WRITE path, so distortion is stored, re-pitched with
  the buffer, and accumulates across passes and stages.
- **Bits and the floor.** Plain 11 bit TPDF is -66 dBFS total. First order
  noise shaping gives an in band factor (pi^2 / 3) / OSR^3, which at OSR = 12.5
  (fs_chip 200 kHz, 8 kHz band) is -27.7 dB, so about -94 dBFS in band. The
  DAC pole at chip rate is what stops the shaped noise, which peaks near
  fs_chip / 2, folding back when the read decimates 200 k to 48 k. Judges'
  estimates of the delivered floor at Time 0 span -84 to -89 dBFS, so the test
  bracket is -82 dBFS, not -90. The bit drop 11 to 8 on u^2 models the
  modulator failing when overclocked low.

## 5. PTStage (host rate)

```
processSample(x, f):
  a = inPole.lp (x); a = inSvf.lowpass (a)              // fixed 3 pole MFB 8.8 kHz (one pole + SVF Q 0.9)
  if constexpr (kGuardPoles >= 1) a = guard1.lp (a) with coeff f.aGuard    // tracks fcRec, no-op above fs_host
  if constexpr (kGuardPoles == 2) a = guard2.lp (a) with coeff f.aGuard
  for j in 0 .. f.nTicks - 1:
      t   = (f.tickOffset + j) * f.invRatio             // host fraction of this tick, in (0, 1]
      xin = xPrev + t * (a - xPrev)                     // linear write resampling
      core.tick (xin, f, rng)
  xPrev = a
  r = core.dacPrev + f.readFrac * (core.dacCur - core.dacPrev)   // linear read resampling
  r = recSvf.lowpass (r)                                // reconstruction, tracks min(0.45 fs_chip, 8 kHz)
  r += hissPole.lp (f.noiseAmp * noiseSrScale * rng.white())     // hiss AFTER reconstruction, fixed 6 kHz pole
  bleedEnv *= bleedDecay
  if (f.nTicks > 0) { bleedEnv = 1; if (f.nTicks & 1) sub = -sub; }
  r += f.bleedLvl * fastTanh (bleedEnv + BLEED_SUB_RATIO * sub)  // tick train at fs_chip plus fs_chip/2 square
  return outSvf.lowpass (r)                             // fixed output MFB 4.5 kHz Q 0.6
```

**Graft applied:** the hiss is injected after the reconstruction filter through
a fixed 6 kHz pole. Injected before it, all hiss at Time 1 lands below
fs_chip / 2 = 750 Hz and reads as rumble rather than the tape hiss the plan
asks for. Amplitude scales as sqrt(sr / 48000) so the in band density, not the
per sample amplitude, is what stays constant across sample rates.

Bandwidth at max clock, tap 1: 8.8 k 3 pole + 8 k DAC pole + 8 k reconstruction
pair + 4.5 k output MFB gives -0.3 dB at 1 kHz, -2.6 dB at 2.8 kHz, -21 dB at
8 kHz, which is the plan's "flat to about 1 kHz then rolls off". At 1 s the
reconstruction pair alone takes 2 kHz down 8 dB. Each stage carries its own
filters as each real chip does, so later taps are progressively darker.

Bleed is injected after reconstruction (a 5.5 kHz tick would otherwise be
filtered away) and before the output MFB and the loop, so it gets re-delayed
and burbles: 0 above 20 kHz, -72 dBFS at 342 ms, -49 dBFS at 1 s, -40 dBFS at
3.6 s.

## 6. TimeFilterLoop

```
process (in, wet, n, p, modOct):
  if (clearCounter changed) begin the Clear fade
  if (!isfinite (fb)) flushAll()                        // per block backstop
  clock.setTime01 (p.time01); set smoother targets
  for i in 0 .. n-1:
    f = clock.advance (modOct ? modOct[i] : 0)
    if (--controlCountdown <= 0) { controlCountdown = kCtrlInterval; refreshLoopCoeffs(); }   // PERSISTENT across blocks
    x = in[i]; if (!(|x| < kInputCeiling)) x = 0        // per sample non finite guard, kInputCeiling 1e4
    node = x + fb                                       // feedback lands here with one sample of delay
    s = node; tapSum = 0
    for k in 0 .. kStages-1: s = stages[k].processSample (s, f); tapSum += TAP_W[k] * s
    y = loopFilter.lowpass (tapSum)
    hp = y - absorbShelf.lp (y); y -= absorbShelfDepth * hp        // Absorb darkening, 1.2 kHz shelf
    y = sat.process (y)                                 // asymmetric tanh plus DC blocker
    y *= clearGain; advanceClearFade()
    fb = (kFeedbackFromTapSum ? y : lastStageOut) * decayGain * absorbFbGain
    wet[i] = y * absorbOutGain * kWetMakeup
    peak = max (peak, |y|)
  publish uiLoopEnergy (peak with a 50 ms release), uiDelaySeconds (kTotalWords / f.fsChip)
```

**Fatal flaw 1, fixed:** the control cadence is a member countdown decremented
every sample, so `skip (kCtrlInterval)` advances the smoothers by exactly the
samples that elapsed regardless of host block size. The original
`if (i % kCtrlInterval == 0)` restarted the cadence at every block boundary,
which made smoothing times depend on block size (a 17 sample block would
advance the smoothers 16 steps per 17 samples; a 4096 sample block 256 times).

```
refreshLoopCoeffs():
  fc = clamp (cutoffSmooth.skip (kCtrlInterval), 20, 0.45 * sr); loopFilter.setG (tan (pi * fc / sr))
  res = resSmooth.skip (kCtrlInterval)
  k = 2 * (1 - res)^RES_CURVE - RES_OVERDRIVE * smoothstep (RES_OVERDRIVE_START, 1, res)
  loopFilter.setK (k)                                   // k = 2 is Q 0.5; res 0.8 is Q 5.6; res 1 is k = -0.03, active
  A = absorbSmooth.skip (kCtrlInterval)
  absorbShelfDepth = ABSORB_SHELF_MAX * A
  sat.setDrive (SAT_DRIVE * (1 + ABSORB_DRIVE * A))
  absorbOutGain = 10^(-ABSORB_OUT_MAX_DB * A / 20)
  absorbFbGain  = 10^(-ABSORB_FB_MAX_DB * kAbsorbFeedbackShare * A / 20)
  decayGain = decaySmooth.skip (kCtrlInterval)
```

TptSvf with a bounded state, which is the resonance path nonlinearity:

```
setG/setK cache h = 1 / (1 + g * (g + k))               // no per sample division
lowpass(x):
  hp = h * (x - (g + k) * s1 - s2); bp = g * hp + s1; s1 = g * bp + bp_in...
  lp = g * bp + s2; s2 = g * bp + lp
  if (stateLimit > 0) s1 = stateLimit * fastTanh (s1 / stateLimit)   // bounds self oscillation, analog style
  return lp
```

LoopSaturator:

```
process(x): y = (tanh (drv * x + SAT_BIAS) - tanhBias) * invDrv     // H2/H1 = 0.025 A
            out = y - x1 + R * y1; x1 = y; y1 = out                 // DC blocker, R = 1 - 2pi * 10 / sr
```

Tap weights `{1.0, 0.85, 0.7}` normalised to sum 1, so `{0.392, 0.333, 0.275}`,
which is what makes "Decay 1.15 is 15 % over unity" true. The 7.7 dB that
normalisation costs the first echo is given back by `kWetMakeup`, applied to
the wet output only, outside the feedback path.

Self oscillation. Two bounds cap the loop: the saturator (tanh asymptote
1 / drv) and the SVF state limit. At Decay 1.15, Absorb 0, short Time, the
describing function 1 - A^2 / 4 = 1 / 1.15 gives A = 0.72 peak into the
saturator, so tanh(0.72) = 0.62 out: about -4 dBFS peak, -7.5 dBFS RMS at the
wet tap. At long Time the chip's own drive compresses first and pulls it to
-8 to -10 dBFS. The wet tap can never exceed 1.0 and nothing hard clips.
High Resonance alone rings at the filter frequency regardless of Decay,
bounded at the state limit: intended.

Headroom contract: wet peaks near -4 dBFS in runaway, Blend is true equal power
(0.707 each at centre), so wet plus dry can reach about -1 dBFS. Out carries
the rest.

## 7. Clear

`requestClear()` is `clearCounter.fetch_add (1, release)` from the message
thread. Momentary, never a parameter, never in state. At the start of each
block the audio thread compares the counter with `lastClearSeen`; a change
starts a fade out over `CLEAR_FADE_SEC` (6 ms), calls `flushAll()` at zero
gain, then fades back in. `flushAll()` zeroes every chip memory, filter state,
`fb`, the saturator history and the clock phase, keeping the Time target. It
is also called from `prepare` and from the non finite guard. No allocation,
no locks.

## 8. Constants (`ChipConstants.h`, namespace `pt`)

| Name | Value | Audible effect |
|---|---|---|
| `kStages` | 3 (`DYBBUK_PT_STAGES` define) | three step repeat versus single echo |
| `kMemoryWords` / `kStageWords` | 5504 / 1834 | delay range |
| `FS_CHIP_MAX` / `FS_CHIP_MIN` | 200 k / 1.5 kHz | 27.5 ms to 3.67 s |
| `FS_CHIP_HARD_MAX/MIN` | 250 k / 750 Hz | FM excursion headroom before clamping |
| `TIME_SMOOTH_SEC` | 0.020 | knob sweep warble length |
| `kCtrlInterval` | 16 | slow coefficient rate |
| `TAP_W_RAW` | {1, 0.85, 0.7} | brightness of the three step repeat |
| `kWetMakeup` | 2.55 (+8.1 dB) | undoes tap normalisation on the wet only |
| `kFeedbackFromTapSum` | true | plan topology versus pure series loop |
| `kGuardPoles` | 1 | 0, 1 or 2 pole tracking write guard: aliased sparkle versus dark chip |
| `kFmLawLinear` | false | exponential octaves versus linear clock FM |
| `CLIP_KNEE` | 0.8 | where the chip's input clip bites |
| `SIGMA_DRIVE_MIN` / `_SLOPE` | 0.265 / 2.0 | THD at short Time / growth with Time |
| `SIGMA_BIAS` | 0.03 | even harmonic share |
| `BITS_MAX` / `MIN` / `CURVE` | 11 / 8 / 2 | floor at short Time / hiss at long Time |
| `kNoiseShaping` | true | delta sigma SNR collapse versus plain PCM |
| `NOISE_DB_SHORT` / `_LONG` | -86 / -48 | analog hiss at each end |
| `HISS_FC` | 6000 Hz | keeps long Time hiss bright, not rumble |
| `IN_MFB_FC` / `_Q` | 8800 / 0.9 | input anti alias voicing |
| `REC_TRACK` / `REC_FC_MAX` / `REC_Q` | 0.45 / 8000 / 0.6 | bandwidth collapse with Time |
| `OUT_MFB_FC` / `_Q` | 4500 / 0.6 | fixed darkness |
| `BLEED_ONSET_HZ` | 20000 | where ticking starts (275 ms) |
| `BLEED_MAX_DB` / `_CURVE` | -55 / 1.5 | tick loudness at 3.6 s / how late it arrives |
| `BLEED_SUB_RATIO` / `_TICK_TAU_SEC` | 0 / 40e-6 | the fs/2 square is off: in the audio band it is a pitch, not a burble |
| `kBleedGateScale` | 4.0 | loop level at which bleed reaches full; an empty loop makes none |
| `SVF_SAT_LIM` | 0.5 | self oscillation level and softness |
| `RES_CURVE` / `RES_OVERDRIVE` / `_START` | 1.5 / 0.03 / 0.92 | resonance feel |
| `ABSORB_SHELF_FC` / `_MAX` | 1200 / 0.7 | tape age darkening per iteration |
| `ABSORB_DRIVE` | 1.0 | extra grit at high Absorb |
| `ABSORB_OUT_MAX_DB` / `_FB_MAX_DB` | 18 / 4 | how much Absorb quiets wet / shortens Decay |
| `kAbsorbFeedbackShare` | 1.0 | scales the feedback half of Absorb |
| `SAT_DRIVE` / `SAT_BIAS` | 1.0 / 0.05 | runaway level / warmth |
| `DC_BLOCK_HZ` | 10 | offset removal |
| `DECAY_MAX` / `DECAY_SMOOTH_SEC` | 1.15 / 0.03 | runaway ceiling / knob smoothing |
| `CLEAR_FADE_SEC` | 0.006 | Clear click suppression |
| `kInputCeiling` | 1e4 | per sample non finite guard threshold |

`TimeFilterLoop::Params` defaults are for the harness only. APVTS defaults
belong to the user (CLAUDE.md).

## 9. Expected measurements (the verification section's source of truth)

- expected: `coredelay` one bare stage, impulse peak at kStageWords / ratio
  plus filter group delay, within 2 host samples, at 44.1 / 48 / 96 / 192 kHz.
- expected: `thd` single stage, 400 Hz at 0.5 peak: THD in [0.06, 0.30] % at
  Time01 0.0244, [0.5, 2.0] % at 0.5151, at least 1.8 % at 0.7344.
- expected: `noise` loop, silent, Decay 0: wet RMS at most -82 dBFS at Time01 0,
  in [-72, -50] dBFS at 0.5151, in [-60, -30] at 1.0, monotonic with at least
  2 dB per step.
- expected: `bandwidth` single stage: |H(1k)| - |H(500)| in [-1.5, +0.5] dB and
  |H(4k)| - |H(1k)| in [-10, -4] dB at Time01 0; |H(2k)| - |H(1k)| at most
  -4 dB at Time01 0.7344.
- expected: `repitch` 450 Hz, Time01 0.2638 stepped to 0.4054: amp(225) /
  amp(450) at least 6 in the window 25 to 65 ms after the step, and the
  reverse ratio at least 6 in 250 to 400 ms. Smear variant: pitch continuous,
  every 20 ms window inside [200, 460] Hz, minimum at most 340 Hz. Envelope
  variant: a 30 ms burst emerges as 62 +- 10 ms at half pitch with no dropout.
- expected: `threestep` impulse energy peaks at D/3, 2D/3, D within 3 ms, tap
  ratios 0.8 +- 0.15, later taps darker; a single peak with kStages 1.
- expected: `runaway` Decay 1.15 for 6 s: peak at most 0.95, RMS in [-16, -3]
  dBFS, second second within 2 dB of the first, crest at most 4, all finite.
  Worst case variant (Time Mod depth 1 at 173 Hz, Res 0.95, Filter 20 Hz) has
  the same bounds. Decay 0.95 decays, Decay 1.0 sustains.
- expected: `bleed` silent loop: Goertzel at 750 Hz in [-62, -34] dBFS at
  Time01 1, at most -90 dBFS at Time01 0.3.
- expected: `fm` 990 Hz, modOct 0.25 at 105 Hz: grid energy at 990 +- k*105
  more than 10x the half grid energy and more than 0.3 of total; at most 0.01
  of carrier with depth 0.
- expected: `clear` largest sample to sample step during the fade at most 1.5x
  the largest step in the preceding 200 ms; RMS 30 ms later at most -78 dBFS.
- expected: `nan` one NaN, one inf and one 1e30 sample injected: output finite
  from the next block onward.
- expected: `nonidentical` two unseeded loops differ (fraction of differing
  samples at least 0.5) while rms(a - b) / rms(a) stays under 0.1; two loops
  with the same seed are bit identical (max difference exactly 0).
- expected: `srmatrix` and `blockmatrix`: Time-0 floor, Time-1 floor, echo
  time, THD, smear ratio and runaway RMS all within tolerance across
  44.1 / 48 / 96 / 192 kHz and blocks 1 / 17 / 128 / 4096.

## 10. Open questions (resolved with the cheapest to change option)

1. **Feedback topology.** Plan says both "sum taps into the filter" and "the
   loop delay is the full chain". Default `kFeedbackFromTapSum = true` (plan
   literal); the flag is the A/B. Tap sum feedback combs with near nulls
   between multiples of 3/D, so off peak material dies fast even at Decay 1.0.
   Listening call at the Phase 1 gate.
2. **FM law.** Exponential by default, `kFmLawLinear` ships the linear
   alternative. The hardware's VCO is current controlled and the Strega's CV
   conditioning is unmeasured.
3. **Bit and noise calibration.** 11 to 8 bits with hiss -86 to -48 dBFS. The
   only anchors are the plan's "-90 dBFS at short times" and "clearly audible
   hiss"; the test brackets are wide on purpose. Tune by ear before narrowing.
4. **Write guard poles.** Default 1. Whether an overclocked chip folds 8 kHz
   down to 200 Hz is exactly the long Time character question; `kGuardPoles`
   0 and 2 are the A/B and `bandwidth` prints the folded alias level.
5. **Ticking model.** Plan literal (pulse train at fs_chip plus fs_chip/2
   square). A memory wrap click is the alternative hypothesis, not shipped.
6. **Per chip detune.** `kStageDetune` would need per stage phase accumulators
   and would break the single shared Frame. Parked in IDEAS.md.
7. **Preset load.** Time snaps on preset load (via `snapTimeOnNextBlock`) and
   smears on knob and Sync division changes.
