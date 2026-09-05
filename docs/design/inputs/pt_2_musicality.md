# Dybbuk PT core + Time/Filter loop: design

Sources read: `dybbuk-plan.md`, `CLAUDE.md`, all of `Source/`, both harnesses, `CMakeLists.txt`, the sibling plugins' `EngineTest.cpp` / `Parameters.cpp` / dsp headers, and JUCE 8.0.12 at `build/_deps/juce-src/` (present). One consequence of reading `juce_StateVariableTPTFilter.cpp`: its state and `update()` are private and `setCutoffFrequency` recomputes `tan()` on every call, so it cannot host a tanh in the resonance path or per-block coefficient reuse. The in-loop filter is therefore our own TPT SVF; JUCE's is used only for the fixed 8.8 kHz MFB stage.

Design stance, in one sentence: everything that makes the five tells happen falls out of *one* structural decision, a fixed-length ring of chip samples driven by a per-sample clock ratio, and the rest of this document is about keeping that structure honest and bounded.

---

## 1. Class layout (`Source/dsp/`, one class per `.h/.cpp` pair)

```
dsp/NoiseSource.h          header-only xorshift32 (same idiom as teder), seedable
dsp/ChipClock.{h,cpp}      Time -> fs_chip, 20 ms log-domain smoothing, per-sample audio-rate mod,
                           control-rate curve evaluation (bits, noise, bleed, filter coeffs)
dsp/PTStage.{h,cpp}        one virtual PT2399: MFB in, clip, write guard, ring, resampler,
                           quantizer, reconstruction, hiss, clock bleed, wrap tick
dsp/LoopFilter.{h,cpp}     TPT SVF lowpass, tanh on the bandpass state (resonance path)
dsp/Absorb.{h,cpp}         high-shelf darkening + drive coupling + wet/feedback attenuation
dsp/LoopSaturator.{h,cpp}  asymmetric tanh + 10 Hz DC blocker
dsp/TimeFilterLoop.{h,cpp} the loop: kStageCount PTStages in series, summed taps, filter,
                           absorb, saturator, decay feedback, clear mailbox, ember atomic
dsp/DybbukEngine.{h,cpp}   (replaces ExampleEngine) stereo->mono, Strength, loop, Blend, Out
```

Compile-time stage count, in `PTStage.h`:

```cpp
#ifndef DYBBUK_PT_STAGES
 #define DYBBUK_PT_STAGES 3          // cmake -DDYBBUK_PT_STAGES=1 for the A/B build
#endif
namespace pt
{
    inline constexpr int   kStageCount   = DYBBUK_PT_STAGES;
    inline constexpr int   kMemoryTotal  = 5504;                       // "44 Kbit at 8 bits"
    inline constexpr int   kReadGuard    = 2;                          // slots between write and read
    inline constexpr int   kStageMemory  = kMemoryTotal / kStageCount; // 1834 at 3 stages
    inline constexpr int   kDelayChipSamples = kStageCount * (kStageMemory - kReadGuard); // 5496
}
```

### Public APIs

