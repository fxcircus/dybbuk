# Dybbuk verification plan

Everything below is designed against the house idiom in the three siblings: named scenarios selected by argv, `check (ok, what, measured)` lines that print the number even when they pass, a global failure count that turns the exit code non-zero, FNV-1a hashes over float bits for bit-identity, and a second console target for everything that lives above the engine (frippertronics `ProcessorTest`, Infinite Sustainer `PresetProbe`). Nothing here is aspirational: every scenario names its input, its fixed parameters, the algorithm, the threshold, and what a red line means.

---

## 0. Harness architecture and the engine test contract

### 0.1 Targets

| Target | Links | Purpose |
|---|---|---|
| `EngineTest` | `ENGINE_SOURCES` + `juce_dsp` only | Drives `DybbukEngine` like a host. 24 scenarios, section 1 A-E. |
| `ProcessorTest` | `PLUGIN_SOURCES` (like `UISnapshot`) | APVTS, state, presets, bypass crossfade, readouts, parameter order. Section 1 F. This is the repo's PresetProbe; it needs the processor, so it cannot live in EngineTest. |
| `UISnapshot` | existing | Renders the editor states listed in the manual gates. |

`build.sh` runs both test binaries after `cmake --build` and before pluginval, exactly as frippertronics does, so a red scenario stops the build script.

### 0.2 What the engine must expose (the test contract)

The scenarios assume this surface. If the engine designer changes a name, change it in one place in the harness; if a piece is missing, the scenario that needs it cannot be written.

```cpp
class DybbukEngine
{
public:
    struct Params
    {
        float strengthDb = 0.0f;      // 0..40
        float time01     = 0.3f;      // 0..1, knob position
        bool  timeSync   = false;
        int   syncDivision = 6;       // index into kSyncDivisions
        double bpm = 120.0; bool ppqValid = false;
        float timeMod01  = 0.0f;      // 0..1 depth of audio-rate FM on fs_chip
        float decay      = 0.0f;      // 0..1.15
        float filterHz   = 18000.0f;  // 20..18000
        float resonance01 = 0.0f;     // 0..1, self-osc at >= kResSelfOsc01
        float absorb01   = 0.0f;
        float blend01    = 1.0f;      // equal power
        float agitate01  = 0.0f;      // macro over the three hero routes
        float agitSpeedHz = 1.0f;     // 0.016..1000
        bool  agitGate   = false;     // false = loop
        float outDb      = 0.0f;      // <= -99 means -inf
        bool  bypass     = false;     // engine keeps running on SILENCE, still writes wet
    };

    // Hidden tunables. Plain struct, read once per block; written only before
    // prepare() or by tests. Production never touches it. Every field is a
    // scale on the corresponding named constant (1 = the tuned value).
    struct Trims
    {
        float chipNoise = 1.0f;   // hiss inside the core
        float dither    = 1.0f;   // TPDF before the quantiser
        float quantize  = 1.0f;   // 0 = quantiser bypassed (float path)
        float bleed     = 1.0f;   // clock pulse train below kBleedOnsetHz
        float drift     = 1.0f;   // slow random on fs_chip
        static Trims clean() { return { 0.0f, 0.0f, 1.0f, 0.0f, 0.0f }; } // quantiser stays on: it is deterministic
    };
    Trims trims;

    void prepare (double sampleRate, int maxBlockSize);   // repeat-safe
    void process (juce::AudioBuffer<float>&, const Params&);
    void requestClear();                       // atomic counter mailbox, honoured at the next process()
    void seedForTests (juce::int64 seed);      // reseeds every RNG: chip noise, dither, drift, interference init
                                               // (default construction seeds from juce::Random::getSystemRandom())

    // Engine -> UI, read by the harness once per block.
    std::atomic<float> uiLoopEnergy, uiOutputLevel, uiTimeSeconds, uiFilterHz,
                       uiDecay, uiAgitation, uiInterference, uiFollower;

    // Pure mappings shared by the DSP, the editor readouts and the harness.
    static double fsChipForTime01 (float t);            // kFsChipMax * pow (kFsChipMin / kFsChipMax, t)
    static double loopSecondsForTime01 (float t) { return kMemorySamples / fsChipForTime01 (t); }
    static float  timeModOctaves (float timeMod01);     // peak FM depth in octaves at that knob position

    static constexpr int    kMemorySamples = 5502;      // 3 x 1834 (plan says 5504; make it divisible by the stage count)
    static constexpr int    kNumStages     = 3;
    static constexpr int    kControlChunk  = 32;        // control-rate hop, aligned to sample 0 of the stream, not to block starts
    static constexpr double kFsChipMax = 200000.0, kFsChipMin = 1500.0;
    struct SyncDivision { const char* name; double beats; };
    static const SyncDivision kSyncDivisions[12];       // 1/16, 1/8T, 1/16., 1/8, 1/4T, 1/8., 1/4, 1/2T, 1/4., 1/2, 1 bar, 2 bars
};
```

Numbers the scenarios rely on (from the mapping above, N = 5502):

| time01 | fs_chip | loop period T = N/fs_chip | tap 1 (T/3) | note |
|---|---|---|---|---|
| 0.00 | 200.0 kHz | 27.5 ms | 9.2 ms | shortest, clean |
| 0.20 | 75.2 kHz | 73.2 ms | 24.4 ms | timemod carrier setting |
| 0.30 | 46.1 kHz | 119.4 ms | 39.8 ms | timesweep start |
| 0.35 | 36.1 kHz | 152.5 ms | 50.8 ms | runaway setting |
| 0.40 | 28.3 kHz | 194.7 ms | 64.9 ms | |
| 0.47 | 20.0 kHz | 275 ms | | clock bleed onset (kBleedOnsetHz = 20 kHz) |
| 0.50 | 17.3 kHz | 317.6 ms | 105.9 ms | multitap setting |
| 0.55 | 13.6 kHz | 405.6 ms | | generative setting |
| 0.60 | 10.6 kHz | 518.2 ms | 172.7 ms | timesweep end |
| 0.75 | 5.10 kHz | 1.080 s | | bleed mid |
| 1.00 | 1.5 kHz | 3.668 s | 1.223 s | longest, destroyed |

Named constants the scenarios reference (starting values; all "tune by ear" items get a name so the number can move without touching a test):

| Constant | Start | Lives in |
|---|---|---|
| `kTimeSmoothMs` | 20 | TimeFilterLoop |
| `kBitsAtMaxClock` / `kBitsAtMinClock` | 10 / 6 | PTCore |
| `kChipNoiseDbAtMaxClock` / `kChipNoiseDbAtMinClock` | -90 / -45 dBFS | PTCore |
| `kBleedOnsetHz` / `kBleedDbAtMinClock` | 20000 / -36 dBFS | PTCore |
| `kReconMaxHz` | 8000 | PTCore |
| `kTapWeights[3]` | 1.0, 0.85, 0.7 | TimeFilterLoop |
| `kDcBlockHz` | 10 | TimeFilterLoop |
| `kResSelfOsc01` | 0.85 | TimeFilterLoop |
| `kAbsorbMaxAttenDb` | -30 | TimeFilterLoop |
| `kClearFadeOutMs` / `kClearFadeInMs` | 2 / 5 | TimeFilterLoop |
| `kFollowerAttackMs` / `kFollowerReleaseMs` | 5 / 100 | Strength |
| `kAgitToFilterOct` / `kInterfToTimeOct` / `kFollowerToDecay` | 4.0 / 0.5 / +0.30 | ModMatrix |
| `kTimeModMaxOct` | 1.0 (square taper: oct = max * d*d) | ModMatrix |
| `kChaosDriveMin` / `kChaosDriveMax` | 3.57 / 3.99 | Interference (logistic map r) |
| `kInterferenceSlewMs` | 20 | Interference |
| `kDriftHz` / `kDriftDepthOct` | 0.3 / 0.01 | TimeFilterLoop |
| `kCpuBudgetPct` | 5 (PASS gate at 10) | EngineTest |

