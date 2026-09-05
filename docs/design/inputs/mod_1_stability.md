# Dybbuk modulation system design (plan §2.5), control-rate architecture and stability

Source of idiom: `infinite_sustainer` runs its tone filter in 32-sample sub-blocks with `skip(n)` on the knob smoothers (FreezeEngine.cpp ~L1125); `teder` builds drift from an xorshift32 into two cascaded one-poles with an analytic RMS normaliser (PolyEngine.cpp L65-70, L237-241). Both patterns are reused below. JUCE 8.0.12 source is present in `build/_deps/juce-src/`; I checked `SmoothedValue::reset(int numSteps)` snaps current to target (hence the tiny custom `ControlRamp` below) and `SmoothedValue<float, Multiplicative>` exists for log-domain cutoff smoothing.

## 0. Architecture in one paragraph

The engine processes every host block as a chain of sub-blocks that end exactly on a **control tick every `kControlBlock = 32` samples**, using a persistent `samplesUntilTick` counter so ticks stay 32 samples apart across any host block size (1, 33, 2048). Four sources produce values: Agitation and Input Follower are computed **per sample** (cheap, and Agitation reaches 1 kHz); Interference and Drift are computed **per tick** and linearly ramped per sample for the Time column. The 4x7 matrix combines sources into seven destinations. **Time** is the only audio-rate destination: its modulation is summed per sample in octaves, added to the log2-domain smoothed knob/sync target, clamped, and converted to a per-sample `ratio[i] = fs_chip/fs_host` array the three PT cores consume (no division anywhere per sample). The six other destinations are combined once per tick, one-pole smoothed at control rate, clamped in musical units, and either applied to the SVF per sub-block (cutoff, resonance) or ramped linearly across the 32 samples (decay, absorb, blend, strength) so gains never step. There is exactly one modulation feedback path (loop energy -> Interference -> Time -> loop energy) and it is bounded at every stage; §3.6 gives the argument and §6 the test that measures it.

## 1. Files and classes (`Source/dsp/`, one class per pair, all added to `ENGINE_SOURCES`)

```
Source/dsp/Xorshift32.h        header-only POD RNG shared by Drift, Interference, PTCore noise
Source/dsp/Agitation.{h,cpp}   AD function generator, audio-rate, loop/gate
Source/dsp/InputFollower.{h,cpp}  peak follower + gate detector on the post-Strength signal
Source/dsp/Interference.{h,cpp}   Lorenz "wander" at control rate + energy-driven crackle at audio rate
Source/dsp/Drift.{h,cpp}       0.05..2 Hz filtered random, control rate, ramped
Source/dsp/ModMatrix.{h,cpp}   routing table, macro, Time path (log2 smoother + ratio fill), control-rate combine/clamp/ramps
Source/dsp/ModConstants.h      namespace modk { every constant in §5 }   (header-only, no class)
```

Nothing allocates after construction: every per-sample scratch buffer is `std::array<float, modk::kControlBlock>` because the engine only ever asks for at most one control block at a time. `prepare()` computes coefficients from `sr` and the control rate `fc = sr / kControlBlock` and resets state; it is idempotent.

```cpp
// Xorshift32.h
struct Xorshift32 {
    uint32_t s = 0x9E3779B9u;
    void seed (uint32_t v) noexcept { s = v != 0 ? v : 0x9E3779B9u; }
    float nextBipolar() noexcept { s ^= s << 13; s ^= s >> 17; s ^= s << 5; return (float)(int32_t) s * (1.0f / 2147483648.0f); } // -1..1
    float next01() noexcept { return 0.5f + 0.5f * nextBipolar(); }
};
```

```cpp
// Agitation.h
class Agitation {
public:
    enum class Mode { loop = 0, gate };
    void prepare (double sampleRate);
    void setSpeedHz (float hz);          // 0.016..1000, log-mapped by the parameter
    void setAngle (float angle01);       // 0..1, 0.5 = triangle (fixed 0.5 in v1)
    void setMode (Mode m);
    // Per sample. followerEnv is the InputFollower's linear envelope (gate detection);
    // out[i] in 0..1 (unipolar, like the 0..6 V hardware output). Returns the block mean
    // (the anti-aliased value control-rate destinations must use).
    float process (float* out, const float* followerEnv, int n);
    void resyncPhase (double phase01);   // host-sync hook, unused in v1
    float getPhase() const;
private:
    double sr = 48000.0, phase = 0.0, phaseInc = 0.0;
    float  faEff = 0.5f, invFa = 2.0f, invFd = 2.0f;   // attack fraction and reciprocals, updated in recomputeShape()
    float  speedHz = 0.5f, angle = 0.5f, lastOut = 0.0f;
    Mode   mode = Mode::loop;
    bool   running = true, armed = true;              // gate mode: one-shot state + hysteresis arm
    int    holdOffSamples = 0;
    void   recomputeShape();
};
```

```cpp
// InputFollower.h
class InputFollower {
public:
    void prepare (double sampleRate);
    // in = post-Strength mono block. envOut[i] = linear peak envelope (>= 0),
    // outputs 0..1 via value01(). Returns env at end of block.
    float process (const float* in, float* envOut, int n);
    static float value01 (float env) { return juce::jmin (1.0f, env * modk::kFollowerInvFullScale); }
    float getEnv() const { return env; }
private:
    float env = 0.0f, aAtk = 0.0f, aRel = 0.0f;
};
```