```cpp
// NoiseSource.h
struct NoiseSource
{
    juce::uint32 s = 0x9e3779b9u;
    void  seed (juce::uint32 v)   { s = v != 0 ? v : 0x9e3779b9u; }
    float nextUnit()              { s ^= s << 13; s ^= s >> 17; s ^= s << 5;
                                    return (float) (juce::int32) s * (1.0f / 2147483648.0f); } // [-1,1)
    float nextTpdf()              { return 0.5f * (nextUnit() + nextUnit()); }                  // triangular, peak +-1
};

// ChipClock.h
struct ChipFrame                    // one per host sample, shared by every stage
{
    float ratio;                    // fs_chip / fs_host       (chip samples per host sample)
    float chipPeriod;               // fs_host / fs_chip       (host samples per chip sample)
    // refreshed every kControlInterval host samples:
    float quantStep, invQuantStep;  // 2^(1-bits), 1/that
    float ditherGain;               // kDitherLsb * quantStep
    float hissGain;                 // post-reconstruction white noise RMS
    float bleedToneGain, bleedTickGain;
    float reconCoeff, guardCoeff;   // one-pole a = 1 - exp(-2pi fc / fs_host)
    bool  bleedOn;
};
class ChipClock
{
public:
    void  prepare (double sampleRate);
    void  reset (float time01);                 // snap the smoother (first block, preset load)
    void  setTime (float time01);               // once per block
    ChipFrame next (float modOctaves);          // once per sample; modOctaves is audio-rate
    float getFsChip() const;
    static double delaySecondsFor (float time01);       // readouts
    static float  time01ForDelaySeconds (double secs);  // Time Sync inverse map (clamped)
private:
    double sr = 48000.0; float invSr = 0;
    float timeOctTarget = 0, timeOct = 0, timeSmoothCoeff = 0;
    float fsChip = 200000.0f;
    int   controlCountdown = 0;
    ChipFrame frame {};
    void refreshControlRate();                  // the curves in section 2.4
};

// PTStage.h
class PTStage
{
public:
    void  prepare (double sampleRate, int stageIndex);
    void  reset();                              // zero ring + every filter state, keep writeIdx stagger
    float processSample (float x, const ChipFrame& f, NoiseSource& rng);
private:
    std::array<float, pt::kStageMemory> ring {};   // fixed size: no heap, flush is a std::fill
    int   writeIdx = 0;                            // next slot to write
    float nextWriteTime = 0.0f;                    // host samples until the next chip write
    float xPrev = 0.0f;                            // previous guard-filtered host input
    // fixed 8.8 kHz 3-pole MFB: one-pole + biquad (Q 0.9), coefficients computed in prepare
    float mfb1 = 0; float b0, b1, b2, a1, a2; float z1 = 0, z2 = 0;
    float guard1 = 0, guard2 = 0;                  // tracking anti-alias pair (write side)
    float recon1 = 0, recon2 = 0;                  // tracking reconstruction pair (read side)
    float hissLp = 0, hissCoeff = 0;               // 6 kHz colour on the hiss
    float toneSign = 1.0f, toneLp = 0, toneCoeff = 0; // clock bleed square at fs_chip/2
    float tickEnv = 0, tickDecay = 0, tickSign = 1.0f; // memory-wrap tick
    float hissRateComp = 1.0f;                     // sqrt(48000 / sr): constant spectral density
};

// LoopFilter.h  (TPT SVF, Zavalishin form, lowpass out, tanh on the BP state)
class LoopFilter
{
public:
    void  prepare (double sr); void reset();
    void  setCutoffAndResonance (float hz, float res01);   // control rate only
    float processLowpass (float x);
private:
    float g = 0, k = 2, a1 = 1, a2 = 0, a3 = 0, ic1 = 0, ic2 = 0; double sr = 48000.0;
};

// Absorb.h
class Absorb
{
public:
    void  prepare (double sr); void reset();
    void  set (float absorb01);                 // control rate: shelf gain, drive, attenuations
    float shelf (float x);                      // darkening, pre-saturator
    float drive() const, driveComp() const, wetGain() const, feedbackGain() const;
private:
    float lp = 0, lpCoeff = 0, shelfGain = 1, drv = 1, comp = 1, wet = 1, fb = 1;
};

// LoopSaturator.h
class LoopSaturator
{
public:
    void  prepare (double sr); void reset();
    float process (float x, float drive, float driveComp);   // asym tanh, then DC block
private:
    float dcX1 = 0, dcY1 = 0, dcR = 0.9987f;
};

// TimeFilterLoop.h
class TimeFilterLoop
{
public:
    struct Params                       // struct defaults are for the harness; APVTS defaults are the user's
    {
        float time01 = 0.35f;           // Time (Sync already resolved to 0..1 by the processor)
        float timeModDepth = 0.0f;      // 0..1, scales the per-sample mod buffer
        float decay = 0.5f;             // 0..1.15
        float filterHz = 8000.0f;       // 20..18000
        float resonance01 = 0.2f;
        float absorb01 = 0.0f;
    };
    void prepare (double sampleRate, int maxBlockSize);
    void reset();                                       // full audio-state flush (audio thread)
    // in/out may alias. timeModOctaves is per-sample, nullable (0 = no mod). Mono.
    void process (const float* in, float* out, int numSamples, const Params& p,
                  const float* timeModOctaves);
    void requestClear()                { clearCounter.fetch_add (1, std::memory_order_release); }
    void snapTimeOnNextBlock()         { snapTime = true; }        // preset load: no 7-octave swoop
    void setNoiseSeedForTests (juce::uint32 seed);
    double getDelaySeconds() const;                     // for the readout / synced clamp display
    std::atomic<float> uiLoopEnergy { 0.0f };           // the ember, peak of |y| with 50 ms release
private:
    ChipClock clock;
    PTStage stages[pt::kStageCount];
    NoiseSource rng[pt::kStageCount];
    LoopFilter filter; Absorb absorb; LoopSaturator sat;
    juce::SmoothedValue<float> decaySmooth, absorbSmooth, resSmooth;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Multiplicative> cutoffSmooth;
    float feedback = 0.0f, tapWeight[pt::kStageCount];
    std::atomic<int> clearCounter { 0 }; int lastClearSeen = 0;
    float clearFade = 1.0f, clearFadeStep = 0; bool clearing = false, snapTime = true;
    float ember = 0, emberRelease = 0; double sr = 48000.0; int controlCountdown = 0;
};
```

### Allocation

Nothing on the heap in the loop: the rings are `std::array` (3 × 1834 × 4 B = 22 KB), all filters are scalars. `DybbukEngine::prepare` allocates one mono scratch buffer of `maxBlockSize` floats and one for the per-sample Time-mod signal; both sized for the worst case and re-sized only in `prepare`. Repeat `prepare` calls recompute coefficients and call `reset()`; sample-rate changes are fully covered because every constant is expressed in seconds or in chip samples.

---

## 2. The PT core, sample by sample

### 2.1 Time to fs_chip

```
kFsChipMax  = 200000 Hz   (Time 0)         kFsChipMin = 1500 Hz   (Time 1)
kOctaveSpan = log2(kFsChipMax / kFsChipMin) = 7.06 octaves
kFsChipFloor = 600 Hz, kFsChipCeil = 400000 Hz     (mod excursion clamps; ratio <= 9.1 at 44.1 kHz)

fsChip(time01, modOct) = clamp( kFsChipMax * 2^-(timeOctSmoothed * kOctaveSpan + modOct), floor, ceil )
delaySeconds           = kDelayChipSamples / fsChip        -> 27.5 ms at Time 0, 3.66 s at Time 1
perStageDelay          = (kStageMemory - kReadGuard) / fsChip  -> 9.2 ms .. 1.22 s
```

Smoothing is a one-pole on `timeOct` (the knob, normalised 0..1 in log-clock space) with `kTimeSmoothMs = 20`. Only the knob is smoothed; `modOct` is added raw, per sample, so audio-rate FM keeps its edges. `reset(time01)` snaps the smoother; the loop calls it on the first block and after a preset load, otherwise every preset change is a seven-octave tape swoop.

`ChipClock::next` per sample: one `exp2f`, one division (`chipPeriod = sr / fsChip`), one multiply. Well under 20 ns; the plan's worry about divisions is not worth a fast-path until a profile says so.