Assumptions to confirm with the DSP design (they shape three scenarios): (a) **Time Mod = depth of Agitation -> Time applied per sample after the knob smoother**, independent of the Agitate macro, since Agitation is the only source that reaches audio rate before Tones exist; (b) the dry path is the untouched input (pre-Strength), so Blend 0 is a null; (c) `Params` defaults above are *test* baselines, not user defaults.

### 0.3 The rig (implement once)

```cpp
namespace
{
constexpr double kSr = 48000.0;
constexpr int    kBlock = 128;
int failures = 0;

void check (bool ok, const char* what, const juce::String& measured)
{
    std::printf ("  [%s] %-52s %s\n", ok ? "ok" : "FAIL", what, measured.toRawUTF8());
    if (! ok) ++failures;
}
void report (const char* what, const juce::String& measured)   // a number worth printing, no verdict
{ std::printf ("  [--] %-52s %s\n", what, measured.toRawUTF8()); }

using Params = DybbukEngine::Params;

struct Trace { std::vector<float> energy, timeSec, filterHz, decay, agit, interf, follow; }; // one entry per block

struct Run
{
    std::vector<float> in, L, R;   // mono input as fed, both outputs, host rate
    Trace ui;
    double sr = kSr; int block = kBlock;
    bool nonFinite = false;
    double meanBlockUs = 0.0, maxBlockUs = 0.0;
};

struct Rig
{
    double sr = kSr;
    int block = kBlock;
    juce::int64 seed = 42;                                   // < 0: leave the engine's own seeding alone
    DybbukEngine::Trims trims = DybbukEngine::Trims::clean();
    std::function<float (double t)> input;                   // mono source; nullptr = silence
    std::function<void (double t, Params&, DybbukEngine&)> perBlock;   // schedule: steps, Clear, mode flips
    std::function<int (int blockIndex)> blockSize;           // optional varying sizes (<= block)
};

Run render (const Rig& rig, double seconds, Params p);
```

`render` constructs a fresh engine, applies `trims`, seeds if `seed >= 0`, calls `prepare (sr, block)`, then loops: `juce::ScopedNoDenormals` (mirrors the processor), fills both channels from `input (t)`, calls `perBlock`, times `engine.process` with `juce::Time::getHighResolutionTicks()`, appends L/R, snapshots the eight atomics into `ui`, flags any non-finite sample. Input generators live beside it: `sine (hz, amp)`, `burst (hz, amp, t0, ms)` (Hann-windowed), `noise (amp, seed)` (own `juce::Random`), `impulse (t0, amp)`, `gate (t0, t1, gen)`, `sum (a, b)`. `base()` returns the `Params` defaults from the contract.

---

## 1. EngineTest scenarios

Format per scenario: argv token, purpose, input, fixed params (anything not listed = `base()`), trims, measurement, PASS, what FAIL means. "Clean" trims = `Trims::clean()`; "production" = the engine defaults.

### A. The PT core

#### `delaytime`
- **Purpose**: the Time readout is the truth. Loop period T = N / fs_chip at three settings, and the feedback repeat period equals T.
- **Input**: one Hann burst at t0 = 0.1 s per setting: `{time01, burstHz, burstMs} = {0.0, 500, 6}, {0.5, 500, 6}, {1.0, 100, 40}` (the long setting needs a tone under the 750 Hz chip Nyquist), amp 0.5. Render T·2.5 + 0.5 s (9.7 s at the long setting).
- **Params**: decay 0 for the tap measurement; a second pass at time01 0.5 with decay 0.6 for the repeat period. blend 1, filter 18k, absorb 0.
- **Trims**: clean.
- **Measure**: `xcorrLag (out, burst, searchFrom = 0.9·T, searchTo = 1.1·T)` gives the third-tap arrival (the window excludes tap 2 at 0.667·T). Report the correlation coefficient too; below 0.3 the echo was not found. Repeat pass: lag near 2T minus lag near T.
- **PASS**: |lag - T_expected| < max (1 ms, 0.5 %·T) at all three settings (at 1.0 that is 18 ms, comfortably above the 2 ms of linear-interp latency from three stages at 1.5 kHz plus the 0.5 ms group delay of a 675 Hz two-pole); repeat period within the same tolerance of T; `loopSecondsForTime01 (0)` in [25, 30] ms and `(1)` in [3.5, 3.8] s (pins the range so nobody quietly narrows it).
- **FAIL means**: phase accumulator ratio wrong (usually fs_chip/fs_host inverted or off by the stage count), stage memory not N/3, or the readout mapping and the DSP disagree.

