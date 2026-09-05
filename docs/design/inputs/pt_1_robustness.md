# Dybbuk PT core and Time/Filter loop: design

Plan sections 2.1 to 2.4. Mono, host-rate `float`; one `double` (tick phase). All constants live in `Source/dsp/LoopTuning.h` (`namespace tuning`, `inline constexpr`), shared with `Tests/EngineTest.cpp` so expectations derive from the same numbers.

## 1. Class layout (`Source/dsp/`, one class per pair, all in `ENGINE_SOURCES`)

| File | Class | Role |
|---|---|---|
| `LoopTuning.h` | `tuning` | every constant of section 5 |
| `PTCore.{h,cpp}` | `PTCore` | one virtual PT2399: fixed memory, tick phase, write/read resampling, quantiser, chip noise, clock bleed, reconstruction LP |
| `MfbLowpass.{h,cpp}` | `MfbLowpass` | fixed 3-pole input LP: TPT SVF 2-pole (8.8 kHz, Q 0.9) + TPT 1-pole (8.8 kHz) |
| `LoopFilter.{h,cpp}` | `LoopFilter` | in-loop TPT SVF LP with nonlinear resonance path (JUCE's SVF cannot host it; same coefficient math) |
| `TimeFilterLoop.{h,cpp}` | `TimeFilterLoop` | clock, `kNumStages` cores in series, tap sum, filter, Absorb, saturator, DC blocker, Decay, flush |
| `DybbukEngine.{h,cpp}` | `DybbukEngine` | replaces `ExampleEngine`: mono sum, Strength, loop, equal-power Blend, Out, UI atomics; template shape (`Params`, `prepare`, `process`) |

```cpp
struct ChipClock { float r, invR; };                       // ticks per host sample and reciprocal
struct ChipCharacter { float step, invStep, noiseAmp, bleedAmp;   // block rate
                       float reconG, reconH; };                   // ramped per sample by the loop
class PTCore {
public:
    static constexpr int memorySize = tuning::kStageMemory;
    void prepare (double sr);                 // host-rate coefficients (bleedDecay), then reset()
    void reset();                             // zero memory + all state, keeps seed
    void seedNoise (juce::uint32 s) { rng = s | 1u; }
    float processSample (float x, const ChipClock&, const ChipCharacter&) noexcept;
private:
    void tick (float xin, const ChipCharacter&) noexcept;
    float rnd() noexcept;                     // xorshift32 -> [-1,1)
    std::array<float, memorySize> mem {};     // 1835 floats = 7.3 KB, compile-time, no heap
    int pos = 0; double phase = 0.0;          // pos in [0,N), phase in [0,1) always
    float xPrev = 0, yPrev = 0, yCur = 0, reconS1 = 0, reconS2 = 0;
    float bleedEnv = 0, bleedSub = 1, bleedDecay = 0;
    juce::uint32 rng = 0x9E3779B9u;
};
class TimeFilterLoop {
public:
    struct Params { float time01 = .5f, syncSeconds = 0 /* >0 overrides */, timeModDepth01 = 0,
                    decay = .5f /* 0..1.15 */, filterHz = 18000, resonance01 = .2f, absorb01 = 0; };
    void prepare (double sr);  void reset();  void seedNoise (juce::uint32);
    void requestClear() { clearCounter.fetch_add (1, std::memory_order_release); }   // UI thread
    void process (float* io, int n, const float* timeMod /* [-1,1] or null */, const Params&) noexcept;
    static float fsChipForTime01 (float t);  static float time01ForFsChip (float fs);
    static float delaySecondsForFsChip (float fs) { return tuning::kTotalMemory / fs; }
    std::atomic<float> uiLoopEnergy { 0 }, uiFsChip { 0 };
private:
    MfbLowpass mfb; std::array<PTCore, kNumStages> stages; LoopFilter svf;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Multiplicative> fsSmooth;   // 20 ms
    juce::SmoothedValue<float> fmDepthS, decayS, absorbGainS, absorbShelfS, absorbDriveS,
                               absorbInvDriveS, reconGS, reconHS, flushGain;
    ChipCharacter character; std::array<ChipClock, kNumStages> clocks;
    float fb = 0, absorbLp = 0, dcState = 0, invSr = 0, dcCoef = 0, absorbShelfCoef = 0, satBiasTanh = 0, satNorm = 1;
    std::atomic<int> clearCounter { 0 }; int lastClearSeen = 0;
    enum class Flush { idle, fadingOut, fadingIn } flushState = Flush::idle;
};
```
Allocation: 3 cores = 22 KB by value, independent of sample rate and block size (memory is in chip samples). Engine: one `AudioBuffer<float> mono (1, maxBlock)`; larger blocks are processed in `maxBlock` chunks. `prepare` is idempotent: recompute coefficients, reset smoothers, `reset()` (chip content would survive a rate change but host-rate filter states would not, so flush).

## 2. PT core algorithm

**Mapping.** `kOctaveSpan = log2(200000/1500) = 7.06`; `fsChipForTime01(t) = 200000 * exp2(-t*7.06)`; inverse `log2(200000/fs)/7.06`. `kStageMemory = (5504+2)/3 = 1835`, `kTotalMemory = 5505` → 27.5 ms .. 3.67 s. Sync: `fsTarget = clamp(5505/syncSeconds, 1500, 200000)` (readout shows the clamp). Extra read tick per stage = 0.05 %, ignored.

**Clock, once per host sample in the loop (shared by all stages):**
```
fsBase = fsSmooth.getNextValue()                 // multiplicative = linear glide in octaves, symmetric, one mul, no exp
mult   = max (kFmMinMult, 1 + fmDepth * (timeMod ? timeMod[i] : 0))   // LINEAR FM: ring-mod sidebands, no exp
fsInst = clamp (fsBase * mult, kFsChipAbsMin, kFsChipAbsMax)            // 1 kHz..220 kHz whatever FM does
r = fsInst * invSr;  invR = 1.0f / r             // the ONE division per host sample, shared by 3 stages
clocks[s] = { r * kStageDetune[s], invR * kStageDetuneInv[s] }         // chip tolerance, constants
```
Bounds: 44.1 kHz → r in [0.023, 4.99] (max 5 ticks per host sample, 6 with a boundary); 192 kHz → [0.005, 1.15]. The per-tick loop has no division, transcendental or modulo.

**Why write and read cannot drift:** the PT2399 is a FIFO with one pointer: each tick reads the oldest cell then overwrites it. Delay is fixed in ticks. One `phase` accumulator decides when ticks happen and is also the read-interpolation fraction, so changing `r` per sample changes traversal speed for write and read together; content written at `fsW` and read at `fsR` plays at `fsR/fsW`, continuously. Tape smear and FM clang are this and nothing else.

```
float PTCore::processSample (float x, const ChipClock& c, const ChipCharacter& k)
{
    x = softCubic (x, kChipClipInvLimit, kChipClipLimit);   // chip input clip, once per host sample
    const double before = phase;  double p = before + c.r;  int ticks = 0;
    if (p >= 1.0)
    {
        ticks = (int) p;                                    // floor, p >= 1
        float u = (float) (1.0 - before) * c.invR;          // host-time fraction of first tick, in (0,1]
        for (int j = 0; j < ticks; ++j) { tick (xPrev + u * (x - xPrev), k); u += c.invR; }
        p -= (double) ticks;
    }
    phase = p;  xPrev = x;                                  // phase in [0,1): time since last tick, in ticks
    float y = yPrev + (float) phase * (yCur - yPrev);       // causal first-order hold, reaches yCur at the next tick
    if (ticks) { bleedEnv = 1.0f; if (ticks & 1) bleedSub = -bleedSub; }
    bleedEnv *= bleedDecay;                                 // RC click per tick + flip-flop at fs_chip/2
    const float bleed = k.bleedAmp * (bleedEnv + kBleedSubMix * bleedSub);
    if (! kBleedPostRecon) y += bleed;
    const float hp = k.reconH * (y - reconS1 * (k.reconG + kReconR2) - reconS2);   // 2-pole TPT LP
    const float bp = k.reconG * hp + reconS1;  reconS1 = k.reconG * hp + bp;
    const float lp = k.reconG * bp + reconS2;  reconS2 = k.reconG * bp + lp;
    return kBleedPostRecon ? lp + bleed : lp;
}
void PTCore::tick (float xin, const ChipCharacter& k)
{
    const float oldest = mem[pos];                          // read BEFORE write: delay = memorySize ticks
    const float noisy  = xin + k.noiseAmp * rnd();          // self-noise is recorded, so it recirculates
    const float tpdf   = 0.5f * (rnd() + rnd());            // triangular +-1 LSB
    mem[pos] = k.step * (float) juce::roundToInt (noisy * k.invStep + tpdf);   // |arg| <= 2^bits + 1
    pos = (pos + 1 == memorySize) ? 0 : pos + 1;            // the only index arithmetic in the class
    yPrev = yCur;  yCur = oldest;
}
```
Regimes: `r = 4.5` → 4 or 5 ticks per host sample, input interpolated at 4-5 instants, output from the two newest chip samples (discarded intermediates carry only chip noise, since the input passed the 8.8 kHz MFB). `r = 1/32` → a tick every ~32 host samples, output ramps `yPrev → yCur`, reconstruction at 675 Hz rounds it; the write samples an 8.8 kHz-wide signal at 1.5 kHz with no further filtering, exactly like the hardware's fixed input filter in front of an overclocked-low chip. Invariants: `u` in (0,1] from `1-before <= r` and `ticks <= before + r`; stored values are multiples of `step >= 2^-10`, never denormal.

`softCubic(x, invLimit, limit)`: `t = jlimit(-1,1, x*invLimit); return limit*(t - t*t*t/3)`, `limit = 1.5*ceiling`, `invLimit = 1/limit`: unity gain at 0, hard ceiling, C1 knee.

**Character (block rate, from `fsBase` at block start, one `log2`):**
```
u = clamp (log2 (kFsChipMax / fsBase) / kOctaveSpan, 0, 1)                    // 0 clean .. 1 destroyed
bits = kBitsAtMaxClock - (kBitsAtMaxClock - kBitsAtMinClock) * u^kBitsCurve   // 10 -> 6
step = kChipClip * exp2 (1 - bits);  invStep = 1 / step                        // exact pair, never ramped
noiseAmp = dbToGain (kNoiseDbAtMaxClock + (kNoiseDbAtMinClock - kNoiseDbAtMaxClock) * u^kNoiseCurve) * sqrt(3)
reconHz = min (kReconTrackRatio * fsBase, kReconMaxHz, 0.45 * sr);  g = tan (pi*reconHz/sr);  h = 1/(1 + kReconR2*g + g*g)  // ramped 5 ms
bleedAmp = fsBase < kBleedStartHz ? dbToGain (kBleedDbAtStart + (kBleedDbAtMinClock - kBleedDbAtStart)
                                              * log2 (kBleedStartHz/fsBase) / log2 (kBleedStartHz/kFsChipMin)) : 0
```
Quantiser is memoryless, so a block-edge `step` change alters the error pattern, not gain (ramping `step`/`invStep` separately gives up to 1 dB gain wobble; never do it). FM moves the clock per sample and the character per block; FM is symmetric about the base so this is inaudible and keeps `exp2/log2` off the per-sample path.

## 3. The loop

```
for i in 0..n-1:
    (clock as above; character.reconG/H = reconGS/HS.getNextValue())
    x = io[i];  if (! (abs (x) < kInputCeiling)) x = 0          // NaN/inf guard: the only way to poison the loop
    g = flushGain.getNextValue()
    sum = x + fb                                                  // loop sum node
    a = mfb.process (sum)                                         // fixed 3-pole 8.8 kHz
    s = a; tapSum = 0
    for k: s = stages[k].processSample (s, clocks[k], character); tapSum += kTapWeights[k] * s
    tapSum *= g
    f = svf.process (tapSum)                                      // LoopFilter
    w = softCubic (f * absorbDrive, kAbsorbInvLimit, kAbsorbLimit) * absorbInvDrive   // Absorb: tape sat
    absorbLp += (w - absorbLp) * absorbShelfCoef;  w -= absorbShelfDepth * (w - absorbLp)  // shelf cut
    w *= absorbGain                                               // Absorb: less survives to blend AND decay
    sat = (tanh (kSatDrive * w + kSatBias) - satBiasTanh) * satNorm   // asymmetric soft clip
    dcState += (sat - dcState) * dcCoef;  wet = sat - dcState     // DC blocker AFTER the asymmetry
    fb = wet * decay                                              // Decay after the saturator
    io[i] = wet;  energy += fb * fb
```
Block rate: `decay`, `fmDepth` smoothed 20 ms; `absorbGain = dbToGain(-kAbsorbMaxCutDb*absorb)`, `absorbShelfDepth = kAbsorbShelfDepth*absorb`, `absorbDrive = kAbsorbSatBase + kAbsorbSatRange*absorb`, `absorbInvDrive = 1/absorbDrive` (all 5 ms ramps). `absorbShelfCoef = 1 - exp(-2pi*kAbsorbShelfHz/sr)`, `dcCoef = 1 - exp(-2pi*kDcBlockHz/sr)`, `satBiasTanh = tanh(kSatBias)`, `satNorm = 1/(kSatDrive*(1 - satBiasTanh^2))` (unity small-signal gain; ceilings +0.894/kSatDrive and -1.135/kSatDrive).

**LoopFilter** (`g`, `R2`, `h` recomputed at the 32-sample control tick, each ramped 5 ms; no per-sample tan/div):
```
R2 = kResR2Max * (1-res)^kResCurve - kResOverdrive * max (0, (res - kResOscStart)/(1 - kResOscStart))  // < 0 past 0.8
g = tan (pi * clamp (fc, 20, min (18000, 0.45 sr)) / sr);  h = 1 / (1 + R2 g + g^2)   // > 0 for R2 >= -0.05, all g
process (x): s1nl = s1 - kResSat * tanh (s1 * kResSatInv)          // ~ s1^3/(3 kResSat^2): zero when quiet
   hp = h * (x - s1*(g + R2) - kResSoft*s1nl - s2)
   bp = g*hp + s1; s1 = g*hp + bp;  lp = g*bp + s2; s2 = g*bp + lp
   s1 = jlimit (-kSvfStateClamp, kSvfStateClamp, s1); s2 likewise (backstop only);  return lp
```
A plain `tanh(s1)` in the damping term does not bound a negative-R2 SVF (it caps the pump rate, adds no loss). The `s1 - tanh` term is loss growing with amplitude², so self-oscillation settles where `kResSoft * s1nl ≈ |R2| * s1` (≈ 0.3 peak at res = 1) and it only ever removes energy, so it cannot destabilise anything.

**Runaway bound.** Loop linear gain `G = decay * H`, `H` = product of MFB, chip, recon, filter, Absorb gains at the oscillating frequency (≈ 0.9 with Filter open). The saturator's describing function gives steady state `A_wet ≈ (2/kSatDrive) * sqrt(1 - 1/(decay*H))`: at Decay 1.15, `H = 0.9` → `A ≈ 0.37` (about -9 dBFS), 3 % THD, never a flat top. Feedback `1.15 * 0.37 = 0.43` into the chip clip = 1.5 % THD. Everything re-entering is bounded by two soft ceilings (saturator, chip clip), so no setting, including Time FM at full depth, can exceed `1.135/kSatDrive` peak; darker Filter lowers `H` and the level, and below `decay*H = 1` the oscillation dies into the noise floor.

## 4. Flush / Clear

UI: `requestClear()` bumps an atomic counter (Infinite Sustainer command-counter idiom). Audio thread, top of `process`: `if (clearCounter.load(acquire) != lastClearSeen) { lastClearSeen = ...; flushState = fadingOut; flushGain.setTargetValue(0); }`. `flushGain` (kFlushFadeMs = 5) multiplies `tapSum`, so wet and feedback fade together. Per sample, when `fadingOut && !flushGain.isSmoothing()`: `reset()` (each core `mem.fill(0)`, `pos = phase = 0`, interpolation, recon and bleed state; `mfb`, `svf` states, `absorbLp`, `dcState`, `fb` = 0; `fsSmooth` and parameter smoothers untouched), then `fadingIn`, target 1. 5.5k float stores, no lock, no allocation, click-free at any loop level. `prepare` and transport reset call `reset()` directly.

## 5. Tunables (`tuning::`)

| Name | Start | Audible effect |
|---|---|---|
| kFsChipMax / kFsChipMin | 200000 / 1500 Hz | Time range: 27.5 ms .. 3.67 s |
| kFsChipAbsMax / kFsChipAbsMin | 220000 / 1000 Hz | how far FM may push the clock past the knob |
| kMemorySize, kNumStages | 5504, 3 (A/B 1) | delay per Hz; 1 = single-tap echo, 3 = triple-step |
| kStageDetune | {1.0, 1.012, 0.991} | chip mismatch: tap chorus, bleed beating; {1,1,1} = identical |
| kTapWeights | {0.40, 0.33, 0.27} (sum 1; 1 stage: {1}) | equal vs decaying triple repeat, sets unity loop gain |
| kTimeSmoothMs | 20 | knob/sync glide length (warble vs zipper) |
| kFmDepthMax / kFmMinMult | 0.9 / 0.05 | clang intensity at full Time Mod; clock-stall floor |
| kBitsAtMaxClock / kBitsAtMinClock / kBitsCurve | 10 / 6 / 1.0 | grit vs Time |
| kNoiseDbAtMaxClock / kNoiseDbAtMinClock / kNoiseCurve | -90 / -42 / 1.5 | hiss vs Time; curve > 1 = cliff at the long end |
| kChipClip | 1.0 peak | chip input saturation point |
| kMfbHz / kMfbQ | 8800 / 0.9 | brightness entering the chip |
| kReconTrackRatio / kReconMaxHz / kReconQ | 0.45 / 8000 / 0.6 | bandwidth collapse; lower ratio = more image burble |
| kBleedStartHz / kBleedDbAtStart / kBleedDbAtMinClock | 20000 / -90 / -36 | onset and level of ticking |
| kBleedTickMs / kBleedSubMix / kBleedPostRecon | 0.25 / 0.5 / false | tick sharpness, sub buzz, thump vs click |
| kFilterMinHz / kFilterMaxHz | 20 / 18000 | Filter range |
| kResR2Max / kResCurve / kResOscStart / kResOverdrive | 2.0 / 2 / 0.8 / 0.05 | resonance taper, where self-osc starts, how hard |
| kResSoft / kResSat / kSvfStateClamp | 0.5 / 0.5 / 8 | self-osc level and softness; backstop |
| kAbsorbMaxCutDb / kAbsorbShelfHz / kAbsorbShelfDepth | 18 / 2000 / 0.8 | how much dies, how dark |
| kAbsorbSatBase / kAbsorbSatRange (cubic ceiling 1) | 0.25 / 0.75 | extra tape crunch at high Absorb |
| kSatDrive / kSatBias | 1.0 / 0.12 | runaway loudness (A ∝ 1/kSatDrive), even harmonics |
| kDcBlockHz / kDecayMax | 10 / 1.15 | DC removal; runaway ceiling of the knob |
| kCoeffRampMs / kGainRampMs / kFlushFadeMs | 5 / 20 / 5 | smoothing; clear speed |
| kInputCeiling | 1e4 | NaN/blow-up guard |

## 6. EngineTest scenarios (48 kHz, block 128, wet only, Decay 0 unless stated; `seedNoise(1234)` except #4)

1. **`smear`** (Time sweep repitches): 30 ms 440 Hz burst at 0.3, Time = `time01ForFsChip(46100)`; at t = 35 ms set Time = `time01ForFsChip(22200)`. Third-tap playback lands in [0.201, 0.263] s. Assert: Goertzel at 212 Hz in [0.205, 0.26] ≥ 20 dB above Goertzel at 440 Hz; zero-crossing frequency 212 Hz ± 4 %; envelope span (10 ms RMS > 0.25 max) in [0.195, 0.27] = 62 ± 10 ms (input was 30 ms: stretched, not crossfaded); min 5 ms RMS inside the span > 0.4 max (no dropout).
2. **`crust`** (long Time dark, hissy, ticking): Time 1.0. (a) no input: RMS in [1, 2] s ≤ -40 dBFS and ≥ -52; same at Time 0 ≤ -80 dBFS; difference ≥ 30 dB. (b) two-tone 300 Hz + 4 kHz at 0.3: Goertzel(4000) ≤ -40 dB re Goertzel(300); alias at 500 Hz (4000 - 3·1500) within [-9, 0] dB re 300; at Time 0 the 4 kHz tone is within 6 dB of 300 Hz. (c) no input: summed Goertzel power at 750·kStageDetune[s] ≥ 10 dB above Goertzel at 640 Hz.
3. **`runaway`**: Decay 1.15, Filter 18 kHz, Res 0.2, Time 0.5; 50 ms noise kick at 0.3; 6 s. Assert last 2 s: RMS ≥ -30 dBFS; peak ≤ 1.2; RMS of s 5-6 within 3 dB of s 4-5; zero runs of ≥ 3 consecutive samples with |x| > 0.9 peak and |Δ| < 1e-4 (no flat tops); all finite. Repeat with timeMod = 1 kHz sine, depth 1.0: same bounds.
4. **`drift`** (never bit-identical): two engines, no test seed (prepare seeds from `juce::Random::getSystemRandom()`), 1 s 220 Hz, Time 0.6, Decay 0.8. Assert RMS(diff) > 1e-6 and RMS(diff)/RMS(out) < 0.05; RMS levels within 0.5 dB.
5. **`fm`** (audio-rate Time Mod sidebands): Time = `time01ForFsChip(28200)`, 500 Hz at 0.3, timeMod 80 Hz sine, depth so `fmDepth = 0.02`. β ≤ 2·0.02·500/80 = 0.25 → J1/J0 = -18 dB. Assert Goertzel(420) and Goertzel(580) each in [-26, -12] dB re 500 Hz and within 3 dB of each other; with depth 0 both ≤ -50 dB.

Robustness extras: **`coredelay`** (single `PTCore`, `ChipClock{4, 0.25}`, 24-bit step, no noise; impulse peaks at (1835+1)/4 + recon delay = 461 ± 2 samples); **`srmatrix`** (#1 and #3 at 44.1/96/192 kHz and blocks 1/17/4096: same numbers within tolerance, no NaN); **`flush`** (loop ringing at Decay 1.1, `requestClear`: max |Δ| during the fade < 0.05, RMS 30 ms later < -80 dBFS); **`nan`** (inject a NaN sample: output finite from the next block on).

## 7. Risks and mitigations

- **Character at block rate vs audio-rate FM**: bits/noise/recon lag the FM. Inaudible by symmetry; if wanted later, ramp `reconG/H` toward the FM-instantaneous clock at control rate.
- **Linear interpolation near r ≈ 1 (192 kHz host, short Time)**: the interpolation fraction sweeps slowly, causing a slow HF flutter at |r-1|·fs. Confined to 192 kHz at the top of Time; fix is a 4-point read if heard. Sound is close but not identical across rates; validate at 44.1 and 96.
- **Ramped `h` is an approximation** while `g`/`R2` move: acts like a slightly wrong damping for 5 ms. The `s1nl` loss and state clamp bound it; verify with a fast Filter sweep at res 1.
- **Tap-sum feedback topology** turns 3 taps into a dense smear at high Decay (Echo-Verb). If the hardware proves single-path, set weights {0,0,1} or `kNumStages = 1`; no code change.
- **Bleed sub-square re-entering at chip Nyquist aliases to DC**: the DC blocker removes it, but a slow pump is possible; `kBleedSubMix` is the lever.
- **Wet peak can reach 1.135/kSatDrive**: Blend/Out stage should carry its own soft ceiling; keep `kSatDrive ≥ 0.7`.
- **Noise floor keeps the loop alive** at Decay ≥ 1/H (random-walk build-up from hiss): intended, matches hardware "infinity", bounded by the saturator; the ember shows it.
- **CPU**: ≈ 300 flops + 2 tanh per host sample, ≈ 15 Mflop/s at 48 kHz; worst case 44.1 kHz with FM at 220 kHz = 6 ticks × 3 stages. One float division per host sample is 0.005 % of a core.
- **Denormals/NaN**: FTZ via `ScopedNoDenormals` (also placed inside `DybbukEngine::process` so the harness gets it); quantised memory can't go denormal; the input guard is the only NaN entry point; `1/r` is safe because `r ≥ kFsChipAbsMin/sr > 0`.