Time Sync (processor side): `time01ForDelaySeconds(secs)` inverts the map (`log(kDelayChipSamples / secs / kFsChipMax) / log(kFsChipMin / kFsChipMax)`) clamped to 0..1, and the result goes through the same smoother, so switching divisions smears.

### 2.2 The write/read resampler (the heart)

State per stage: `ring[N]`, `writeIdx` (next slot), `nextWriteTime` (host samples from the previous host sample to the next chip write), `xPrev`.

```
processSample(x_host, f, rng):
    // --- analog front end of the chip -------------------------------------------
    x  = mfbOnePole(x_host); x = mfbBiquad(x)               // fixed 8.8 kHz, 3-pole, Q 0.9
    x  = chipClip(x)                                        // section 2.6
    x += guardCoeff * (x - guard1) ... (two cascaded one-poles at min(0.45 fs_chip, 8 kHz))
    // --- write side: host -> chip, linear interpolation between xPrev and x ---------
    while (nextWriteTime < 1.0f)
        v = xPrev + (x - xPrev) * nextWriteTime            // chip sample at its exact host time
        v += f.ditherGain * rng.nextTpdf()                  // TPDF dither, +-kDitherLsb LSB
        v  = f.quantStep * roundToNearest(v * f.invQuantStep)   // the stored word
        ring[writeIdx] = v
        toneSign = -toneSign                                // clock bleed: square at fs_chip/2
        if (++writeIdx == N) { writeIdx = 0; tickEnv = 1; tickSign = -tickSign; }   // memory wrap
        nextWriteTime += f.chipPeriod
    nextWriteTime -= 1.0f                                   // now in [0, chipPeriod)
    xPrev = x
    // --- read side: the oldest data, resampled back to host rate ---------------------
    frac = 1.0f - nextWriteTime * f.ratio                   // in (0, 1]: where "now" sits between chip samples
    a = ring[(writeIdx + 1) % N]; b = ring[(writeIdx + 2) % N]
    y = a + (b - a) * frac                                  // delay = N - kReadGuard + frac chip samples
    y = reconPair(y, f.reconCoeff)                          // two one-poles at min(0.45 fs_chip, 8 kHz)
    // --- analog back end: hiss, clock bleed, wrap tick (all bypass the recon filter) ----
    hissLp += hissCoeff * (rng.nextUnit() - hissLp);  y += f.hissGain * hissRateComp * hissLp
    if (f.bleedOn)
        toneLp += toneCoeff * (toneSign - toneLp);    y += f.bleedToneGain * toneLp
        y += f.bleedTickGain * tickSign * tickEnv
    tickEnv *= tickDecay
    return y
```

Why this is correct: after the write loop, the next chip write lies `nextWriteTime` host samples ahead, so "now" in chip time is `writeIdx - nextWriteTime * ratio`, which is strictly inside `(writeIdx - 1, writeIdx]`. Reading `N - kReadGuard` chip samples behind that lands between slots `writeIdx + 1` and `writeIdx + 2`, both written at least one chip sample ago and never the slot being written this host sample, even when the loop wrote nine of them. Delay in seconds is `(N - 2)/fs_chip` regardless of host rate.

Why the tells fall out of it:
- **Smear, not crossfade.** The ring holds N chip samples no matter what; changing `ratio` changes how fast the read pointer walks through content written at the old rate. Content written at rate A and read at rate B plays at pitch ratio B/A, for one delay time, then the new material arrives at pitch. There is no second read head to crossfade to.
- **Audio-rate FM sidebands.** Both write density and read speed follow `ratio` per sample. Output pitch ratio is `ratio(t) / ratio(t - D)`; for mod periods much shorter than D the two are uncorrelated and the FM depth is up to double the modulation, which is the clang.
- **Dark at long Time.** `reconCoeff` tracks 0.45 fs_chip, so at Time 1 the wet path is a 675 Hz two-pole per stage, cumulatively 6 poles on tap 3.

When `ratio > 1` (short Time, up to ~4.5 at 44.1 kHz) the while loop runs several times and the read skips chip samples; content is already band-limited to 8.8 kHz by the MFB so nothing aliases. When `ratio < 1` (long Time) the read interpolates between the same two chip samples for several host samples (a first-order hold whose images the reconstruction pair mostly removes) and the write decimates the host signal, which is where the guard pair matters.

### 2.3 The write guard (an addition to the plan, and why)

A real PT2399 is a delta-sigma modulator: at low clock its bandwidth and SNR collapse, but it does not fold 8 kHz down to 200 Hz the way a naive decimator does. Without a tracking pre-filter, at Time 1 every guitar harmonic between 750 Hz and 8.8 kHz aliases into the 0..750 Hz band, and the "dark, hissy" tell turns into "harsh and digital", which is the one thing the emulation must not be. So each stage runs two one-poles at `min(kGuardRatio * fs_chip, kGuardMaxHz)` in front of the write. Two poles is deliberate: 12 dB/oct lets an octave of aliased grit through at -12 dB (crust), while three octaves up is gone. `kGuardPoles` is a compile-time 0/1/2 for the A/B; 0 is the plan as written.

At short Times the guard sits at 8 kHz, so the per-stage front end is 3 + 2 poles near 8.5 kHz and the back end 2 more: seven poles per stage. Tap 1 is -3 dB near 2.6 kHz, tap 3 (21 poles) near 1.5 kHz. That is the plan's "flat to ~1 kHz then rolls off" arriving for free from the series topology, and it is why `kReconMaxHz` stays at 8 kHz rather than the hardware's 2.8-5.8 kHz output stage: the cascade already does that job. In the single-stage A/B build the wet will be audibly brighter (2.6 kHz corner); that is expected, not a bug.