```cpp
// Interference.h
class Interference {
public:
    void prepare (double sampleRate);
    void seed (uint32_t s);
    // Once per control tick. loopEnvLinear = TimeFilterLoop's |signal| envelope at the
    // feedback tap. Returns the control-rate value (wander, -1..1) for control destinations.
    float tick (float loopEnvLinear);
    // Per sample for the Time column: ramped wander + crackle, hard-limited to -1..1.
    void fillAudio (float* out, int n);
    float getEnergy01() const { return e; }          // also feeds the ember
    float getBlockMeanAudio() const { return blockMean; }
private:
    double sr = 48000.0, fc = 1500.0;
    Xorshift32 rng;
    float x = 1.0f, y = 1.0f, z = 20.0f;             // Lorenz state
    float e = 0.0f, aEUp = 0.0f, aEDown = 0.0f;      // slewed energy 0..1
    float wanderSlew = 0.0f, aSlew = 0.0f;           // one-pole at kIntfSlewHz (control rate)
    float wanderCur = 0.0f, wanderInc = 0.0f;        // per-sample ramp across the tick
    float crackleP = 0.0f, crackleTarget = 0.0f, crackleEnv = 0.0f, aCrackleAtk = 0.0f, crackleDecay = 0.0f;
    float blockMean = 0.0f;
};
```

```cpp
// Drift.h
class Drift {
public:
    void prepare (double sampleRate);
    void seed (uint32_t s);
    float tick();                       // once per control tick, returns -1..1
    void fillAudio (float* out, int n); // ramp from previous tick value to current
private:
    Xorshift32 rng;
    float lp1 = 0, lp2 = 0, hpState = 0, aLp = 0, aHp = 0, gain = 0;
    float cur = 0, inc = 0, last = 0;
};
```

```cpp
// ModMatrix.h
class ModMatrix {
public:
    enum Src { srcAgitation = 0, srcFollower, srcInterference, srcDrift, numSrc };
    enum Dst { dstTime = 0, dstFilter, dstResonance, dstDecay, dstAbsorb, dstBlend, dstStrength, numDst };

    struct Routing { float depth[numSrc][numDst] = {}; };            // bipolar -1..1 per cell
    static Routing heroRouting();                                    // the three v1 routes

    struct Sources {                       // one control block's worth, filled by the engine
        const float* audio[numSrc];        // per-sample arrays (32): agitation, follower01, interference, drift
        float ctl[numSrc];                 // control-rate representative: agit block mean, follower01 at end, intf wander, drift
    };
    struct Knobs {                         // knob smoother outputs, already skip(n)'d by the engine
        float cutoffHz, resonance, decay, absorb, blend, strengthDb;
    };
    struct Applied {                       // what the loop uses for this sub-block
        float cutoffHz, resonance;         // set on the SVF once per sub-block
        ControlRamp decay, absorb, blend, strengthGain;   // per-sample .next()
    };
    struct ControlRamp {                   // 32-sample linear ramp, no division (kInvControlBlock)
        float cur = 0.0f, inc = 0.0f;
        void snap (float v) { cur = v; inc = 0.0f; }
        void setTarget (float t) { inc = (t - cur) * modk::kInvControlBlock; }
        float next() { cur += inc; return cur; }
    };

    void prepare (double sampleRate);
    void setRouting (const Routing& r) { routing = r; }
    void setMacro (float agitate01, float timeMod01);            // per block, raw knob values (smoothed inside)
    void setTimeTargetLog2 (float log2FsChip);                   // per block: knob or sync target (already clamped)
    void tick (const Sources& s, const Knobs& k);                // once per control tick -> applied
    void fillTimeRatio (float* ratio, const Sources& s, int n); // per sample, for the PT cores
    const Applied& applied() const { return app; }
    float lastTimeModOctMean() const { return timeModMean; }     // UI: modulated arc on Time
    float destModNorm (Dst d) const;                             // UI: -1..1 per knob ring
    void snapAll (const Knobs& k);                               // first block after prepare
private:
    Routing routing;
    Applied app;
    float macro = 0.0f, macroTarget = 0.0f, timeModDepth = 0.0f, timeModTarget = 0.0f, aMacro = 0.0f;
    float lTarget = 0.0f, lSmooth = 0.0f, aTime = 0.0f, logFsHost = 0.0f;
    float modSm[numDst] = {}, aDest[numDst] = {}, timeModMean = 0.0f;
    float effDepth[numSrc][numDst] = {};    // routing x dstScale x macro, refreshed per tick
};
```

Engine-side (in `TimeFilterLoop`/the engine, not mine to write but required by this design): a per-sample `loopEnv += (|fb| - loopEnv) * (|fb| > loopEnv ? aAtk3ms : aRel150ms)` at the feedback tap, exposed as `getLoopEnvelope()`; per-block atomics `uiLoopEnergy`, `uiModSource[4]`, `uiInterferenceLevel`, `uiTimeModOct`, `uiDestMod[7]`; `setSeedsForTests(uint32_t)` fanning one master seed to Drift, Interference and the PT-core noise RNGs (`seed`, `seed*2654435761u`, `seed^0xA5A5A5A5u`); production seeds come from `juce::Random::getSystemRandom().nextInt()` in the processor constructor so two instances never match.

Engine block skeleton:

