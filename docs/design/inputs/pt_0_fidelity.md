# Dybbuk PT core and Time/Filter loop: design (plan 2.1 to 2.4)

Angle: chip fidelity first. Every formula is pinned to a plan number. "Tune by ear" items are named constants (section 5). Nothing here needs `stampExtraState`: every audible setting is an APVTS parameter or a compile-time constant; Clear is a momentary command.

## 0. Calibration backbone

PT2399: 44 Kbit RAM; 22 MHz = 31 ms, 2 MHz = 342 ms, i.e. a fixed memory read at a variable clock. With N = 5504 words: fs_chip = 5504/0.031 = 177.5 kHz at 31 ms, 16.1 kHz at 342 ms. The plan's 200 kHz..1.5 kHz is the in-spec range plus the overclocked-low tail.

```
fs_chip(t)  = FS_CHIP_MAX * exp(-LOG_RANGE * t)         t = Time 0..1 after smoothing and mod
LOG_RANGE   = ln(200000 / 1500) = 4.8929
u           = clamp((ln FS_CHIP_MAX - ln fs_chip) / LOG_RANGE, 0, 1)   // "crust": 0 in spec, 1 destroyed
delay(t)    = kTotalWords / fs_chip(t)                   // 5502 words: 27.5 ms .. 3.668 s
```

| delay | fs_chip | u | plan fact |
|---|---|---|---|
| 31 ms | 177.5 kHz | 0.0244 | THD 0.13%, noise ~-90 dBFS |
| 114.6 ms | 48 kHz | 0.2917 | chip slower than host from here |
| 275 ms | 20 kHz | 0.4706 | clock bleed onset |
| 342 ms | 16.09 kHz | 0.5151 | THD 1% |
| 1.0 s | 5.50 kHz | 0.7344 | THD 3%+ |
| 3.67 s | 1.5 kHz | 1.0 | destroyed |

Level convention: chip full scale is +-1.0 peak (the "1.3 Vrms" clip). Nominal loop level 0.3..0.7 peak. THD figures are matched at A_REF = 0.5 peak.

## 1. Class layout (Source/dsp/)