### 2.4 Control-rate curves (every `kControlInterval = 16` host samples)

With `u = clamp(log2(kFsChipMax / fsChip) / kOctaveSpan, 0, 1)` (0 at Time 0, 1 at Time 1, mod included):

```
bits        = kBitsAtMaxClock - (kBitsAtMaxClock - kBitsAtMinClock) * u^kBitsCurve      // 12 -> 7, linear in octaves
quantStep   = 2^(1 - bits)                        // 0.00049 at 12 bits, 0.0156 at 7 bits (continuous, no zipper)
ditherGain  = kDitherLsb * quantStep              // TPDF peak in LSB
hissDb      = kHissDbAtMaxClock + (kHissDbAtMinClock - kHissDbAtMaxClock) * u^kHissCurve  // -90 -> -60, curve 1.5
bleedOn     = fsChip < kBleedStartHz (20 kHz)
v           = clamp(log2(kBleedStartHz / fsChip) / log2(kBleedStartHz / kFsChipMin), 0, 1)   // 0 at 20 kHz, 1 at 1.5 kHz
bleedToneDb = -80 + (kBleedToneDbAtMinClock + 80) * v      // -> -34 dB at Time 1
bleedTickDb = -80 + (kBleedTickDbAtMinClock + 80) * v      // -> -30 dB at Time 1
both x kCrust (linear, 1.0)
reconFc     = clamp(min(kReconRatio * fsChip, kReconMaxHz), kFilterMinHz, 0.45 * fsHost); reconCoeff = 1 - exp(-2 pi reconFc / fsHost)
guardFc     = clamp(min(kGuardRatio * fsChip, kGuardMaxHz), kFilterMinHz, 0.45 * fsHost); same form
```

16 samples is 0.33 ms at 48 kHz: it tracks a 200 Hz Time-mod cycle with 15 updates per period, which is plenty for coefficients while `ratio` itself stays per-sample.

### 2.5 Noise budget (so the constants are not guesses)

Per stage, single pass, at Time 1 with 7 bits and 0.5 LSB TPDF: quantisation noise `q/sqrt(12)` = -47 dBFS, dither `0.5q/sqrt(6)` = -50 dBFS, together about -45 dBFS *before* the 675 Hz reconstruction pair, which passes most of it since the chip band is only 0..750 Hz. Three taps at normalised weights (0.41/0.33/0.26, power sum 0.59) give a wet floor near -49 dBFS at Time 1 and near -80 dBFS at Time 0 (12 bits). Add hiss at -60/-90 and the wet floor is roughly **-48 dBFS at Time 1, -78 dBFS at Time 0**, before the loop. At Decay 0.9 the loop adds about +7 dB. That is "clearly audible hiss at the long end, quiet but not sterile at the short end"; the plan's 10 -> 6 bits would put the long end at -36 dBFS per stage, which recirculates into a roar at Decay 0.9. Start at 12 -> 7 and move by ear.

`kDitherLsb` is the crunch-versus-hiss trim: 1.0 fully decorrelates (clean hiss floor, no signal-dependent grit), 0 is raw requantisation (correlated, "digital" crunch). 0.5 keeps a hint of grit under a mostly steady floor.

Hiss is injected *after* the reconstruction filter through a fixed 6 kHz one-pole, scaled by `sqrt(48000 / fsHost)` so its spectral density does not depend on the host rate. Quantisation noise is dark (it lives inside the chip band); hiss is bright; the in-loop Filter and Absorb then colour both, which is how "Filter + Absorb set the age of the tape" also applies to the noise.

### 2.6 Input clipping

Cubic soft knee reaching a hard ceiling with zero slope, normalised to `kChipClipCeiling = 1.0`:

```
chipClip(x): |x| < 1.5 ? x - (4/27) x^3 : sign(x)          // 0.5 -> 0.48, 1.0 -> 0.85, >= 1.5 -> 1.0
```

Odd harmonics only, then the guard pair darkens them before the chip. With Strength at +40 dB this stage does most of the work; the ceiling is the absolute bound on what enters a ring.

### 2.7 Clock bleed and the wrap tick (the "ticking, burbling")

Two components, both gated below 20 kHz and both scaled by `kCrust`:
- **Tone**: `toneSign` flips on every chip write, so it is an exact square at fs_chip/2, softened by a fixed 3 kHz one-pole. As fs_chip drops from 20 kHz the square's fundamental slides down through the one-pole, so the onset is smooth even before the level curve. It is injected after the reconstruction filter (the filter would otherwise remove it: fs_chip/2 > 0.45 fs_chip) but *before* the loop's Filter and feedback, which matters: recirculating clock tone gets re-decimated by the chip on the next pass and aliases toward DC, producing slow burble as an emergent behaviour rather than a synthesised one.
- **Tick**: a 1.5 ms exponentially decaying click, alternating polarity, fired when `writeIdx` wraps, i.e. once per stage delay. At Time 1 that is a click every 1.22 s per stage. Stages initialise `writeIdx = stageIndex * kStageMemory / kStageCount` so the three ticks are staggered, giving the three-step tick pattern. This is a hypothesis about what the hardware's "ticking" is (the memory refresh period); it is a separate constant so it can be zeroed if the demos say otherwise.

---

## 3. The loop

### 3.1 Per-sample order of operations