```
process(buffer, p):
    set knob smoother targets; matrix.setMacro(p.agitate, p.timeMod)
    matrix.setTimeTargetLog2( p.sync ? log2(clamp(N_total / syncSeconds(p))) : kLogFsMax - p.time01 * kTimeSpanOct )
    monoSum(buffer) -> monoIn[numSamples]
    pos = 0
    while pos < numSamples:
        n = min(numSamples - pos, samplesUntilTick)
        if samplesUntilTick == kControlBlock:            // a tick starts this sub-block
            drift.tick(); intfCtl = interference.tick(loop.getLoopEnvelope())
            (fills srcs.ctl for intf/drift; agit/follower ctl values come from the PREVIOUS block's mean/end,
             one 32-sample latency on control destinations, none on Time)
        strength stage for n samples using app.strengthGain.next()  -> post[n]
        follower.process(post, followerEnv, n); agitMean = agitation.process(agitBuf, followerEnv, n)
        interference.fillAudio(intfBuf, n); drift.fillAudio(driftBuf, n)
        if tick: matrix.tick(srcs, knobs.skip(kControlBlock))      // sets app.cutoffHz etc.
        matrix.fillTimeRatio(ratioBuf, srcs, n)
        loop.process(post, wet, ratioBuf, app, n)                   // SVF set once, gain ramps per sample
        pos += n; samplesUntilTick -= n; if samplesUntilTick == 0: samplesUntilTick = kControlBlock
    blend/out as before
```

(Simplification allowed: call `matrix.tick` at the start of the sub-block using last tick's agitation/follower values; the 32-sample latency on control destinations is inaudible and keeps ordering trivial.)

## 2. Sources

All sources are dimensionless. **Unipolar 0..1:** Agitation, Follower. **Bipolar -1..1:** Interference, Drift.

### 2.1 Agitation (audio rate, 0..1)

Speed knob is log-mapped: `speedHz = kAgitSpeedMin * (kAgitSpeedMax / kAgitSpeedMin)^knob01` (via a custom `NormalisableRange` with exp/log lambdas, readout through `floatParam(..., "Hz")`, so it reads "0.02 Hz" .. "1000 Hz"). Period `T = 1/speedHz`, 62.5 s .. 1 ms.

Angle skew: `fa = kAngleMin + angle01 * (1 - 2*kAngleMin)` with `kAngleMin = 0.01`, so angle 0 -> 1/99, 0.5 -> 50/50, 1 -> 99/1. Segments are floored at `kAgitMinSegmentMs = 0.5` so a 99/1 saw at high speed never becomes a single-sample cliff (a cliff into Time is a pitch snap, i.e. a click):

```
recomputeShape():
    periodSamples = sr / speedHz
    minFrac = kAgitMinSegmentMs * 1e-3 * sr / periodSamples        // fraction of the period
    faEff = clamp(fa, minFrac, 1 - minFrac)   // at 1 kHz, minFrac = 0.5 -> forced triangle
    if minFrac >= 0.5: faEff = 0.5
    invFa = 1 / faEff; invFd = 1 / (1 - faEff)                     // two divisions per parameter change, not per sample
    phaseInc = speedHz / sr

shape(phi) = phi < faEff ? phi * invFa : (1 - phi) * invFd        // 0..1, linear AD

process(out, followerEnv, n):
    for i in 0..n-1:
        if mode == gate:
            if holdOff > 0: --holdOff
            trig = armed && followerEnv[i] > kGateOnLevel && holdOff == 0
            if followerEnv[i] < kGateOffLevel: armed = true
            if trig:
                armed = false; holdOff = kGateHoldMs*1e-3*sr
                // soft retrigger: relaunch the attack FROM THE CURRENT OUTPUT so there is no jump
                phase = lastOut * faEff; running = true
        if running: phase += phaseInc
        if phase >= 1:
            if mode == loop: phase -= 1
            else: phase = 0; running = false          // one-shot done, output rests at 0
        lastOut = out[i] = running || mode == loop ? shape(phase) : 0
        sum += out[i]
    return sum / n              // block mean, the value control-rate destinations use
```

Why the block mean: a 1 kHz generator point-sampled at 1.5 kHz aliases into garbage on the filter; the mean over 32 samples is a box low-pass, so at high speeds control destinations converge to the shape's DC (0.5 for any linear AD) while the audio-rate detail is available to Time. Loop mode is free-running (transport-independent, like the sibling's Tone Sweep). `resyncPhase()` is the later host-sync hook: set `phase = fmod(ppq / beatsPerCycle, 1)` once per block when synced.

### 2.2 Input Follower (audio rate, 0..1)

Peak follower on the post-Strength mono signal, asymmetric one-pole:

```
prepare: aAtk = 1 - exp(-1 / (kFollowerAttackMs*1e-3 * sr));  aRel = 1 - exp(-1 / (kFollowerReleaseMs*1e-3 * sr))
process: for i: r = |in[i]|; env += (r - env) * (r > env ? aAtk : aRel); envOut[i] = env
value01(env) = min(1, env * kFollowerInvFullScale)     // kFollowerFullScale = 0.5: a -6 dBFS peak reads 1.0
```

Gate detection (used by Agitation) is on the same linear `env` with hysteresis: on above `kGateOnLevel = 10^(-30/20) = 0.0316`, re-arm below `kGateOffLevel = 10^(-36/20) = 0.0158`, hold-off `kGateHoldMs = 20`. No feedback path: the follower reads the input, never the loop.

### 2.3 Interference (control-rate chaos + audio-rate crackle, -1..1)

Two layers, both driven by the loop's own energy, so the source is quiet when the loop is quiet and wild when it is hot.

**Energy normalisation (per tick):**

```
eRaw = clamp((gainToDb(loopEnv + 1e-9) - kEnergyFloorDb) / (kEnergyCeilDb - kEnergyFloorDb), 0, 1)   // -54 dB -> 0, -6 dB -> 1
e += (eRaw - e) * (eRaw > e ? aEUp : aEDown)          // 20 ms up, 400 ms down, coefficients at control rate fc
```