#### `multitap`
- **Purpose**: three series stages, taps summed: exactly `kNumStages` echoes at k·T/3 with sensible weights.
- **Input**: burst 500 Hz, 6 ms, amp 0.5 at t0 = 0.1 s. time01 0.5 (taps at 105.9 / 211.8 / 317.6 ms). Render 0.8 s.
- **Params**: decay 0, blend 1, filter 18k. Second pass decay 0.7.
- **Trims**: clean.
- **Measure**: `envelope (out, attack 1 ms, release 3 ms)`, `pickPeaks (env, from t0 + 0.02, to t0 + 0.5, minSep 20 ms, rel 0.15)`. Reference time = envelope peak of the input burst.
- **PASS**: peak count == kNumStages; peak k at t_in + k·T/3 ± (3 ms + 1 %·T); amplitude ratio tap3/tap1 in [0.3, 1.2]; tap2/tap1 in [0.5, 1.2] (`kTapWeights` plus three reconstruction filters' cumulative loss). Decay 0.7 pass: additional peaks at 4T/3, 5T/3, 2T (the loop re-enters the sum node after the full chain, so the second circulation shows the same three-step pattern), each within tolerance.
- **FAIL means**: one peak = taps not summed or stage count 1; wrong spacing = stage memory unequal; four or more peaks in the decay-0 pass = a stage writes twice or the feedback path is live at decay 0.

#### `timesweep`
- **Purpose**: a Time change repitches the buffer (tape smear), the glide is smooth, and nothing crossfades.
- **Input**: continuous 440 Hz sine, amp 0.25. time01 0.30 for 1.0 s, then stepped to 0.60 at t = 1.0 s (the harness sets `p.time01` in `perBlock`; the engine's `kTimeSmoothMs` turns the step into a glide). Render 2.2 s.
- **Params**: decay 0, blend 1, filter 18k, absorb 0, agitate 0.
- **Trims**: clean.
- **Measure**: expected ratio fs_new/fs_old = 0.0075^0.3 = 0.2304, so the buffered tone should read 440 × 0.2304 = 101.4 Hz until the first stage's old content is exhausted at 1.0 + T_new/3 = 1.173 s. `freqPrecise` over [1.10, 1.17] (smoother is 99.3 % settled by 1.10 s; 7 cycles of 101 Hz) and over [1.70, 2.00] (all three stages refilled). Also `freqPrecise` in 5 ms hops over [1.00, 1.10] for the glide shape, and the per-block `ui.timeSec` trace.
- **PASS**: pitch in [1.10, 1.17] = 101.4 Hz ± 5 %; pitch in [1.70, 2.00] = 440 ± 1 %; hop-to-hop pitch ratio during the glide never below 0.5 (a one-pole of 20 ms gives at worst 0.69 per 5 ms; an unsmoothed jump gives 0.23); `ui.timeSec` monotonic from 0.119 to 0.518 s, max per-block step < 0.08 s (1.5 × the largest legitimate one-pole step), final within 1 %.
- **FAIL means**: pitch stays 440 throughout = a crossfading or fractional-tap delay, i.e. the core was not built as a virtual chip; a hop ratio near 0.23 = the smoother is missing or applied to the wrong quantity; overshoot in `ui.timeSec` = smoothing in the wrong domain or sign.

#### `sync`
- **Purpose**: Time Sync derives fs_chip from the host tempo, runs through the same smoother, and clamps to the maximum.
- **Input**: burst 500 Hz 6 ms amp 0.5 at t0 = 0.1 s (delay measurement); continuous 440 Hz sine for the division switch pass.
- **Params**: timeSync true, ppqValid true, bpm 120; division "1/4" (500 ms) and "1/8" (250 ms); then "2 bars" at 60 BPM (8 s, must clamp to 3.668 s); then bpm 120 -> 100 stepped at 1.0 s with "1/4" held. Division switch pass: "1/8" -> "1/4" at 1.0 s.
- **Trims**: clean.
- **Measure**: `xcorrLag` as in `delaytime` around the expected T. Switch pass: `freqPrecise` in [1.08, 1.15] (expected 440 × 0.5 = 220 Hz because the chip clock halves) and [1.8, 2.0] (440). Tempo step: `ui.timeSec` trace from 0.5 to 0.6 s.
- **PASS**: 1/4 -> 500 ms ± 0.5 %; 1/8 -> 250 ms ± 0.5 %; 2 bars @ 60 -> 3.668 s ± 0.5 % (clamped); switch window pitch 220 ± 5 % then 440 ± 1 %; tempo glide monotonic, max step < 1.5 × the one-pole bound, no discontinuity; with ppqValid false the engine holds the last valid tempo (delay still 500 ms ± 0.5 %) and never emits NaN.
- **FAIL means**: an unsmeared division switch = sync bypasses the smoother; 8 s measured = no clamp; wrong division times = beats-per-division table or the readout table disagree.

#### `bandwidth`
- **Purpose**: fidelity collapses with Time: the reconstruction filter tracks fs_chip.
- **Input**: white noise amp 0.1 (seed 7). Per setting render T + 2.5 s, analyse the last 2 s. time01 ∈ {0.0, 0.5, 1.0}. Plus a stepped-sine frequency-response probe at time01 0.0: sines amp 0.05 at {100, 500, 1k, 2k, 4k, 8k, 12k} Hz, 0.6 s each, Goertzel over the last 0.3 s.
- **Params**: decay 0, blend 1, filter 18k, resonance 0, absorb 0.
- **Trims**: clean.
- **Measure**: `spectrum (out, order 12, Hann, averaged)`, `centroid (20 Hz .. 20 kHz)`, `bandEnergyDb (2k..8k) - bandEnergyDb (100..1k)` (HF/LF). Probe: 20·log10 (G(f)/G(500)).
- **PASS**: centroid(0.0) > 1500 Hz; centroid(1.0) < 1200 Hz and < 0.35 × centroid(0.0); centroid(0.5) < centroid(0.0); HF/LF drops by > 15 dB between 0.0 and 1.0. Probe at time01 0.0: |H(1k)| in [-3, +1] dB, |H(4k)| in [-9, 0] dB, |H(8k)| in [-24, -3] dB, |H(12k)| < -12 dB relative to 500 Hz (fixed 8.8 kHz MFB + `kReconMaxHz`; wide bounds because the curve is tuned by ear, but the shape must roll off).
- **FAIL means**: flat centroid across Time = reconstruction filter fixed at host rate instead of min (0.45·fs_chip, kReconMaxHz); centroid(0.0) low = the fixed filters are too dark or applied twice; probe flat to 12 kHz = the MFB is missing.

#### `noisefloor`
- **Purpose**: self-noise rises with Time and is quiet at short settings.
- **Input**: silence. time01 ∈ {0.0, 0.5, 1.0}, render T + 3 s, measure the last 1 s.
- **Params**: decay 0, blend 1, filter 18k, absorb 0, strength 0, out 0.
- **Trims**: production.
- **Measure**: `dB (rms (L, last 1 s))`.
- **PASS**: rms(0.0) < -70 dBFS; rms(1.0) in [-60, -30] dBFS; rms(1.0) > rms(0.5) + 6 dB > rms(0.0) + 12 dB (monotone with real steps). Report all three with the expected `kChipNoiseDb` curve values next to them.
- **FAIL means**: identical numbers = noise not scaled by fs_chip; silence everywhere = noise injected before a gate or after the taps; rms(0.0) hot = dither amplitude not scaled with the bit depth.

#### `clockbleed`
- **Purpose**: the ticking is present only below `kBleedOnsetHz` and grows as the clock falls.
- **Input**: silence. time01 ∈ {0.30 (46 kHz, none), 0.75 (5.10 kHz), 1.00 (1.5 kHz)}. Render T + 2 s, measure the last 1 s.
- **Params**: decay 0, blend 1, filter 18k, absorb 0.
- **Trims**: clean except `bleed = 1` (so the only possible output is the bleed).
- **Measure**: `goertzel (out, f = fsChipForTime01 (t))` and at f/2, and broadband RMS. Frequencies are exact because drift is off.
- **PASS**: rms(0.30) < -100 dBFS (bit silent); goertzel(5096 Hz) at 0.75 > -60 dBFS and > 40 dB above the same bin at 0.30; at 1.0, both 1500 Hz and 750 Hz present (> -60 dBFS), and the fundamental level at 1.0 exceeds the one at 0.75; level at 1.0 within [-50, -25] dBFS (`kBleedDbAtMinClock`).
- **FAIL means**: bleed at 0.30 = the onset test is missing or compares the wrong rate; no bleed at 1.0 = injected before the reconstruction filter (which sits at 0.45·fs_chip and eats it) or the pulse train frequency is not fs_chip; no subharmonic = the /2 divider is missing.

### B. Loop, filter, absorb, drive

#### `runaway`
- **Purpose**: Decay past unity self-oscillates, bounded, never digitally clipped, no DC, recovers when backed off.
- **Input**: burst 220 Hz, 200 ms, amp 0.5 at 0.1 s, then silence. Render 15 s. Three sub-cases: (a) plain; (b) `timeMod01 1.0, agitate 1.0, agitSpeedHz 500` (worst case: modulated clock at runaway); (c) `strengthDb 40` (hottest input). At t = 12 s `perBlock` sets decay 0.3.
- **Params**: decay 1.15, time01 0.35, filter 4000, resonance 0.2, absorb 0, blend 1, out 0.
- **Trims**: production.
- **Measure**: 500 ms window RMS and peak over [6, 12] s; `crest = peak / rms` per window; `dcOffset` over [10, 12]; global max |sample|; any non-finite; RMS at [14.5, 15].
- **PASS**: every window RMS in [0.02, 0.8]; max |sample| < 0.999; crest in [1.2, 6.0]; |RMS(window 6) - RMS(window 12)| < 3 dB (steady, not still growing); |DC| < 0.01; zero non-finite samples in all three sub-cases; RMS at [14.5, 15] < -40 dBFS (backing Decay off lets the loop die).
- **FAIL means**: peak == 1.0 or crest ≈ 1.0 = hard clipping somewhere after the saturator or the saturator has a clamp not a curve; RMS > 0.8 or still growing = saturator outside the loop; DC > 0.01 = DC blocker missing or after the tap; NaN = filter state blew up under modulated Time (usually cutoff not clamped below Nyquist) - this is the plan's risk item 2, so this scenario ships with Phase 2, not later.

#### `strength`
- **Purpose**: Strength is gain plus drive: clean at 0 dB, saturated and compressed at +40 dB, never beyond ±1.
- **Input**: 220 Hz sine amp 0.1, 1.0 s, measure [0.5, 1.0].
- **Params**: strength 0 then 40, time01 0.0, decay 0, blend 1, filter 18k.
- **Trims**: clean with `quantize = 0` (so the only distortion is the drive).
- **Measure**: Goertzel at 220, 440, 660 Hz; RMS; peak.
- **PASS**: at 0 dB, 3rd harmonic < -40 dB below the fundamental; at +40 dB, 3rd harmonic > -20 dB (it IS driven), RMS(40) - RMS(0) < 30 dB (at least 10 dB of the 40 was compressed), peak < 0.999. The envelope follower is untested here (see `follower`).
- **FAIL means**: harmonic-free at +40 = the drive curve is linear; 40 dB of RMS gain = the soft clip is after the wet tap; peak >= 1 = clip not soft.

#### `filter`
- **Purpose**: the in-loop SVF cutoff is what the knob says, full CCW passes almost nothing.
- **Input**: stepped sines amp 0.05 at fc/4, fc, 2·fc, 4·fc, 0.5 s each, Goertzel over the last 0.25 s. fc ∈ {200, 1000, 4000}. Normalise each probe against the same frequency with filter 18k (removes the fixed chip filters from the reading).
- **Params**: time01 0.0, decay 0, blend 1, resonance 0, absorb 0.
- **Trims**: clean.
- **PASS**: gain(fc) = -3 ± 1.5 dB; gain(2fc) = -12 ± 3 dB; gain(4fc) = -24 ± 4 dB; gain(fc/4) = 0 ± 1 dB (Butterworth-shaped 2-pole at resonance 0, which is what a TPT SVF at Q 0.707 gives). Filter 20 Hz: wet RMS of a 220 Hz sine is > 30 dB below the filter-18k reading.
- **FAIL means**: -3 dB point off by more than 1.5 dB at 4 kHz but fine at 200 Hz = no prewarp; all readings flat = the filter sits outside the wet path; fc/4 not at 0 dB = the tanh in the resonance path is compressing at 0.05 amplitude (too much drive).

#### `resonance`
- **Purpose**: resonance reaches self-oscillation, at the cutoff frequency, bounded by the tanh, and does not oscillate at half.
- **Input**: one 5 ms click amp 0.1 at t = 0.05 s to kick it, then silence. Render 3 s.
- **Params**: filter 1000 then 200; resonance 1.0; then resonance 0.5 at 1000. time01 0.0, decay 0, blend 1.
- **Trims**: clean.
- **Measure**: `freqPrecise` and RMS over [2.0, 3.0]; peak.
- **PASS**: resonance 1.0: RMS > 0.01 (it sustains with no other energy source), pitch within ±10 % of the cutoff at both settings, peak < 0.999; resonance 0.5: RMS < 1e-4.
- **FAIL means**: silent at 1.0 = damping never reaches zero (`kResSelfOsc01` mapping); pitch far off = oscillation frequency tracks something other than cutoff; peak at 1.0 = tanh missing on the feedback state.

#### `absorb`
- **Purpose**: Absorb attenuates and darkens, and it sits inside the loop (so it shortens the decay).
- **Input**: white noise amp 0.1 (seed 7), 2 s, measure the last 1 s. Loop pass: burst 220 Hz 200 ms amp 0.5 then silence, 5 s.
- **Params**: time01 0.2, decay 0, blend 1, filter 18k, resonance 0; absorb ∈ {0, 0.5, 1.0}. Loop pass: decay 1.0, absorb 0 then 0.7.
- **Trims**: clean.
- **Measure**: RMS dB and `centroid` per setting; loop pass RMS at [0.5, 1.0] and [4.0, 4.5].
- **PASS**: RMS(0.5) < RMS(0) - 3 dB; RMS(1.0) < RMS(0) - 12 dB (heading toward `kAbsorbMaxAttenDb`); centroid(1.0) < 0.7 × centroid(0) and monotone non-increasing; loop pass: absorb 0 holds (RMS at 4 s > RMS at 1 s - 6 dB) while absorb 0.7 decays (> 20 dB down).
- **FAIL means**: attenuation without darkening = the tilt stage is missing; loop still holding at absorb 0.7 = Absorb was placed after the feedback tap, contradicting the topology in plan 2.0.

### C. Commands and robustness

#### `clear`
- **Purpose**: Clear flushes every buffer and filter state at the next block, is not sticky, and does not click.
- **Input**: 220 Hz sine amp 0.5 for 1.5 s, then silence; at t = 2.0 s `perBlock` calls `engine.requestClear()`; new sine from 2.5 s to 4.0 s. Render 4 s.
- **Params**: decay 1.0, time01 0.4, blend 1, filter 6000.
- **Trims**: clean (so the post-flush floor is exactly zero; with production trims the hiss regenerates, which `noisefloor` covers).
- **Measure**: RMS of the block before the request; RMS of the second block after it; max |sample| over [2.02, 2.5]; `maxStep` over [1.99, 2.05] versus the steady-state `maxStep` over [1.0, 1.9]; RMS over [3.5, 4.0].
- **PASS**: pre-request block RMS > -20 dBFS; second-block RMS < -80 dBFS (with `kClearFadeOutMs` = 2 the fade fits inside the first block); residual max < 1e-4 (filters and DC blocker flushed too, no ringing); click check `maxStep` (transition) < 3 × steady; RMS over [3.5, 4.0] > -20 dBFS (rebuilds).
- **FAIL means**: residual ringing = SVF or saturator state not reset; sticky silence = the mailbox counter is consumed but the input gate never reopens; a click = the flush is instantaneous (if the design deliberately keeps it instantaneous, drop the click check and say so in PROGRESS.md).

#### `zipper`
- **Purpose**: no parameter change clicks (PLAN Phase 1 gate), one parameter at a time.
- **Input**: 220 Hz sine amp 0.25 continuous. For each row, 1.0 s of settle, the step at t = 1.0 s, measure `maxStep` over [1.0, 1.1] against `maxStep` over [0.5, 1.0]. Rows: filter 200 -> 8000; decay 0 -> 1.0; blend 0 -> 1 -> 0; absorb 0 -> 1; strength 0 -> 40; resonance 0 -> 0.9; outDb -12 -> +6; time01 0.2 -> 0.8; timeMod 0 -> 1 (agitSpeed 100 Hz); agitate 0 -> 1.
- **Params**: time01 0.3, decay 0.5, blend 0.5, filter 4000 unless the row says otherwise.
- **Trims**: clean.
- **PASS**: every row's transition `maxStep` < 3 × its own steady `maxStep`, and no row exceeds an absolute 0.1 (a real click is ~0.5).
- **FAIL means**: the named parameter bypasses `SmoothedValue`, or (Blend) the crossfade is linear with a hard endpoint, or (Time) the smoother is applied to the readout but not to fs_chip.

#### `passthrough`
- **Purpose**: the dry path is a null, the wet is mono and identical on both channels, odd channel layouts do not crash.
- **Input**: 220 Hz sine amp 0.4; second pass with L = sine, R = white noise amp 0.2 (the harness fills channels separately here); third pass with a 1-channel buffer.
- **Params**: blend 0 with strength 0 and again with strength 40; out -6 dB; out -100 (-inf); blend 1 for the channel checks.
- **Trims**: clean.
- **PASS**: blend 0: max |out - in| < 1e-6 over [0.2, 1.0] at both Strength settings (the dry is pre-Strength, assumption (b)); out -6: RMS ratio 0.5012 ± 0.1 %; out -inf: RMS < 1e-7; L != R input: max |wetL - wetR| < 1e-7 and its hash equals the hash of a run fed (L + R)/2 on both channels; 1-channel buffer: no crash, RMS > 0.
- **FAIL means**: blend-0 residual = the dry path runs through the drive or the output is post-mono-sum; L/R mismatch = a stereo state leaked into the "mono" core.

#### `nondeterminism`
- **Purpose**: the plan's tell 4: two runs are never bit-identical, but the music is the same; and seeded runs ARE identical (which every hash in this suite depends on).
- **Input**: 220 Hz sine amp 0.25 continuous, 4 s, measure the last 2 s.
- **Params**: (A) decay 0.9, agitate 0, seed -1 on both engines; (B) same with seed 42 on both; (C) decay 1.05, agitate 1.0, time01 0.5, seed -1.
- **Trims**: production.
- **Measure**: `fnv1a (L, last 2 s)` and RMS for each engine.
- **PASS**: (A) hashes differ, RMS within 1 %; (B) hashes equal; (C) hashes differ, RMS within 3 dB, energy traces' peak cross-correlation < 0.95.
- **FAIL means**: (A) equal hashes = every RNG is seeded with a constant or `getSystemRandom` is used only once for both; (B) unequal = some RNG escapes `seedForTests` (usually the interference initial state or the drift), which makes `pin` and `blocksize` meaningless.

### D. Modulation

#### `agitation`
- **Purpose**: the function generator's period, triangle shape, range, gate mode, and its hero route to the filter.
- **Input**: silence for loop mode; for gate mode a 200 ms burst 220 Hz amp 0.5 at 1.0 s and again at 3.0 s. White noise amp 0.1 for the audio confirmation.
- **Params**: agitSpeedHz ∈ {0.1, 0.5, 4.0}, agitGate false, render 3 periods + 1 s (32 s at 0.1 Hz); gate pass agitGate true, 5 s; route pass agitate 1.0 vs 0, filter 2000, decay 0, blend 1, 4 s at 0.5 Hz.
- **Trims**: clean.
- **Measure**: `ui.agit` per block (375 Hz sampling): period from upward crossings of 0.5 (mean of crossing intervals); rise fraction = share of samples where Δ > 0 within a cycle; min/max. Gate pass: count of complete rise+fall pairs. Route pass: `log2 (max / min)` of `ui.filterHz`; `centroid` of the output in 100 ms hops, correlated against `ui.agit`.
- **PASS**: period = 1/speed ± 2 % at all three; rise fraction 0.50 ± 0.05 (fixed 50/50 angle); min < 0.05, max > 0.95; gate: 0 cycles before the first burst, exactly 1 after it, 2 after the second, resting at < 0.02 between; route: swing at agitate 1.0 in [1, 8] octaves (`kAgitToFilterOct` scaled by the macro), < 0.01 oct at agitate 0; Pearson (centroid trace, ui.agit) > 0.7.
- **FAIL means**: period scales with sample rate (see `samplerate`) = phase increment computed once at 48 k; gate keeps looping = mode flag ignored; route swing at agitate 0 = the macro does not scale that route.

#### `follower`
- **Purpose**: the input follower's attack, release, linearity, and its route to Decay.
- **Input**: silence 0.5 s, 220 Hz sine amp 0.5 from 0.5 to 1.5 s, silence to 2.5 s; second run at amp 0.25.
- **Params**: strength 0, agitate 0; route pass agitate 1.0, decay 0.5.
- **Trims**: clean.
- **Measure**: `ui.follow` per block; attack = time to 63 % of the plateau after 0.5 s; release = time to 37 % after 1.5 s; plateau ratio between the two amplitudes; route pass: max `ui.decay` while playing, value 0.5 s after the note ends.
- **PASS**: attack = `kFollowerAttackMs` (5) ± 3 ms (block quantisation is 2.7 ms); release = `kFollowerReleaseMs` (100) ± 20 %; plateau(0.25)/plateau(0.5) = 0.5 ± 10 % (linear, not log); route: `ui.decay` rises above 0.55 while playing at agitate 1.0 and returns within 0.01 of 0.5 within 0.5 s of silence; at agitate 0 it never leaves 0.5 ± 0.001.
- **FAIL means**: times scale with rate = coefficients not recomputed in `prepare`; plateau ratio 0.25 = the follower reports power not amplitude (fine if intended, then fix the test and the ember scaling together).

#### `interference`
- **Purpose**: the chaos source is bounded, slewed, correlated with the loop, wilder when the loop is hot, and its route wobbles Time.
- **Input**: burst 220 Hz 300 ms amp 0.5 then silence. Render 12 s.
- **Params**: decay 0.9 (0.92 dB/pass at T = 0.195 s: 60 dB in ~13 s, a full hot-to-cold sweep), time01 0.4, blend 1, filter 6000. Route pass agitate 1.0 vs 0, 4 s. Stress pass decay 1.15, 20 s.
- **Trims**: production.
- **Measure**: `ui.interf` and `ui.energy` per block. std of interf over [1, 3] s (hot) vs [9, 12] s (cold); Pearson between |interf| smoothed 50 ms and energy over [0.5, 12]; max block-to-block step; `autocorrMax (interf, 0.25 s, 4 s)`; route pass std of `ui.timeSec` / mean.
- **PASS**: std_hot > 5 × std_cold; Pearson > 0.5; |interf| ≤ 1 and finite through the 20 s stress pass; max step < 0.5 (`kInterferenceSlewMs`); autocorrelation max < 0.9 (not periodic); route: relative Time wobble > 1 % at agitate 1.0 (`kInterfToTimeOct`), < 1e-6 at agitate 0 (drift is off in this rig).
- **FAIL means**: std_hot ≈ std_cold = the drive parameter is not fed from the loop envelope; NaN or |x| > 1 = logistic r exceeds 4 or the Lorenz step is unstable at the control rate (clamp to `kChaosDriveMax`); periodic autocorrelation = r is stuck below the chaotic threshold (< 3.57).

#### `timemod`
- **Purpose**: audio-rate Time modulation produces sidebands at exactly f_c ± k·f_m, and the modulation reaches fs_chip per sample rather than through the knob smoother.
- **Input**: 1 kHz sine amp 0.25, 2.5 s, analyse the last 1.0 s (two 16384-point Hann frames, 2.9 Hz bins).
- **Params**: time01 0.2 (T 73.2 ms: none of the three tap delays is an integer multiple of either modulation period, which would cancel the effect), decay 0, blend 1, filter 18k, agitate 0. Depth: solve `timeModOctaves (d) = 0.03` by bisection in the test (2.1 % peak rate deviation, whatever taper the engine picks). Runs: (A) agitSpeed 50 Hz; (B) 500 Hz; (C) timeMod 0 at 500 Hz.
- **Trims**: clean.
- **Measure**: carrier bin C at 1000 Hz; sideband S1 = mean of bins at 1000 ± f_m; the two strongest bins between 200 and 3000 Hz excluding the carrier. Why the pair: the output frequency is f_in · fs_read(t) / fs_write(t - τ), so peak deviation is 2·m·f_in·|sin(π f_m τ)| and the modulation index β = Δf / f_m falls 6 dB per octave of f_m intrinsically; a 20 ms one-pole in the path adds another 6 dB/oct above 8 Hz. Correct: β(50)/β(500) ≈ 10. Bugged: ≈ 100.
- **PASS**: (A) S1/C > 0.1 (-20 dB) and the two strongest non-carrier bins sit at 950 and 1050 Hz ± 1 bin; (B) S1/C > 0.01 and bins at 500 and 1500 Hz ± 1 bin; ratio (S1/C)_A / (S1/C)_B in [3, 30]; (C) S1/C < 0.001 and the output hash equals a run with agitSpeed 1 Hz, timeMod 0 (zero depth is bit-exactly zero).
- **FAIL means**: ratio near 100 = the FM is added to the smoother's target, not after it; no sidebands = Time Mod routes a control-rate source; spurious bins at other spacings = the mod waveform is not the agitation triangle (or the taps are aliasing the modulation, which shows as bins at f_m/3 multiples).

#### `generative`
- **Purpose**: the Phase 3 milestone: no input, Interference + Decay high produces a self-playing texture that evolves and never exactly repeats.
- **Input**: silence, 60 s (`--quick`: 30 s). Two seeds (1 and 2).
- **Params**: decay 1.10, agitate 1.0, time01 0.55, filter 3000, resonance 0.4, absorb 0.3, blend 1, agitSpeedHz 0.3.
- **Trims**: production.
- **Measure**: RMS in 20 ms hops → energy trace e[k]; `bloomSeconds` = first hop where RMS > -40 dBFS; over [20, 60] s: coefficient of variation of e; `autocorrMax (e normalised, 0.25 s, 20 s)`; `centroid` trace in 200 ms hops and its CV; peak, non-finite; cross-correlation peak between the two seeds' energy traces; hashes.
- **PASS**: RMS over [40, 60] > -40 dBFS (it starts itself from the floor; report `bloomSeconds`, expected 15-35 s at +0.83 dB per 0.4 s pass); CV(e) > 0.1 and CV(centroid) > 0.05 (it moves in level AND timbre); autocorrelation max < 0.9 (no exact repeat); peak < 0.999, no non-finite, RMS < -3 dBFS; the two seeds differ in hash and their energy traces' peak cross-correlation < 0.9.
- **FAIL means**: never blooms = noise floor at that Time too low for Decay 1.10 to lift within a minute (raise `kChipNoiseDb` at long Time or accept a longer bloom and say so); autocorrelation ≈ 1 at the agitation period = the agitation LFO dominates and the interference route is too weak; CV ≈ 0 = a static self-oscillating tone, the whole concept is not yet there. This scenario is expected to be red until Phase 3 is tuned; it prints its numbers regardless.

### E. Invariance and cost

#### `samplerate`
- **Purpose**: the chip, filter, noise, modulation and follower do not depend on the host rate.
- **Input/Params**: for fs_host ∈ {44100, 48000, 96000, 192000}: (1) `delaytime` at time01 0.5; (2) self-osc pitch with filter 5000, resonance 1.0 (5 kHz exposes a missing prewarp at 44.1 k); (3) `noisefloor` at 0.5, production trims; (4) wet RMS of a 220 Hz sine at time01 0.3, decay 0.8; (5) `bandwidth` centroid at 1.0; (6) agitation period at 2 Hz; (7) follower attack.
- **PASS**: delay within ±0.5 % of nominal at every rate; self-osc within ±1 % across rates; noise RMS within ±3 dB; sine RMS within ±0.5 dB; centroid within ±10 %; agitation period ±2 %; attack ±3 ms. Prints one table row per rate.
- **FAIL means**: whichever column drifts names the module whose coefficients are computed at a fixed 48 k or per host sample instead of per chip sample.

#### `blocksize`
- **Purpose**: identical output regardless of how the host chops the stream.
- **Input**: 220 Hz burst 300 ms amp 0.5 plus white noise amp 0.05 (seed 9), 4 s at 48 k.
- **Params**: decay 0.9, agitate 0.7, time01 0.4, timeMod 0.3, agitSpeedHz 3, filter 3000, seed 42.
- **Trims**: production.
- **Measure**: `fnv1a (L)` and per-sample max |Δ| against the 128-block run for block sizes 32, 512, 2048, and for a varying sequence {128, 37, 512, 1, 200, 64, 333, ...} cycling under a 2048 maximum.
- **PASS**: 32, 512, 2048 hash-identical to 128 (params are static, `kControlChunk` boundaries are stream-aligned, RNGs are consumed per sample, so nothing may depend on block length); the varying sequence: max |Δ| < 1e-3 and RMS within 0.1 dB.
- **FAIL means**: hash mismatch on power-of-two sizes = something is computed per block (a per-block linear ramp toward a target, a per-block control-rate update, a per-block RNG draw); the varying sequence off by more = control chunks restart at each block start.

#### `cpu`
- **Purpose**: the plan's budget and the risk note on per-sample divisions.
- **Input**: white noise amp 0.1, 30 s at 48 k/128; then 10 s silence with decay 0; then 192 k/32 for 10 s.
- **Params**: decay 1.1, agitate 1.0, timeMod 1.0, agitSpeedHz 1000, resonance 0.8, filter 4000 (the busiest legal setting).
- **Trims**: production.
- **Measure**: `Run::meanBlockUs`, `maxBlockUs`; percent of real time = meanBlockUs / (1e6 · block / sr).
- **PASS**: 48 k/128 < 10 % (report against `kCpuBudgetPct` 5; the gate is loose so a loaded laptop does not flake); 192 k/32 < 25 %; max block < 20 × mean (no allocation or periodic heavy work); silent-input mean ≤ 1.5 × busy mean (no denormal cliff even with FTZ on; the driver sets `ScopedNoDenormals` like the processor).
- **FAIL means**: spikes = allocation or a `std::function` in the block path; silence slower than noise = denormals reaching a filter without FTZ (the standalone path always has it, but the AU host might not honour it in every thread).

#### `pin`
- **Purpose**: one regression hash over the whole audio path, updated only on purpose.
- **Input**: burst 220 Hz 300 ms amp 0.5 + white noise amp 0.05 (seed 9), 3 s at 48 k/128, hash over [1, 3] s.
- **Params**: decay 0.95, agitate 0.6, timeMod 0.2, agitSpeedHz 2, time01 0.4, filter 3000, resonance 0.4, absorb 0.3, seed 42.
- **Trims**: production.
- **PASS**: hash == `kPinnedHash`. While `kPinnedHash == 0` the scenario reports and passes (unpinned). Pin it at the Phase 2 gate; from then on a hash change must be a deliberate retune, and the commit that retunes updates the constant.

### F. ProcessorTest scenarios (the processor, state, presets)

Same `check`/`report` helpers, a `Fixture` that owns a prepared `DybbukProcessor` (frippertronics idiom), `juce::ScopedJuceInitialiser_GUI` in `main`.

#### `state`
- Enumerate `proc.getParameters()`; set each `RangedAudioParameter` to normalised `frac (0.137 + 0.271·i)` via `setValueNotifyingHost`, read back what it snapped to as the reference; set the extra state (`darkMode`/theme id and anything `stampExtraState` writes); `getStateInformation` → blob → fresh processor → `setStateInformation`. **PASS**: every parameter |Δ| < 1e-6, every stamped property equal, `stateVersion` present. Then: junk input (`nullptr, 0`; foreign XML; non-XML) leaves the parameters untouched and does not crash; a blob with one `PARAM` child removed loads the default for that id and restores the rest (forward compatibility). **FAIL means**: the classic silent-save bug from CLAUDE.md; the missing property names the culprit.

#### `presets`
- `saveCurrent ("__probe_<pid>")` after the same parameter walk, load it in a fresh processor, compare, delete the file (it lives in the real `~/Library/Audio/Presets/fxcircus/Dybbuk`; the name makes leftovers obvious). Factory list contains Init plus the four Patch Corner presets once they ship (Echo-Verb, Wow and Flutter, Bat Cave, Breathing) and each loads with `getCurrentName()` matching. **Bypass preservation**: set Bypass 1, load a preset → Bypass still 1 (CLAUDE.md: a preset never takes the plugin in or out of circuit). Note: the template's `PresetManager::finishLoad` forces every `performanceParams()` entry to 0 on load, which contradicts that rule for Bypass; the implementer must exempt Bypass from that loop (or treat Clear, not Bypass, as the momentary performance param). This check is the pin for whichever way it is resolved.

#### `bypass`
- Processor path (the crossfade lives in `processBlock`). 220 Hz sine amp 0.5, Decay 0.8, Blend 0.5, Filter 2000, Time 0.3. Bypass param → 1 at 1.0 s, → 0 at 1.5 s. **PASS**: `maxStep` over [0.99, 1.10] and [1.49, 1.60] < 1.5 × `maxStep` over [0.5, 0.99] (no click; a hard switch adds a step of order 0.5); over [1.2, 1.5] max |out - in| < 1e-6 (true dry after the 20 ms fade); `getOutputLevel`/loop energy at 1.49 s between 0.2 and 0.6 × its value at 0.99 s (the engine kept running on silence: 4.2 passes at 0.8 ≈ -8 dB, so a loop that stayed constant was being fed input while bypassed, and one at zero was stopped); wet present again over [1.6, 2.0] (RMS(out - in) > 0.01); `getBypassParameter()` is the `bypass` parameter and it is declared last.

#### `readouts`
- For every float parameter at normalised {0, 0.25, 0.5, 0.75, 1}: `getText`. **PASS**: no text matches `\d+\.\d{3,}`; Time free-mode text matches `^\d+ ms$` below 100 ms and `^\d+\.\d{2} s$` at or above; Filter and Agit Speed match `^\d+ Hz$` at ≥ 100 Hz and `^\d+\.\d{2} Hz$` below; Blend, Absorb, Agitate, Time Mod, Resonance match `^\d+ ?%$`; Strength and Out match `^-?\d+\.\d{1,2} dB$` or `-Inf` at the bottom of Out; Decay matches `^\d\.\d{2}$` (the "runaway" word is the editor's readout strip, not the host string); the parameter label is empty for the ms parameter. Print the full table so the human can eyeball it against the UISnapshot images.

#### `order`
- Parameter IDs unique; declaration order begins `time, decay, filter, resonance, absorb, blend, agitate, agitSpeed` (Push bank 1); `bypass` last; `getVersionHint()` strictly ascending across the list (the AU wrapper sorts by hint then hash-of-id, so equal hints scramble the Push order - documented in the sibling's PROGRESS.md); every parameter has a non-empty name under 20 characters and no em dash anywhere in names or texts.

#### `clearui`
- `proc` exposes the UI's Clear entry point (whatever wraps `engine.requestClear()`); drive it mid-runaway through `processBlock` and assert the same second-block silence as `EngineTest clear`. Proves the mailbox is wired through the processor, not just the engine.

---

## 2. Shared measurement helpers (implement once, in the anonymous namespace of EngineTest; ProcessorTest gets the subset it needs)

```cpp
double rms (const std::vector<float>& v, double from, double to);          // linear
double dB (double linear) { return 20.0 * std::log10 (juce::jmax (1.0e-12, linear)); }
double peak (const std::vector<float>& v, double from, double to);
double crest (v, from, to) { return peak / rms; }
double maxStep (v, from, to);            // max |x[n] - x[n-1]|: the click detector
double dcOffset (v, from, to);           // mean
double freqZeroCross (v, from, to);      // crossings / 2 / seconds: coarse, robust
double freqPrecise (v, from, to);        // (rises - 1) / (last - first) over interpolated rising crossings: ~0.1 % (teder)
double goertzel (v, from, to, hz);       // amplitude of one sinusoid at exactly hz (teder partialAmp)

struct Spectrum { std::vector<float> mag; double binHz; };
Spectrum spectrum (v, from, to, int order = 12);      // juce::dsp::FFT, Hann, |X| averaged over hopped frames
double centroid (const Spectrum&, double fLo, double fHi);   // sum f·m / sum m over the band
double bandEnergyDb (const Spectrum&, double fLo, double fHi);
std::vector<int> strongestBins (const Spectrum&, double fLo, double fHi, int count, double excludeHz, double excludeWidthHz);

juce::uint64 fnv1a (v, double from = 0, double to = 1e9);
//   h = 1469598103934665603ULL; per sample: memcpy float -> uint32 bits; h = (h ^ bits) * 1099511628211ULL  (IS idiom)

std::vector<float> envelope (v, double attackSec, double releaseSec);          // one-pole on |x|
struct Peak { double t; double amp; };
std::vector<Peak> pickPeaks (const std::vector<float>& env, double from, double to,
                             double minSepSec, double relThreshold);            // local maxima above rel·max, greedy by amplitude

struct Lag { double seconds; double coefficient; };
Lag xcorrLag (const std::vector<float>& out, const std::vector<float>& ref,   // normalised cross-correlation of ref
              double refStart, double refLen, double searchFrom, double searchTo); // against out, argmax with parabolic interpolation

double pearson (const std::vector<float>& a, const std::vector<float>& b);
double autocorrMax (const std::vector<float>& series, int minLag, int maxLag); // mean-removed, unit-variance, max |rho|
double timeModForOctaves (double octaves);   // bisection on DybbukEngine::timeModOctaves
```

Rules for the helpers: windows are in seconds and clamp to the vector; every helper returns 0 rather than dividing by zero on an empty window (so a broken render reads as a FAIL on the value, not a crash); `spectrum` uses `performFrequencyOnlyForwardTransform` on a `2 * size` scratch, frames hopped by size/2; nothing in this file allocates inside a timed region (the `cpu` scenario preallocates its `Run` vectors before rendering).

---

## 3. Run-all mode and the runtime budget

Dispatch, in the frippertronics shape:

```cpp
struct Scenario { const char* name; void (*run)(); double approxAudioSeconds; };
const std::vector<Scenario>& scenarios();   // ordered as in section 1: A core, B loop, C commands, D modulation, E invariance

int main (int argc, char* argv[])
{
    // no args         -> run every scenario
    // <name> [<name>] -> only those (unknown name: print the list, exit 2)
    // list            -> names and approx cost, exit 0
    // --quick         -> sets g_quick: generative 30 s and one seed, samplerate skips 192 k, cpu 10 s
    for each selected: printf ("\n=== %s ===\n"); tick; s.run(); printf ("  (%.2f s)\n", elapsed); accumulate
    summary table: name, ok/FAIL, seconds; then "all checks passed" or "%d check(s) FAILED"
    return failures == 0 ? 0 : 1;
}
```

`failures` is per-check (as in the siblings), so the exit code reflects any red line anywhere; the summary table additionally marks which scenarios contained one. Print the numbers even on PASS: the human reads them into PROGRESS.md.

Runtime budget. Wall time is dominated by audio-seconds rendered; three linear-interp stages plus SVF, saturator and control-rate mod cost roughly 100-300 ns per sample in RelWithDebInfo, so about 5-15 ms of wall per second of 48 k audio.

| Scenario | Audio seconds (48 k equivalent) |
|---|---|
| delaytime | 3 settings + repeat pass ≈ 13 |
| multitap | 2 |
| timesweep | 2 |
| sync | 5 runs ≈ 12 |
| bandwidth | 3 settings + probe ≈ 12 |
| noisefloor | 12 |
| clockbleed | 8 |
| runaway | 3 × 15 = 45 |
| strength, filter, resonance, absorb | ≈ 25 |
| clear, zipper, passthrough | ≈ 20 |
| nondeterminism | 6 engines × 4 = 24 |
| agitation | 32 + 5 + 8 ≈ 45 |
| follower, interference | 5 + 12 + 4 + 20 ≈ 41 |
| timemod | 4 × 2.5 = 10 |
| generative | 2 × 60 = 120 |
| samplerate | 4 rates × ~15 s, 192 k counts 4× ≈ 120 |
| blocksize | 5 × 4 = 20 |
| cpu | 30 + 10 + 40 (192 k) = 80 |
| pin | 3 |
| **Total** | **≈ 615 audio-seconds ≈ 3-9 s wall** |

That leaves a 3-10× margin under the 30 s target. Keep it that way: every new scenario states its `approxAudioSeconds`; `EngineTest list` sums them and prints a warning if the total exceeds 1500 (≈ 20 s wall at the slow estimate); `--quick` trims the three heavy ones to ≈ 350 total for the inner loop. Never lengthen a render to make a threshold pass: shorten the settle by choosing a faster Time setting instead.

`build.sh` addition (after `cmake --build`, before pluginval):

```bash
echo "--- EngineTest ---"
"build/EngineTest_artefacts/${CONFIG}/EngineTest"
echo "--- ProcessorTest ---"
"build/ProcessorTest_artefacts/${CONFIG}/ProcessorTest"
```

`set -e` already makes a non-zero exit stop the script. Also worth lifting from the sibling's `build.sh`: the warning grep `^[^ ]*/(Source|Tests)/[^ ]*: (warning|error):` over the build log, since clang prints the path before the word "warning" and a naive grep passes forever.

---

## 4. Manual gates (paste into `docs/PROGRESS.md` under `## Phase gates`)

Automated items name their scenario; the rest need hands and ears. Procedures once, at the top of the section.

```markdown
## Phase gates

Procedures referenced below:
- **Standalone rig**: `open build/Dybbuk_artefacts/RelWithDebInfo/Standalone/Dybbuk.app`,
  Options -> Audio Settings -> pick the interface, input = guitar DI channel,
  48 kHz / 128. No rescan needed; quit and relaunch after every build.
- **Live rig**: Ableton Live 12 only rescans plugins at startup, so after
  `./build.sh` **quit Live completely and reopen it** before testing. Insert
  Dybbuk (VST3 and AU are separate entries; test both) on an audio track
  with the DI, monitoring In. Null test = duplicate the track, bypass the
  copy, insert Utility (phase invert) after it, sum: silence.
- **Listening tells** come from the plan's tuning references (section 5).
  Each one states what a FAIL sounds like; if it fails, stop and tune, do
  not move on.

### Phase 0 — Skeleton
- [ ] `./build.sh` completes with zero warnings from `Source/` and `Tests/`
- [ ] pluginval strictness 5 passes
- [ ] `auval -v aumf Dybk Fxci` passes
- [ ] Live rig: appears in the browser (both formats), passes audio unchanged with Blend 0 (null test silent)

### Phase 1 — DSP core
- [ ] `EngineTest` green: delaytime multitap timesweep bandwidth noisefloor clockbleed runaway strength filter resonance absorb clear zipper passthrough blocksize samplerate cpu
- [ ] `EngineTest pin` pinned (kPinnedHash set) at the end of this phase
- [ ] CPU measured (`EngineTest cpu`, 48 k/128, busiest setting): ____ % of one core, max block ____ us
- [ ] Tell 1, standalone, Blend 50 %, Decay 0.6, Time 0.3: sweep Time slowly by hand while a chord rings. PASS: the repeats glide in pitch like a tape machine changing speed. FAIL sounds like: two clean delay times crossfading or stuttering with no pitch change.
- [ ] Tell 2, standalone, Time at max, Decay 0.5, Filter open: play one note. PASS: repeats are dark and hissy and there is a faint ticking/burbling under them that gets louder as Time is pushed further. FAIL: repeats stay bright, or the noise is a constant broadband hiss with no tick.
- [ ] Tell 3, standalone, Decay 1.15, any Time: play a chord, stop. PASS: the loop swells into a warm saturated drone that stays there. FAIL: a buzzing square-wave edge (digital clip), a rising howl that keeps getting louder, or the loop dying out.
- [ ] Patch Corner "Echo-Verb" (short Time, moderate Decay, Filter dark): reads as a dark room, not a slapback
- [ ] Patch Corner "Wow and Flutter" (Blend 50 %, Absorb or Filter high): a picked chord comes back as aged, wobbling tape
- [ ] No clicks by hand: with a chord ringing, wiggle every knob fast in the standalone (Zipper scenario covers steps; this covers knob throw)
- [ ] Clear (double-click Decay or the button) silences a runaway loop instantly with no thump

### Phase 2 — Parameters and state
- [ ] `ProcessorTest` green: state presets bypass readouts order clearui
- [ ] Live rig: every parameter appears in the automation chooser with the readout matching the UI (screenshot the chooser: Time in ms/s, Filter in Hz, no three-decimal values)
- [ ] Live rig: first eight in Live's Configure panel and on Push are Time, Decay, Filter, Resonance, Absorb, Blend, Agitate, Speed
- [ ] Live rig: set every knob off default, toggle Sync on with 1/8, switch theme, save the set, quit Live, reopen, values and theme restored (both formats)
- [ ] Live rig: Sync follows a tempo change from 120 to 90 with a smear, not a jump; 2 bars at 60 BPM shows the clamped readout
- [ ] Live rig: copy-paste the device to another track keeps its state
- [ ] Live rig: host bypass (device on/off) mid-runaway: no click either way, loop still there when re-enabled
- [ ] pluginval strictness 10 passes; `auval` passes

### Phase 3 — Playability (modulation)
- [ ] `EngineTest` green: agitation follower interference timemod sync nondeterminism generative
- [ ] Tell 4, standalone: same settings, play the same chord twice with a Clear in between. PASS: the two repeats are recognisably the same but not identical (drift, different hiss grain). FAIL: bit-identical repeats, or so different the settings feel random.
- [ ] Tell 5, standalone, Time Mod up, Speed in the audio range, Time short: play a single note. PASS: metallic ring-mod-like clang whose pitch tracks the note. FAIL: a wobble or vibrato (control-rate modulation) instead of clang.
- [ ] Generative milestone by ear: no input, Decay 1.1, Agitate full, Time 0.55, Filter 3 kHz. Leave it five minutes. PASS: it plays itself, it changes, nothing repeats verbatim, and the crackle audibly rides the loop's level. FAIL: a static drone, a loop you can hum along to, or silence after a minute.
- [ ] Agitation gate mode fires once per picked note and rests between notes
- [ ] Played through the standalone for a full session with guitar; the feel notes below are resolved: Time knob throw (fine control via shift-drag), Decay's runaway zone reachable but not accidental, Clear reachable without looking

### Phase 4 — Presets and control
- [ ] Preset save / load / rename / delete / star from the header; prev/next arrows step in list order
- [ ] Four Patch Corner factory presets ship and `ProcessorTest presets` loads them
- [ ] Bypass state survives a preset load (a preset never takes the plugin out of circuit); Clear is momentary and never saved
- [ ] Live rig: Clear mapped to a MIDI button via Live's mapping works while a loop runs away

### Phase 5 — UI
- [ ] `UISnapshot` renders, and every image was opened and looked at: default light, default dark, active (ember lit, OUT meter moving), Decay in the runaway zone (red, readout "1.15 runaway"), Sync on (note-value readout), hovered readout strip, modulated-knob state, Agit gate mode
- [ ] Value readouts checked on the screenshots for every knob at min, default, max: two decimals maximum, units baked in, no em dashes anywhere in UI copy (`grep -rn "—" Source/ui` is empty)
- [ ] Readout strip shows name + value for every control on hover and drag (replaces the template's tooltip gate; the design has no floating tooltips)
- [ ] Window scales and stays aspect-locked; theme toggle persists across reopen
- [ ] Ember animation reads the loop energy: dark at Decay 0 after silence, glowing steadily in runaway, breathing with Agitation

### Phase 6 — Validation matrix
- [ ] Sample rates 44.1 / 48 / 96 / 192 kHz in Live's audio prefs: Tell 1 and Tell 2 unchanged, `EngineTest samplerate` green
- [ ] Buffer sizes 32 / 64 / 128 / 512 / 2048 in Live: no dropouts at the busiest preset; `EngineTest blocksize` green
- [ ] Mono -> stereo: insert on a mono track, output identical on L and R
- [ ] Offline render of a Time automation sweep matches realtime by ear and within 1 dB RMS
- [ ] Automation across a bounce: Time, Decay, Filter, Blend, Bypass all follow their lanes with no clicks
- [ ] State persistence; device copy-paste
- [ ] Host bypass mid-runaway: no click, loop intact on return
- [ ] 30-minute soak at Decay 1.15, Agitate full, Time Mod full: no NaN (sound never stops), CPU flat in Activity Monitor, memory flat
- [ ] Two instances chained on one track, second one fed by the first's runaway, stays bounded
```

---

## 5. Decisions the engine designer must confirm (they change a scenario if decided otherwise)

1. **Time Mod source and placement**: Agitation -> Time at audio rate, added to fs_chip after the `kTimeSmoothMs` smoother, independent of the Agitate macro. `timemod` is written for this; if a dedicated internal sub-oscillator becomes the source instead, the scenario needs a settable rate for it.
2. **Dry path is pre-Strength**: `passthrough` asserts a null at Blend 0 regardless of Strength. If the hardware-true choice (dry = post-preamp) is preferred, the Blend 0 check becomes "identical to the drive stage alone" and the Phase 0 null test moves to Strength 0.
3. **Clear is fade-flush-fade (2 ms / 5 ms)** rather than instantaneous; `clear` has a click check that goes if the instantaneous version is chosen.
4. **Bypass survives preset loads**; the template's `finishLoad` currently forces performance params to 0. `ProcessorTest presets` pins whichever resolution is adopted.
5. **`kMemorySamples` = 5502** rather than 5504 so three stages are equal; both satisfy every tolerance here.
6. **Block-size bit-identity** for power-of-two sizes is a real engine constraint (stream-aligned `kControlChunk`, per-sample smoothing, no per-block-length math). `blocksize` treats a hash mismatch as a bug, not noise.