```
process(in, out, n, p, timeModOct):
    ScopedNoDenormals (cheap, and EngineTest has no processor around it)
    handleClear()                                   // section 4, block boundary
    clock.setTime(p.time01); if (snapTime) { clock.reset(p.time01); snapTime = false; }
    set smoother targets: decay, cutoff (multiplicative), resonance, absorb

    for i in 0..n-1:
        frame = clock.next(timeModOct ? p.timeModDepth * kTimeModOctaves * timeModOct[i] : 0)
        if (--controlCountdown <= 0):               // every 16 samples
            controlCountdown = kControlInterval
            filter.setCutoffAndResonance(cutoffSmooth.skip(16), resSmooth.skip(16))
            absorb.set(absorbSmooth.skip(16)); decayGain = decaySmooth.skip(16) * kDecayGainScale

        loopIn = in[i] + feedback                   // feedback is last sample's value (one host sample late,
                                                    // irrelevant next to a 9 ms minimum stage delay)
        s = loopIn; tapSum = 0
        for k in stages: s = stages[k].processSample(s, frame, rng[k]); tapSum += tapWeight[k] * s

        f  = filter.processLowpass(tapSum)          // the in-loop SVF (3.2)
        sh = absorb.shelf(f)                        // darkening (3.4)
        y  = sat.process(sh, absorb.drive(), absorb.driveComp())   // asym tanh + DC block (3.3)

        out[i]   = y * absorb.wetGain() * clearFade
        feedback = y * absorb.feedbackGain() * decayGain * clearFade
        ember    = max(|y|, ember * emberRelease)
    uiLoopEnergy.store(ember)
```

Tap weights: `kTapWeights = {1.0, 0.8, 0.64}`, normalised so they sum to 1 (`{0.41, 0.33, 0.26}`). Normalising is what makes "Decay = 1.15 is 15% over unity" true: the loop's in-phase gain is exactly `decayGain`. The cost is that a single first echo at Decay 0 sits at -7.7 dB relative to a single-stage build; that is a Blend/Out calibration matter, kept as `kWetGainDb` (0) rather than hidden in the loop. An impulse now produces echoes at D/3, 2D/3, D with those weights, and each round trip re-spreads them: a dense, quickly-reverberant repeat. That *is* the manual's "Echo-Verb", and the single-stage A/B will sound like a plain lo-fi echo by comparison.

### 3.2 LoopFilter: TPT SVF with tanh in the resonance path

```
setCutoffAndResonance(hz, res01):
    g  = tan(pi * clamp(hz, 20, 0.45 fs) / fs)
    k  = 2 (1 - res01)^kResCurve  -  kResNegDamp * smoothstep(0.9, 1.0, res01)   // 2 .. -0.05
    a1 = 1 / (1 + g (g + k)); a2 = g a1; a3 = g a2
processLowpass(x):
    v3 = x - ic2; v1 = a1 ic1 + a2 v3; v2 = ic2 + a2 ic1 + a3 v3
    ic1 = kSvfSatLevel * tanh((2 v1 - ic1) / kSvfSatLevel)     // the bandpass state feeds k; saturating it
    ic2 = 2 v2 - ic2                                            // is the tanh in the resonance path
    return v2
```

`k` reaches slightly negative at the top of the knob (`kResNegDamp = 0.05`) so the filter genuinely self-oscillates rather than merely ringing; the tanh on the bandpass state bounds that oscillation at about `kSvfSatLevel = 1.5` peak and compresses resonance peaks warmly instead of letting a 20 dB peak hit the saturator as a brick. Cutoff arrives through a multiplicative `SmoothedValue` (30 ms) so the Filter knob sweeps in octaves.

### 3.3 LoopSaturator: asymmetric tanh + DC blocker

```
b = kSatBias (0.15), d = drive (1 .. 2.5 from Absorb)
sat(x) = ( tanh(d x + b) - tanh(b) ) / ( d (1 - tanh(b)^2) )        // unity small-signal gain at rest
       positive ceiling  (1 - tanh b) / (1 - tanh^2 b) = 0.87
       negative ceiling  (1 + tanh b) / (1 - tanh^2 b) = 1.175      (at d = 1)
y  = sat(x) * driveComp                                              // driveComp = d^-kAbsorbDriveComp
dc: out = y - dcX1 + dcR * dcY1;  dcX1 = y;  dcY1 = out;  dcR = exp(-2 pi kDcBlockHz / fs)   // 0.99869 at 48k/10 Hz
```

The bias generates even harmonics (warmth) and a level-dependent DC offset; the 10 Hz blocker removes the offset before it can recirculate and pump. `std::tanh` twice per sample (SVF + saturator) is about 40 ns; a rational approximation is an option, not a need.

### 3.4 Absorb (post-filter, pre-saturator for tone; post-saturator for level)

```
set(a):
    shelfGain    = 10^(kAbsorbShelfMaxDb * a / 20)      // -12 dB at a = 1, first-order high shelf at kAbsorbShelfHz = 2 kHz
    drv          = 1 + kAbsorbDriveMax * a              // 1 .. 2.5 into the saturator ("a little extra saturation")
    comp         = drv^-kAbsorbDriveComp                // 0.5: half compensated, so hot signals also duck
    wet          = 10^(kAbsorbMaxAttenDb * a / 20)      // -18 dB at a = 1: "diminished into the earth"
    fb           = wet^kAbsorbFeedbackShare             // 1.0 = plan (Absorb also shortens Decay); 0 = wet only
shelf(x): lp += lpCoeff (x - lp); return lp + (x - lp) * shelfGain
```

`kAbsorbFeedbackShare` exists because the plan and the manual can be read two ways (Absorb as an in-loop sink versus an output tap). Ship the plan's reading (1.0), A/B by ear; either way the "Wow and Flutter" patch (Absorb high, Blend 50%) gets a quiet, dark, compressed wet.

### 3.5 Bounds and the steady state at Decay 1.15

