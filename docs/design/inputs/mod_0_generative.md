# Dybbuk modulation system design (plan 2.5), angle: the generative pass/fail

Everything below assumes the loop topology of plan section 2 (three series PT cores at a shared `fs_chip`, SVF, Absorb, saturator, Decay). Where this design needs something from the loop it is stated as an interface contract. All rates derive from seconds so behaviour is identical at 44.1/48/96/192 kHz. Nothing here allocates outside `prepare()`.

Two conventions used throughout:

- **Control tick** = every `modk::controlInterval = 32` host samples, counted continuously across block boundaries (a countdown that persists between `process()` calls, so block size 1, 100 or 2048 all see identical tick timing). Tick period `ctrlDt = 32 / sr` seconds.
- **Source ranges**: Agitation 0..1 (unipolar, like the hardware's 0..6 V), Follower 0..1 (unipolar), Interference -1..1 (bipolar, tanh-bounded), Drift -1..1 (bipolar, tanh-bounded, RMS ~ 1/3).

---

## 1. Classes and files

All under `Source/dsp/`, one class per pair, all added to `ENGINE_SOURCES` in CMake so `EngineTest` can instantiate each one directly.

| File | Class | Runs at | Owns / allocates |
|---|---|---|---|
| `ModTuning.h` | (constants only; namespaces `agitk`, `folk`, `intk`, `driftk`, `modk`) | n/a | nothing. One header so the tuning pass touches one file (same idea as `transient`/`morphk` in Infinite Sustainer, gathered) |
| `FastMath.h` | `inline float fastExp2 (float)` | audio | nothing |
| `Agitation.{h,cpp}` | `Agitation` | **per sample** (6 flops) | POD only |
| `InputFollower.{h,cpp}` | `InputFollower` | `push()` per sample, `tick()` per control tick | POD only |
| `Interference.{h,cpp}` | `Interference` | per control tick | POD only |
| `Drift.{h,cpp}` | `Drift` | per control tick | POD only |
| `ModMatrix.{h,cpp}` | `ModMatrix`, `ModDepths`, `DestMod`, enums | tick + one inline per-sample call for Time | POD only |
| (engine, whatever replaces `ExampleEngine`: call it `DybbukEngine`) | orchestration, seeding, UI atomics | | `juce::HeapBlock<float> chipRatio` sized `maxBlock` in `prepare()`; that is the only heap in this subsystem |

### 1.1 Header sketches

```cpp
// Agitation.h : looping / gated Attack-Decay function generator, 0..1
class Agitation
{
public:
    enum class Mode { loop, gate };
    void prepare (double sampleRate);           // also resets
    void reset();                               // phase 0, out 0; gate mode idles
    void setSpeedHz (float hz);                 // per block, agitk::speedMinHz..speedMaxHz
    void setAngle (float rise01);               // per block, clamped agitk::angleMin..1-angleMin
    void setMode (Mode m);                      // loop->gate finishes the current cycle then idles; gate->loop resumes
    void trigger();                             // gate mode: restart the RISE from the current level (no step)
    float tick() noexcept;                      // per sample, returns 0..1
    float value() const noexcept { return out; }
    bool isRunning() const noexcept { return running; }
private:
    double phase = 0.0, inc = 0.0;              // double: 0.016 Hz at 192 kHz is inc = 8e-8
    float rise = 0.5f, riseInv = 2.0f, fallInv = 2.0f, out = 0.0f;
    Mode mode = Mode::loop; bool running = true;
    double sr = 48000.0;
};
```

```cpp
// InputFollower.h : envelope of the post-Strength mono signal, 0..1, plus the onset detector Agitation's gate mode keys on
class InputFollower
{
public:
    void prepare (double sampleRate, int controlInterval);
    void reset();
    void setTimes (float attackMs, float releaseMs);      // defaults folk::attackMs / folk::releaseMs
    inline void push (float x) noexcept                    // per sample
    {
        const float a = std::abs (x);
        env += (a > env ? attackCoeff : releaseCoeff) * (a - env);
    }
    bool tick() noexcept;                                  // per control tick: updates baseline/shaped value, true on an onset
    float envelope() const noexcept { return env; }        // raw linear peak-ish
    float value() const noexcept { return shaped; }        // 0..1, min(1, sqrt(env * folk::sensitivity))
private:
    float env = 0.f, baseline = 0.f, shaped = 0.f;
    float attackCoeff = 1.f, releaseCoeff = 1.f, baselineCoeff = 1.f;
    int refractory = 0, refractoryTicks = 0;
};
```

```cpp
// Interference.h : chaos derived from the loop state, -1..1
class Interference
{
public:
    void prepare (double sampleRate, int controlInterval);
    void reset (juce::uint32 seed);                        // seeded start point ON the attractor, never (0,0,0)
    float tick (float loopEnergy, float loopSample) noexcept; // per control tick
    float value() const noexcept { return out; }
    float drive() const noexcept { return s; }             // 0..1, what the loop energy resolved to (UI / tests)
private:
    float x = 1.f, y = 1.f, z = 20.f;                      // Lorenz state
    float heat = 0.f, s = 0.f;                             // slow memory of loop energy; current drive
    float dc = 0.f, slewed = 0.f, out = 0.f;
    float tickScale = 1.f, heatCoeff = 0.f, dcCoeff = 0.f, slewCoeff = 0.f, invEnvRef = 4.f;
    juce::uint32 seed = 1;
};
```

```cpp
// Drift.h : two-band filtered random for analog clock wander, -1..1, RMS ~ driftk::rms
class Drift
{
public:
    void prepare (double sampleRate, int controlInterval);
    void reset (juce::uint32 seed);
    float tick() noexcept;                                 // per control tick
    float value() const noexcept { return out; }
private:
    juce::uint32 rng = 0x9e3779b9u;
    float slow1 = 0.f, slow2 = 0.f, wow1 = 0.f, wow2 = 0.f, out = 0.f;
    float slowAlpha = 0.f, wowAlpha = 0.f, slowGain = 0.f, wowGain = 0.f;
};
```

```cpp
// ModMatrix.h
enum class ModSource : int { agitation, follower, interference, drift, count };   // Tones sub-osc joins as a 5th column in Phase 4
enum class ModDest   : int { time, cutoff, resonance, decay, absorb, blend, strength, count };

struct ModDepths                                           // bipolar -1..1 per cell; plain struct so Params can carry it
{
    float d[(int) ModSource::count][(int) ModDest::count] {};
    float& at (ModSource s, ModDest t) { return d[(int) s][(int) t]; }
    float  at (ModSource s, ModDest t) const { return d[(int) s][(int) t]; }
    static ModDepths heroDefaults();                       // the three v1 routes, see 3.3
};

struct DestMod                                             // offsets in MUSICAL units, already macro-scaled and slewed
{
    float timeOct = 0.f;      // octaves of fs_chip (control-rate part only; see 4)
    float cutoffOct = 0.f;    // octaves
    float resonance = 0.f;    // 0..1 domain
    float decay = 0.f;        // linear feedback gain
    float absorb = 0.f;       // 0..1 domain
    float blend = 0.f;        // 0..1 domain
    float strengthDb = 0.f;   // dB
};

class ModMatrix
{
public:
    void prepare (double sampleRate, int controlInterval);
    void reset();
    void setDepths (const ModDepths& depths);              // per block
    void setMacros (float agitate01, float timeMod01);     // per block
    void setDriftTimeOct (float oct);                      // hidden always-on trim (driftk::timeDepthOct), Params can zero it for tests
    // per control tick: agitationMean = box mean of the last interval's per-sample values
    const DestMod& tick (float agitationMean, float follower, float interference, float drift) noexcept;
    // per sample: the whole Time offset for this sample, octaves
    inline float timeOctForSample (float agitationSample) noexcept
    {
        timeCtrl += timeCtrlInc;                                   // linear ramp of the control-rate part between ticks
        return timeCtrl + timeAgitGain * (2.0f * agitationSample - 1.0f);  // Agitation is CENTRED on the Time column (pitch axis)
    }
    const DestMod& current() const noexcept { return slewed; }
private:
    ModDepths depths; float agitate = 0.f, timeMod = 0.f, driftTimeOct = 0.f;
    DestMod target, slewed; float ctrlSlew = 1.f;
    float timeCtrl = 0.f, timeCtrlInc = 0.f, timeAgitGain = 0.f, invInterval = 1.f / 32.f;
};
```

Engine-side additions (whatever the engine class is called):

```cpp
struct Params {   // additions to the existing per-block snapshot
    float agitate01 = 0.35f, agitSpeedHz = 0.25f; bool agitGate = false;
    float timeMod01 = 0.25f;
    // Hidden / tuning: filled with defaults by the processor, overridable by EngineTest
    ModDepths depths = ModDepths::heroDefaults();
    float driftTimeOct = driftk::timeDepthOct;
    float agitAngle = agitk::angle;
};
void seedForTests (juce::uint32 seed);   // before prepare(); otherwise prepare() seeds from the clock ^ this
// Engine -> UI (relaxed atomics, one store per block, polled at 30 Hz)
std::atomic<float> uiLoopEnergy, uiAgitation, uiFollower, uiInterference;
std::atomic<float> uiModOffset[(int) ModDest::count];    // -1..1 of each destination's range, for the knobs' modulated state
```

Contract with the loop (`TimeFilterLoop`/PT cores):
- consumes `const float* chipRatio` per sample (= `fs_chip / fs_host`), never a delay time in seconds, never divides;
- maintains `loopEnergy()` = one-pole rectified follower on the post-saturator loop signal, tau `modk::loopEnergyMs = 10` (this is also the ember value) and `lastLoopSample()`;
- injects chip noise + quantisation noise inside the loop (Phase 1 constants; see risk 7). The generative milestone cannot self-start without it.

---

## 2. Sources

### 2.1 Agitation (0..1)

Angle `a` = rise fraction of the cycle. Plan: 1/99 .. 50/50 .. 99/1, fixed at `agitk::angle = 0.5` for v1.

```
setSpeedHz(hz):   inc = clamp(hz, speedMinHz, speedMaxHz) / sr        (double)
setAngle(a):      rise = clamp(a, angleMin, 1 - angleMin); riseInv = 1/rise; fallInv = 1/(1 - rise)   (per block, divisions allowed here)

tick():
    if (!running) return out = 0
    phase += inc
    if (phase >= 1.0):
        if (mode == loop)  phase -= 1.0                     (inc <= 1000/44100 = 0.023, one subtraction suffices)
        else             { running = false; phase = 0; return out = 0 }
    lin = phase < rise ? phase * riseInv : (1 - phase) * fallInv      // 0 -> 1 -> 0, straight segments
    out = lin + agitk::curve * (lin*lin - lin)                         // curve 0 = linear; 0.25 rounds the corners slightly (exp-ish rise, log-ish fall)
    return out

trigger():   // gate mode only. Restart the rise FROM THE CURRENT LEVEL so a retrigger mid-cycle never steps
    running = true
    c = agitk::curve
    linNow = c > 0 ? (-(1-c) + sqrt((1-c)^2 + 4*c*out)) / (2*c) : out     // invert the curve; sqrt only at trigger time
    phase = linNow * rise
```

Mode semantics: `loop` free-runs and wraps; `gate` idles at 0 until `trigger()`, runs one A-D cycle and idles again. Switching loop -> gate finishes the current cycle; gate -> loop sets `running = true` and continues from the current phase, so a mode flip never clicks. Trigger source = `InputFollower::tick()` returning true (2.2). Host sync later: the only input is `setSpeedHz`, so sync is `hz = bpm / (60 * beatsPerCycle)` computed in the processor, nothing changes here.

Speed parameter: genuine log range 0.016..1000 Hz (`logRange` helper from Infinite Sustainer, not `setSkewForCentre`). Readout: under 1 Hz show the period ("62.5 s", one decimal), 1..100 Hz two decimals, above 100 Hz integers, unit baked in and label empty (the house exception Infinite Sustainer's Sweep Rate already made; it satisfies the "readouts for musicians" rule).

Why per-sample: the generator goes to 1 kHz and the Time column needs it at audio rate for FM clang. The control-rate columns receive the **box mean** of the last 32 samples (`agitSum * invInterval`), which is what a slewed CV into a cutoff would do; above ~400 Hz this collapses toward the cycle's mean, i.e. Agitation into Filter goes from "sweep" to "buzz" to "static offset", the same progression the hardware exhibits when a slope generator outruns a CV input's slew.

### 2.2 Input Follower (0..1)

Asymmetric one-pole on |x| of the post-Strength mono signal (so Strength drive is heard by the follower as on hardware, where the follower sits after the preamp). Falling edges are exact one-poles; rising edges only track while |x| > env, so the effective attack on a sine is a little longer than the constant (test allows for that).

```
prepare:  attackCoeff  = 1 - exp(-1 / (folk::attackMs  * 1e-3 * sr))       // 5 ms
          releaseCoeff = 1 - exp(-1 / (folk::releaseMs * 1e-3 * sr))       // 100 ms
          baselineCoeff = 1 - exp(-ctrlDt / (folk::baselineMs * 1e-3))     // 300 ms, tick rate
          refractoryTicks = round(folk::refractoryMs * 1e-3 / ctrlDt)      // 60 ms

push(x):  see header (per sample)

tick():   // control rate
    shaped = min(1, sqrt(env * folk::sensitivity))          // sensitivity 4: -12 dBFS input reads full; sqrt so quiet playing still moves things
    onset = refractory == 0
            && env > folk::gateOnLin                        // -30 dBFS = 0.0316
            && env > baseline * folk::onsetRatio            // 1.8x (+5 dB) above the recent baseline: a new attack, not a sustain
    baseline += baselineCoeff * (env - baseline)            // updated AFTER the test so the onset sees the lagging baseline
    if (refractory > 0) --refractory
    if (onset) refractory = refractoryTicks
    return onset
```

Limitation to record in IDEAS: legato re-attacks quieter than +5 dB over the ring-out do not retrigger. Infinite Sustainer's HF-band transient detector is the upgrade if it is missed.

### 2.3 Interference (-1..1): the generative ingredient

Choice: a **Lorenz system integrated by forward Euler at control rate**, with the loop energy driving three things at once: the chaos parameter rho (calm loop = stable spiral that rings down to a fixed point = silence after DC removal; hot loop = full chaos), the integration rate (hotter = faster burble), and a small state perturbation by the loop audio itself (so the trajectory's phase belongs to the audio, not to the clock). A slow "heat" integrator gives it memory on the 5-10 s scale, which is what turns stationary chaos into something that evolves. Logistic map was rejected: it is memoryless, its amplitude jumps between periodic windows as r crosses thresholds, and it has no natural spectral shape; Lorenz produces the 5-40 Hz irregular oscillation with intermittent lobe switching that reads as "burble/crackle" when it hits a delay clock.

```
constants:  sigma = 10, beta = 8/3
prepare:    tickScale = ctrlDt * 1500                       // dt below is defined per 1/1500 s tick so 44.1k..192k behave identically
            heatCoeff = 1 - exp(-ctrlDt / intk::heatSeconds)          // 6 s
            dcCoeff   = 1 - exp(-2*pi*intk::dcHz * ctrlDt)            // 0.3 Hz
            slewCoeff = 1 - exp(-ctrlDt / (intk::slewMs * 1e-3))      // 4 ms
            invEnvRef = 1 / intk::envRef                              // 0.25 (-12 dBFS loop level = fully wild)

reset(seed):  u1,u2,u3 from xorshift32(seed) in 0..1
              x = 1 + 4*u1;  y = 1 + 4*u2;  z = 20 + 5*u3     // somewhere on the attractor; never the unstable origin
              heat = dc = slewed = out = 0

tick(loopEnergy, loopSample):
    s  = min(1, loopEnergy * invEnvRef)                                  // drive 0..1
    heat += heatCoeff * (s - heat)                                       // long memory of how hot the experiment has been
    rho = intk::rhoCalm + (intk::rhoWild - intk::rhoCalm) * s + intk::heatRho * heat   // 14 .. 30 (+8 when it has been hot a while); chaos begins at 24.74
    dt  = min(intk::dtMax, intk::dtBase * (1 + intk::rateGain * s) * tickScale)        // 0.006 .. 0.015 per tick; dtMax 0.02 keeps Euler stable at rho <= 40
    x  += intk::inject * s * loopSample                                  // 0.15: the audio nudges the state; the chaos is OF the loop, not next to it
    dx = sigma * (y - x);   dy = x * (rho - z) - y;   dz = x*y - beta*z
    x += dt*dx;  y += dt*dy;  z += dt*dz
    if (!isfinite(x+y+z)) reset(seed)                                    // belt and braces; cannot happen at dtMax 0.02, rho <= 40
    raw = x * intk::outScale                                             // 1/16: std(x) ~ 8 at rho 28 -> ~0.5
    dc += dcCoeff * (raw - dc);  hp = raw - dc                           // removes the fixed-point offset: calm -> exactly 0 output
    slewed += slewCoeff * (hp - slewed)
    out = tanh (slewed + intk::crackle * (hp - slewed))                  // crackle 0.15 lets some raw per-tick jitter through (the "static"); tanh bounds
    return out
```

Behaviour by regime: loopEnergy 0 -> rho 14, the state spirals into C+ or C- with decaying oscillation (a 1-2 s "ring-down" of interference after the loop dies: audibly the crackle fades rather than cuts), output -> 0. loopEnergy >= ~0.17 (s >= 0.68) -> rho > 24.74, chaotic, amplitude RMS ~0.45, dominant 8-30 Hz. Sustained hot loop -> heat rises -> rho up to 38, faster and wider. Interference's own dynamics never repeat (positive Lyapunov exponent ~0.9 per time unit, i.e. e^8 per real second at 9 time units/s).

### 2.4 Drift (-1..1)

Teder's tube-drift idiom (two cascaded one-poles over uniform white, gain from the exact stationary RMS `sigma_white * sqrt(alpha) / 2`, sigma_white = 1/sqrt(3)), extended to two bands: a thermal band and a wow band, because a single corner reads as an LFO once you know it is there.

```
prepare:  slowAlpha = 1 - exp(-2*pi*driftk::slowHz * ctrlDt)     // 0.08 Hz
          wowAlpha  = 1 - exp(-2*pi*driftk::wowHz  * ctrlDt)     // 0.6 Hz
          statRms(alpha) = sqrt(alpha) * 0.5 / sqrt(3)
          slowGain = driftk::rms * (1 - driftk::wowMix) / statRms(slowAlpha)
          wowGain  = driftk::rms * driftk::wowMix       / statRms(wowAlpha)
tick():
    rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5;  white = (int32) rng * (1 / 2^31)
    slow1 += slowAlpha * (white - slow1);  slow2 += slowAlpha * (slow1 - slow2)
    wow1  += wowAlpha  * (white - wow1);   wow2  += wowAlpha  * (wow1  - wow2)
    return out = tanh (slow2 * slowGain + wow2 * wowGain)          // RMS ~ driftk::rms = 1/3, |out| <= 1
```

Always-on use: `driftTimeOct = driftk::timeDepthOct * drift` is added to the Time column regardless of Agitate/Time Mod (it is not a matrix cell; the matrix cell `drift -> *` is the assignable version, default 0). At `timeDepthOct = 0.004` oct the RMS pitch wander is 1.6 cents, 3-sigma about 4.8 cents, bounded by tanh at 4.8 cents. Teder's approved 8 cents RMS was for a tube instrument; a delay clock should be felt, not heard as vibrato.

### 2.5 Seeding (the "never bit-identical" tell without breaking the test rig)

`prepare()` derives `seed = seedForTests ? testSeed : uint32(Time::getHighResolutionTicks()) ^ uint32(uintptr_t(this))`, then hands `seed ^ 0x9e3779b9` to Drift, `seed ^ 0x7f4a7c15` to Interference, `seed ^ 0x2545f491` to the PT chip noise. Production runs therefore never match bit for bit (plan tell 4); EngineTest scenarios that want regression hashes call `seedForTests()`; the two production-behaviour scenarios in 6.6 deliberately do not.

---

## 3. Matrix

### 3.1 Evaluation, per control tick

```
tick(agitMean, follower, interference, drift):
    src[agitation] = agitMean;  src[follower] = follower;  src[interference] = interference;  src[drift] = drift
    macroCtrl = pow(agitate, modk::agitateCurve)                                   // 1.0 = linear
    macroTime = timeMod * (1 - modk::agitateOnTime + modk::agitateOnTime * macroCtrl) // agitateOnTime 0: Time Mod is independent (recommended); 1: plan-literal, Agitate scales the Time column too
    for each dest t != time:   off[t] = macroCtrl * range[t] * sum_s depth[s][t] * src[s]
    // Time column, control-rate part (Agitation handled per sample, see timeOctForSample):
    timeCtrlTarget = macroTime * modk::timeRangeOct * (depth[follower][time]*src[follower] + depth[interference][time]*src[interference] + depth[drift][time]*src[drift])
                   + driftTimeOct * drift                                          // hidden always-on trim, NOT scaled by any macro
    timeCtrlInc  = (timeCtrlTarget - timeCtrl) * invInterval                        // ramp over the next 32 samples
    timeAgitGain = macroTime * modk::timeRangeOct * depth[agitation][time]
    // one-pole slew on the control-rate destinations so a fast Agitation or a crackly Interference steps by <= a few % per tick
    for each t != time:  slewed[t] += ctrlSlew * (off[t] - slewed[t])              // ctrlSlew from modk::ctrlSlewMs = 3 at tick rate
```

`range[t]` (value of the offset at |depth| = 1, |source| = 1, macro = 1):

| Dest | `modk::` constant | Unit at full | Applied as | Clamp after summing with the knob |
|---|---|---|---|---|
| Time | `timeRangeOct = 2.0` | octaves of fs_chip | added in log2(fs_chip), per sample | [log2 1500, log2 200000] (PT core clock limits) |
| Filter cutoff | `cutoffRangeOct = 4.0` | octaves | `Hz = exp2(log2(knobHz) + off)` once per tick | [20 Hz, min(18 kHz, 0.45 sr)] |
| Resonance | `resRange = 0.5` | of the 0..1 resonance param | `res01 = knob + off` per tick | [0, 1] |
| Decay | `decayRange = 0.4` | linear feedback gain | `fb = knob + off` per tick | [0, 1.15] |
| Absorb | `absorbRange = 0.5` | of 0..1 | per tick | [0, 1] |
| Blend | `blendRange = 0.5` | of 0..1 | per tick, then equal-power gains | [0, 1] |
| Strength | `strengthRangeDb = 20` | dB | `gain = dbToGain(knobDb + off)` per tick | [0, 40] dB |

Knob values arrive once per block, go through their own smoothers (20-30 ms as the template does, cutoff smoothed in log2 domain so knob sweeps are pitch-linear), and are combined with the mod offset additively in the destination's musical domain. Clamp once, after the sum. Agitation is unipolar into every control-rate column (hardware-true: 0..6 V raises the cutoff, never lowers it) and centred (`2a - 1`) into Time only, because Time is a pitch axis and an offset there would detune the knob.

### 3.2 Delivery to each destination

Engine per-block skeleton (the loop's feedback forces per-sample processing, so the engine runs sub-blocks between ticks):

```
process(buffer, p):
    push knob targets to smoothers; matrix.setDepths(p.depths); matrix.setMacros(p.agitate01, p.timeMod01); matrix.setDriftTimeOct(p.driftTimeOct)
    agitation.setSpeedHz(p.agitSpeedHz); setAngle(p.agitAngle); setMode(p.agitGate ? gate : loop)
    n = 0
    while n < numSamples:
        if samplesToTick == 0:
            if (follower.tick() && mode == gate) agitation.trigger()
            interf = interference.tick (loop.loopEnergy(), loop.lastLoopSample())
            dr     = drift.tick()
            dm     = matrix.tick (agitSum * invInterval, follower.value(), interf, dr);  agitSum = 0
            loop.setCutoffHz (clamp (exp2 (log2CutoffKnob + dm.cutoffOct), 20, min(18000, 0.45 sr)))
            loop.setResonance (clamp (resKnob + dm.resonance, 0, 1));  loop.setDecay (clamp (decayKnob + dm.decay, 0, 1.15))
            loop.setAbsorb (clamp (absorbKnob + dm.absorb, 0, 1));    blend = clamp (blendKnob + dm.blend, 0, 1) -> equal-power gains
            strengthGain = dbToGain (clamp (strengthKnobDb + dm.strengthDb, 0, 40))
            samplesToTick = controlInterval
        run = min (numSamples - n, samplesToTick)
        for i in 0..run:                                           // per sample
            a = agitation.tick();  agitSum += a
            l2 = log2FsKnob.next() + matrix.timeOctForSample (a)       // see 4
            chipRatio[n+i] = fastExp2 (clamp (l2, log2FsMin, log2FsMax) - log2FsHost)
            x = strength (monoIn[n+i]);  follower.push (x);  strengthOut[n+i] = x
        loop.process (strengthOut + n, wet + n, chipRatio + n, run)   // PT cores read chipRatio per sample
        n += run;  samplesToTick -= run
    blend, out level, publish uiLoopEnergy / uiAgitation / uiFollower / uiInterference / uiModOffset[]
```

`samplesToTick` starts at 0 in `prepare()` so the first sample of the first block has a valid tick. Per-tick cost: one `exp2`, one `dbToGain`, two `tanh`, one `sqrt`, ~90 flops. Per-sample cost: ~25 flops. Under 0.1% of a core at 48 kHz.

### 3.3 Hero routes and defaults

`ModDepths::heroDefaults()` sets exactly three cells, all others 0:

| Route | Constant | Default depth | At macro 1 this means |
|---|---|---|---|
| Agitation -> Filter | `modk::heroAgitationToCutoff` | +1.0 | cutoff sweeps knob .. knob + 4 oct (the hardware normal) |
| Interference -> Time | `modk::heroInterferenceToTime` | +0.5 | +-1 oct of fs_chip at Time Mod 1, scaled by Interference's -1..1 |
| Follower -> Decay | `modk::heroFollowerToDecay` | +0.35 | up to +0.14 feedback while you play (a swell into regeneration that lets go within the 100 ms release; the saturator bounds it) |

Also available at depth 0: Agitation -> Time (turn it up and push Speed past 100 Hz for the hardware's audio-rate FM clang before Phase 4's sub-osc exists).

The Agitate macro scales every control-rate column (0 = only the always-on drift moves anything). Time Mod scales the Time column. Recommended reading of the plan's "Agitate scales all three": `modk::agitateOnTime = 0`, i.e. Time Mod is independent, because two multiplying knobs give a dead Time Mod knob whenever Agitate sits at 0, and the UI places Time Mod beside Time, where a player will read it as "how much Time wobbles". Flip the constant to 1 for the plan-literal behaviour. **Init preset values are the user's call**; proposed starting point: Agitate 0.35, Time Mod 0.25, Speed 0.25 Hz (4 s), Mode loop.

State discipline: in v1 every depth is a compile-time constant and Agitate / Time Mod / Speed / Mode are APVTS parameters, so nothing needs `stampExtraState`. The moment depths become user-editable and are not parameters, they must be stamped, or presets will silently lose them.

UI feed: `uiModOffset[t] = slewed[t] / range[t]` (Time: `timeOct / timeRangeOct`), -1..1, so each knob can draw its modulated position without knowing units.

---

## 4. Audio-rate Time path

Everything is accumulated in **log2(fs_chip)** and converted to a resampling ratio once per sample with `fastExp2`. There is no division anywhere per sample.

Per block (divisions allowed here):

```
log2FsMax = log2(ptk::fsChipMax) = log2(200000) = 17.61;  log2FsMin = log2(1500) = 10.55;  span = 7.06 oct
free:  log2FsTarget = log2FsMax - time01 * span                                      // exponential Time mapping, 27.5 ms .. 3.67 s with N = 5504
sync:  delaySec = beatsForDivision * 60 / bpm;  log2FsTarget = clamp (log2(N_total) - log2(delaySec), log2FsMin, log2FsMax)
       (readout shows the clamped value; a 1/1 at 60 BPM is 4 s and clamps to 3.67 s)
log2FsKnob.setTarget (log2FsTarget)
```

`log2FsKnob` is a **one-pole in the log domain**, time constant `modk::timeKnobSmoothMs = 20` (coefficient `1 - exp(-1/(0.02 sr))`, applied per sample). The plan asks for a one-pole here and the exponential approach is the tape feel: a 7-octave jump lands within 1% in ~100 ms with a decelerating glide. This is the same path free and synced, so switching divisions smears pitch instead of jumping (plan 2.6). It fulfils the "all continuous parameters are smoothed" rule; it is not a `juce::SmoothedValue` because JUCE offers only linear and multiplicative ramps, not a one-pole.

Per sample:

```
a  = agitation.tick()
l2 = log2FsKnob.next()                                  // knob, smoothed (one-pole)
   + matrix.timeOctForSample (a)                        // = ramped control-rate sum (Interference, Follower, assignable Drift, hidden Drift trim) + Agitation centred, audio rate
l2 = clamp (l2, log2FsMin, log2FsMax)                   // single clamp at the destination
chipRatio[i] = fastExp2 (l2 - log2FsHost)               // log2FsHost = log2(sr), precomputed in prepare()
```

`fastExp2(x)`: split `x = fi + f`, `f` in [0,1); `2^f` by a 6th-order polynomial (Taylor coefficients 0.6931472, 0.2402265, 0.0555041, 0.0096181, 0.0013333, 0.0001540; relative error < 2e-5 = 0.03 cents, far under the drift), `2^fi` by building the float exponent bits with `memcpy`. ~12 flops, no division, no libm call. Accuracy is more than enough: float precision at l2 ~ 17.6 is 2e-6 (0.003 cents).

The PT cores consume `chipRatio` directly: the write-side phase accumulator advances by `chipRatio[i]` chip samples per host sample (`while (phase >= 1) writeChipSample()`, at most `ceil(fsChipMax/44100) + 1 = 6` iterations, bounded, no allocation); the host-rate output interpolates between the last two chip output samples using the same accumulator's fraction, so the read side needs no `1/ratio` either. Because the memory is fixed-length in chip samples, modulating the ratio repitches the buffer for free (tape smear and FM clang) and the telescoping ratio `fs(now)/fs(at write)` means Drift never random-walks the pitch across passes; it stays inside the drift range.

Bypass: engine keeps running on silence (follower falls, Interference rings down, Agitation keeps its phase). Clear: flushes buffers and filter states only; Agitation phase and Interference state are runtime like any LFO and are neither cleared nor saved.

---

## 5. Tunable constants (`Source/dsp/ModTuning.h`)

| Namespace::name | Start | Audible effect when raised |
|---|---|---|
| `modk::controlInterval` | 32 samples | lower = smoother control-rate mod, more CPU; changes nothing about Time (audio rate) |
| `modk::ctrlSlewMs` | 3 ms | rounds steps of fast Agitation/Interference into cutoff, decay etc.; too high blunts a saw-shaped Agitation |
| `modk::loopEnergyMs` | 10 ms | Interference and ember react slower to loop level |
| `modk::timeRangeOct` | 2.0 oct | wider Time swings for the same depth; FM gets more metallic |
| `modk::cutoffRangeOct` | 4.0 oct | Agitation sweeps the filter further |
| `modk::resRange` / `absorbRange` / `blendRange` | 0.5 | more travel on those (non-hero) columns |
| `modk::decayRange` | 0.4 | playing can push Decay further into runaway |
| `modk::strengthRangeDb` | 20 dB | Follower->Strength becomes a stronger compressor/expander |
| `modk::heroAgitationToCutoff` | +1.0 | deeper filter sweep at a given Agitate |
| `modk::heroInterferenceToTime` | +0.5 | wilder pitch chaos at a given Time Mod |
| `modk::heroFollowerToDecay` | +0.35 | playing raises feedback more; negative ducks the tail while playing |
| `modk::agitateCurve` | 1.0 | >1 makes the first half of the Agitate knob subtler |
| `modk::agitateOnTime` | 0 | 1 = Agitate also scales Time Mod (plan-literal; dead Time Mod at Agitate 0) |
| `modk::timeKnobSmoothMs` | 20 ms | longer = more tape smear on Time moves and sync switches |
| `agitk::speedMinHz` / `speedMaxHz` | 0.016 / 1000 Hz | range of the Speed knob |
| `agitk::angle` | 0.5 | <0.5 = fast rise slow fall (ramp down), >0.5 = slow rise fast fall (saw up) |
| `agitk::angleMin` | 0.01 | shortest segment is 1% of the cycle |
| `agitk::curve` | 0.25 | 0 = straight segments; higher = rounder peak, exp-ish rise |
| `folk::attackMs` / `releaseMs` | 5 / 100 ms | follower speed; release also sets how long Follower->Decay holds after a note |
| `folk::sensitivity` | 4 (full at -12 dBFS) | quieter playing already reads as full |
| `folk::gateOnLin` | 0.0316 (-30 dBFS) | gate-mode trigger floor |
| `folk::onsetRatio` | 1.8 | higher = only clearly separated attacks retrigger |
| `folk::baselineMs` | 300 ms | shorter = sustained notes stop retriggering sooner |
| `folk::refractoryMs` | 60 ms | double-trigger guard on a single pluck |
| `intk::envRef` | 0.25 | lower = the loop needs less energy to go fully chaotic |
| `intk::rhoCalm` / `rhoWild` | 14 / 30 | calm end: spiral ring-down; wild end: chaos strength (chaos onset at 24.74) |
| `intk::heatRho` / `heatSeconds` | 8 / 6 s | how much and how slowly a hot loop makes Interference wilder; the long-timescale evolution knob |
| `intk::dtBase` / `rateGain` / `dtMax` | 0.006 / 1.5 / 0.02 | burble speed calm..hot (about 9..25 Hz); dtMax is the Euler stability guard |
| `intk::inject` | 0.15 | how much the loop audio steers the chaos; too high = filtered audio, not chaos |
| `intk::outScale` | 1/16 | output amplitude before tanh |
| `intk::dcHz` | 0.3 Hz | how fast Interference returns to 0 when the loop calms |
| `intk::slewMs` | 4 ms | smoother Interference; lower = more zipper "static" |
| `intk::crackle` | 0.15 | amount of raw per-tick jitter mixed back in (shortwave static on Time) |
| `driftk::slowHz` / `wowHz` / `wowMix` | 0.08 Hz / 0.6 Hz / 0.35 | thermal wander vs wow character |
| `driftk::rms` | 1/3 | output RMS before tanh (peaks ~1) |
| `driftk::timeDepthOct` | 0.004 oct (1.6 cents RMS) | always-on Time wander; the drift test bounds it at 8 cents |

---

## 6. EngineTest scenarios and numeric expectations

Harness notes: default `sr = 48000`, `block = 128`; every scenario wraps `engine.process` in `juce::ScopedNoDenormals` exactly as `processBlock` does (Interference ring-down and the DC blocker generate denormals otherwise, which would make offline runs both slow and numerically different from the plugin). Helpers to add, following Teder's `check()` / `rmsWindow` / `freqPrecise` (Goertzel-refined) style: `fnv1a(vec, from, to)`, `centroidSeries(vec, hop = 0.25 s, juce::dsp::FFT order 12, Hann)`, `maxXcorrDecimated(probeStart, probeLen, searchFrom, searchTo)` (one-pole LPF at 1.2 kHz, keep every 12th sample -> 4 kHz; probe 0.5 s = 2000 samples over a 25 s search = 2e8 MACs, ~0.2 s), `goertzelDb(vec, hz, from, to)`. Sources are also tested standalone (they are in `ENGINE_SOURCES`), which is where exact timing numbers come from.

Analysis constants live at the top of `EngineTest.cpp`:

```
REPEAT_XCORR_MAX = 0.6      EVOLVE_CENTROID_STD_MIN = 60 Hz     EVOLVE_RMS_STD_MIN = 1.0 dB
EVOLVE_PATTERN_ACF_MAX = 0.8 (centroid-series autocorrelation, lags 2..15 s)
DIVERGE_NRMSD_MIN = 0.5     SELFSTART_MAX_S = 30                DRIFT_CENTS_MAX = 8
```

### 6.1 `agitation` (class-level, per-sample tick)

| Setup | Measure | Expect |
|---|---|---|
| loop, 2 Hz, angle 0.5, 4 s | period from rising crossings of out - 0.5 | 0.5000 +- 0.001 s |
| same | max / min | >= 0.999 / <= 0.001 |
| same | rise time min->max, fall time max->min | 0.250 / 0.250 +- 0.002 s |
| same | cycle mean | `0.5 - agitk::curve/6` +- 0.01 (0.458 at curve 0.25) |
| 2 Hz, angle 0.1 | rise / fall | 0.050 / 0.450 +- 0.002 s |
| 2 Hz, angle 0.9 | rise / fall | 0.450 / 0.050 +- 0.002 s |
| 0.016 Hz, 130 s | period | 62.5 +- 0.1 s |
| 1000 Hz, 1 s | period / mean | 1.000 ms +- 1 sample / 0.458 +- 0.01 |
| gate, 1 Hz, `trigger()` at 1.0 s | first sample > 0.01; peak time; return to 0; max over 2.2..4 s | <= 1.0 + 1 ms; 1.50 +- 0.01 s; 2.00 +- 0.01 s; < 0.001 |
| gate, retrigger at 1.3 s (mid-rise) | largest sample-to-sample jump in 1.29..1.31 s | < 2 * inc * riseInv (= 8.3e-5 at 1 Hz): continuous |
| loop->gate flip mid-cycle | out continues to the cycle end, then idles | jump < 1e-4 at the flip |

### 6.2 `follower` (class-level)

| Setup | Expect |
|---|---|
| 1 kHz sine, 0.5 peak, on at 0.1 s | `env` reaches 0.316 (63%) between 4 and 9 ms after onset (peak follower on a sine tracks slower than the raw 5 ms) |
| off at 1.0 s | `env` = 0.184 (37%) at 100 +- 3 ms after off (exact one-pole) |
| steady 0.0625-peak sine | `value()` = 0.5 +- 0.03 (sqrt(0.0625 * 4)) |
| steady 0.5-peak sine | `value()` = 1.0 (clamped) |
| bursts at 0.5 s and 2.0 s, -20 dBFS, 200 ms, silence between | `tick()` true exactly twice, within 2 ticks (1.3 ms) of each onset; never during the sustain |
| sustained note from 0.5 s | exactly one trigger in 4 s |

### 6.3 `interference` (class-level, fed synthetic loopEnergy and a 0-mean loopSample noise)

| Setup | Expect |
|---|---|
| energy held at 0, 0.05, 0.10, 0.20, 0.40, 0.80 for 6 s each (measure last 4 s of each) | RMS non-decreasing across steps; RMS(0) < 0.02; RMS(0.8) in [0.25, 0.8]; Pearson r(step energy, step RMS) > 0.9 |
| any | max abs(out) <= 1.0 |
| energy 0.8 | dominant frequency (zero-crossing rate of out) in [5, 60] Hz, printed |
| energy 0.8, two instances same seed, second gets x += 1e-6 at start | RMS(out1 - out2) over 0..0.5 s < 1e-3, over 5..10 s > 0.3: sensitive dependence, i.e. chaos |
| same seed, same input, two instances | outputs bit-identical (FNV equal): the rig is deterministic when told to be |
| energy 0.8 for 20 s then 0 | out decays below 0.02 within 5 s (ring-down, then DC-blocked to zero) |

### 6.4 `drift` (class-level, 1200 s of ticks)

| Expect |
|---|
| RMS in [0.30, 0.37]; max abs(out) <= 1 |
| dominant frequency estimate `RMS(diff) / (2 pi RMS(out) ctrlDt)` < 0.6 Hz |
| two seeds: correlation of the two series < 0.2 |

### 6.5 Routes through the engine

| Scenario | Setup | Expect |
|---|---|---|
| `timefm` (audio-rate proof) | 1 kHz sine in, Blend 100% wet, Decay 0, Agitation->Time depth 0.1, Speed 700 Hz, Time Mod 1, Agitate 0, all other routes 0, drift trim 0 | Goertzel at 300 and 1700 Hz between -26 and -8 dB re the 1 kHz carrier (exp-FM with +-0.2 oct, index ~0.3); the control-rate alias at 1500 - 700 = 800 Hz < -40 dB re carrier. A per-block or per-tick Time path fails the 800 Hz line |
| `interftime` | 220 Hz sine at -6 dBFS held 20 s, Decay 0.6 so the loop is hot, Time Mod 1, hero depths, Agitate 0 | wet instantaneous pitch (50 ms windows) 5th..95th percentile spread >= 0.6 oct; `uiInterference` RMS over the last 10 s > 0.25; with the sine at -60 dBFS instead, spread < 0.05 oct and `uiInterference` < 0.03 (correlates with loop energy) |
| `agitfilter` | white noise in, Decay 0, Blend wet, Filter knob 500 Hz, Agitate 1, Speed 0.5 Hz | centroid series peak-to-peak cycle at 2.00 +- 0.05 s; max centroid / min centroid >= 8 (about 3+ of the 4 oct after the noise's own tilt); Filter knob 18 kHz: output finite, no cutoff above min(18 kHz, 0.45 sr) (assert via published `uiModOffset[cutoff]` clamped effect: centroid variance < 5% of the 500 Hz case) |
| `followdecay` | Decay knob 1.15, hero Follower->Decay, hot input | effective decay published <= 1.15 (clamp); with Decay 0.9 and bursts, wet tail RMS 0.5 s after a burst is >= 2 dB higher than with the route zeroed |
| `timesmooth` | Time 0.3 -> 0.6 step at 1 s, no mod, drift trim 0, test tap on `chipRatio` (engine exposes `lastChipRatio()` for tests) | max per-sample relative ratio step < 0.5%; ratio reaches within 1% of target between 80 and 120 ms after the step (one-pole, 20 ms tau) |
| `timesync` | sync on, 120 BPM, 1/4; impulse in, Decay 0.5 | repeat spacing 0.500 +- 0.002 s; switch to 1/8 at 3 s: ringing content's pitch trace has no jump > 5% between adjacent 5 ms windows, spacing settles to 0.250 s |

### 6.6 The generative milestone

Setup `generative`: no input at all. Time 0.6 (fs_chip ~10.5 kHz, ~0.52 s loop), Decay 1.15, Filter 3 kHz, Resonance 0.3, Absorb 0.15, Blend 1.0, Strength 0 dB, Agitate 0 (so only Interference -> Time plus the always-on drift and in-loop noise move anything), Time Mod 0.6, hero depths, `seedForTests(1)`. Render 90 s, analyse 30..90 s. Also run at 44.1 kHz/block 64 and 96 kHz/block 512 and require the same PASS lines (values printed; they will differ).

| Check | Measure | Expect |
|---|---|---|
| self-starts | first 250 ms frame with RMS > -30 dBFS | < `SELFSTART_MAX_S` = 30 s (depends on PT noise/quantisation levels, see risk 7) |
| bounded, analog, no clip | peak over 30..90 s; abs(mean)/RMS | peak < 1.0; DC ratio < 0.05 |
| never exactly repeats (waveform) | `maxXcorrDecimated` of the 0.5 s probe at 30 s against 35..60 s | < `REPEAT_XCORR_MAX` = 0.6 (a static runaway or self-oscillating filter scores ~1.0) |
| never repeats (bit level) | FNV of each 1 s window, 30..90 s | all 60 distinct |
| evolving (timbre) | std of `centroidSeries` | >= 60 Hz |
| evolving (dynamics) | std of frame RMS in dB | >= 1.0 dB |
| evolution is not a pattern | max autocorrelation of the centroid series for lags 2..15 s | < 0.8 |
| chaotic, not merely noisy | second run, same seed, one extra input sample of 1e-5 at t = 0; `RMS(a - b) / RMS(a)` over 80..90 s | > 0.5 (uncorrelated equal-power signals give 1.41) |
| deterministic when seeded | third run, same seed, no perturbation | FNV over 30..90 s equal to run one |
| production runs differ | two engines, no `seedForTests` | FNV differ |
| breathing (printed, not asserted yet) | RMS of each 10 s window | listed for the tuning pass; the target is visible variation, not a flat line |

`generative-agitated`: same, with Agitate 0.6 and Speed 0.05 Hz (20 s Agitation cycle starving and re-opening the loop). Same checks minus the pattern-ACF line (the 20 s period is intended), plus: centroid std >= 150 Hz.

### 6.7 Drift on Time

`drift` (engine): 220 Hz burst for 0.5 s then silence, Decay 1.05 (the repeats sustain, bounded by the saturator), Filter open, Absorb 0, Agitate 0, Time Mod 0, only the hidden drift trim active. Pitch of the repeating content via `freqPrecise` in 1 s windows over 2..40 s:

| Expect |
|---|
| every window within +- `DRIFT_CENTS_MAX` = 8 cents of 220 Hz (design: 1.6 cents RMS, tanh-bounded at 4.8) |
| std across windows >= 0.3 cents (it IS moving) |
| two seeds: correlation of the two cents traces < 0.5; FNV of the outputs differ |
| `p.driftTimeOct = 0`: std < 0.05 cents and, with `seedForTests`, two runs are bit-identical (the trim is the only always-on randomness in the Time path) |

---

## 7. Risks and mitigations

1. **Stationary chaos reads as mush, not music.** Constant full-tilt Interference on Time can sound like a broken tape rather than an evolving texture. Mitigations: the `heat` memory and the calm/wild rho split (chaos only when the loop is hot, ring-down when not), Agitation -> Filter periodically starving the loop so energy and therefore chaos cycles, and the `generative` scenario printing 10 s RMS windows so the tuning pass can see breathing. First knobs to turn: `intk::envRef`, `intk::heatRho`, `modk::heroInterferenceToTime`. This is where the plan says to budget time.
2. **Runaway + Time FM pumps energy into DC and subsonics** (FM folds content down; the saturator rectifies asymmetrically). The loop's DC blocker at ~10 Hz is mandatory, and `generative` asserts DC ratio < 0.05 and peak < 1.0 at the worst-case settings.
3. **Fast Agitation aliases into control-rate destinations.** Accepted by design (box mean per tick); Time is the audio-rate destination and `timefm` proves the alias line at 800 Hz is absent there. If Agitation-into-Filter at audio rate is missed, the SVF cutoff would have to move per sample; park in IDEAS.
4. **"Never bit-identical" vs regression nets.** Production seeds from the clock; tests pin with `seedForTests`. Keep the two scenarios that assert difference separate from the ones that assert hashes, and re-record hashes deliberately, never to make a red line green.
5. **Denormals and NaN.** Lorenz ring-down, DC blockers and the follower tail all produce denormals: `ScopedNoDenormals` in `processBlock` (already there) and in every EngineTest loop. Euler blow-up is prevented by `dtMax = 0.02` with rho <= 38, and guarded by the `isfinite` reset.
6. **Sample-rate dependence.** Every coefficient comes from seconds and `tickScale` normalises Lorenz's dt; the generative scenario at three rates is the check.
7. **Self-start depends on Phase 1 work, not this design.** With no input the loop only starts from in-loop noise plus quantisation grunge. If `generative` fails the 30 s self-start line, the fix is the PT core's noise floor and bit-depth curve, not the mod system; say so in PROGRESS rather than "fixing" it by injecting noise in the matrix.
8. **UX of Agitate vs Time Mod** is a user decision (`modk::agitateOnTime`). Ask before shipping the Init preset, and never retune the defaults as a side effect.
9. **Gate mode misses soft legato re-attacks** (needs +5 dB over the ring-out). Documented; the HF transient detector from Infinite Sustainer is the upgrade path.
10. **CPU of per-sample `fastExp2`** is ~12 flops; if profiling ever disagrees, the fallback is exact `exp2` at each tick with a multiplicative per-sample ramp re-anchored every 32 samples (small pitch error only under extreme audio-rate FM). Do not go to per-block: `timefm` would fail and the clang would be gone.
11. **Filter mod against Nyquist at high sample rates** and Decay mod past 1.15: single clamp at each destination after the sum; `agitfilter` and `followdecay` assert them.
12. **State discipline creep.** Today nothing in this subsystem needs `stampExtraState`. The first hidden depth that becomes user-adjustable without being an APVTS parameter must be stamped, or it will be the preset bug the user finds.