**Wander: Lorenz system, RK2 (midpoint), integrated per tick.** rho below 24.74 has stable fixed points (the system comes to rest), above it is chaotic; the loop energy sweeps it across that threshold and also sets the integration speed:

```
rho = kIntfRhoMin + e * (kIntfRhoMax - kIntfRhoMin)                  // 18 .. 45
dt  = (kIntfSpeedMin + e * (kIntfSpeedMax - kIntfSpeedMin)) / fc      // 6 .. 18 Lorenz time units per second -> per tick
f(x,y,z) = ( sigma*(y-x),  x*(rho - z) - y,  x*y - beta*z )           // sigma 10, beta 8/3
k1 = f(x,y,z); (xm,ym,zm) = (x,y,z) + 0.5*dt*k1; k2 = f(xm,ym,zm); (x,y,z) += dt*k2
x = clamp(x, -kLorenzBound, kLorenzBound); y likewise; z = clamp(z, 0, 2*kLorenzBound)
if !isfinite(x+y+z): x = 1, y = 1, z = 20                             // belt and braces; never observed with the clamps
wanderRaw = tanh((y - x) * kIntfWanderScale)                           // -1..1
wanderSlew += (wanderRaw - wanderSlew) * aSlew                          // one-pole at kIntfSlewHz = 40 Hz
wanderInc = (wanderSlew - wanderCur) * kInvControlBlock                 // per-sample ramp for the Time column
```

Why `y - x`: it is `dx/dt / sigma`, exactly zero at any fixed point, so a quiet loop produces zero modulation rather than a random DC pitch offset (using `x` directly would park at +-sqrt(beta(rho-1)) = +-6.7 and detune the delay by whichever lobe it fell into). Velocity is also spikiest at lobe switches, which is where the crackle feel comes from. Orbit rate at dt=18/fc is roughly 20-30 lobe cycles per second at full energy, 6-8 at rest-ish energies, so the wander band is ~5..50 Hz before the 40 Hz slew.

**Crackle: sparse ticks, audio rate.** A Poisson tick train whose density grows with the square of energy and whose amplitude follows the wander:

```
per tick:   density = kCrackleMaxRate * e*e                            // 0 .. 400 ticks/s
            crackleP = density / sr                                     // one division per tick
per sample: if rng.next01() < crackleP:
                crackleTarget = (rng.nextBipolar() >= 0 ? 1 : -1) * kCrackleAmp * (0.5 + 0.5*|wanderSlew|)
            crackleEnv += (crackleTarget - crackleEnv) * aCrackleAtk    // 0.3 ms rise: no instantaneous clock jump
            crackleTarget *= crackleDecay                               // 1.5 ms fall
            wanderCur += wanderInc
            out[i] = clamp(wanderCur + crackleEnv, -1, 1)
```

Control-rate destinations receive `wanderSlew` (plus nothing from crackle: `ctl[srcInterference] = wanderSlew`); the Time column receives the full audio array. `uiInterferenceLevel` = RMS of `out` over the block (cheap, one multiply-add per sample already in the loop).

Fallback for A/B during tuning (compile-time `kInterferenceModel = 1`): logistic map `xn = r*xn*(1-xn)`, `r = 2.9 + 1.1*e` iterated per tick, output `2*(xn - 0.5)` through the same slew. It rests at a fixed point below r=3 and goes chaotic above 3.57; the periodic windows in between produce audible fixed FM tones at fc/2, fc/4, which is why Lorenz is the default.

### 2.4 Drift (control rate, -1..1)

White noise -> two cascaded one-poles at `kDriftLpHz = 1.0` -> one-pole high-pass at `kDriftHpHz = 0.05` (so it never sits on one offset for minutes) -> analytic gain -> soft limit. Band of interest 0.05..2 Hz as the plan asks (12 dB/oct above 1 Hz means 2 Hz is -12 dB, the wobble is wow, not flutter).

```
prepare: aLp = 1 - exp(-2*pi*kDriftLpHz / fc); aHp = 1 - exp(-2*pi*kDriftHpHz / fc)
         statRms = sqrt(aLp) * 0.5 / sqrt(3)         // uniform white through two one-poles (teder's approximation)
         gain = kDriftTargetRms / statRms             // target RMS 0.3 -> +-1 is ~3 sigma
tick():  w = rng.nextBipolar(); lp1 += (w - lp1)*aLp; lp2 += (lp1 - lp2)*aLp
         hpState += (lp2 - hpState)*aHp; v = tanh((lp2 - hpState) * gain)
         last = cur target; inc = (v - cur) * kInvControlBlock; return v
fillAudio: out[i] = (cur += inc)
```

Drift reaches Time in two ways: the **always-on hidden trim** `kDriftTimeOct = 0.006` (outside the matrix, not scaled by Agitate, the "same settings never land twice" guarantee), and as an assignable matrix source (zero depth in v1).

## 3. Matrix

### 3.1 Data layout and scaling

`Routing::depth[4][7]`, bipolar -1..1. Each destination has a fixed full-scale in musical units:

| Dst | Unit | `dstScale` | Combine with knob | Clamp |
|---|---|---|---|---|
| Time | octaves of fs_chip | 2.0 | `L = Lsmooth + mod` (log2 domain) | `L in [log2 kFsChipHardMin, log2 kFsChipMax]`, summed mod first clamped to +-`kTimeModClampOct` = 2.0 |
| Filter | octaves | 4.0 | `cutoff = knobHz * exp2(mod)` | `[20 Hz, min(18 kHz, 0.45 sr)]` |
| Resonance | linear 0..1 | 0.5 | `knob + mod` | `[0, kResMax]` (filter designer's self-osc ceiling) |
| Decay | linear feedback gain | 0.5 | `knob + mod` | `[0, 1.15]` |
| Absorb | linear | 0.5 | `knob + mod` | `[0, 1]` |
| Blend | linear | 0.5 | `knob + mod` | `[0, 1]` |
| Strength | dB | 20.0 | `knobDb + mod` then dbToGain once per tick | `[0, 40] dB` |

Effective depth refreshed per tick: `effDepth[s][d] = depth[s][d] * dstScale[d] * macro` (Time additionally `* timeModDepth`). Unipolar sources (Agitation, Follower) with positive depth only push a destination upward from the knob, which is the hardware normal: set Filter dark, Agitation opens it.

### 3.2 Hero routes (v1, fixed constants in `ModMatrix::heroRouting()`)

| Route | Constant | Depth | Effect at Agitate = 1 |
|---|---|---|---|
| Agitation -> Filter | `kHeroAgitFilter` | +0.75 | cutoff rises up to +3 oct over the cycle |
| Interference -> Time | `kHeroIntfTime` | +0.50 | +-1 oct wander/crackle at Time Mod = 1 |
| Follower -> Decay | `kHeroFollowerDecay` | +0.30 | Decay rises up to +0.15 while you play (dig in near 1.0 and it tips into runaway, relaxes when you stop). Flip the sign to get a ducking delay; it is one constant. |

### 3.3 Agitate macro and Time Mod

```
macroTarget = pow(agitate01, kAgitateCurve)      // kAgitateCurve = 1.5: subtle in the lower half, wild at the top
macro += (macroTarget - macro) * aMacro          // 30 ms one-pole at control rate
timeModDepth likewise from timeMod01 (linear)
```

`macro` multiplies **every matrix cell** (`kMacroScalesAllRoutes = true`, one mental model: "how much modulation overall"); `timeModDepth` multiplies the Time column only, so Time Mod at 0 is the hardware's "nothing modulates the clock" regardless of Agitate. The hidden drift trim on Time is scaled by neither. Proposed parameter defaults (owner decides, per "defaults belong to the user"): Agitate 0.35, Time Mod 0.25, Agit Speed 0.5 Hz, Mode loop.

### 3.4 Control-rate destinations (per tick)

```
tick(s, k):
    refresh effDepth
    for d in {Filter..Strength}:
        mod = sum_s effDepth[s][d] * s.ctl[s]
        modSm[d] += (mod - modSm[d]) * aDest[d]                 // one-pole at kDestSmoothHz[d], computed for fc
    app.cutoffHz  = clamp(k.cutoffHz * exp2(modSm[Filter]), 20, min(18000, 0.45*sr))    // one exp2 per tick
    app.resonance = clamp(k.resonance + modSm[Resonance], 0, kResMax)
    app.decay.setTarget      (clamp(k.decay  + modSm[Decay],  0, kDecayMax))
    app.absorb.setTarget     (clamp(k.absorb + modSm[Absorb], 0, 1))
    app.blend.setTarget      (clamp(k.blend  + modSm[Blend],  0, 1))
    app.strengthGain.setTarget(dbToGain(clamp(k.strengthDb + modSm[Strength], 0, 40)))   // one pow per tick
```

Cutoff and resonance are set on the SVF once per sub-block (coefficient update = one `tan` per tick, as in the sibling). The four gain destinations are `ControlRamp`s read per sample inside the loop, so a 32-sample control update never becomes a gain step; the ramp always spans exactly 32 samples because ticks are exactly 32 samples apart, which is why the increment is `* kInvControlBlock` and not a division. Knob smoothers stay in the engine (cutoff knob as `SmoothedValue<float, Multiplicative>` so its own sweeps are log-domain; the rest linear, 30 ms) and are `skip(n)`'d per sub-block.

`kDestSmoothHz`: Filter 30, Resonance 30, Decay 40 (keeps the follower's 5 ms snap), Absorb 20, Blend 20, Strength 20. The step per tick after a full-scale jump is at most `1 - exp(-2*pi*40/1500) = 15%` of the jump, and gain destinations ramp within the tick anyway.

### 3.5 UI feeds

`uiDestMod[d] = modSm[d] / dstScale[d]` (-1..1) for the knob rings; `uiTimeModOct = timeModMean` (block mean of the per-sample Time modulation, for the Time knob arc); `uiLoopEnergy = e` for the ember; `uiModSource[4]` last control values.

### 3.6 Why the feedback path cannot blow up

The only modulation feedback loop is: loop signal -> `loopEnv` (3 ms / 150 ms) -> `e` (20 ms / 400 ms, clamped 0..1) -> (rho, dt, density) -> Lorenz (state hard-clamped, output through tanh, then 40 Hz slew) and crackle (amplitude <= kCrackleAmp, output hard-limited) -> `+-1` -> `* depth * dstScale * macro * timeModDepth` (all <= 1 except dstScale 2) -> summed Time mod clamped to +-2 oct -> `L` clamped to the core's hard fs range -> the loop. Every arrow maps a bounded set to a bounded set, so no state can diverge whatever the loop does; the worst possible outcome is a limit cycle, not growth.

Follower and Agitation have no path back from the loop (Follower reads the input; gate mode is triggered by the input follower, not the loop). Drift is free-running. Decay > 1 is bounded by the loop saturator independently of Time, so the amplitude branch of the loop is contractive at the ceiling: `E <= E_sat` whatever the modulation does.

What the loop actually does dynamically: hot loop -> e high -> fast, deep clock modulation -> the delayed content smears spectrally and the reconstruction LPF / Absorb tilt remove the smeared HF -> loop energy drops -> e falls over ~400 ms -> modulation calms -> energy rebuilds. That is negative feedback through three cascaded low-passes (150 ms envelope, 400 ms slew, 40 Hz output slew), so the loop bandwidth is under ~3 Hz and the observable behaviour is a slow breathing of 0.5..3 s period rather than chatter. The three things that keep it "musical chaos" rather than "bad chaos" are (1) continuity: soft retrigger, ramped control values, 0.3 ms crackle rise, box-averaged Agitation; (2) bandwidth: each source owns its slew so only Agitation can reach audio rate, and only when asked; (3) headroom: all destination clamps are musical ranges (cutoff 20 Hz..18 kHz, decay <= 1.15, Time inside the core's clock range) and the macro scales everything down with one knob. Test §6.6 measures the breathing period and the energy ceiling under worst-case settings.

## 4. Audio-rate Time path (inside `ModMatrix`)

Work in `L = log2(fs_chip)`. The knob is linear in this domain, so no `pow` is needed:

```
per block (engine):
    free:  Ltarget = kLogFsMax - time01 * kTimeSpanOct          // kTimeSpanOct = log2(200000/1500) = 7.06
    sync:  tau = clamp(beats * 60 / bpm, kTauMin, kTauMax); Ltarget = log2(kMemorySamplesTotal / tau)   // one div + one log2 per block
    matrix.setTimeTargetLog2(clamp(Ltarget, kLogFsMin, kLogFsMax))

prepare: aTime = 1 - exp(-1 / (kTimeSmoothMs*1e-3 * sr));  logFsHost = log2(sr)

fillTimeRatio(ratio, s, n):
    dT = effDepth[*][dstTime]           // already includes dstScale, macro, timeModDepth
    for i in 0..n-1:
        lSmooth += (lTarget - lSmooth) * aTime                              // 20 ms one-pole, log domain: sweeps glide in pitch
        mod = dT[srcAgitation]*s.audio[srcAgitation][i] + dT[srcFollower]*s.audio[srcFollower][i]
            + dT[srcInterference]*s.audio[srcInterference][i] + dT[srcDrift]*s.audio[srcDrift][i]
        mod = clamp(mod, -kTimeModClampOct, kTimeModClampOct) + kDriftTimeOct * s.audio[srcDrift][i]
        L = clamp(lSmooth + mod, kLogFsHardMin, kLogFsMax)
        ratio[i] = exp2f(L - logFsHost)                                      // fs_chip / fs_host, chip samples per host sample
        acc += mod
    timeModMean = acc / n   (one division per sub-block, UI only)
```

The PT cores consume `ratio[i]` directly: write accumulator `+= ratio[i]`, emitting chip samples while it exceeds 1; the read position is the write position minus the fixed memory length, so read and write share the same accumulator and there is no reciprocal anywhere. `exp2f` is ~8 ns on arm64 and shared by all three stages (0.04% of a core at 48 kHz); if a profile ever objects, swap in `fastExp2(x)`: `xi = floor(x); f = x - xi; p = 1 + f*(0.6931472 + f*(0.2402265 + f*(0.0555041 + f*0.0096181)))`, scale by `2^xi` via `ldexp` (relative error ~6e-6, 0.01 cent). Note on the sound: a variable-clock delay pitches its output by `fs(now)/fs(then)`, i.e. by how much `L` changed over one delay time, not by the offset; static offsets are tape-length changes, only movement is pitch. Audio-rate Agitation into Time therefore gives FM sidebands; drift gives wow proportional to how much it moves within one delay period.

## 5. Tunable constants (`Source/dsp/ModConstants.h`, `namespace modk`)

| Name | Start | Audible effect |
|---|---|---|
| `kControlBlock` / `kInvControlBlock` | 32 / 1/32 | control-rate resolution; smaller = smoother fast filter mod, more tan() per second |
| `kTimeSmoothMs` | 20 | knob/sync glide on Time; longer = more tape warble on sweeps |
| `kTimeSpanOct` | 7.06 | derived from fs 200 kHz..1.5 kHz |
| `kLogFsMax`, `kLogFsMin`, `kLogFsHardMin` | log2(200000), log2(1500), log2(1000) | HardMin lets modulation push one third past the knob floor into extra crust |
| `kTimeModClampOct` | 2.0 | max total pitch excursion from all sources stacked |
| `kDriftTimeOct` | 0.006 | always-on wow (+-7 cents peak per side, ~14 cents worst case at long Time) |
| `kAgitSpeedMin` / `kAgitSpeedMax` | 0.016 / 1000 Hz | generator range |
| `kAngleMin` | 0.01 | extreme rise/fall ratio 1/99 |
| `kAgitMinSegmentMs` | 0.5 | shortest ramp segment; prevents a one-sample cliff into Time |
| `kGateOnLevel` / `kGateOffLevel` | -30 / -36 dBFS | gate sensitivity and hysteresis on the follower |
| `kGateHoldMs` | 20 | double-trigger guard on picked transients |
| `kFollowerAttackMs` / `kFollowerReleaseMs` | 5 / 100 | plan values; snap vs sag of Follower->Decay |
| `kFollowerFullScale` | 0.5 | input level that reads 1.0; lower = more sensitive |
| `kEnergyFloorDb` / `kEnergyCeilDb` | -54 / -6 | where Interference wakes up / saturates; re-check floor against the core's idle hiss at longest Time (aim 6 dB above it) |
| `kEnergyUpMs` / `kEnergyDownMs` | 20 / 400 | how fast Interference reacts / how long it lingers; DownMs sets the breathing period floor |
| `kLoopEnvAttackMs` / `kLoopEnvReleaseMs` | 3 / 150 | loop envelope (engine side) |
| `kIntfRhoMin` / `kIntfRhoMax` | 18 / 45 | rest below 24.74; higher max = more violent lobe switching |
| `kIntfSpeedMin` / `kIntfSpeedMax` | 6 / 18 units/s | wander tempo at quiet / hot; higher = flutter, lower = wobble |
| `kLorenzSigma` / `kLorenzBeta` | 10 / 8/3 | classic; leave |
| `kLorenzBound` | 60 | hard state clamp; never reached in normal operation |
| `kIntfWanderScale` | 0.05 | wander output level before tanh (y-x spans ~+-20) |
| `kIntfSlewHz` | 40 | wander smoothness; lower = smoother wobble, higher = grittier |
| `kCrackleMaxRate` | 400 /s | static density at full energy |
| `kCrackleAmp` | 0.6 | tick size relative to full modulation |
| `kCrackleAttackMs` / `kCrackleDecayMs` | 0.3 / 1.5 | tick shape; longer decay = "burble", shorter = "static" |
| `kInterferenceModel` | 0 (Lorenz) | 1 = logistic map for A/B |
| `kDriftLpHz` / `kDriftHpHz` | 1.0 / 0.05 | drift band |
| `kDriftTargetRms` | 0.3 | how often drift approaches its +-1 rails |
| `dstScale[7]` | 2, 4, 0.5, 0.5, 0.5, 0.5, 20 dB | full-depth excursion per destination |
| `kHeroAgitFilter` / `kHeroIntfTime` / `kHeroFollowerDecay` | 0.75 / 0.5 / 0.3 | the three v1 routes |
| `kAgitateCurve` | 1.5 | macro taper; 1.0 = linear |
| `kMacroSmoothMs` | 30 | macro and Time Mod knob smoothing |
| `kDestSmoothHz[7]` | -, 30, 30, 40, 20, 20, 20 | control destination smoothing |
| `kDecayMax`, `kResMax` | 1.15, filter designer's | destination clamps |

## 6. EngineTest scenarios (numbers, not ears)

Sources are instantiated directly (they are in `ENGINE_SOURCES`), engine-level tests go through `DybbukEngine::process` with `setSeedsForTests`. Reuse the sibling's `check(what, ok, detail)` printer. Helper `demodPitch(out, fHz, windowSec)`: multiply by cos/sin at `fHz`, 20 Hz one-pole, `atan2`, unwrap; pitch deviation (cents) = `1200*log2(1 + dphi/dt / (2*pi*fHz))`. It resolves ~0.1 cent on the lo-fi wet path where zero crossings cannot.

**6.1 `agitation`** (direct, sr 48k, per sample)
- 1 Hz, angle 0.5, loop, 10 s: period from upward 0.5-crossings = 1.000 +- 0.001 s; max 1.000 +- 0.001, min 0 +- 0.001, mean 0.500 +- 0.005; time from min to max = 0.500 +- 0.001 s.
- 1 Hz, angle 0.1: rise = 0.108 +- 0.002 s, fall = 0.892 +- 0.002 s; angle 0.9 mirrored; mean still 0.5 (linear AD area invariant).
- 0.016 Hz, 130 s: period 62.5 +- 0.1 s.
- 1000 Hz, angle 0.9: crossings give 1000 +- 1 Hz; shape forced to 50/50 by `kAgitMinSegmentMs` (rise 0.5 +- 0.02 ms); block means (32-sample) have std < 0.03 (the filter sees ~0.5, not aliasing). Same run at 44.1 k and 96 k: period within 0.1%.
- gate, 4 Hz: synthetic follower env steps to 0.1 at t=0.1 s. Output starts rising within 1 sample of the step, returns to exactly 0 by t = 0.1 + 0.25 + 1 ms and stays 0 to t=1 s; env drop below 0.0158 then a second step at 1.2 s retriggers. Retrigger mid-cycle (second step at 0.18 s without re-arm should NOT trigger; with a dip below off-level between them it should): max per-sample |delta y| over the whole run <= 1.05 * (1/(faEff*periodSamples)) proves the soft retrigger never jumps.

**6.2 `follower`** (direct): 1 kHz square, amplitude 0.25 from 0.1 to 0.6 s, then 0. `env` reaches 0.25*(1-e^-1) = 0.158 at 0.1 s + 5.0 +- 0.3 ms; falls to 0.25*e^-1 = 0.092 at 0.6 s + 100 +- 3 ms; `value01` settles at 0.50 +- 0.01. Same at 96 k within the same tolerances.

**6.3 `drift`** (direct, fc = 1500): seeds A and B, 120 s: RMS in [0.22, 0.38] each; max |v| <= 1.0; max |a-b| > 0.5; lag-1 s autocorrelation > 0.5 (slow); mean |v| over the last 60 s < 0.15 (HP works); zero crossings per second in [0.2, 2.0].

**6.4 `drifttime`** (engine): 440 Hz sine, Time 0.75 (~1 s), Decay 0, Blend 100%, Agitate 0, Time Mod 0, 30 s, ignore first 3 s. `demodPitch` deviation of the wet output: max |dev| < 15 cents, RMS in [0.5, 6] cents. Seeds A vs B: max sample |diff| > 1e-3 after 3 s (not bit-identical), yet both satisfy the bound. With `setDriftDepthForTests(0)` (test-only override of `kDriftTimeOct`): RMS < 0.1 cent, proving both the measurement floor and that drift is the cause.

**6.5 `interference`** (engine): Decay 0.95, Filter 4 kHz, Agitate 1, Time Mod 0 (measure the source, not its effect), input = 1 s burst / 3 s silence x 20. Per 250 ms window collect `uiLoopEnergy` and `uiInterferenceLevel`: Pearson r > 0.7; windows with energy < 0.05 have level < 0.03; windows with energy > 0.6 have level > 0.15; level never > 1; after the last burst, level falls below 0.03 within 2 s (rests, no self-excitation with Time Mod 0). Extra: e=1 held for 10 min at 44.1 k and 192 k with the direct class: all states finite, |x| never hits `kLorenzBound`.

**6.6 `generative`** (the Phase 3 pass/fail): no input, Decay 1.1, Time 0.6, Filter 2 kHz, Resonance 0.3, Absorb 0.3, Blend 100%, Agitate 0.7, Time Mod 0.5, Agit Speed 0.2 Hz, 90 s.
- Alive: output RMS in every 5 s window after 10 s is between -40 and -3 dBFS; peak sample < 0.99 (saturator, never digital clip); all finite.
- Non-repeating: 50 Hz RMS envelope, normalised autocorrelation for lags 2..40 s: max < 0.6.
- Evolving: spectral centroid per 5 s window (FFT 4096, averaged) has std/mean > 0.05; at least 3 distinct 5 s windows where `uiLoopEnergy` mean differs by > 0.15 from the run mean.
- Seed A vs B: per-window RMS correlation < 0.9, sample-wise different after 5 s.
- Breathing: dominant autocorrelation peak of `e` (lags 0.3..10 s) between 0.5 and 5 s, i.e. slow modulation of modulation, no chatter.

**6.7 `worstcase`** (stability): Decay 1.15, Agitate 1, Time Mod 1, Time 0.9, Filter 18 kHz, Resonance max, 1 s full-scale noise burst at t=1, then silence, 120 s: finite; peak < 0.99; `uiLoopEnergy` mean over 100..120 s within +-0.15 of 40..60 s (bounded, stationary); max per-sample |delta L| < 0.05 oct except inside crackle rises (report the count of samples above 0.05; must be < 0.5% of samples). Repeat at block sizes 1, 33, 128, 2048 with the same seed: control ticks counted = floor(samples/32) in all cases and the four outputs match to 1e-6 (tick alignment is block-size independent).

**6.8 `timemod`** (audio-rate FM reaches the clock): Agitation temporarily routed to Time via a test routing (depth 0.25 = +-0.5 oct), 1 kHz agitation, 440 Hz sine, Decay 0, Time 0.2: FFT of wet shows sideband energy at 440 +- 1000 Hz at least 20 dB above the same bins with Time Mod 0; with the route back on Filter only, those bins are within 3 dB of the Time Mod 0 case.

## 7. Risks and mitigations

1. **Interference character is a taste call** (crackle vs wander balance, energy thresholds). Every knob of it is a `modk` constant; the logistic-map A/B is one define; §6.5 keeps the correlation property while tuning by ear.
2. **Control-rate stepping on the filter with fast Agitation (50..300 Hz).** Box averaging removes aliasing but also removes the wobble the user asked for at those speeds. If the sweep sounds too tame there, drop `kControlBlock` to 16 for the SVF only, or add per-sample cutoff updates behind `kFilterPerSample` (one tan per sample, ~0.3% CPU).
3. **Lorenz numerics.** RK2 at dt <= 0.013 with rho <= 45 is well inside stability, and the hard clamps plus the finite check make the worst case a stuck orbit, never NaN. 6.5's 10-minute soak at both extreme sample rates is the gate.
4. **Future user routes can create new feedback:** Follower -> Strength positive is gain-on-gain (bounded by the 40 dB clamp and the drive tanh, but it pumps). When the full matrix ships, cap that cell's positive depth to 0.5 or document it; no v1 route has this issue.
5. **Energy floor vs core noise.** If the PT core's idle hiss at longest Time sits above -54 dBFS, Interference never rests with Decay < 1; re-tune `kEnergyFloorDb` after the core is voiced (6.5's "rests within 2 s" check catches it).
6. **Doppler misunderstanding.** Static Time offsets do not shift pitch; movement does. Tests use phase demodulation, and drift's audible size therefore grows with delay time (bounded 14 cents by `2*kDriftTimeOct`).
7. **Block size 1 / odd sizes / sample-rate changes.** Persistent `samplesUntilTick` keeps ticks at 32 samples; every coefficient derives from `sr` or `fc`; 6.7 asserts block-size invariance and 6.1/6.2 check 44.1/96 k.
8. **Determinism.** Production seeds from the system RNG per instance; tests inject one master seed. Agitation phase, Lorenz state and drift filters are performance state, deliberately not saved (a reload starts fresh, like powering the hardware on).
9. **PT core ratio range.** Modulation can push `ratio` to `kFsChipMax/44100 = 4.5`; the core's write loop must tolerate up to `kMaxRatio = 8` chip samples per host sample; share the constant.
10. **Gate mis-triggers on hot or noisy sources.** -30 dBFS with 6 dB hysteresis and 20 ms hold-off; if a real guitar double-fires, expose `kGateOnLevel` as a hidden trim before adding a parameter.
11. **Parameter readout for Agit Speed** below 1 Hz reads "0.02 Hz" under the house Hz rule; if that reads badly on the readout strip, give the parameter a string function that shows the period ("62 s") below 1 Hz. Not a DSP change.