Every node has a hard bound with no `jlimit` in the signal path:
- into a ring: `chipClip` ceiling 1.0;
- filter state: `kSvfSatLevel` 1.5 (so a Resonance-boosted peak enters the saturator at most at 1.5 plus a bounded amount of lowpass state);
- loop output `y`: saturator ceilings 0.87 / 1.175 (DC blocker can transiently double a step, not sustain it);
- feedback: 1.175 × 1.15 × fb(≤1) = 1.35, which `chipClip` turns back into 1.0.

So runaway is a limit cycle, not growth. Small-signal loop gain at Decay 1.15, Filter open, Absorb 0 is 1.15 at the comb peaks (multiples of 1 / stage delay); the tanh describing function `N(A) ≈ 1 - A²/4` must fall to 1/1.15, giving a peak amplitude near **0.7 at the saturator input and ~0.6 at its output, i.e. wet RMS around 0.4-0.5 (-7 dBFS)**. The oscillation organises itself onto the comb: at short Time a pitched howl (109 Hz and harmonics at Time 0), at long Time a rhythmic pulse; the SVF resonance and the 21-pole darkness pick which harmonics win. At Decay exactly 1.0 only the in-phase comb frequencies sustain (`sum w'² = 0.34`, everything else decays), which is the manual's "infinity" arriving as a slow narrowing into tones rather than a flat sustain. That is desirable and it is what the single-stage A/B loses.

---

## 4. Flush / Clear

UI thread: `requestClear()` bumps `clearCounter` (release). Audio thread, at the top of `process`:

```
handleClear():
    c = clearCounter.load(acquire)
    if (c != lastClearSeen && !clearing) { lastClearSeen = c; clearing = true; clearFadeStep = 1 / (kClearFadeMs * sr / 1000); }
per sample while clearing:
    clearFade -= clearFadeStep
    if (clearFade <= 0):                          // 8 ms after the request
        reset(): std::fill every ring, zero mfb/guard/recon/hiss/tone/tick states, LoopFilter ic1/ic2,
                 Absorb lp, saturator dc state, feedback, ember; keep writeIdx stagger, keep ChipClock
        clearFade = 1; clearing = false
```

No allocation (fixed arrays), no locks, a `std::fill` of 5.5 k floats (about a microsecond). The 8 ms fade is on the wet out *and* the feedback, so a loud loop does not go from 0.7 to 0 between two samples; it also means the request-to-silence latency is one block plus 8 ms, which is perceptually instant. `reset()` is public and synchronous for `prepare` and for tests; `requestClear()` is the only path the editor may use. The ChipClock smoother is deliberately not reset (a clear must not re-pitch anything that follows); `snapTimeOnNextBlock()` is the separate hook for preset loads. Clear is momentary and never stored in state, so it needs nothing from `stampExtraState`.

---

## 5. Tunable constants

| Name | Start | What it does to the sound |
|---|---|---|
| `kMemoryTotal` | 5504 | Delay range as a whole; bigger = longer at the same fidelity per second. Compile-time. |
| `kStageCount` | 3 | 1 = plain lo-fi echo, brighter; 3 = three-step repeats that blur into Echo-Verb, darker later taps. |
| `kReadGuard` | 2 | Safety slots only; no audible effect. |
| `kFsChipMax` / `kFsChipMin` | 200 kHz / 1.5 kHz | Shortest/longest Time (27 ms / 3.66 s) and how destroyed the long end is. |
| `kFsChipFloor` / `kFsChipCeil` | 600 Hz / 400 kHz | How far Time Mod can push beyond the knob; ceiling bounds the write loop at ~9 writes/sample. |
| `kTimeSmoothMs` | 20 | Knob sweep warble speed; shorter = zipper, longer = seasick. |
| `kTimeModOctaves` | 3.0 | Peak FM depth at full Time Mod; the clang range. 3 = Strega-deep. |
| `kControlInterval` | 16 | Coefficient update granularity; only matters under fast Time Mod. |
| `kMfbHz` / `kMfbQ` | 8800 / 0.9 | Per-stage input bandwidth and the small presence bump before the roll-off. |
| `kChipClipCeiling` | 1.0 | Where the chip input saturates; lower = Strength bites earlier. |
| `kGuardPoles` / `kGuardRatio` / `kGuardMaxHz` | 2 / 0.45 / 8000 | Aliasing at long Time: 0 poles = harsh folded grit, 2 = dark with a crust. **First thing to A/B by ear.** |
| `kReconRatio` / `kReconMaxHz` / `kFilterMinHz` | 0.45 / 8000 / 150 | Wet brightness versus Time; the automatic darkening curve. |
| `kBitsAtMaxClock` / `kBitsAtMinClock` / `kBitsCurve` | 12 / 7 / 1.0 | Noise floor and grit versus Time (section 2.5). Lower min bits = more roar in the loop at long Time. |
| `kDitherLsb` | 0.5 | Crunch (0) versus steady hiss (1). |
| `kHissDbAtMaxClock` / `kHissDbAtMinClock` / `kHissCurve` | -90 / -60 / 1.5 | Bright analog hiss; curve > 1 keeps mid Times cleaner. |
| `kHissColourHz` | 6000 | Hiss tone; lower = tape-like, higher = transistor-like. |
| `kBleedStartHz` | 20 kHz | Where clock bleed begins to exist (about Time 0.47). |
| `kBleedToneDbAtMinClock` / `kBleedToneColourHz` | -34 / 3000 | Whine level at the long end and how soft its edges are. |
| `kBleedTickDbAtMinClock` / `kTickDecaySeconds` | -30 / 0.0015 | Click level per memory wrap and click length. |
| `kCrust` | 1.0 | Master scale on tone + tick. **The one to expose as a trim.** |
| `kTapWeights` | {1, 0.8, 0.64} | Relative loudness of the three steps; steeper = more "echo", flatter = more "verb". |
| `kWetGainDb` | 0 | Post-loop wet makeup for the normalised tap sum; not in the feedback path. |
| `kResCurve` / `kResNegDamp` / `kSvfSatLevel` | 1.7 / 0.05 / 1.5 | Where resonance starts to bite, how eager self-osc is, how loud and compressed it gets. |
| `kSatBias` | 0.15 | Even-harmonic warmth and asymmetry of the runaway ceiling; 0 = symmetric, cleaner, colder. |
| `kDcBlockHz` | 10 | Pumping suppression; higher thins the low end of long loops. |
| `kDecayGainScale` | 1.0 | Calibration so 1.0 on the knob is unity; leave unless the tap normalisation changes. |
| `kAbsorbShelfHz` / `kAbsorbShelfMaxDb` | 2000 / -12 | The "tape age" darkening. |
| `kAbsorbDriveMax` / `kAbsorbDriveComp` | 1.5 / 0.5 | Extra saturation at high Absorb and how much of it is level-compensated. |
| `kAbsorbMaxAttenDb` / `kAbsorbFeedbackShare` | -18 / 1.0 | How much is "diminished into the earth", and whether that also shortens Decay. |
| `kClearFadeMs` | 8 | Clear click suppression. |
| `kEmberReleaseMs` | 50 | Ember decay speed only. |