| File | Class | Role |
|---|---|---|
| `ChipConstants.h` | `namespace pt` | all constexpr tunables (section 5), `kStages`, `delaySecondsForTime01()`, `time01ForDelaySeconds()` (readouts, Time Sync clamp) |
| `Rng.h` | `struct Rng` | xorshift32, `white()` in [-1,1). Header-only POD |
| `OnePole.h` | `struct OnePole` | `s += a*(x-s)`; lp = s, hp = x - s. Header-only |
| `TptSvf.h/.cpp` | `TptSvf` | hand-rolled Zavalishin TPT SVF (JUCE's hides s1/s2; the loop filter needs tanh on the state). `setG`, `setK`, `stateLimit` (0 = linear), `lowpass(x)`, `reset()` |
| `ChipClock.h/.cpp` | `ChipClock` | log-fs Time smoothing, audio-rate mod input, the ONE phase accumulator shared by all stages, control-rate derived values in a `Frame` |
| `PTCore.h/.cpp` | `PTCore` | the silicon: fixed memory, input clip, delta-sigma THD nonlinearity, noise-shaped TPDF quantizer, DAC pole. `tick()` per chip sample |
| `PTStage.h/.cpp` | `PTStage` | chip on its board: input MFB, write AA pole, PTCore, interpolated read, analog noise, tracking reconstruction SVF, bleed generator, fixed output MFB |
| `LoopSaturator.h/.cpp` | `LoopSaturator` | asymmetric tanh with drive, 10 Hz DC blocker |
| `TimeFilterLoop.h/.cpp` | `TimeFilterLoop` | kStages PTStages in series, tap sum, loop TptSvf, Absorb, saturator, Decay, Clear state machine, loop energy atomic |
| `DybbukEngine.h/.cpp` | `DybbukEngine` | replaces ExampleEngine: mono sum, Strength (Phase 1 hardcoded tanh drive), loop, equal-power Blend, Out; same `Params`/`prepare`/`process` shape as the template |

```cpp
class ChipClock {
public:
  struct Frame {
    int nTicks; float invRatio, tickOffset, readFrac;        // per sample
    float aDac, aAA, recG, d, invD, biasComp, step, invStep, // refreshed every kCtrlInterval samples
          noiseAmp, bleedLvl, u, fsChip;
  };
  void prepare(double sr); void reset();                      // reset keeps Time, zeroes phase
  void setTime01(float t);                                    // once per block (target of the 20 ms smoother)
  const Frame& advance(float modOctaves);                     // once per host sample
  void seedForTests(uint32_t s) {}                            // no rng here; provided on stages
private:
  juce::SmoothedValue<float> logFsSmooth; double phase = 0; float ratio = 1; int ctrlCountdown = 0;
  double sr, invSr; Frame f;
};
class PTCore {
public:
  void reset();                                               // memory.fill(0), states 0
  void tick(float xin, const ChipClock::Frame& f, Rng& rng);  // one chip sample
  float dacPrev = 0, dacCur = 0;                              // read by PTStage
  static constexpr int words = pt::kStageWords;
private:
  std::array<float, words> memory{}; int ptr = 0; float qErr = 0;
};
class PTStage {
public:
  void prepare(double sr); void reset();
  float processSample(float x, const ChipClock::Frame& f);    // host rate
  void seedForTests(uint32_t s) { rng.seed(s); }
private:
  OnePole inPole, aaPole, shelfDummy; TptSvf inSvf, recSvf, outSvf; PTCore core; Rng rng;
  float xPrev = 0, bleedEnv = 0, sub = 1, bleedDecay = 0, noiseSrScale = 1;
};
class LoopSaturator { public: void prepare(double sr); void reset(); void setDrive(float drv);
  float process(float x); private: float R, x1 = 0, y1 = 0, drv = 1, invDrv = 1, tanhBias = 0; };
class TimeFilterLoop {
public:
  struct Params { float time01 = 0.3f, decay = 0.5f, filterHz = 18000.f, resonance01 = 0.2f, absorb01 = 0.f; };
  void prepare(double sr, int maxBlock); void reset();
  void process(const float* in, float* wet, int n, const Params& p, const float* modOct /*nullable*/);
  void requestClear() { clearCounter.fetch_add(1, std::memory_order_release); }
  std::atomic<float> uiLoopEnergy { 0 }; std::atomic<float> uiDelaySeconds { 0 };
  void seedForTests(uint32_t s);
private:
  ChipClock clock; std::array<PTStage, pt::kStages> stages; TptSvf loopFilter; OnePole absorbShelf;
  LoopSaturator sat; juce::SmoothedValue<float> decaySmooth, absorbSmooth, resSmooth;
  juce::SmoothedValue<float, juce::ValueSmoothingTypes::Multiplicative> cutoffSmooth;
  float fb = 0, clearGain = 1; enum class Clear { idle, fadingOut, fadingIn } clearState = Clear::idle;
  std::atomic<int> clearCounter { 0 }; int lastClearSeen = 0; float clearStep = 0;
};
```

Allocation: none at runtime and none in prepare beyond `DybbukEngine`'s mono scratch (`maxBlock` floats; chunk if a host passes more). Chip memory is `std::array<float, kStageWords>`: 1834 x 4 B = 7.3 KB per stage, 22 KB for three, independent of host sample rate (the delay time is N/fs_chip, so 44.1k and 96k give identical delays). `prepare` is repeat-safe: recompute host-rate coefficients, then `reset()`.

## 2. PT core algorithm

### 2.1 Clock, phase accumulator, control-rate derived values

```
setTime01(t):  logFsSmooth.setTargetValue(LOG_FS_MAX - t * LOG_RANGE)      // 20 ms linear ramp IN LOG FS
advance(modOct):                                                             // per host sample
  logFs = logFsSmooth.getNextValue() + modOct * LN2                           // mod bypasses the smoother: audio rate
  fs    = clamp(exp(logFs), FS_CHIP_HARD_MIN, FS_CHIP_HARD_MAX)               // one exp per sample (~0.1% CPU); fast exp2 later if profiled
  ratio = fs * invSr
  phaseBefore = phase; phase += ratio
  f.nTicks    = floor(phase) - floor(phaseBefore)
  f.invRatio  = 1 / ratio                                                     // the only division per sample, shared by all stages
  f.tickOffset= 1 - frac(phaseBefore)                                         // tick j at host fraction (tickOffset + j) * invRatio
  f.readFrac  = frac(phase)
  if (--ctrlCountdown <= 0) { ctrlCountdown = kCtrlInterval; refreshSlow(fs, logFs) }

refreshSlow(fs, logFs):                                                      // every 16 samples
  u        = clamp((LOG_FS_MAX - logFs) * INV_LOG_RANGE, 0, 1)
  fcRec    = min(REC_TRACK * fs, REC_FC_MAX)                                 // min(0.45 fs_chip, 8 kHz)
  aDac     = 1 - exp(-2pi * fcRec / fs)          // one pole AT CHIP RATE: 0.222 at 200 kHz, 0.941 when tracking
  aAA      = 1 - exp(-2pi * fcRec / sr)          // write anti-alias pole at host rate (bites only when fs < sr)
  recG     = tan(pi * fcRec / sr)
  d        = SIGMA_DRIVE_MIN * exp(SIGMA_DRIVE_SLOPE * u); invD = 1/d
  biasComp = fastTanh(d * SIGMA_BIAS) * invD
  bits     = BITS_MAX - (BITS_MAX - BITS_MIN) * u^BITS_CURVE
  step     = exp2(1 - bits); invStep = 1/step                                // LSB with full scale +-1
  noiseAmp = 10^((NOISE_DB_SHORT + (NOISE_DB_LONG - NOISE_DB_SHORT) * u) / 20)
  bleedLvl = fs >= BLEED_ONSET_HZ ? 0 : BLEED_MAX_AMP * pow(ln(BLEED_ONSET_HZ / fs) * INV_LOG_BLEED, BLEED_CURVE)
```

Why log-domain smoothing: a linear ramp in log fs is a constant-rate pitch glide, which is what a tape/clock sweep sounds like; the 20 ms ramp is the plan's "warble instead of zipper". Stepping the slow fields at 3 kHz is inaudible because Time itself is 20 ms smoothed; only the phase increment needs true audio rate (that is where FM character lives).

### 2.2 The tick (PTCore, one chip sample)

```
fastTanh(x): x = clamp(x, -3, 3); return x * (27 + x*x) / (27 + 9*x*x)      // Pade; cubic coeff c = 0.296 (tanh: 0.333)

chipClip(x):                                                                  // the 1.3 Vrms input ceiling
  ax = |x|; if (ax <= CLIP_KNEE) return x
  return sign(x) * (CLIP_KNEE + (1 - CLIP_KNEE) * fastTanh((ax - CLIP_KNEE) / (1 - CLIP_KNEE)))   // C1 at the knee, asymptote 1.0

tick(xin, f, rng):
  v = chipClip(xin)
  v = fastTanh(d * (v + SIGMA_BIAS)) * invD - biasComp        // delta-sigma THD: unity small-signal gain, drive rises with u
  w = v - qErr                                                 // 1st-order error feedback: NTF = 1 - z^-1 (the modulator's noise shaping)
  dith = 0.5 * (rng.white() + rng.white()) * step              // TPDF, +-1 LSB
  q = step * (float) roundToInt((w + dith) * invStep)
  q = clamp(q, -1, 1)                                          // converter full scale
  qErr = q - w
  y = memory[ptr]; memory[ptr] = q; if (++ptr == words) ptr = 0 // read the oldest, overwrite it: fixed-size memory, one pointer
  dacPrev = dacCur; dacCur += aDac * (y - dacCur)              // the DAC's first analog pole, at chip rate
```

Justifications:
- THD. For y = x - c d^2 x^3 (Pade c = 0.296), H3/H1 = c d^2 A^2 / 4. At A_REF 0.5: 0.13% needs d = 0.265; 1% at u = 0.515 needs d = 0.735, hence slope ln(0.735/0.265)/0.515 = 2.0. Result: d = 0.265 e^{2u}: 0.13% at 31 ms, 1.0% at 342 ms, 2.4% at 1 s, ~5% at 2 s, 7% (compressed to less) at 3.6 s. The bias adds H2/H1 = 3 c d^2 SIGMA_BIAS A / 2 = 0.05% at 31 ms rising to 2.5% at u = 1 (even harmonics grow with Time, as the chip's do). Because the nonlinearity is in the WRITE path, distortion is stored and re-pitched with the buffer and accumulates over iterations and stages (3 chips in series really do 3x the THD).
- Bits and the -90 dBFS floor. Plain 10-bit TPDF noise is -66 dBFS total: 25 dB too loud. The real chip is a delta-sigma, whose in-band noise falls as OSR^3 (9 dB per clock doubling) for first order. The error-feedback quantizer gives exactly that: at 11 bits, step^2/4 = -66 dBFS total; in-band factor (pi^2/3)/OSR^3 with OSR = fs_chip/(2 * 8 kHz) = 12.5 at max clock is -27.7 dB, so -94 dBFS in the 8 kHz band. Without the shaper the 200 kHz to 16 kHz trip would raise noise only 11 dB; with it the rise is ~30 dB, which is the "SNR collapses with Time" the plan describes. The bit drop (11 to 8, u^2 so it acts mostly past spec) models the modulator falling apart when overclocked low. Expected floor at the wet output: ~-89 dBFS at 27 ms, ~-61 dBFS at 342 ms, ~-45 dBFS at 3.6 s (before the bleed).
- Why the DAC pole runs at chip rate: the shaped noise sits near fs_chip/2 (100 kHz at max clock); a linear-interpolation read only attenuates it by sinc^2(0.5) = -3.9 dB before it folds into 0..24 kHz. The pole at 8 kHz knocks it down 16..22 dB first. Without it the -90 dBFS floor is impossible.

### 2.3 The stage (host rate)

```
PTStage::processSample(x, f):
  a = inPole.lp(x); a = inSvf.lowpass(a)                       // fixed 3-pole MFB 8.8 kHz: one-pole + SVF Q 0.9 (coeffs from prepare)
  if constexpr (kWriteAntiAlias) a = aaPole.lp(a) with aaPole.a = f.aAA   // tracks fcRec; a no-op above fs_host
  for j in 0 .. f.nTicks-1:                                    // 0..5 ticks (ratio <= 4.54 at 44.1k)
      t   = (f.tickOffset + j) * f.invRatio                    // host fraction of this chip tick, in (0, 1]
      xin = xPrev + t * (a - xPrev)                            // linear write resampling
      core.tick(xin, f, rng)
  xPrev = a
  r = core.dacPrev + f.readFrac * (core.dacCur - core.dacPrev)  // linear read resampling; one chip tick of latency, negligible
  r += f.noiseAmp * noiseSrScale * rng.white()                 // analog output-stage noise, host rate (noiseSrScale = sqrt(sr/48000))
  r = recSvf.lowpass(r) with recSvf.g = f.recG, k = 1/REC_Q     // reconstruction 2-pole tracking min(0.45 fs_chip, 8 kHz)
  bleedEnv *= bleedDecay; if (f.nTicks > 0) { bleedEnv = 1; if (f.nTicks & 1) sub = -sub; }
  r += f.bleedLvl * fastTanh(bleedEnv + BLEED_SUB_RATIO * sub) // clock bleed: tick train at fs_chip + square at fs_chip/2
  return outSvf.lowpass(r)                                     // fixed output MFB 4.5 kHz Q 0.6
```

Bandwidth justification (tap 1, max clock): 8.8k 3-pole + 8k DAC pole + 8k rec 2-pole + 4.5k out MFB = -0.3 dB at 1 kHz, -2.6 dB at 2.8 kHz, -7 dB at 4.5 kHz, -21 dB at 8 kHz: "flat to ~1 kHz then rolls off", and the reconstruction pair collapses it further as the clock falls (at 1 s: -2 dB at 1 kHz, -8 dB at 2 kHz). Each stage carries its own filters as each real chip does, so later taps are progressively darker.

Bleed justification: injected AFTER the reconstruction filter (a 5.5 kHz tick would otherwise be filtered to nothing by a 2.5 kHz cutoff) and BEFORE the output MFB and the loop, so it gets re-delayed and burbles. Level: 0 above 20 kHz, -72 dBFS at 342 ms, -49 dBFS at 1 s, -43 dBFS at 2 s, -40 dBFS at 3.6 s.

When fs_chip < fs_host, `nTicks` is 0 on most samples and `readFrac` ramps, so the output is a linear ramp between chip samples (the DAC's staircase smoothed). Images at fs_chip +- f are attenuated by sinc^2 (-25 dB at fs_chip = 5.5 kHz, f = 1 kHz) then the rec and MFB filters: audible crust, accepted per plan.

## 3. The loop

```
TimeFilterLoop::process(in, wet, n, p, modOct):
  handleClearRequest(); guard: if (!isfinite(fb)) flushAll()          // per block
  clock.setTime01(p.time01); decaySmooth.setTarget(min(p.decay, DECAY_MAX)); cutoffSmooth.setTarget(p.filterHz)
  resSmooth.setTarget(p.resonance01); absorbSmooth.setTarget(p.absorb01)
  for i in 0..n-1:
    f = clock.advance(modOct ? modOct[i] : 0)
    if (i % kCtrlInterval == 0) refreshLoopCoeffs()                    // filter g/k, absorb gains, sat drive
    node = in[i] + fb                                                  // feedback lands here with a one-sample delay
    s = node; tapSum = 0
    for k in 0..kStages-1: s = stages[k].processSample(s, f); tapSum += TAP_W[k] * s
    y = loopFilter.lowpass(tapSum)                                     // TPT SVF, state bounded by tanh (below)
    hp = y - absorbShelf.lp(y); y -= absorbShelfDepth * hp             // Absorb darkening: one-pole high shelf cut at 1.2 kHz
    y = sat.process(y)                                                 // asymmetric tanh (drive incl. Absorb) + DC blocker
    y *= clearGain; advanceClearFade()
    fb     = y * decayGain * absorbFbGain
    wet[i] = y * absorbOutGain
    energy += y * y
  uiLoopEnergy.store(sqrt(energy / n)); uiDelaySeconds.store(kTotalWords / f.fsChip)

refreshLoopCoeffs():
  fc = clamp(cutoffSmooth.skip(16), 20, 0.45 * sr); loopFilter.setG(tan(pi * fc / sr))
  res = resSmooth.skip(16); k = 2 * (1 - res)^RES_CURVE - RES_OVERDRIVE * smoothstep(RES_OVERDRIVE_START, 1, res)
  loopFilter.setK(k)                                                   // k = 2 -> Q 0.5; res 0.8 -> Q 5.6; res 1 -> k = -0.03 (active, tanh-bounded)
  A = absorbSmooth.skip(16)
  absorbShelfDepth = ABSORB_SHELF_MAX * A;  sat.setDrive(SAT_DRIVE * (1 + ABSORB_DRIVE * A))
  absorbOutGain = 10^(-ABSORB_OUT_MAX_DB * A / 20);  absorbFbGain = 10^(-ABSORB_FB_MAX_DB * A / 20)
  decayGain = decaySmooth.skip(16)
```

TptSvf with bounded state (the resonance path nonlinearity):
```
lowpass(x): h = 1 / (1 + g*(g + k))
  hp = h * (x - (g + k) * s1 - s2); bp = g*hp + s1; s1 = g*hp + bp; lp = g*bp + s2; s2 = g*bp + lp
  if (stateLimit > 0) s1 = stateLimit * tanh(s1 / stateLimit)        // bounds the bandpass state = bounds self-oscillation, analog-style
  return lp
```

LoopSaturator (host rate, std::tanh is fine at 48k calls/s):
```
setDrive(d): drv = d; invDrv = 1/d; tanhBias = tanh(SAT_BIAS)
process(x): y = (tanh(drv * x + SAT_BIAS) - tanhBias) * invDrv        // slight asymmetry: H2/H1 = 0.025 A (0.75% at A 0.3, 1.75% at 0.7)
            out = y - x1 + R * y1; x1 = y; y1 = out; return out        // DC blocker, R = 1 - 2pi*10/sr = 0.99869 at 48k
```

Tap weights: `TAP_W_RAW = {1.0, 0.85, 0.7}` normalised to sum 1 ({0.392, 0.333, 0.275}); `{1}` when kStages = 1 (`constexpr` array built with `if constexpr`). Feedback is taken from the loop output, which contains the summed taps, as the plan specifies; `kFeedbackFromTapSum` exists for A/B (false = pure series loop, taps to the output only; see risk 4).

Self-oscillation bound and steady-state level. Small-signal loop gain is decay x (stage passband loss) x (tap comb, max 1) x (filter gain) x absorbFbGain. Two bounds cap it: the saturator (tanh asymptote 1/drv <= 1) and the SVF state limit (0.5). With decay 1.15, Absorb 0, short Time, the describing function 1 - A^2/4 = 1/1.15 gives A = 0.72 peak at the saturator input, output peak tanh(0.72) = 0.62: about -4 dBFS peak, -7.5 dBFS RMS at the wet tap. At long Time the chip's own d compresses first (three stages of (1 - 0.222 d^2 A^2)), pulling the runaway down to roughly -8..-10 dBFS peak. The wet tap can never exceed 1.0 (tanh asymptote); no hard clip anywhere. Note the stage filters lose ~0.5 dB per full iteration at 1 kHz, so runaway at 1.15 builds from the darkest frequencies (as a real PT2399 loop does); Filter shapes it. High Resonance alone can make the loop ring at the filter frequency regardless of Decay, bounded at the -6 dBFS state limit: intended.

DybbukEngine placement (context only): mono sum -> Strength -> `loop.process` -> equal-power Blend -> Out. Bypass: the processor crossfades; the engine keeps running and feeds the loop silence while bypassed (a delay should not collect what you play while out of circuit).

## 4. Flush / Clear

- UI/processor: `loop.requestClear()` = `clearCounter.fetch_add(1, release)`. Momentary, not a parameter, not automated, not in state.
- Audio thread, start of block: `c = clearCounter.load(acquire); if (c != lastClearSeen) { lastClearSeen = c; clearState = fadingOut; clearStep = 1 / (CLEAR_FADE_SEC * sr); }`
- Per sample: `fadingOut`: `clearGain -= clearStep`; when it reaches 0: `flushAll(); clearState = fadingIn`. `fadingIn`: `clearGain += clearStep` to 1, then idle. A hard cut of a -4 dBFS runaway would click; 3 ms each way is inaudible and needs no lookahead.
- `flushAll()`: for each stage `reset()` (`memory.fill(0)` = 7.3 KB memset, ~1 us; ptr, qErr, dacPrev/Cur, xPrev, bleedEnv, all filter states = 0), `loopFilter.reset()`, `absorbShelf`, `sat.reset()` (x1, y1), `fb = 0`, `clock.reset()` (phase 0, Time smoother untouched). No allocation, no locks. Also called from `prepare` and from the non-finite guard.

## 5. Tunable constants (`ChipConstants.h`, namespace `pt`)

| Name | Start | Audible effect |
|---|---|---|
| `kStages` | 3 | 1 vs 3 stage A/B; three-step repeat vs single echo |
| `kMemoryWords` / `kStageWords` | 5504 / 5504÷kStages (1834) | delay range; readout uses kTotalWords = 5502 |
| `FS_CHIP_MAX` / `FS_CHIP_MIN` | 200 kHz / 1.5 kHz | 27.5 ms .. 3.67 s range; where "crust" starts |
| `FS_CHIP_HARD_MAX/MIN` | 250 kHz / 750 Hz | FM excursion headroom before clamping |
| `TIME_SMOOTH_SEC` | 0.020 | knob sweep warble length |
| `kCtrlInterval` | 16 | rate of slow coefficient updates (lower if FM sounds stepped) |
| `TAP_W_RAW` | {1, 0.85, 0.7} | brightness/decay of the three-step repeat; comb depth |
| `kFeedbackFromTapSum` | true | plan topology vs pure series loop (risk 4) |
| `kWriteAntiAlias` | true | long-Time character: dark and noisy (chip) vs aliased sparkle |
| `CLIP_KNEE` | 0.8 | where the chip's own input clip starts biting (full scale 1.0) |
| `SIGMA_DRIVE_MIN` / `SIGMA_DRIVE_SLOPE` | 0.265 / 2.0 | THD at short Time / how fast THD grows with Time |
| `SIGMA_BIAS` | 0.03 | even-harmonic share of chip distortion |
| `BITS_MAX` / `BITS_MIN` / `BITS_CURVE` | 11 / 8 / 2 | floor at short Time (-90 dBFS) / hiss at long Time / where the drop starts. Plan suggested 10/6: crustier, try after listening |
| `kNoiseShaping` | true | delta-sigma SNR collapse vs plain PCM (+11 dB only) |
| `NOISE_DB_SHORT` / `NOISE_DB_LONG` | -86 / -48 | analog hiss at Time 0 / Time 1 (log-linear in u) |
| `IN_MFB_FC` / `IN_MFB_Q` | 8800 Hz / 0.9 | input anti-alias voicing |
| `REC_TRACK` / `REC_FC_MAX` / `REC_Q` | 0.45 / 8000 Hz / 0.6 | bandwidth collapse with Time |
| `OUT_MFB_FC` / `OUT_MFB_Q` | 4500 Hz / 0.6 | fixed darkness ("flat to 1 kHz") |
| `BLEED_ONSET_HZ` | 20000 | Time where ticking starts (275 ms) |
| `BLEED_MAX_DB` / `BLEED_CURVE` | -40 / 1.5 | tick loudness at 3.6 s / how late it arrives |
| `BLEED_SUB_RATIO` / `BLEED_TICK_TAU_SEC` | 0.5 / 40e-6 | fs/2 burble vs tick; tick sharpness |
| `SVF_SAT_LIM` | 0.5 | self-oscillation level and softness |
| `RES_CURVE` / `RES_OVERDRIVE` / `RES_OVERDRIVE_START` | 1.5 / 0.03 / 0.92 | resonance knob feel; how hard the top self-oscillates |
| `ABSORB_SHELF_FC` / `ABSORB_SHELF_MAX` | 1200 Hz / 0.7 | tape-age darkening per iteration (-10.5 dB HF at full) |
| `ABSORB_DRIVE` | 1.0 | extra grit at high Absorb (+6 dB into the saturator) |
| `ABSORB_OUT_MAX_DB` / `ABSORB_FB_MAX_DB` | 18 / 4 | how much Absorb quiets the wet / how much it shortens Decay |
| `SAT_DRIVE` / `SAT_BIAS` | 1.0 / 0.05 | runaway level (-4 dBFS peak) / warmth (even harmonics) |
| `DC_BLOCK_HZ` | 10 | offset removal; raise if runaway pumps |
| `DECAY_MAX` / `DECAY_SMOOTH_SEC` | 1.15 / 0.03 | runaway ceiling / knob smoothing |
| `CLEAR_FADE_SEC` | 0.003 | Clear click suppression |

## 6. EngineTest scenarios

House style: `EngineTest <scenario>`, a `check(what, ok, detail)` lambda printing PASS/FAIL plus the numbers, Goertzel `partialAmp()` as in Teder, 48 kHz, block 128. Chip-level tests drive `ChipClock` + one `PTStage` directly (`clock.setTime01(t)`, per sample `stage.processSample(x, clock.advance(0))`); loop tests drive `TimeFilterLoop` with Filter 18 kHz, Res 0, Absorb 0 unless stated. `seedForTests(1)` everywhere except `drift`. Test tones sit on tap-comb peaks (multiples of 3/D) so the comb never confounds levels.

1. `thd` (chip calibration, backs tell 2's "fidelity collapses"): single stage, 400 Hz at 0.5 peak (harmonics up to 2 kHz stay under fs_chip/2 = 2.75 kHz at the 1 s point), 0.5 s settle, Goertzel H1..H5 over 0.5 s, THD = sqrt(sum H2..H5^2)/H1. Time01 = 0.0244: THD in [0.06%, 0.30%]. Time01 = 0.5151: [0.5%, 2.0%]. Time01 = 0.7344: >= 1.8%. Fails if the drive curve, bias, or dither (harmonics from an undithered quantizer) breaks.
2. `noise` (tell 2, hiss): loop, silent input, Decay 0. RMS dBFS of wet over 1 s after 0.5 s settle at Time01 = {0, 0.25, 0.5151, 0.75, 1}: L(0) <= -82 dBFS; L(0.5151) in [-72, -50]; L(1) in [-60, -30]; each step >= 2 dB above the previous (monotonic). Fails if the shaper, DAC pole, or noise curve is wrong (e.g. shaped noise aliasing lifts L(0) to -70).
3. `bandwidth` (tell 2, dark): single stage, sequential 0.3 peak sines at 500, 1000, 2000, 4000, 8000 Hz, Goertzel at the tone. Time01 = 0: |H(1k)| - |H(500)| in [-1.5, +0.5] dB; |H(4k)| - |H(1k)| in [-10, -4] dB; |H(8k)| - |H(1k)| <= -15 dB. Time01 = 0.7344: |H(2k)| - |H(1k)| <= -4 dB (at Time01 = 0 that difference is >= -2.5 dB). Fails if reconstruction stops tracking the clock or the fixed MFBs move.
4. `repitch` (tell 1, smear not crossfade): loop, Decay 0, continuous 450 Hz at 0.5 (450 = 15 x 3/D at D = 100 ms, in phase on all taps; 225 = 15 x 3/D at 200 ms). Time01 0.2638 (100 ms) for 1 s, then step to 0.4054 (200 ms: fs halves). Window [step+25 ms, step+65 ms] (all three taps still emit pre-step content): amp(225)/amp(450) >= 6. Window [step+250, step+400 ms]: amp(450)/amp(225) >= 6. A crossfading delay shows no 225 at all. Smear variant: ramp Time01 linearly 0.2638 to 0.4054 over 300 ms; zero-crossing frequency in every 20 ms window during the ramp within [200, 460] Hz and the minimum <= 340 Hz (pitch bent by >= 5 semitones at some point, continuously).
5. `runaway` (tell 3): loop, 100 ms burst of 450 Hz at 0.3, then silence; Decay 1.15, Filter 2 kHz, Res 0.3, Time01 0.3; 6 s. Over [4, 6] s: peak |wet| <= 0.95; RMS in [-16, -3] dBFS; RMS(5..6 s) within 2 dB of RMS(4..5 s) (steady, not growing); crest factor <= 4; all samples finite. Fails if the saturator/DC blocker/state bound is missing (grows to 1.0 and hard-clips, or pumps).
6. `drift` (tell 4): two fresh `TimeFilterLoop`s WITHOUT `seedForTests` (prepare seeds from `Time::getHighResolutionTicks() ^ (uintptr_t) this`), identical settings (Decay 0.9, Time01 0.5), 1 s of 450 Hz then 2 s silence. max|a - b| > 1e-7 (not bit-identical) AND rms(a - b)/rms(a) < 0.1 (still the same sound).
7. `bleed` (tell 2, ticking): loop, silent input, Decay 0. Time01 = 1 (fs_chip 1500 Hz): Goertzel at 750 Hz in [-62, -34] dBFS and at 1500 Hz in [-70, -34]. Time01 = 0.3 (fs_chip 46 kHz, above onset): Goertzel at 750 Hz <= -90 dBFS. Fails if the level curve or injection point (before the rec filter would kill it) is wrong.
8. `fm` (tell 5, Phase 3 hook exercised now): loop, Decay 0, 990 Hz at 0.3, Time01 0.2638, `modOct[i] = 0.25 * sin(2pi * 105 * t)` (105 Hz so the write/read mod phases are anti-phase across the 100 ms loop; 100 Hz would cancel exactly). A variable clock shifts pitch by the clock RATIO, not by dD/dt: deviation ~410 Hz, index ~4. Sum of Goertzel energies at 990 +- 105, 210, 315 >= energy at 990. With mod 0: sideband sum <= 0.01 x carrier.
9. `clear`: runaway as in 5, `requestClear()` at 3.0 s. RMS over [3.02, 3.5] s <= -78 dBFS; max sample-to-sample |difference| during [2.995, 3.01] s <= 0.05 (a hard cut from -4 dBFS gives >= 0.3).

Also `render <time01> <decay> <in.wav> <out.wav>` (juce_audio_formats comes with juce_dsp) for the by-ear milestones; not a pass/fail.

## 7. Risks and mitigations

1. Noise/bit curve louder than the real chip at long Time (my estimate: -61 dBFS at 342 ms; a real unit is probably nearer -70). Everything is in `BITS_*` and `NOISE_DB_*`; the `noise` brackets are wide on purpose. Tune by ear before narrowing them.
2. THD calibrated at A_REF = 0.5; if the Strength stage lands the loop at a different nominal level, audible THD shifts by (A/0.5)^2. Retune `SIGMA_DRIVE_MIN` only, slope stays.
3. CPU: up to 4.5 ticks x 3 stages per sample at 44.1k (~600k ticks/s). Each tick is ~25 flops with no divisions or transcendentals (Pade tanh, control-rate coefficients). Expected < 1% of a core; measure in Phase 1 and fall back to a fast exp2 for the per-sample `exp` if profiling says so.
4. Tap-sum feedback makes the loop a comb with peaks every 3/D and near-nulls (|sum| dips to 0.10) between them, so off-peak frequencies die in two iterations even at Decay 1.0 and repeats ring at 3/D harmonics. Every feedback delay is a comb, but this one is deeper than a series loop. `kFeedbackFromTapSum = false` is the A/B; if pure series wins, the taps still go to the output but the loop delay becomes the full chain (which the plan's "loop delay is the full chain" sentence also implies).
5. Aliasing on write when fs_chip < fs_host is only a one-pole AA. If long Time sounds "digital" rather than dark, promote the AA to a 2-pole tracking the reconstruction cutoff (same TptSvf, coefficient already computed).
6. Linear read interpolation leaves -25 dB images at fs_chip +- f at long Time. Reconstruction and MFB sit after the read to trim them; if still harsh, a 4-point read on the DAC-pole history (keep 4 values instead of 2) is a local change.
7. Error-feedback quantizer with a clamp: bounded because |v| <= 1 (chipClip) and |qErr| <= 1.5 LSB; dither prevents idle tones at small signals (idle tones would actually be authentic; `kNoiseShaping` can be flipped for comparison).
8. Negative k at Resonance 1 relies on the state bound; the TPT solve stays valid since 1 + g(g + k) > 0 for k > -g - 1/g. Verified by `runaway` with Res 1 as an extra case.
9. Control-rate stepping (16 samples) of step/d/aDac/recG on very fast Time sweeps: masked by the 20 ms Time smoother; if zipper is heard under audio-rate FM in Phase 3, lower `kCtrlInterval` to 4 or interpolate `recG`.
10. Runaway loudness downstream: -4 dBFS peak wet plus dry at Blend 50% can exceed 0 dBFS; that is the Out slider's job, but Blend should be true equal-power (0.707 each at centre), not linear.
11. Sample-rate dependence: in-band noise density is normalised by sqrt(sr/48000); filters are designed per sr; the chip itself is sr-independent. Add 44.1/96/192k runs of `noise` and `bandwidth` to the Phase 6 matrix rather than assuming.
12. A host NaN would poison the loop forever through tanh; the per-block `isfinite(fb)` guard flushes instead (the same bug class the sibling's duck follower had).