Candidates for later exposure as trims, in order: `kCrust`, `kDitherLsb` (as "Grit"), `kTapWeights` slope (as "Steps"), `kAbsorbFeedbackShare` (as a switch).

---

## 6. EngineTest scenarios

Harness shape follows teder: a `run()` returning a `std::vector<float>` of wet output, driving `TimeFilterLoop` directly (mono, no Strength/Blend in the way), plus `rmsWindow`, `freqEstimate` (zero crossings), `partialAmp` (Goertzel), `maxAbs`, `hfFraction` (energy after an offline 4 kHz one-pole highpass over total), and a `check(what, ok, detail)` printer. All scenarios except `nonidentical` call `setNoiseSeedForTests(1)`. Sample rate 48 kHz, block 128, Time-mod buffer supplied as a per-sample lambda.

```
Run run (double seconds, Params p, std::function<float(double t)> input,
         std::function<void(double t, Params&)> automate = {},
         std::function<float(double t)> timeModOct = {}, juce::uint32 seed = 1);
```

**1. `smear` (tell 1: Time sweeps repitch, never crossfade).** Continuous 440 Hz sine at -12 dB, Decay 0, Filter 18 kHz, Res 0, Absorb 0, Time 0.30 (stage delay 39.7 ms). At t0 = 1.0 s step Time to 0.50 (ratio 0.376, stage delay 106 ms).
- `freqEstimate` over [t0+0.07, t0+0.11] within **165 Hz ± 20 %** (440 × 0.376 = 165; all three taps are draining old content at the new rate in that window).
- Glide, not switch: `freqEstimate` in 20 ms windows from t0 to t0+0.08 is non-increasing (each ≤ 1.1 × previous) and never silent (RMS > 1e-3). A crossfading delay reads 440 throughout and fails the first check.
- Recovery: [t0+0.45, t0+0.60] back within 440 ± 3 %.
- Mirror: step 0.50 -> 0.30, expect **1170 Hz ± 20 %** in [t0+0.03, t0+0.05].

**2. `crust` (tell 2: long Time is dark, hissy, ticking).** Decay 0, Filter 18 kHz, Absorb 0.
- Dark: 3.3 kHz sine at -12 dB, measure `partialAmp(3300)` in [2, 3] s at Time 0 and Time 1; expect the Time 1 value **≥ 30 dB below** the Time 0 value (design predicts about -50 dB; 3.3 kHz aliases to 300 Hz, not DC). Control: 300 Hz sine transmits within 6 dB of its Time 0 level at Time 1.
- Hissy: no input, wet RMS over [2, 4] s at Time 1 **between -58 and -40 dBFS**; at Time 0 **below -70 dBFS**; difference ≥ 20 dB.
- Whine: `partialAmp(750)` (fs_chip/2 at Time 1) with no input **≥ -50 dBFS**; the same bin at Time 0 ≤ -90 dBFS.
- Ticking: with no input at Time 1 over 8 s, count 50 ms windows whose peak exceeds 4 × the global RMS: expect **between 4 and 24** (three staggered wraps per 3.66 s), and the median spacing between them within ±15 % of 1.22 s ÷ 3 or 1.22 s (stagger pattern). With `kCrust = 0` (a test-only setter or compile flag) the count is 0.

**3. `runaway` (tell 3: Decay past unity self-oscillates warmly, never hard-clips).** 220 Hz burst at -12 dB for 0.3 s then silence, Time 0.35, Decay 1.15, Filter 18 kHz, Res 0.2, Absorb 0, run 6 s.
- Bounded and alive: wet RMS over [4, 6] s **between 0.25 and 0.75**; `maxAbs` over the whole run **< 1.2**; all samples finite.
- Warm, not clipped: crest factor over [5, 6] s **between 1.2 and 3.0** (a rail-clipped loop sits near 1.0); `hfFraction` over [5, 6] s **< 0.05**.
- Settles: RMS[4,5] ≥ 0.9 × RMS[5,6] and ≤ 1.1 × it.
- Same patch with Decay 0.95 decays: RMS[3,4] < 0.3 × RMS[0.5,1.0]. With Decay 1.0: RMS[5,6] between 0.25 × and 1.5 × RMS[1,2] (sustains, does not explode).
- Worst case: repeat the 1.15 run with Time Mod depth 1 at 173 Hz and Res 0.95; same bounds must hold.

**4. `nonidentical` (tell 4: never bit-identical).** Same 2 s scenario (440 Hz, Time 0.5, Decay 0.7) on two freshly constructed loops with **default seeding** (constructor seeds from `Time::getHighResolutionTicks()` mixed with `this`).
- Fraction of sample positions where the outputs differ **≥ 0.5**; RMS of the difference **between -90 and -35 dBFS** (different, but the same sound).
- Control: both seeded with `setNoiseSeedForTests(123)`: max |difference| **== 0.0** exactly, which is what makes every other scenario repeatable.

**5. `sidebands` (tell 5: audio-rate Time Mod gives ring-mod sidebands).** 1 kHz sine at -12 dB, Time 0.5, Decay 0, Filter 18 kHz, Time Mod depth such that peak excursion is ±0.35 octave (depth = 0.35 / `kTimeModOctaves`). Mod frequency chosen from the measured delay so write- and read-side FM add rather than cancel: `D = getDelaySeconds()`, `m = floor(200 D)`, `fMod = (m + 0.5) / D` (about 200 Hz). Measure over [1.0, 2.0] s.
- Grid energy `G = sum_{k=1..6} A(1000 ± k fMod)²` versus half-grid `H = sum A(1000 ± k fMod ± fMod/2)²`: **G > 10 H** (the lines are exactly f_mod apart, the ring-mod signature) and **G > 0.3 × total energy**.
- Control with depth 0: G < 0.01 × total; A(1000) accounts for > 0.95 of total.

**Extras that guard the topology and the rules:**
- `threestep`: 1 ms click at -6 dB, Time 0.5, Decay 0. Energy in 5 ms windows peaks at **106, 212, 317 ms ± 3 ms** with peak ratios tap2/tap1 = 0.8 ± 0.15, tap3/tap2 = 0.8 ± 0.15, and `hfFraction` of tap 3 < that of tap 1 (later steps are darker). With `DYBBUK_PT_STAGES=1`: one peak at 317 ms. Prints all three so the A/B is a number.
- `srinvariance`: the `threestep` echo time at 44.1 / 48 / 96 / 192 kHz within **±1 %** of each other; wet noise RMS at Time 1 within ±2 dB across rates.
- `clear`: runaway loop at RMS ~0.5, `requestClear()` at t = 3.0 s. Wet RMS over [3.02, 3.10] **< -80 dBFS**; the largest sample-to-sample step during [2.99, 3.02] **≤ 1.5 ×** the largest step during [2.8, 2.99] (a fade, not a click).

---

## 7. Risks in this design, and mitigations

1. **Aliasing balance at long Time.** Two guard poles might still be harsh on bright sources or, with a dark source, too polite to feel "destroyed". `kGuardPoles` is compile-time 0/1/2 and `crust` prints the 3.3 kHz alias level, so the listening pass has numbers to anchor to. Do this A/B before anything downstream.
2. **Normalised tap sum makes Decay feel weak and short Decay feel reverberant.** That is the hardware's character by the plan's own account, but if it fights musicality, the fix is `kTapWeights` slope (steeper = echo) and `kWetGainDb`, not the normalisation (which keeps 1.15 meaning 1.15). `threestep` and `runaway` catch any change in loop gain.
3. **Noise recirculation at Decay 0.9-1.0 with long Time.** The -48 dBFS single-pass floor becomes about -40 dBFS in the loop and at Decay 1.0 climbs until the saturator holds it. If that reads as a roar rather than a hiss bed, raise `kBitsAtMinClock` to 8 first, then `kDitherLsb` down; `crust` and `runaway` print the floors.
4. **Control-rate stepping under deep audio-rate Time Mod.** `quantStep` and the filter coefficients update every 16 samples while `ratio` moves per sample; at ±3 octaves and 1 kHz mod that is a staircase. If audible, interpolate `quantStep` and the two one-pole coefficients linearly across the interval (three extra multiplies per sample), or drop `kControlInterval` to 8.
5. **The wrap-tick hypothesis.** If the hardware's ticking turns out to be something else (clock subharmonics rather than the memory period), the tick constant goes to -inf and the tone component carries the tell; nothing else depends on it.
6. **Preset load swoop.** A 20 ms one-pole on Time is right for knobs and wrong for preset changes; `snapTimeOnNextBlock()` must be called from `applyExtraState` / preset load and on the first block, and the `smear` scenario should be paired with a snap test (Time jump via `snap` produces no intermediate pitch window).
7. **Self-oscillating filter with negative damping.** `k < 0` plus the tanh is stable by construction, but with cutoff at 20 Hz and Decay 1.15 the loop can sit in a very low-frequency limit cycle that reads as pumping through the 10 Hz DC blocker. If the worst-case `runaway` variant shows RMS wobble > 3 dB at sub-2 Hz rates, raise `kDcBlockHz` to 15 or clamp the SVF minimum cutoff to 30 Hz inside the loop only.
8. **CPU.** Estimated 150-250 ns per sample at 3 stages (one `exp2f`, two `tanh`, up to 9 writes with 2 PRNG calls each at short Time), about 1 % of a core at 48 kHz; 192 kHz is 4 %. If the profile disagrees, the first savings are a rational tanh and a polynomial exp2, both invisible to the tests above.
9. **Test fragility from tuned constants.** `smear`, `threestep` and `sidebands` derive their expectations from the Time map and memory size; every expectation above is written against `ChipClock::delaySecondsFor` and `getDelaySeconds()` rather than literal milliseconds, so retuning `kMemoryTotal` or the clock range moves the expectations with it. The tolerances (±20 % pitch, ±3 ms echo) are wide enough for interpolation and filter group delay but narrow enough that a crossfade delay, a hard clipper, or a lost stage fails.