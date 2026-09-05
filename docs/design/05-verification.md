# 05. Verification plan

This is the implementers' spec for the two console harnesses (`EngineTest`,
`ProcessorTest`), the shared measurement helpers, the run-all mode, and the
manual gates that go into `docs/PROGRESS.md`. It reproduces the verification
design (`tests.md`) with every scenario kept, criteria tightened where a bug
could have slipped through, and the judges' grafts folded in: the sibling
scenarios from the non-winning PT designs (`smear` envelope stretch, `thd`,
`threestep` darkening, `nan`, the relative Clear criterion, the grid-vs-half-grid
sideband metric, the exact-zero determinism control) and from the modulation
verdict (the control-rate line check, the drift bound in rail-to-rail terms,
the split generative gate, the chaos triad, the alias-line assertion).

Precedence when inputs disagreed: judge winners and grafts, then CLAUDE.md,
then `dybbuk-plan.md`. Section 10 lists every judge fatal flaw and the line
here that catches it; section 12 lists the assumptions other sections must
honour and the questions left open (each resolved with the cheapest-to-change
option, usually a compile-time constant with both paths).

House idiom throughout: named scenarios selected by argv, `check (ok, what,
measured)` lines that print the number even when they pass, a global failure
count that turns the exit code non-zero, FNV-1a over float bits for
bit-identity, a second console target for everything that lives above the
engine (Infinite Sustainer's `PresetProbe` is the model). No em dashes in
anything the harnesses print.

---

## 1. Targets, build integration, and the engine test contract

### 1.1 Targets

| Target | Sources | Links | Purpose |
|---|---|---|---|
| `EngineTest` | `Tests/EngineTest.cpp` + `ENGINE_SOURCES` | `juce_dsp`, `juce_audio_formats` (for `render`) | Drives `DybbukEngine` like a host. 34 scenarios, sections 4 A to F. |
| `ProcessorTest` | `Tests/ProcessorTest.cpp` + `PLUGIN_SOURCES` | as `UISnapshot` (`juce_audio_utils`, `juce_dsp`, `JUCE_MODAL_LOOPS_PERMITTED=1`, `JucePlugin_Name`, `JucePlugin_VersionString`) | APVTS, state, presets, bypass crossfade, readouts, parameter order, host sync, the Clear mailbox through the processor. Section 6. |
| `UISnapshot` | existing | existing | Renders the editor states listed in `04-ui.md` section 14. Section 7 says which images the gate needs. |

CMake additions:

```cmake
juce_add_console_app(ProcessorTest PRODUCT_NAME "ProcessorTest")
target_sources(ProcessorTest PRIVATE Tests/ProcessorTest.cpp ${PLUGIN_SOURCES})
target_compile_definitions(ProcessorTest PRIVATE
    JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_MODAL_LOOPS_PERMITTED=1
    "JucePlugin_Name=\"Dybbuk\"" "JucePlugin_VersionString=\"0.1.0\"")
target_link_libraries(ProcessorTest PRIVATE juce::juce_audio_utils juce::juce_dsp
    PUBLIC juce::juce_recommended_config_flags juce::juce_recommended_warning_flags)

target_link_libraries(EngineTest PRIVATE juce::juce_audio_formats)   # render subcommand

# 1-vs-3 stage A/B (graft from PT design 2). Default 3; the harness reads
# DybbukEngine::kNumStages, so every tap expectation follows the define.
set(DYBBUK_PT_STAGES 3 CACHE STRING "PT stages in series (1 or 3)")
target_compile_definitions(Dybbuk PUBLIC DYBBUK_PT_STAGES=${DYBBUK_PT_STAGES})
target_compile_definitions(EngineTest PRIVATE DYBBUK_PT_STAGES=${DYBBUK_PT_STAGES})
target_compile_definitions(ProcessorTest PRIVATE DYBBUK_PT_STAGES=${DYBBUK_PT_STAGES})
target_compile_definitions(UISnapshot PRIVATE DYBBUK_PT_STAGES=${DYBBUK_PT_STAGES})
```

`build.sh` runs both test binaries after `cmake --build` and before pluginval,
and gates warnings from our own code with the sibling's grep (clang prints the
path BEFORE the word "warning", so a naive `grep "warning:.*Source/"` passes
forever):

```bash
BUILD_LOG=$(mktemp); trap 'rm -f "$BUILD_LOG"' EXIT
cmake --build build 2>&1 | tee "$BUILD_LOG"
if grep -E '^[^ ]*/(Source|Tests)/[^ ]*: (warning|error):' "$BUILD_LOG"; then
    echo "FAILED: warnings from our own code (Source/ and Tests/ must be clean)"; exit 1
fi
echo "--- EngineTest ---";    "build/EngineTest_artefacts/${CONFIG}/EngineTest"
echo "--- ProcessorTest ---"; "build/ProcessorTest_artefacts/${CONFIG}/ProcessorTest"
```

`set -euo pipefail` already makes a non-zero exit stop the script. The full
run is the gate; `EngineTest --quick` is the inner loop (section 5).

### 1.2 The engine surface the harness compiles against

The scenarios assume this surface. If the engine or modulation section names
something differently, the harness changes in exactly three places: `base()`,
`withTime01()`, and the `Trace` snapshot in `render()`. If a piece is
missing, the scenario that needs it cannot be written; the piece is not
optional.

```cpp
class DybbukEngine
{
public:
    struct Params
    {
        // Time. The PROCESSOR resolves free vs sync into one number (params section);
        // the engine smooths log2 (timemap::kMemorySamples / delayTargetSec) with a
        // kTimeSmoothMs one-pole and never sees a division table or a play head.
        float delayTargetSec = 0.1187f;   // timemap::delaySecondsForTime01 (0.30), test baseline
        bool  timeSynced   = false;       // UI publish only, no engine branch
        bool  syncClamped  = false;       // same
        float timeMod01    = 0.0f;        // 0..1, scales the Time column of the mod matrix
        float decay        = 0.0f;        // 0..1.15 linear feedback gain
        float filterHz     = 18000.0f;    // 20..18000, in-loop SVF cutoff
        float resonance01  = 0.0f;        // 0..1, self-oscillation from kResSelfOsc01 (0.85..0.90)
        float absorb01     = 0.0f;        // 0..1
        float blend01      = 1.0f;        // 0..1, equal power
        float agitate01    = 0.0f;        // macro over the hero routes
        float agitSpeedHz  = 1.0f;        // 0.016..1000
        int   agitMode     = 0;           // 0 loop, 1 gate
        float strengthDb   = 0.0f;        // 0..40, drive tied to gain
        float outDb        = 0.0f;        // <= kOutFloorDb + 0.05 means -inf (engine mutes through its own 20 ms smoother)
        float tonesLevel01 = 0.0f, tonesPitchHz = 110.0f, spread01 = 0.0f;   // Phase 4, harness leaves at defaults
        double bpm = 120.0; float beatsPerBar = 4.0f; double ppqPosition = 0.0;
        bool  ppqValid = false, transportPlaying = false;                     // Agitation host-sync later
        bool  bypass = false;             // engine keeps running and is fed SILENCE while bypassed; wet still written
    };

    // Hidden tunables. Plain struct read once per block; written only before
    // prepare() or by tests. Production never touches it. Every field scales the
    // corresponding named constant (1 = the tuned value); 0 removes the effect.
    struct Trims
    {
        float chipNoise  = 1.0f;   // analog hiss inside each stage
        float dither     = 1.0f;   // TPDF before the quantiser
        float quantize   = 1.0f;   // 0 = quantiser bypassed (float memory)
        float bleed      = 1.0f;   // clock pulse train below pt::kBleedOnsetHz
        float drift      = 1.0f;   // always-on slow random on fs_chip (modk::kDriftTimeOct)
        float loopSat    = 1.0f;   // 0 = loop saturator is an identity (its DC blocker stays)
        float inputDrive = 1.0f;   // 0 = Strength is a pure gain (no soft clip)
        static Trims clean()    { return { 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 1.0f, 1.0f }; } // deterministic, quantiser on
        static Trims chipOnly() { return { 0.0f, 1.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f }; } // the chip alone (thd)
    };
    Trims trims;

    static constexpr int   kNumStages       = DYBBUK_PT_STAGES;   // 3
    static constexpr bool  kDryIsPreStrength = true;              // Blend 0 is a null regardless of Strength
    static constexpr float kOutFloorDb      = -60.0f;             // params section: bottom of Out renders -Inf

    void prepare (double sampleRate, int maxBlockSize);           // repeat-safe; snaps every smoother to the next block's Params
    void process (juce::AudioBuffer<float>&, const Params&);     // 1 or 2 channels; ANY length (chunks internally above maxBlockSize)
    void requestClear() noexcept;                                 // atomic counter mailbox, honoured at the next process()
    void seedForTests (juce::uint32 seed);                        // fans one seed to every RNG: chip noise, dither, drift, interference
                                                                  // (default construction seeds from juce::Random::getSystemRandom())
    void setRoutingForTests (const ModMatrix::Routing&);          // replaces ModMatrix::heroRouting() for this instance

    // Engine -> UI, written once per block, read by the harness once per block.
    std::atomic<float> uiLoopEnergy;        // 0..1, log-mapped loop level (ember); 0 after Clear + silence
    std::atomic<float> uiOutputLevel;       // linear peak after Out
    std::atomic<float> uiDelaySeconds;      // smoothed delay in force, PRE-modulation (free or synced)
    std::atomic<float> uiTimeModOct;        // block mean of the audio-rate Time modulation, octaves
    std::atomic<float> uiFilterModOct;      // applied cutoff offset from the knob, octaves
    std::atomic<float> uiDecayMod;          // applied decay offset from the knob
    std::atomic<float> uiModSource[ModMatrix::numSrc];   // last control values: agitation (block mean), follower01, interference wander, drift
    std::atomic<int>   uiClearsServed;      // increments when a Clear flush has happened
};
```

Header-only constants the harness includes directly (no duplication of the
mapping anywhere in the tests):

| Header | Used symbols | Start values the scenarios were calibrated against |
|---|---|---|
| `Source/dsp/TimeMap.h` (`namespace timemap`) | `kMemorySamples`, `kDelayMinSec`, `kDelayMaxSec`, `kFsChipMaxHz`, `kFsChipMinHz`, `delaySecondsForTime01 (t)`, `time01ForDelaySeconds (s)`, `fsChipForDelaySeconds (s)`, `kDivisions[kDivisionCount]`, `divisionIndexForTime01 (t)`, `syncedDelaySeconds (index, bpm, beatsPerBar)`, `kTimeSmoothMs` | 5504, 0.02752 s, 3.6 s, 200 kHz, 1528.9 Hz, 14 divisions, 20 ms |
| `Source/dsp/ChipConstants.h` (`namespace pt`) | `kTapWeights[kNumStages]` (normalised, sum 1), `kCtrlInterval`, `kBleedOnsetHz`, `kChipNoiseDbAtMaxClock` / `kChipNoiseDbAtMinClock` (printed next to `noisefloor`) | {0.392, 0.333, 0.275}, 16, 20000, -86 / -48 dBFS |
| `Source/dsp/ModConstants.h` (`namespace modk`) | `kControlBlock`, `kDstScaleTimeOct`, `kDriftTimeOct`, `kFollowerAttackMs`, `kFollowerReleaseMs`, `kHeroAgitFilter`, `kHeroIntfTime`, `kHeroFollowerDecay`, `kMacroScalesTimeColumn` | 32, 2.0, 0.004, 5, 100, 0.75, 0.5, 0.30, true |
| `Source/dsp/ModMatrix.h` | `ModMatrix::Routing`, `ModMatrix::heroRouting()`, enums `srcAgitation, srcFollower, srcInterference, srcDrift, numSrc` and `dstTime, dstFilter, dstResonance, dstDecay, dstAbsorb, dstBlend, dstStrength, numDst` | |

Numbers the scenarios rely on, from the mapping above (delay = 27.52 ms x
130.81^t, fs_chip = 5504 / delay; every scenario recomputes them from
`timemap` at run time, this table is for reading the output):

| time01 | fs_chip | loop period T | T/3 | note |
|---|---|---|---|---|
| 0.00 | 200.0 kHz | 27.5 ms | 9.2 ms | shortest, clean |
| 0.20 | 75.5 kHz | 72.9 ms | 24.3 ms | timemod carrier setting |
| 0.30 | 46.4 kHz | 118.7 ms | 39.6 ms | timesweep start, test baseline |
| 0.35 | 36.3 kHz | 151.5 ms | 50.5 ms | runaway setting |
| 0.40 | 28.5 kHz | 193.3 ms | 64.4 ms | interference / drift setting |
| 0.47 | 20.2 kHz | 271.9 ms | | clock-bleed onset (pt::kBleedOnsetHz = 20 kHz) |
| 0.50 | 17.5 kHz | 314.8 ms | 104.9 ms | multitap setting |
| 0.55 | 13.7 kHz | 401.6 ms | | generative setting |
| 0.60 | 10.7 kHz | 512.5 ms | 170.8 ms | timesweep end |
| 0.75 | 5.17 kHz | 1.065 s | | bleed mid |
| 1.00 | 1.53 kHz | 3.600 s | 1.200 s | longest, destroyed |

A halving of fs_chip is a step of `ln 2 / ln (kDelayMaxSec / kDelayMinSec)`
= 0.1422 in time01; the harness derives it (`time01ForDelaySeconds (2 * T)`),
it never types it.

Assumptions baked into three scenarios (confirmed against the winning
designs, restated in section 12): (a) the dry path is the untouched input
(`kDryIsPreStrength`), so Blend 0 is a null at any Strength; (b) Time Mod
scales the Time column of the matrix and the harness opens an
Agitation -> Time cell through `setRoutingForTests`, so the audio-rate tests
do not depend on which source ships routed to Time; (c) `Params` defaults
above are test baselines, not user defaults (CLAUDE.md: defaults belong to
the user; the APVTS layout owns them).

---

## 2. The rig (implement once, anonymous namespace of `EngineTest.cpp`)

```cpp
namespace
{
constexpr double kSr    = 48000.0;
constexpr int    kBlock = 128;
constexpr int    kCurrentPhase = 1;       // bump in the commit that closes each phase (section 5)
constexpr juce::uint64 kPinnedHash = 0;   // set at the Phase 2 gate (pin scenario)
int failures = 0;                         // per check, not per scenario
bool gQuick = false;

void check (bool ok, const char* what, const juce::String& measured)
{
    std::printf ("  [%s] %-56s %s\n", ok ? "ok" : "FAIL", what, measured.toRawUTF8());
    if (! ok) ++failures;
}
void report (const char* what, const juce::String& measured)    // a number worth printing, no verdict
{ std::printf ("  [--] %-56s %s\n", what, measured.toRawUTF8()); }
void pending (const char* what)                                 // phase-gated line, printed, never counted
{ std::printf ("  [..] %-56s pending (phase > kCurrentPhase)\n", what); }

using Params = DybbukEngine::Params;

struct Trace   // one entry per processed block, snapshot of the atomics after process()
{
    std::vector<float> energy, delaySec, timeModOct, filterModOct, decayMod;
    std::vector<float> src[ModMatrix::numSrc];
    std::vector<int>   clearsServed;
};

struct Run
{
    std::vector<float> in, L, R;   // mono input as fed (channel 0), both outputs, host rate
    Trace ui;
    double sr = kSr; int block = kBlock;
    bool nonFinite = false;        // any non-finite output sample anywhere
    double firstNonFiniteSec = -1.0;
    double meanBlockUs = 0.0, maxBlockUs = 0.0;
    int blocks = 0;
};

struct Rig
{
    double sr = kSr;
    int block = kBlock;                                       // maxBlockSize passed to prepare()
    juce::int64 seed = 42;                                    // < 0: leave the engine's own seeding alone
    DybbukEngine::Trims trims = DybbukEngine::Trims::clean();
    std::optional<ModMatrix::Routing> routing;                // nullopt = shipped heroRouting()
    std::function<float (double t)> input;                    // mono source; nullptr = silence
    std::function<float (double t)> inputR;                   // optional separate right channel (passthrough)
    std::function<void (double t, Params&, DybbukEngine&)> perBlock;   // schedule: steps, Clear, mode flips
    std::function<int (int blockIndex)> blockSize;            // optional per-call size (may exceed block: chunking check)
    int channels = 2;                                         // 1 for the mono-buffer check
};

Run render (const Rig& rig, double seconds, Params p);
```

`render`:

```
engine = fresh DybbukEngine; engine.trims = rig.trims
if rig.seed >= 0: engine.seedForTests ((uint32) rig.seed)
if rig.routing:   engine.setRoutingForTests (*rig.routing)
engine.prepare (rig.sr, rig.block)
total = round (seconds * sr); produced = 0; b = 0
preallocate run.in/L/R to total and every Trace vector to total / 1 + 1 (nothing allocates inside the timed region)
scratch = AudioBuffer (rig.channels, max (rig.block, largest blockSize the schedule can return, 4096))
while produced < total:
    n = rig.blockSize ? rig.blockSize (b) : rig.block;  n = min (n, total - produced)
    t = produced / sr
    for i in 0..n-1: x = rig.input ? rig.input (t + i / sr) : 0
                     scratch[0][i] = x;  if channels > 1: scratch[1][i] = rig.inputR ? rig.inputR (t + i / sr) : x
                     run.in.push (x)
    if rig.perBlock: rig.perBlock (t, p, engine)
    view = AudioBuffer<float> (scratch.getArrayOfWritePointers(), channels, n)
    { juce::ScopedNoDenormals nd;                        // mirrors processBlock; every scenario runs under FTZ
      t0 = Time::getHighResolutionTicks(); engine.process (view, p); dt = ticks -> us }
    mean/max update; append L (and R, or L again if mono); flag non-finite (record first time)
    snapshot the atomics into run.ui; produced += n; ++b
```

Input generators (all pure functions of `t`, never of call order, so a run is
identical however the schedule chops blocks):

| Generator | Definition |
|---|---|
| `sine (hz, amp, phase0 = 0)` | `amp * sin (2 pi hz t + phase0)` |
| `burst (hz, amp, t0, ms)` | `sine` times a Hann window over `[t0, t0 + ms]`, 0 elsewhere |
| `click (t0, amp, ms = 1)` | rectangular `amp` over `[t0, t0 + ms]` |
| `impulse (t0, amp)` | `amp` on the single sample `round (t0 sr)` |
| `noise (amp, seed)` | `amp * bipolar (hash32 (uint32 (round (t sr)) * 2654435761u ^ seed))` with `hash32` = xorshift-multiply mix; deterministic per sample index |
| `gate (t0, t1, gen)` | `gen (t)` inside `[t0, t1)`, 0 outside |
| `sum (a, b)` | `a (t) + b (t)` |
| `steppedSines ({ {hz, seconds}... }, amp)` | consecutive segments, phase-continuous |
| `base()` | the `Params` defaults from the contract (test baseline) |
| `withTime01 (Params&, t)` | `p.delayTargetSec = timemap::delaySecondsForTime01 (t)`; returns the reference |
| `loopSeconds (t)` | `timemap::delaySecondsForTime01 (t)` |
| `fsChip (t)` | `timemap::kMemorySamples / loopSeconds (t)` |
| `time01ForFs (hz)` | `timemap::time01ForDelaySeconds (timemap::kMemorySamples / hz)` |
| `divisionIndexByName (name)` | linear scan of `timemap::kDivisions`; a missing name is a FAIL line, not a crash |
| `heroWith (src, dst, depth)` | copy of `ModMatrix::heroRouting()` with one cell replaced |
| `onlyCell (src, dst, depth)` | all-zero `Routing` with one cell set |

---

## 3. Shared measurement helpers

All windows are in seconds and clamp to the vector (`i0 = clamp (round (from
sr), 0, n)`, `i1 = clamp (round (to sr), i0, n)`); every helper returns 0 on
an empty window instead of dividing by zero, so a broken render reads as a
FAIL on the value, not a crash. `ProcessorTest` gets the subset it needs by
`#include "TestHelpers.h"` (a header in `Tests/`, inline functions, the only
shared file between the two harnesses).

| Helper | Algorithm |
|---|---|
| `double rms (v, from, to)` | `sqrt (sum x^2 / count)` |
| `double dB (x)` | `20 log10 (max (1e-12, x))` |
| `double peak (v, from, to)` | `max |x|` |
| `double crest (v, from, to)` | `peak / rms` (0 if rms == 0) |
| `double maxStep (v, from, to)` | `max |x[i] - x[i-1]|` for `i in (i0, i1)`: the click detector |
| `double dcOffset (v, from, to)` | mean |
| `double freqZeroCross (v, from, to)` | sign changes (positive vs non-positive) / 2 / seconds; coarse, robust |
| `double freqPrecise (v, from, to)` | rising crossings with linear interpolation `tc = (i - 1) + x[i-1] / (x[i-1] - x[i])`; `f = sr (count - 1) / (tLast - tFirst)`; 0 unless count >= 3; about 0.1 % on a clean tone (Teder) |
| `std::vector<std::pair<double,double>> instFreqTrack (v, from, to)` | `{t, f}` per pair of consecutive interpolated rising crossings, `f = sr / (tk - tk-1)`; used where a glide is too fast for `freqPrecise` |
| `double goertzel (v, from, to, hz)` | Hann-windowed Goertzel: `w = 2 pi hz / sr; c = 2 cos w; s0 = x_i * hann_i + c s1 - s2` (Teder's recurrence); amplitude `= 4 sqrt (re^2 + im^2) / N` (the 4 = 2 for a real sinusoid times 2 for the Hann coherent gain). Hann, not rectangular, so a harmonic 8 bins away leaks under -60 dB |
| `struct Spectrum { std::vector<float> mag; double binHz; }` `spectrum (v, from, to, order = 12)` | `juce::dsp::FFT fft (order)`, `size = 1 << order`, Hann, frames hopped by `size / 2`, each frame copied into a `2 * size` zeroed scratch, `performFrequencyOnlyForwardTransform`, `mag[k]` accumulated for `k < size / 2` and divided by frame count and by `size / 4`; `binHz = sr / size` |
| `double centroid (spec, fLo, fHi)` | `sum f_k mag_k / sum mag_k` over bins in the band (magnitude weighted) |
| `double bandEnergyDb (spec, fLo, fHi)` | `10 log10 (sum mag_k^2)` |
| `std::vector<int> strongestBins (spec, fLo, fHi, count, excludeHz, excludeWidthHz)` | candidate bins in the band excluding `|f - excludeHz| < excludeWidthHz`, sorted by `mag` descending, first `count` |
| `juce::uint64 fnv1a (v, from = 0, to = 1e9)` | `h = 1469598103934665603ULL`; per sample `memcpy (bits, &x, 4); h = (h ^ bits) * 1099511628211ULL` (Infinite Sustainer idiom) |
| `std::vector<float> envelope (v, attackSec, releaseSec)` | one-pole on `|x|`: `e += (|x| - e) * (|x| > e ? aA : aR)`, `a = 1 - exp (-1 / (tau sr))` |
| `struct Peak { double t, amp; }` `pickPeaks (env, from, to, minSepSec, rel)` | local maxima (`e[i] >= e[i-1] && e[i] > e[i+1]`) above `rel * max (env in window)`, accepted greedily by amplitude unless within `minSep` of an accepted one, returned sorted by time |
| `struct Lag { double seconds, coefficient; }` `xcorrLag (out, ref, refStart, refLen, searchFrom, searchTo)` | template = `ref[refStart, refStart + refLen)`; for each lag `L` in samples over `[searchFrom, searchTo)`: `c (L) = sum tmpl_j out_{refStart + L + j} / (||tmpl|| ||out segment||)`; argmax with parabolic interpolation on its neighbours; returns `{L / sr, c}` |
| `double pearson (a, b)` | standard, over `min (a.size, b.size)` |
| `double autocorrMax (series, minLag, maxLag)` | mean removed, divided by variance, `max |rho (L)|` over `L in [minLag, maxLag]` (lags in samples of the series) |
| `std::vector<float> demodPitch (v, from, to, fHz)` | `i = x cos (2 pi f t)`, `q = -x sin (2 pi f t)`, both through two cascaded one-poles at 20 Hz (66 dB down at 2f for f >= 220 Hz), `phi = unwrap (atan2 (q, i))`, `dev = (phi[n] - phi[n-1]) sr / (2 pi)`, `cents = 1200 log2 (1 + dev / f)`; first 0.2 s discarded (filter settle). Resolves 0.1 cent on the lo-fi wet path where crossings cannot |
| `double thd23 (v, from, to, f0)` | `sqrt (G (2 f0)^2 + G (3 f0)^2) / G (f0)` with `G = goertzel` |
| `double combPeakHz (targetHz, T)` | `k = max (1, round (targetHz * T / kNumStages)); return k * kNumStages / T` (taps at multiples of T/3 make the tap sum a comb with peaks every 3/T; test tones sit on a peak so the comb never confounds a level) |
| `std::vector<float> hopRms (v, from, to, hopSec)` | RMS per hop |
| `double cv (series)` | standard deviation / mean |
| `bool allFinite (v)` | |
| `double flatTopRuns (v, from, to, pk)` | count of runs of >= 3 consecutive samples with `|x| > 0.9 pk` and `|x[i] - x[i-1]| < 1e-4` (hard-clip detector) |

Rules: nothing in the helpers allocates inside a timed region; `spectrum`
reuses one scratch per call; `xcorrLag` cost is `refLen x searchRange` and
the longest use (40 ms template over a 0.2 T window at T = 3.6 s) is 6.6e7
MACs, about 0.1 s.

---

## 4. EngineTest scenarios

Format per scenario: argv token (phase), purpose, input, fixed params
(anything not listed = `base()`), trims, measurement, PASS with `expected:`
lines, what FAIL means. "Clean" = `Trims::clean()`; "production" = engine
defaults; "chipOnly" = `Trims::chipOnly()`. Every scenario prints its numbers
whether or not it passes; the human copies them into PROGRESS.md.

A scenario tagged with a phase above `kCurrentPhase` runs, prints its lines
as `[..] pending`, and does not count toward the exit code. Sub-cases can be
tagged the same way (`runaway` (b) and (d) need the modulation system).

### A. The PT core

#### `delaytime` (phase 1)
- Purpose: the Time readout is the truth. Loop period T = N / fs_chip at three settings, and the feedback repeat period equals T.
- Input: one Hann burst at t0 = 0.1 s per setting: `{time01, burstHz, burstMs} = {0.0, 500, 6}, {0.5, 500, 6}, {1.0, 100, 40}` (the long setting needs a tone under the 760 Hz chip Nyquist), amp 0.5. Render `2.5 T + 0.5` s (9.5 s at the long setting).
- Params: decay 0, blend 1, filter 18k, absorb 0; a second pass at time01 0.5 with decay 0.6 for the repeat period.
- Trims: clean.
- Measure: `xcorrLag (out, in, t0, burstMs, 0.9 T, 1.1 T)` gives the third-tap arrival (the window excludes tap 2 at 0.667 T). Report the coefficient. Repeat pass: `xcorrLag` in `[1.9 T, 2.1 T]` minus the one in `[0.9 T, 1.1 T]`. Also `ui.delaySec` on the last block.
- PASS: `|lag - T| < max (1 ms, 0.005 T)` at all three settings; coefficient > 0.3 at each; repeat period within the same tolerance of T; `ui.delaySec` within 0.1 % of `loopSeconds (t)` at each setting; `loopSeconds (0)` in [25, 30] ms and `loopSeconds (1)` in [3.5, 3.8] s (pins the range so nobody quietly narrows it).
- expected: T = 27.5 ms / 314.8 ms / 3.600 s; lag error under 0.5 ms at 0.0 (three chip ticks plus 0.4 ms of fixed-filter group delay), about 3 ms at 1.0 (2 ms of one-tick-per-stage latency plus the 760 Hz reconstruction filters), tolerance there 18 ms; repeat period 314.8 ms +- 1.6 ms.
- FAIL means: phase accumulator ratio wrong (fs_chip/fs_host inverted or off by the stage count), stage memory not N/3, or the readout mapping and the DSP disagree.

#### `multitap` (phase 1)
- Purpose: `kNumStages` series stages with summed taps: exactly `kNumStages` echoes at k T/3, sensible weights, and later taps darker (graft: PT design 2 `threestep`).
- Input: `click (0.1, 0.5, 1 ms)`. time01 0.5 (taps at 104.9 / 209.9 / 314.8 ms). Render 0.8 s; second pass decay 0.7, render 1.2 s.
- Params: decay 0, blend 1, filter 18k, absorb 0.
- Trims: clean.
- Measure: `envelope (out, 1 ms, 3 ms)`, `pickPeaks (env, t0 + 0.02, t0 + 0.5, 20 ms, 0.15)`; per peak, `centroid (spectrum (out, tk - 0.005, tk + 0.025, 10), 100, 12000)`.
- PASS: peak count == `kNumStages`; peak k at `t0 + k T / 3 +- (3 ms + 0.01 T)`; amplitude ratios tap2/tap1 in [0.35, 1.2] and tap3/tap1 in [0.2, 1.2] (`pt::kTapWeights` plus the cumulative loss of one more set of stage filters per tap); centroid (tap3) < centroid (tap1) (each stage carries its own filters, so later taps are darker); decay-0.7 pass: additional peaks within the same tolerance of 4T/3, 5T/3 and 2T (present under both feedback topologies, see section 12). With `DYBBUK_PT_STAGES=1`: exactly one peak at T.
- expected: peaks at 204.9 / 309.9 / 414.8 ms; tap2/tap1 about 0.7, tap3/tap1 about 0.5; centroid tap1 about 2.5 kHz falling to about 1.8 kHz by tap 3.
- FAIL means: one peak = taps not summed or stage count 1; wrong spacing = stage memory unequal; four or more peaks at decay 0 = a stage writes twice or the feedback path is live at decay 0; equal centroids = the stage filters are shared instead of per stage.

#### `timesweep` (phase 1)
- Purpose: a Time change repitches the buffer (tape smear), the glide is smooth, and nothing crossfades.
- Input: continuous 440 Hz sine amp 0.25. time01 0.30 for 1.0 s, then `withTime01 (p, 0.60)` at t = 1.0 s in `perBlock` (the engine's `kTimeSmoothMs` turns the step into a glide). Render 2.2 s.
- Params: decay 0, blend 1, filter 18k, absorb 0, agitate 0, timeMod 0.
- Trims: clean.
- Measure: expected ratio `fsChip (0.60) / fsChip (0.30)` = 130.81^-0.3 = 0.2317, so the buffered tone should read 440 x 0.2317 = 101.9 Hz until the first stage's old content is exhausted at `1.0 + loopSeconds (0.60) / 3` = 1.171 s. `freqPrecise` over [1.10, 1.17] (smoother settled; 7 cycles of 102 Hz) and over [1.70, 2.00] (all three stages refilled). Glide shape: `instFreqTrack` over [1.00, 1.20]. Per-block `ui.delaySec`.
- PASS: pitch in [1.10, 1.17] = 101.9 Hz +- 5 %; pitch in [1.70, 2.00] = 440 +- 1 %; consecutive `instFreqTrack` ratios never below 0.5 (a 20 ms glide in the log domain moves at most 0.37 nats per 5 ms period, ratio 0.69, whether the smoother is a linear ramp in log fs or a 20 ms one-pole; an unsmoothed jump gives 0.23 in one period); `ui.delaySec` monotonic non-decreasing from 0.1187 to 0.5125 s, per-block ratio `T[k+1] / T[k] <= 1.5` (the largest legitimate block step of a 20 ms linear-in-log ramp is e^0.196 = 1.22; a jump is 4.3), final within 1 %.
- expected: 101.9 Hz then 440 Hz; worst period ratio about 0.7; `ui.delaySec` reaches 0.51 s within 25 ms (linear ramp) or 100 ms (one-pole) of the step.
- FAIL means: pitch stays 440 throughout = a crossfading or fractional-tap delay, i.e. the core was not built as a virtual chip; a period ratio near 0.23 = the smoother is missing or applied to the wrong quantity; overshoot or a dip in `ui.delaySec` = smoothing in the wrong domain or sign.

#### `smear` (phase 1)
- Purpose: the sharpest crossfade detector (graft: PT design 1). A burst written at one clock and read at half the clock comes back twice as long at half the pitch, with no dropout.
- Input: `burst (440, 0.3, 0.1, 30 ms)`. time01 0.30; at t = 0.135 s (the burst is fully written into stage 1) `p.delayTargetSec = 2 * loopSeconds (0.30)` (fs halves). Render 0.5 s. Mirror pass: start at `2 * loopSeconds (0.30)` and halve the delay at 0.135 s.
- Params: decay 0, blend 1, filter 18k, absorb 0.
- Trims: clean.
- Measure: tap 1 event window W = [t0 + 0.035, t0 + 0.115] (tap 2 cannot arrive before t0 + 0.125 s); `hopRms (out, W, 10 ms)`; span = contiguous hops above 0.25 x max; `goertzel` at 220 and 440 over the span; min 5 ms RMS inside the span.
- PASS: span = 60 +- 12 ms (input was 30 ms: stretched, not crossfaded); `goertzel (220) >= goertzel (440) + 15 dB`; min 5 ms RMS inside the span > 0.4 x max (no dropout); mirror pass: span = 15 +- 6 ms and `goertzel (880) >= goertzel (440) + 12 dB` (the mirror needs the 880 Hz partial to survive the fixed 8.8 kHz and 4.5 kHz filters, it does).
- expected: span 60 ms at 220 Hz; a crossfading delay returns a 30 ms event at 440 Hz and fails the first two lines.
- FAIL means: span 30 ms = the read side crossfades between two delay taps; a dropout = the smoother reset the read pointer or the stage memory was cleared on a Time change.

#### `sync` (phase 2)
- Purpose: Time Sync is a number in seconds from the host tempo, clamped to the memory range, sent through the SAME smoother as free mode. The engine part is tested here; the play-head resolution is `ProcessorTest synchost`.
- Input: `burst (500, 0.5, 0.1, 6 ms)` for the delay checks; continuous 440 Hz sine for the switch pass.
- Params: `p.timeSynced = true`; `p.delayTargetSec = timemap::syncedDelaySeconds (divisionIndexByName (name), bpm, 4.0).seconds` for `{"1/4", 120}` (0.5 s), `{"1/8", 120}` (0.25 s), `{"1 bar", 60}` (4.0 s, clamps), `{"1/32", 300}` (25 ms, clamps to the floor). Switch pass: 0.25 s -> 0.5 s at t = 1.0 s.
- Trims: clean.
- Measure: pure-function checks on `timemap::syncedDelaySeconds` (seconds and the `clamped` flag); `xcorrLag` as in `delaytime` around the expected T; switch pass `freqPrecise` in [1.08, 1.15] (expected 220 Hz, the clock halves) and [1.8, 2.0] (440); robustness: `p.delayTargetSec = 0`, then `-1`, then `NaN`, then `100` for one block each while a burst circulates.
- PASS: table: 1/4 @ 120 = 0.500 s exactly, 1/8 @ 120 = 0.250 s, 1 bar @ 60 in 4/4 = `kDelayMaxSec` with `clamped == true`, 1/32 @ 300 = `kDelayMinSec` with `clamped == true`, 1 bar @ 120 in 3/4 = 1.5 s; realised delay 500 ms +- 0.5 % and 250 ms +- 0.5 %; clamped case realises `loopSeconds (1)` +- 0.5 %; switch window pitch 220 +- 5 % then 440 +- 1 %; the four bad values produce no non-finite sample and `ui.delaySec` stays inside `[kDelayMinSec, kDelayMaxSec]` (the engine clamps seconds and ignores non-finite targets).
- expected: 0.500 / 0.250 / 3.600 / 0.0275 / 1.500 s.
- FAIL means: an unsmeared division switch = sync bypasses the smoother; 4 s realised = no clamp; wrong table values = the beats column or the bar rule (`numerator * 4 / denominator`) is wrong.

#### `bandwidth` (phase 1)
- Purpose: fidelity collapses with Time: the reconstruction filter tracks fs_chip, and the fixed filters give "flat to about 1 kHz then rolls off".
- Input: `noise (0.1, 7)`. Per setting render T + 2.5 s, analyse the last 2 s. time01 in {0.0, 0.5, 1.0}. Plus a stepped-sine probe at time01 0.0: `steppedSines` amp 0.05 at {100, 500, 1k, 2k, 4k, 8k, 12k} Hz, 0.6 s each, `goertzel` over the last 0.3 s of each.
- Params: decay 0, blend 1, filter 18k, resonance 0, absorb 0.
- Trims: clean.
- Measure: `spectrum (out, order 12)`, `centroid (20, 20000)`, `HFLF = bandEnergyDb (2k, 8k) - bandEnergyDb (100, 1k)`. Probe: `20 log10 (G (f) / G (500))`.
- PASS: centroid (0.0) > 1500 Hz; centroid (1.0) < 1200 Hz and < 0.35 x centroid (0.0); centroid (0.5) < centroid (0.0); HFLF (0.0) - HFLF (1.0) > 15 dB. Probe at time01 0.0 relative to 500 Hz: H (1k) in [-3, +1] dB; H (2k) in [-6, 0] dB; H (4k) in [-12, -2] dB; H (8k) in [-30, -8] dB; H (12k) < -15 dB (the 8 kHz bound is the one that fails a wet path with no output MFB: an 8 kHz reconstruction pole alone sits at -3 dB there).
- expected: centroid about 3.5 / 2.0 / 0.6 kHz (magnitude weighted); probe about -0.3 / -1.5 / -6 / -20 / -30 dB for the winning core.
- FAIL means: flat centroid across Time = reconstruction filter fixed at host rate instead of `min (0.45 fs_chip, kReconMaxHz)`; centroid (0.0) low = the fixed filters are too dark or applied twice; probe flat to 8 kHz = the output MFB is missing.

#### `thd` (phase 1)
- Purpose: the chip's distortion grows with Time against the ElectroSmash table (0.13 % at 31 ms, 1 % at 342 ms, 3 %+ beyond). Graft from PT design 0, lifted to the engine level by trimming everything else out.
- Input: sine amp 0.5 at `f = combPeakHz (400, T)` (all taps in phase for the fundamental AND its harmonics, so the tap sum is coherent). Settle T + 0.5 s, measure 1.0 s. Settings by delay: `time01ForDelaySeconds (0.031)`, `(0.342)`, `(1.0)`.
- Params: decay 0, blend 1, filter 18k, resonance 0, absorb 0, strength 0.
- Trims: chipOnly (dither on so an undithered quantiser cannot fake harmonics; loop saturator and input drive out).
- Measure: `thd23 (out, f)` per setting; the same at 31 ms with production trims (the saturator back in) as a control.
- PASS: with `stageFactor = sum_k pt::kTapWeights[k] * (k + 1)` (1.88 for three normalised stages, 1 for one stage; harmonics generated in the write path are coherent across series stages): thd23 (31 ms) in `[0.06, 0.32] % x stageFactor`; thd23 (342 ms) in `[0.45, 2.6] % x stageFactor`; thd23 (1.0 s) >= `1.1 % x stageFactor`; growth `thd23 (342 ms) / thd23 (31 ms) >= 3.0` (the model-independent line: a Time-independent clipper gives 1.0); control with production trims at 31 ms: thd23 >= 1.0 % (the saturator is present and the trim really removes it).
- expected (three stages): 0.26 % / 2.0 % / 4.5 %; growth about 7.5; control about 2.4 % (tanh at 0.5 peak gives H3/H1 = A^2/12 = 2.1 % plus the bias's H2).
- FAIL means: flat THD = no Time-dependent nonlinearity in the chip model (a cubic clipper alone); THD far above the bracket at 31 ms = the clipper knee is below the 0.5 test level or the drive constant is off; control equal to the trimmed value = `Trims::loopSat` is not wired.

#### `noisefloor` (phase 1)
- Purpose: self-noise rises with Time and is quiet at short settings.
- Input: silence. time01 in {0.0, 0.25, 0.5, 0.75, 1.0}; render T + 3 s, measure the last 1 s.
- Params: decay 0, blend 1, filter 18k, absorb 0, strength 0, out 0.
- Trims: production.
- Measure: `dB (rms (L, last 1 s))` per setting; `centroid` of the noise at 0.0 and 1.0 (reported: rumble vs hiss, section 12).
- PASS: rms (0.0) <= -76 dBFS; rms (1.0) in [-62, -34] dBFS; rms (0.5) >= rms (0.0) + 6 dB; rms (1.0) >= rms (0.5) + 6 dB; the five values are monotone non-decreasing. Print the expected curve from `pt::kChipNoiseDbAtMaxClock / AtMinClock` next to them.
- expected: about -85 / -78 / -62 / -50 / -40 dBFS for the winning core (the shaped quantiser and the analog hiss at the short end, bleed at the long end). The -76 bound is set so a floor of -70 (a plain 10-bit TPDF quantiser whose noise folds down un-filtered, the fatal flaw of PT design 1) fails, while both surviving cores (-80 to -89) pass.
- FAIL means: identical numbers = noise not scaled by fs_chip; silence everywhere = noise injected before a gate or after the taps; rms (0.0) hot = dither amplitude not scaled with the bit depth, or the DAC pole is not run at chip rate before the read decimates.

#### `clockbleed` (phase 1)
- Purpose: the ticking is present only below `pt::kBleedOnsetHz` and grows as the clock falls: a pulse train at fs_chip and a subharmonic at fs_chip / 2 (plan 2.1).
- Input: silence. time01 in {0.30 (46 kHz, none), 0.75 (5.17 kHz), 1.00 (1.53 kHz)}. Render T + 2 s, measure the last 1 s.
- Params: decay 0, blend 1, filter 18k, absorb 0.
- Trims: clean except `bleed = 1` (the only possible output is the bleed).
- Measure: `goertzel (out, fsChip (t))` and at `fsChip (t) / 2`, and broadband RMS. Frequencies are exact because drift is off.
- PASS: rms (0.30) < -100 dBFS (bit silent); goertzel (5171 Hz) at 0.75 > -60 dBFS and > 40 dB above the same bin at 0.30; at 1.0 both 1529 Hz and 764 Hz present (> -60 dBFS), the fundamental level at 1.0 exceeds the one at 0.75, and broadband RMS at 1.0 in [-50, -25] dBFS (`kBleedDbAtMinClock` -40 +- 10).
- expected: about -49 dBFS at 1.0 s of delay, -40 dBFS at 3.6 s; the fs/2 line about 6 dB under the fundamental.
- FAIL means: bleed at 0.30 = the onset test is missing or compares the wrong rate; no bleed at 1.0 = injected before the reconstruction filter (which sits at 0.45 fs_chip and eats it) or the pulse train frequency is not fs_chip; no subharmonic = the divide-by-two is missing. If the core section zeroes the fs_chip tick train in favour of the memory-wrap tick (PT design 2's hypothesis), the fundamental line is replaced by: count of 50 ms windows over 8 s at time01 1.0 whose peak exceeds 4 x global RMS in [4, 24]; say so in PROGRESS.md.

#### `firstblock` (phase 1)
- Purpose: no start-up chirp. Both PT designs that lost had a smoother that started at its hard minimum and ramped to the knob over 20 ms, replaying the first 20 ms of input time-compressed into a tick (musician verdict, fatal flaw 1 on designs 0 and 1). `prepare()` must snap every smoother to the first block's `Params`.
- Input: 1 kHz sine amp 0.25 from sample 0.
- Params: time01 0.0 (run A) and 0.6 (run B), decay 0, blend 1, filter 18k. Run C: prepare at 48 k, process 0.5 s, `prepare (44100, 128)` again through `perBlock` is not possible from the schedule, so run C constructs its own engine: prepare 48 k, process 10 blocks, prepare 44.1 k with time01 0.6, then render as B at 44.1 k. Run D: as A with 0.5 s of silence prepended (`gate (0.5, 1e9, sine)`).
- Trims: clean.
- Measure: `ui.delaySec[0]` (first block); `peak` and `maxStep` over [0, 0.12] vs [0.3, 0.5]; D aligned to A: `max |A[i] - D[i + 0.5 sr]|` over A's [0.05, 0.5].
- PASS: `ui.delaySec[0]` within 0.5 % of `loopSeconds (t)` in A, B and C; peak [0, 0.12] <= 1.5 x peak [0.3, 0.5]; maxStep [0, 0.12] <= 1.5 x maxStep [0.3, 0.5]; max |A - D shifted| < 1e-3 (the response to input does not depend on when the input starts; the residual is interpolation phase, not a chirp).
- expected: 27.5 ms on block 0 of run A; residual A vs D about 1e-5.
- FAIL means: `ui.delaySec[0]` reads seconds instead of milliseconds = the smoother started at the hard minimum; a 4x pitch-up blip in the first 30 ms confirms it; a large A vs D residual with a correct first readout = some other state (Interference, drift ramp) starts mid-transient.

### B. Loop, filter, absorb, drive

#### `runaway` (phase 1; sub-cases (b) and (d) phase 3)
- Purpose: Decay past unity self-oscillates, bounded, never digitally clipped, no DC, recovers when backed off (plan risk 2, so it ships with Phase 1).
- Input: `burst (220, 0.5, 0.1, 200 ms)` then silence. Render 15 s. At t = 12 s `perBlock` sets decay 0.3.
- Sub-cases: (a) plain; (b) `timeMod01 1.0, agitate 1.0, agitSpeedHz 500`, routing `heroWith (srcAgitation, dstTime, 1.0)` (worst case: a 2-octave clock swing at 500 Hz at runaway); (c) `strengthDb 40` (hottest input); (d) graft from PT design 2: `resonance 0.95, filterHz 20, timeMod01 1.0, agitate 1.0, agitSpeedHz 173`, same routing; (e) decay 0.95 (decays); (f) decay 1.0 (sustains, does not explode), (e) and (f) rendered 6 s without the step.
- Params: decay 1.15, time01 0.35, filter 4000, resonance 0.2, absorb 0, blend 1, out 0.
- Trims: production.
- Measure: 500 ms window RMS and peak over [6, 12] s; crest per window; `dcOffset` over [10, 12]; global max |sample|; `flatTopRuns` over [6, 12]; non-finite; RMS at [14.5, 15]; `ui.energy` over [6, 12]; (e) RMS [5, 6] vs [0.5, 1.0]; (f) RMS [5, 6] vs [1, 2].
- PASS (a to d): every window RMS in [0.02, 0.8]; max |sample| < 0.999; crest in [1.2, 6.0]; `|dB (RMS window 6) - dB (RMS window 12)| < 3 dB` (steady, not still growing); |DC| < 0.01; `flatTopRuns == 0`; zero non-finite samples; RMS at [14.5, 15] < -40 dBFS (backing Decay off lets the loop die); `ui.energy` > 0.5 on every block in [6, 12] (the ember is lit in runaway). (e): RMS [5, 6] < 0.3 x RMS [0.5, 1.0]. (f): RMS [5, 6] in [0.1, 1.5] x RMS [1, 2].
- expected: RMS about -7 dBFS (0.42) with peak about -4 dBFS for the winning saturator (tanh asymptote); crest about 1.5; the modulated cases 3 to 6 dB lower.
- FAIL means: peak == 1.0, crest near 1.0 or flat-top runs = hard clipping after the saturator or a clamp instead of a curve; RMS > 0.8 or still growing = saturator outside the loop; DC > 0.01 = DC blocker missing or after the tap; NaN = filter state blew up under modulated Time (usually cutoff not clamped below Nyquist, or the SVF coefficients ramped separately so the TPT solve went inconsistent while k was negative).

#### `strength` (phase 1)
- Purpose: Strength is gain plus drive: clean at 0 dB, saturated and compressed at +40 dB, never beyond +-1.
- Input: 220 Hz sine amp 0.1, 1.0 s, measure [0.5, 1.0].
- Params: strength 0 then 40, time01 0.0, decay 0, blend 1, filter 18k.
- Trims: clean with `quantize = 0` and `loopSat = 0` (the only distortion left is the drive).
- Measure: `goertzel` at 220, 440, 660 Hz; RMS; peak.
- PASS: at 0 dB, H3 < -50 dB below the fundamental (a plain tanh at 0.1 gives -62 dB; the chip's own THD at this level is -86 dB); at +40 dB, H3 > -20 dB (it IS driven), `RMS (40) - RMS (0)` in [6, 30] dB (louder, but at least 10 of the 40 dB were compressed), peak < 0.999.
- expected: H3 about -60 dB at 0 dB; about -10 dB at +40; RMS gain about +20 dB; peak about 0.99.
- FAIL means: harmonic-free at +40 = the drive curve is linear; 40 dB of RMS gain = the soft clip is after the wet tap; peak >= 1 = clip not soft.

#### `filter` (phase 1)
- Purpose: the in-loop SVF cutoff is what the knob says, full CCW passes almost nothing.
- Input: `steppedSines` amp 0.05 at fc/4, fc, 2 fc, 4 fc, 0.5 s each, `goertzel` over the last 0.25 s. fc in {200, 1000, 4000}. Each probe normalised against the same frequency with filter 18k (removes the fixed chip filters from the reading).
- Params: time01 0.0, decay 0, blend 1, resonance 0, absorb 0.
- Trims: clean.
- PASS: gain (fc) in [-7, -1.5] dB (the core section owns the resonance-0 damping: Q 0.5 gives -6 dB, Butterworth -3 dB; both accepted); gain (2 fc) in [-15.5, -9] dB; gain (4 fc) in [-27, -20] dB; gain (4 fc) - gain (2 fc) in [-13, -9] dB (two poles: 12 dB per octave); gain (fc/4) = 0 +- 1 dB; the three fc agree with each other within 1.5 dB at fc and 2 fc (no prewarp error at 4 kHz). Filter 20 Hz: wet RMS of a 220 Hz sine > 30 dB below the filter-18k reading.
- expected: -6 / -14 / -24.6 dB at Q 0.5, -3 / -12 / -24 dB at Q 0.707; 220 Hz at 20 Hz cutoff about -42 dB.
- FAIL means: the -3 dB point off at 4 kHz but fine at 200 Hz = no prewarp; all readings flat = the filter sits outside the wet path; fc/4 not at 0 dB = the state-bounding tanh is compressing at 0.05 amplitude (limit too low).

#### `resonance` (phase 1)
- Purpose: resonance reaches self-oscillation, at the cutoff frequency, bounded by the state limiter, and does not oscillate at half.
- Input: `click (0.05, 0.1, 5 ms)` to kick it, then silence. Render 3 s.
- Params: filter 1000 then 200; resonance 1.0; then resonance 0.5 at 1000; then (guard) resonance 0.95 at 1000 with `perBlock` stepping filter to 4000 at 1.5 s. time01 0.0, decay 0, blend 1.
- Trims: clean.
- Measure: `freqPrecise` and RMS over [2.0, 3.0]; peak; non-finite.
- PASS: resonance 1.0: RMS > 0.01 (it sustains with no other energy source), pitch within +-10 % of the cutoff at both settings, peak < 0.999; resonance 0.5: RMS < 1e-4; guard: finite, peak < 0.999, pitch over [2.5, 3.0] within +-10 % of 4000 Hz (the coefficient triple g, k, h is recomputed together at every control tick, never ramped apart while k is negative).
- expected: about 1000 / 200 Hz at RMS 0.2 to 0.4 (the state limit sets the level).
- FAIL means: silent at 1.0 = damping never reaches zero (`kResSelfOsc01` mapping); pitch far off = oscillation tracks something other than cutoff; peak at 1.0 = tanh missing on the filter state; a blow-up in the guard = inconsistent TPT coefficients during the step (PT design 1's fatal flaw 2).

#### `absorb` (phase 1)
- Purpose: Absorb attenuates and darkens, and it sits inside the loop (so it shortens the decay).
- Input: `noise (0.1, 7)`, 2 s, measure the last 1 s. Loop pass: `burst (220, 0.5, 0.1, 200 ms)` then silence, 5 s.
- Params: time01 0.2, decay 0, blend 1, filter 18k, resonance 0; absorb in {0, 0.5, 1.0}. Loop pass: decay 1.0, absorb 0 then 0.7.
- Trims: clean.
- Measure: RMS dB and `centroid` per setting; loop pass `drop (A) = dB (RMS [4.0, 4.5]) - dB (RMS [0.5, 1.0])` at absorb A.
- PASS: RMS (0.5) < RMS (0) - 3 dB; RMS (1.0) < RMS (0) - 12 dB (heading toward `kAbsorbMaxAttenDb`); centroid (1.0) < 0.7 x centroid (0) and monotone non-increasing across the three; loop pass: `drop (0) > -8 dB` (unity feedback holds a 220 Hz tone; the stage filters cost under 0.1 dB per pass at 220 Hz); `drop (0.7) < -20 dB` and `drop (0.7) < drop (0) - 15 dB` (Absorb inside the loop shortens the decay).
- expected: RMS -9 / -18 dB relative; centroid ratio about 0.55; drop (0) about -4 dB, drop (0.7) below -60 dB with the winner's 4 dB feedback attenuation at full Absorb.
- FAIL means: attenuation without darkening = the tilt stage is missing; the loop still holding at absorb 0.7 = Absorb was placed after the feedback tap, contradicting the plan topology. If the core section ships `kAbsorbFeedbackShare = 0` (Absorb never touches feedback), the two loop-pass lines are dropped and PROGRESS.md says so (section 12).

### C. Commands and robustness

#### `clear` (phase 1)
- Purpose: Clear flushes every buffer and filter state at the next block, is not sticky, does not click, does not reset the modulation sources, and is honoured twice.
- Input: 220 Hz sine amp 0.5 for 1.5 s, then silence; at t = 2.0 s `perBlock` calls `engine.requestClear()`; new sine from 3.5 s to 5.0 s. Render 5 s. Second pass: two `requestClear()` calls one block apart (2.0 s and 2.0027 s).
- Params: decay 1.0, time01 0.4, blend 1, filter 6000, agitate 0.3, agitSpeedHz 0.5 (so `ui.src[srcAgitation]` is a visible triangle).
- Trims: clean (the post-flush floor is exactly zero; with production trims the hiss regenerates, which `noisefloor` covers).
- Measure: RMS of the block before the request; RMS over `[2.0 + kClearSettleSec, 3.4]` with `kClearSettleSec = 0.010` (any fade up to 8 ms plus one block); max |sample| over the same window; `maxStep` over [1.99, 2.02] vs `maxStep` over [1.8, 1.99]; RMS over [4.5, 5.0]; `ui.energy` at 3.4 s; `ui.clearsServed` before and after; per-block `|delta ui.src[srcAgitation]|` across [1.9, 2.1] vs its steady slope.
- PASS: pre-request block RMS > -20 dBFS; RMS after settle < -80 dBFS; residual max < 1e-4 (filters, saturator memory and DC blocker flushed too, no ringing); `maxStep` (transition) <= 1.5 x `maxStep` (preceding 200 ms) (graft: PT design 2's relative criterion; the absolute 0.05 of design 0 is frequency dependent: a 0.63-peak tone at 1 kHz has a natural step of 0.082); RMS over [4.5, 5.0] > -20 dBFS (rebuilds); `ui.energy` < 0.05 at 3.4 s; `ui.clearsServed` incremented by exactly 1 (second pass: by 2, same silence); Agitation slope across the clear within 1.5 x its steady per-block slope (Clear touches buffers and filter states only, never Agitation phase, Lorenz or drift state).
- expected: the block before at about -8 dBFS; the fade adds a per-sample step of `0.63 / (fadeMs * 48)` = 0.0066 at 2 ms, well under the tone's own 0.05.
- FAIL means: residual ringing = SVF or saturator state not reset; sticky silence = the mailbox counter is consumed but the input gate never reopens; a click = the flush is instantaneous; a jump in `ui.src[srcAgitation]` = Clear resets the function generator.

#### `zipper` (phase 1) and `zippermod` (phase 3)
- Purpose: no parameter change clicks or zippers, one parameter at a time. Two detectors: the sample-step click detector and a spectral line at the control rate, which is what a per-tick gain step train (modulation design 0's fatal flaw 1) looks like even when it is too small to click.
- Input: continuous sine at `kZipperToneHz = 48000 / 208` = 230.77 Hz, amp 0.25 (chosen so no harmonic lands within 115 Hz of `fCtl = sr / modk::kControlBlock` = 1500 Hz). Per row: 1.0 s of settle, the step at t = 1.0 s, render 2.0 s.
- Rows (`zipper`): filter 200 -> 8000; decay 0 -> 1.0; blend 0 -> 1 and 1 -> 0; absorb 0 -> 1; strength 0 -> 40; resonance 0 -> 0.9; outDb -12 -> +6; `delayTargetSec` 0.25 -> 0.5 (sync-style step); time01 0.2 -> 0.8 and 0.8 -> 0.2.
- Rows (`zippermod`): timeMod 0 -> 1 (agitSpeedHz 100, agitate 1, routing `heroWith (srcAgitation, dstTime, 0.5)`); agitate 0 -> 1 (hero routing, agitSpeedHz 100); agitMode 0 -> 1.
- Params: time01 0.3, decay 0.5, blend 0.5, filter 4000 unless the row says otherwise.
- Trims: clean.
- Measure: `ref = max (maxStep [0.5, 1.0], maxStep [1.4, 1.9])` (steady before AND after: a filter or Strength step legitimately changes the waveform's own slope); `trans = maxStep [1.0, 1.1]`; `line = goertzel (out, 1.0, 1.08, fCtl)` vs `steadyLine = goertzel (out, 0.5, 0.58, fCtl)` (80 ms Hann windows: the nearest harmonic, 1384.6 or 1615.4 Hz, is 9 bins away). Time rows use `instFreqTrack` instead of `maxStep`.
- PASS: every row `trans <= 1.5 x ref` and `trans < 0.1` absolute (a real click is about 0.5); `line <= steadyLine + 6 dB` and `line <= -60 dBFS`; Time rows: consecutive instantaneous-frequency ratios in [0.5, 2.0] over [1.0, 1.2].
- expected: `trans / ref` about 1.0 to 1.2 on every row; `line` at the noise floor (< -80 dBFS) when gains ramp per sample.
- FAIL means: the named parameter bypasses `SmoothedValue`; (Blend) the crossfade is linear with a hard endpoint; (Time) the smoother is applied to the readout but not to fs_chip; a control-rate line = a destination or macro steps once per tick without an intra-tick ramp (decay/absorb/blend/strength must be `ControlRamp`s read per sample, Agitate and Time Mod must be smoothed).

#### `passthrough` (phase 1)
- Purpose: the dry path is a null, the wet is mono and identical on both channels, odd channel layouts and lengths do not crash.
- Input: 220 Hz sine amp 0.4; second pass with L = sine, R = `noise (0.2, 3)` (`Rig::inputR`); third pass with `channels = 1`; fourth pass a zero-length buffer between normal blocks (`blockSize` returns 0 for block 10).
- Params: blend 0 with strength 0 and again with strength 40; out -6 dB; out -100 (-inf); blend 1 for the channel checks.
- Trims: clean.
- PASS: blend 0, strength 0: `max |out - in| < 1e-6` over [0.2, 1.0]; blend 0, strength 40: the same when `DybbukEngine::kDryIsPreStrength` (otherwise report only); out -6: RMS ratio 0.5012 +- 0.1 %; out -inf: RMS < 1e-7 over [0.5, 1.0]; L != R input: `max |wetL - wetR| < 1e-7` and `fnv1a (L)` equals the hash of a run fed (L + R) / 2 on both channels; 1-channel buffer: no crash, RMS > 0; zero-length buffer: no crash, output identical to the run without it.
- expected: residual 0 (bit-exact null when the dry is a plain add).
- FAIL means: blend-0 residual = the dry path runs through the drive or the output is post-mono-sum; L/R mismatch = a stereo state leaked into the "mono" core.

#### `nondeterminism` (phase 1)
- Purpose: the plan's tell 4: two runs are never bit-identical, but the music is the same; and seeded runs ARE identical (every hash in this suite depends on it).
- Input: 220 Hz sine amp 0.25 continuous, 4 s, measure the last 2 s.
- Params: (A) decay 0.9, agitate 0, seed -1 on both engines; (B) same with seed 42 on both; (C) decay 1.05, agitate 1.0, timeMod 1.0, time01 0.5, seed -1.
- Trims: production.
- Measure: `fnv1a (L, 2, 4)`, RMS, fraction of sample positions where the two outputs differ, `rms (a - b) / rms (a)`, `max |a - b|`; (C) energy traces' peak cross-correlation.
- PASS: (A) hashes differ, differing fraction >= 0.5, `rms (a - b) / rms (a)` < 0.1, RMS within 1 %; (B) `max |a - b| == 0.0` exactly (graft: PT design 2's exact-zero control); (C) hashes differ, RMS within 3 dB, energy-trace cross-correlation < 0.95.
- expected: (A) difference about -50 dB relative; (B) 0.
- FAIL means: (A) equal hashes = every RNG is seeded with a constant or `getSystemRandom` is used once for both; (B) non-zero = some RNG escapes `seedForTests` (usually the interference initial state or the drift), which makes `pin` and `blocksize` meaningless.

#### `nan` (phase 1)
- Purpose: a single bad host sample must not poison the loop forever (graft: PT design 2's fatal flaw, PT design 1's guard). Both defences: per-sample sanitisation at the loop sum node and a per-block `isfinite` flush.
- Input: 220 Hz sine amp 0.25 continuous; `perBlock` overwrites one input sample at t = 1.0 s with NaN (run 1), +inf (run 2), 1e6 (run 3). Render 3 s.
- Params: decay 0.9, time01 0.4, blend 1, filter 6000.
- Trims: production.
- PASS: every output sample from the block after the injection is finite in all three runs (`firstNonFiniteSec` either -1 or inside the injection block); RMS over [2.5, 3.0] within 3 dB of RMS over [0.5, 1.0] (the engine recovered and passes audio); |DC| over [2.5, 3.0] < 0.01 (the 1e6 sample did not park the saturator or DC blocker at a rail); in the NaN run the output over [1.1, 3.0] contains no sample of magnitude > 0.999.
- expected: one bad block (zeros or a flush), then normal.
- FAIL means: non-finite after the injection block = no guard; the loop stays at a rail = the guard zeroes the input but not the state, or the DC blocker was hit with inf.

### D. Modulation

#### `agitation` (phase 3)
- Purpose: the function generator's period, triangle shape, range, gate mode, and its hero route to the filter.
- Input: silence for loop mode; gate pass: `burst (220, 0.5, 1.0, 200 ms)` and again at 3.0 s (peaks -6 dBFS, above the -30 dBFS gate threshold, silence between so both detector flavours re-arm); `noise (0.1, 7)` for the route pass.
- Params: agitSpeedHz in {0.1, 0.5, 4.0}, agitMode 0, render 3 periods + 1 s (31 s at 0.1 Hz); gate pass agitMode 1 at 1 Hz, 5 s; route pass agitate 1.0 vs 0 with filter 2000, decay 0, blend 1, 4 s at 0.5 Hz; alias pass agitSpeedHz 1000 for 1 s.
- Trims: clean.
- Measure: `ui.src[srcAgitation]` per block (375 Hz sampling, interpolated crossings of 0.5 upward); period = mean crossing interval; rise fraction = share of blocks with a positive delta within a cycle; min / max. Gate pass: count of complete rise + fall pairs, level between bursts. Route pass: `max - min` of `ui.filterModOct` (the applied cutoff offset, octaves); `centroid` of the output in 100 ms hops correlated (`pearson`) against `ui.src[srcAgitation]`. Alias pass: std of the block trace.
- PASS: period = 1 / speed within 1 % at 0.1 and 4 Hz and 0.5 % at 0.5 Hz; rise fraction 0.50 +- 0.05 (fixed 50/50 angle); min < 0.02, max > 0.98; gate: 0 cycles before the first burst, exactly 1 after it, 2 after the second, resting < 0.02 between, each cycle 1.0 s +- 5 %; route: swing at agitate 1.0 in [1, 8] octaves (`modk::kHeroAgitFilter x 4 oct x macro`), < 0.01 oct at agitate 0; `pearson` > 0.7; alias pass: block trace std < 0.03 (the control-rate view of a 1 kHz generator is its mean, not aliasing).
- expected: swing 3.0 oct; periods 10.0 / 2.00 / 0.250 s.
- FAIL means: period scales with sample rate (see `samplerate`) = phase increment computed once at 48 k; gate keeps looping = mode flag ignored; route swing at agitate 0 = the macro does not scale that route; alias std large = the control tick point-samples the generator instead of averaging it.

#### `follower` (phase 3)
- Purpose: the input follower's attack, release, linearity, its route to Decay, and that the route ramps within the tick (the hero-route zipper of modulation design 0).
- Input: silence 0.5 s, sine at `kZipperToneHz` amp 0.5 from 0.5 to 1.5 s, silence to 2.5 s; second run at amp 0.25.
- Params: strength 0, agitate 0; route pass agitate 1.0, decay 0.5, time01 0.3.
- Trims: clean.
- Measure: `ui.src[srcFollower]` per block; attack = time to 63 % of the plateau after 0.5 s; release = time to 37 % after 1.5 s; plateau ratio between the two amplitudes; route pass: max `ui.decayMod` while playing, value 0.5 s after the note ends; control-rate line `goertzel (wet, 0.5, 0.58, fCtl)` vs `goertzel (wet, 1.2, 1.28, fCtl)`.
- PASS: attack = `modk::kFollowerAttackMs` (5) +- 4 ms (block quantisation 2.7 ms; a peak follower on a sine reads a little slow); release = `modk::kFollowerReleaseMs` (100) +- 20 %; plateau (0.25) / plateau (0.5) = 0.5 +- 10 % (linear, not log); route: `ui.decayMod` > 0.05 while playing at agitate 1.0 and back within 0.01 of 0 within 0.5 s of silence; at agitate 0 never leaves 0 +- 0.001; onset line <= steady line + 6 dB and <= -60 dBFS.
- expected: +0.15 on Decay at full follower (`kHeroFollowerDecay 0.3 x 0.5 x 1`); attack 5 ms, release 100 ms.
- FAIL means: times scale with rate = coefficients not recomputed in `prepare`; plateau ratio 0.25 = the follower reports power not amplitude (fine if intended, then fix the test and the ember scaling together); a 1500 Hz line at onset = Decay steps per tick.

#### `interference` (phase 3)
- Purpose: the chaos source is bounded, slewed, correlated with the loop, wilder when the loop is hot, rests when it is cold, and its route wobbles Time.
- Input: `burst (220, 0.5, 0.1, 300 ms)` then silence, 12 s (a full hot-to-cold sweep at decay 0.9: 0.92 dB per pass at T = 0.193 s, 60 dB in about 13 s). Route pass: 440 Hz sine amp 0.3 held 4 s. Stress pass: decay 1.15, 20 s.
- Params: decay 0.9, time01 0.4, blend 1, filter 6000, agitate 1.0, timeMod 1.0 (the Time column is scaled by Time Mod), routing `heroWith (srcAgitation, dstFilter, 0)` (keep the filter still so `ui.energy` reflects the loop, not the sweep).
- Trims: production.
- Measure: `ui.src[srcInterference]` and `ui.energy` per block: std over [1, 3] s (hot) vs [9, 12] s (cold); `pearson (|intf| smoothed 50 ms, energy)` over [0.5, 12]; max block-to-block step; `autocorrMax (intf, 0.25 s, 4 s)`; last 2 s: `|intf|` mean. Route pass: `instFreqTrack` of the wet, spread between the 5th and 95th percentile of `log2 (f / 440)`; std of `ui.timeModOct`; the same with timeMod 0 and agitate 0.
- PASS: std_hot > 5 x std_cold; `pearson` > 0.5; |intf| <= 1 and finite through the 20 s stress pass; max step < 0.5 (`kInterferenceSlewMs`); `autocorrMax` < 0.9 (not periodic); mean |intf| over the last 2 s < 0.05 (rests once the loop has died, no self-excitation); route: pitch spread >= 0.1 oct and std (`ui.timeModOct`) > 0.005 oct alive; spread < 0.005 oct and `ui.timeModOct` == 0 with timeMod 0 and agitate 0 (drift is off in this rig); report the spread at agitate 0 / timeMod 1 (documents `modk::kMacroScalesTimeColumn`).
- expected: spread about 0.5 oct at hero depth 0.5 x 2 oct x energy near 1.
- FAIL means: std_hot ~ std_cold = the drive parameter is not fed from the loop envelope; NaN or |x| > 1 = the Lorenz step is unstable at the control rate or unclamped (rho above 45, dt above 0.013); periodic autocorrelation = a stuck orbit or the logistic fallback in a periodic window; no rest = the energy floor sits below the core's idle hiss (re-tune `kEnergyFloorDb`).

#### `timemod` (phase 3)
- Purpose: audio-rate Time modulation produces sidebands at exactly f_c +- k f_m, the modulation reaches fs_chip per sample rather than through the knob smoother, and Time is not point-sampled at the control rate.
- Input: 1 kHz sine amp 0.25, 2.5 s, analyse the last 1.0 s (two 16384-point Hann frames, 2.9 Hz bins).
- Params: time01 0.2 (T 72.9 ms: none of the three tap delays is an integer multiple of either modulation period, which would cancel the effect), decay 0, blend 1, filter 18k, agitate 1.0, timeMod 1.0. Routing `onlyCell (srcAgitation, dstTime, kTimeModTestSwingOct / modk::kDstScaleTimeOct)` with `kTimeModTestSwingOct = 0.10` (the unipolar triangle swings 0..0.10 oct, i.e. +-0.05 about its mean; the mean is a static delay change and makes no pitch). Runs: (A) agitSpeedHz 50; (B) 500; (C) timeMod 0 at 500; (D) alias line: agitSpeedHz 700, swing `kTimeModAliasSwingOct = 0.40`; (E) grid metric (graft: PT design 2): swing `kSidebandSwingOct = 0.70`, `fMod = (floor (200 T) + 0.5) / T` (about 200 Hz, write and read sides add rather than cancel), plus a depth-0 control.
- Trims: clean.
- Measure: carrier bin C at 1000 Hz; S1 = mean of the bins at 1000 +- f_m; the two strongest bins between 200 and 3000 Hz excluding the carrier; (D) `goertzel` at 300, 1700 (true sidebands) and 200, 1800 (1000 +- the alias 1500 - 700 = 800 Hz); (E) `G = sum_{k=1..6} A (1000 +- k fMod)^2`, `H = sum A (1000 +- k fMod +- fMod / 2)^2`, total energy. Why the A/B pair: the output frequency is `f_in fs_read (t) / fs_write (t - tau)`, so the peak deviation is `2 m f_in ln2 |sin (pi f_m tau)|` per tap and the index `beta = deltaF / f_m` falls 6 dB per octave of f_m intrinsically; a 20 ms one-pole wrongly in the path adds another 6 dB per octave above 8 Hz. Correct: `beta (50) / beta (500)` about 10. Bugged: about 100.
- PASS: (A) S1 / C > 0.1 and the two strongest non-carrier bins sit at 950 and 1050 Hz +- 1 bin; (B) S1 / C > 0.01 and bins at 500 and 1500 Hz +- 1 bin; `(S1 / C)_A / (S1 / C)_B` in [2, 40]; (C) S1 / C < 0.001 and the output hash equals a run with agitSpeedHz 1, timeMod 0 (zero depth is bit-exactly zero); (D) 300 and 1700 Hz > -30 dB re carrier, 200 and 1800 Hz < -40 dB re carrier; (E) `G > 10 H` and `G > 0.3 total`; depth-0 control `G < 0.01 total` and carrier > 0.95 total.
- expected: (A) S1 / C about 0.39 (beta about 0.7); (B) about 0.03; ratio about 13; (D) sidebands about -16 dB, alias bins at the floor.
- FAIL means: ratio near 100 = the FM is added to the smoother's target, not after it; no sidebands = Time Mod routes a control-rate source; the 800 Hz alias present = Time is updated once per control tick (a per-block or per-tick Time path); spurious bins at other spacings = the modulation waveform is not the agitation triangle (or the taps alias the modulation, which shows as bins at f_m / 3 multiples). The (E) metric is FM-law independent (linear or exponential clock law both put the lines on the grid, section 12).

#### `drifttime` (phase 3)
- Purpose: the always-on drift moves Time by cents, not semitones, is bounded rail-to-rail, is the only always-on randomness in the Time path, and does not random-walk across passes (the telescoping property: pitch is `fs (now) / fs (original write)`).
- Input: (a) 440 Hz sine amp 0.3, 20 s, time01 0.75 (T 1.065 s), decay 0; (b) graft from modulation design 0: burst of exactly `2 T` at `f = round (220 T) / T` (phase-continuous across the splice) at time01 0.4, then silence, decay 1.05, 30 s.
- Params: blend 1, filter 18k, absorb 0, agitate 0, timeMod 0.
- Trims: clean with `drift = 1` (drift only); a control with `drift = 0`; seeds 1 and 2.
- Measure: `demodPitch` of the wet: (a) over [3, 20] s, RMS and max |cents|; (b) per 1 s window over [2, 30] s the median cents, then max |median| and std across windows; seeds compared by `max |a - b|` and by `pearson` of the cents traces.
- PASS with `bound = 2 x 1200 x modk::kDriftTimeOct x kDriftBoundMargin` (`kDriftBoundMargin = 1.25`; the pitch of a variable-clock delay is `2^(L_read - L_write)`, so the rail-to-rail bound is twice the one-sided depth: 9.6 cents at 0.004 oct, bound 12 cents; the 1x bound was modulation design 0's fatal flaw 3): (a) max |cents| < bound, RMS in [0.3, 6] cents; seeds: `max |a - b| > 1e-3` yet both inside the bound; `drift = 0`: RMS < 0.1 cent (measurement floor, and drift is the cause); (b) every window median within +- bound of 0 (no random walk), std across windows >= 0.2 cents (it moves), seeds' cents traces `pearson` < 0.5, hashes differ.
- expected: (a) RMS about 1.5 cents; (b) medians inside +- 5 cents.
- FAIL means: max above the bound = drift depth or its output limiter wrong; RMS 0 = the trim is not wired or drift is summed inside the +-2 oct clamp with the matrix (it must sit outside, unscaled by Agitate); a growing |median| in (b) = the write clock is being re-referenced each pass (a random walk), i.e. the read side is not sharing the write accumulator.

#### `generative` and `generative-agitated` (phase 3)
- Purpose: the Phase 3 milestone: no input, Interference + Decay high produces a self-playing texture that evolves and never exactly repeats. Split per the modulation verdict so a periodic Agitation sweep cannot masquerade as evolution (and cannot fail an autocorrelation line by construction).
- Input: silence, `kGenerativeSec = 60` (`--quick`: `kQuickGenerativeSec = 30` and one seed). Seeds 1 and 2; a third run with seed 1 plus one extra input sample of 1e-5 at t = 0.5 s (sensitive dependence).
- Params (both): decay 1.10, time01 0.55, filter 3000, resonance 0.4, absorb 0.3, blend 1, timeMod 1.0. `generative`: agitate 1.0 with routing = hero minus Agitation -> Filter and Follower -> Decay (`heroWith` twice, both 0), so only Interference -> Time, the drift trim and the in-loop noise move anything, whatever `modk::kMacroScalesTimeColumn` is. `generative-agitated`: hero routing, agitate 0.6, agitSpeedHz 0.05 (a 20 s cycle starving and re-opening the loop).
- Trims: production.
- Measure: `hopRms (20 ms)` -> energy trace e; `bloomSeconds` = first hop with RMS > -40 dBFS; over [20, 60] s: `cv (e)`, `autocorrMax (e normalised, 0.25 s, 20 s)`; `centroid` trace in 200 ms hops, its `cv`, its std in Hz, and `autocorrMax (centroid trace, 2 s, 15 s)` (the pattern line); peak; non-finite; `fnv1a` of each 1 s window over [20, 60]; seeds: hash and energy cross-correlation peak; perturbation: `rms (a - b) / rms (a)` over [50, 60].
- PASS (`generative`): RMS over [40, 60] > -40 dBFS (it starts itself from the floor; report `bloomSeconds`, expected 15 to 35 s at +0.83 dB per 0.4 s pass); `cv (e)` > 0.1 and `cv (centroid)` > 0.05 (it moves in level AND timbre); `autocorrMax (e)` < 0.9 and the pattern line < 0.8 (no exact or near repeat); peak < 0.999, no non-finite, RMS < -3 dBFS; all 40 window hashes distinct; the two seeds differ in hash and their energy traces' cross-correlation < 0.9; perturbation ratio > 0.5 (chaotic, not merely noisy: uncorrelated equal-power signals give 1.41); with `seedForTests (1)` twice the hashes are equal (deterministic when told to be).
- PASS (`generative-agitated`): the same minus the two autocorrelation lines, plus centroid std >= 150 Hz; report the dominant autocorrelation peak of e over lags 0.3 to 10 s (the breathing period; the modulation design predicts 0.5 to 5 s).
- expected: bloom about 25 s; `cv (e)` about 0.3; energy cross-correlation between seeds about 0.3.
- FAIL means: never blooms = the noise floor at that Time is too low for Decay 1.10 to lift within a minute (raise the long-Time noise or accept a longer bloom and say so); autocorrelation near 1 = a static self-oscillating tone or the drift alone; `cv` near 0 = the whole concept is not yet there; perturbation ratio near 0 = the loop is a stable limit cycle, not chaos. These two scenarios are expected to be red until Phase 3 is tuned; they print their numbers regardless (phase 3 tag).

### E. Invariance and cost

#### `samplerate` (phase 1)
- Purpose: the chip, filters, noise, THD, modulation and follower do not depend on the host rate.
- Input / Params: for `sr` in {44100, 48000, 96000, 192000} (`--quick` skips 192000): (1) `delaytime` at time01 0.5; (2) self-oscillation pitch with filter 5000, resonance 1.0 (5 kHz exposes a missing prewarp at 44.1 k); (3) `noisefloor` at time01 0.0, 0.5 and 1.0, production trims; (4) wet RMS of a 220 Hz sine at time01 0.3, decay 0.8; (5) `bandwidth` centroid at 1.0; (6) agitation period at 2 Hz; (7) follower attack; (8) `thd23` at 342 ms with chipOnly trims.
- PASS (each column against its 48 k value): delay +- 0.5 %; self-osc pitch +- 1 %; noise +- 2 dB at 0.0 and 1.0, +- 3 dB at 0.5 (the 0.0 column at 192 k is what catches an inverted `sqrt (sr / 48000)` hiss scaling, PT design 2's fatal flaw 2: a 6 dB error on the hiss shifts the total by about 4 dB); sine RMS +- 0.5 dB; centroid +- 10 %; agitation period +- 1 %; attack +- 3 ms; THD +- 20 % relative. Prints one table row per rate.
- expected: all columns flat; the delay identical to the sample because it is `N / fs_chip`.
- FAIL means: whichever column drifts names the module whose coefficients are computed at a fixed 48 k or per host sample instead of per chip sample.

#### `blocksize` (phase 1)
- Purpose: identical output regardless of how the host chops the stream, including buffers longer than `maxBlockSize`.
- Input: `burst (220, 0.5, 0.1, 300 ms)` + `noise (0.05, 9)`, 4 s at 48 k (the generators are functions of sample index, so the input is identical for every chopping).
- Params: decay 0.9, agitate 0.7, time01 0.4, timeMod 0.3, agitSpeedHz 3, filter 3000, seed 42.
- Trims: production.
- Measure: reference = 128-sample blocks. Compare `fnv1a (L)` and per-sample `max |delta|` for: 32, 512, 2048 (each with `prepare (sr, thatSize)`); 4096-sample calls after `prepare (sr, 128)` (chunking); size 1; a varying sequence {128, 37, 512, 1, 200, 64, 333, 2048, 5, 96, ...} cycling under a 2048 maximum with `prepare (sr, 2048)`.
- PASS: 32, 512, 2048 and the 4096-after-128 run hash-identical to 128 (multiples of `modk::kControlBlock`: every boundary is a tick boundary, params are static, RNGs are consumed per sample, so nothing may depend on block length); size 1 and the varying sequence `max |delta| < 1e-6` and hash reported (a linear `SmoothedValue::skip (n)` computes `step * n` rather than n additions, which is the only legitimate rounding difference).
- expected: 0 and about 1e-7.
- FAIL means: a hash mismatch on multiples of 32 = something is computed per block (a per-block ramp toward a target, a per-block control-rate update, a per-block RNG draw, a control-rate accumulator reset per sub-block, a `ChipCharacter` refreshed at block start); the varying sequence off by more = control chunks restart at each block start; a crash at 4096 = no chunking above `maxBlockSize`.

#### `cpu` (phase 1)
- Purpose: the plan's budget and the risk note on per-sample divisions.
- Input: `noise (0.1, 11)`, 30 s at 48 k / 128 (`--quick` 10 s); then 10 s of silence with decay 0 (5 s); then 192 k / 32 for 10 s (5 s).
- Params: decay 1.1, agitate 1.0, timeMod 1.0, agitSpeedHz 1000, resonance 0.8, filter 4000 (the busiest legal setting).
- Trims: production.
- Measure: `Run::meanBlockUs`, `maxBlockUs`; percent of real time = `meanBlockUs / (1e6 block / sr)`; ns per sample.
- PASS: 48 k / 128 < `kCpuGatePct = 10` % (report against `kCpuBudgetPct = 5`; the gate is loose so a loaded laptop does not flake); 192 k / 32 < 25 %; max block < 20 x mean (no allocation or periodic heavy work); silent-input mean <= 1.5 x busy mean (no denormal cliff even under FTZ, which the rig sets like the processor).
- expected: 100 to 300 ns per sample in RelWithDebInfo, about 1 to 3 % at 48 k.
- FAIL means: spikes = allocation or a `std::function` in the block path; silence slower than noise = denormals reaching a filter without FTZ (the standalone path always has it, an AU host might not honour it in every thread).

#### `pin` (phase 1, pinned at the phase 2 gate)
- Purpose: one regression hash over the whole audio path, updated only on purpose.
- Input: `burst (220, 0.5, 0.1, 300 ms)` + `noise (0.05, 9)`, 3 s at 48 k / 128, hash over [1, 3] s.
- Params: decay 0.95, agitate 0.6, timeMod 0.2, agitSpeedHz 2, time01 0.4, filter 3000, resonance 0.4, absorb 0.3, seed 42.
- Trims: production.
- PASS: hash == `kPinnedHash`. While `kPinnedHash == 0` the scenario reports and passes (unpinned). Pin it at the Phase 2 gate; from then on a hash change must be a deliberate retune, and the commit that retunes updates the constant and says why in PROGRESS.md.

### F. Not a scenario: `render`

`EngineTest render <in.wav> <out.wav> [--time t] [--decay d] [--filter hz] [--res r] [--absorb a] [--blend b] [--strength db] [--agitate a] [--speed hz] [--timemod m] [--seed s] [--trims clean|production]`
renders a file through the engine (mono sum in, stereo out, the file's sample
rate, 512-sample blocks) for the by-ear milestones (plan Phase 1 and 2:
drum loop, guitar DI, sine). Graft from PT design 0. It prints wet RMS per
second and exits 0; nothing about it is pass/fail.

---

## 5. Run-all mode, phase gating, and the runtime budget

```cpp
struct Scenario { const char* name; void (*run)(); double approxAudioSeconds; int phase; };
const std::vector<Scenario>& scenarios();   // ordered as in section 4: A core, B loop, C commands, D modulation, E invariance

int main (int argc, char* argv[])
{
    // no args          -> run every scenario
    // <name> [<name>]  -> only those (unknown name: print the list, exit 2)
    // list             -> names, phase and approx cost, sum, exit 0 (warns when the sum exceeds kBudgetWarnAudioSeconds = 2000)
    // --quick          -> gQuick = true: generative 30 s one seed, drifttime one seed, samplerate skips 192 k, cpu 10 + 5 + 5 s
    // render ...       -> section 4 F
    for each selected: printf ("\n=== %s%s ===\n", name, phase > kCurrentPhase ? " (pending)" : ""); tick; run(); printf ("  (%.2f s)\n", elapsed)
    // a scenario with phase > kCurrentPhase counts its checks into a separate `pendingFailures` (printed, not fatal)
    summary table: name, ok / FAIL / pending, seconds; then "all checks passed" or "%d check(s) FAILED" (+ "%d pending")
    return failures == 0 ? 0 : 1;
}
```

`failures` is per check, so the exit code reflects any red line anywhere in a
scenario whose phase has landed; the summary table marks which scenarios
contained one. `kCurrentPhase` is bumped in the commit that closes a phase,
which is the moment its pending lines become fatal. Print the numbers even on
PASS: the human reads them into PROGRESS.md.

Runtime budget. Wall time is dominated by audio-seconds rendered; three
linear-interp stages plus SVF, saturator and control-rate modulation cost
roughly 100 to 300 ns per sample in RelWithDebInfo, so about 5 to 15 ms of
wall per second of 48 k audio.

| Scenario | Audio seconds (48 k equivalent) |
|---|---|
| delaytime | 13 |
| multitap, timesweep, smear | 2 + 2.2 + 1 = 5 |
| sync | 9 |
| bandwidth | 16 |
| thd | 8 |
| noisefloor | 20 |
| clockbleed | 11 |
| firstblock | 3 |
| runaway | 42 (phase 1) + 30 (phase 3) |
| strength, filter, resonance, absorb | 2 + 13 + 12 + 16 = 43 |
| clear, zipper, zippermod, passthrough | 10 + 20 + 6 + 8 = 44 |
| nondeterminism, nan | 24 + 9 = 33 |
| agitation | 54 |
| follower, interference | 8 + 40 = 48 |
| timemod | 15 |
| drifttime | 120 (quick 80) |
| generative + generative-agitated | 180 + 120 = 300 (quick 60) |
| samplerate | 160 with 192 k counted 4x (quick 80) |
| blocksize | 28 |
| cpu | 80 (quick 20) |
| pin | 3 |
| Total | about 1100 audio-seconds, 6 to 17 s wall; `--quick` about 550 |

That leaves a 2 to 5x margin under the 30 s target for the full run. Every
new scenario states its `approxAudioSeconds`; `EngineTest list` sums them.
Never lengthen a render to make a threshold pass: shorten the settle by
choosing a faster Time setting instead.

---

## 6. ProcessorTest (the processor, state, presets, host sync)

`Tests/ProcessorTest.cpp`. `juce::ScopedJuceInitialiser_GUI` in `main`, the
same `check` / `report` printers, the helper subset from `Tests/TestHelpers.h`,
and a fixture that owns a prepared processor (Infinite Sustainer's
`PresetProbe` shape, extended with a render loop):

```cpp
struct Fixture
{
    DybbukProcessor proc;
    Fixture() { proc.prepareToPlay (48000.0, 128); }
    void set (const char* id, float plain)         // p->setValueNotifyingHost (p->convertTo0to1 (plain))
    float get (const char* id)                     // proc.apvts.getRawParameterValue (id)->load()
    juce::RangedAudioParameter* param (const char* id);
    // Drives processBlock with an empty MidiBuffer; ScopedNoDenormals like the host would; returns L, R, in.
    Run render (double seconds, std::function<float (double)> input,
                std::function<void (double t)> perBlock = {});
};

struct FixedTempo : juce::AudioPlayHead                 // the same fake the UISnapshot uses
{
    double bpm = 120.0; int num = 4, den = 4; bool withBpm = true;
    juce::Optional<PositionInfo> getPosition() const override
    {
        PositionInfo p;
        if (withBpm) p.setBpm (bpm);
        p.setTimeSignature (TimeSignature { num, den });
        p.setIsPlaying (true); p.setPpqPosition (0.0);
        return p;
    }
};
```

Every scenario below is phase 2 except `presetsaudio` (phase 4); the exit
code rule and phase gating are the same as EngineTest's.

#### `state`
- Enumerate `proc.getParameters()`; set each `RangedAudioParameter` to normalised `frac (0.137 + 0.271 i)` via `setValueNotifyingHost`, read back what it snapped to as the reference; set `apvts.state` properties `theme = 1` and `uiScale = 1.25`; `getStateInformation` -> blob -> fresh processor -> `setStateInformation`.
- PASS: every parameter |delta| < 1e-6; `theme == 1` and `uiScale == 1.25` on the fresh processor (a session carries the theme); `stateVersion` present and equal to the processor's `currentStateVersion`. Then: junk input (`nullptr, 0`; foreign XML `<Other/>`; non-XML bytes) leaves the parameters untouched and does not crash; a blob with one `PARAM` child removed (pick `absorb`) loads that parameter's DEFAULT (not the value the knob held before the load: `restoreMissingParameterDefaults`) and restores the rest (forward compatibility); a blob whose `stateVersion` is absent loads (pre-1 dev builds are treated as 1).
- FAIL means: the classic silent-save bug from CLAUDE.md; the missing property names the culprit.

#### `presets`
- After the same parameter walk: `saveCurrent ("__probe_<pid>")`; the file exists under `presetManager.userFolder()` (the real `~/Library/Audio/Presets/fxcircus/Dybbuk`, the name makes leftovers obvious); parse it: root tag `"DybbukPreset"`, no `theme`, `uiScale` or `presetDate` property in the embedded state; load it in a fresh processor whose `theme` was set to 1 and whose `bypass` was set to 1 first; compare; delete the file.
- PASS: every parameter |delta| < 1e-6 except `bypass`, which is still 1 (a preset never takes the plugin in or out of circuit; the template's `finishLoad` forcing performance params to 0 and `applyFactoryDefaults` touching them are both gone); `theme` still 1 after the load (`editorOnlyProperties` preserved); `isDirty()` false right after the load; `getCurrentName()` equals the saved name; `getPresets()` begins with "Init" followed by the factory table entries in table order; each factory entry (the table count is reported; Echo-Verb, Wow and Flutter, Bat Cave, Breathing once they ship) loads with `getCurrentName()` matching and `theme` still 1; loading "Init" restores every non-performance parameter to its default and leaves `bypass` alone; loading a preset while a 220 Hz sine runs: `maxStep` over the 100 ms after the load <= 3 x the steady `maxStep` (parameters jump but every path is smoothed; whether Time snaps or smears on a preset load is reported, section 12).
- FAIL means: bypass flipped = `performanceParams()` not exempted on load; theme flipped = presets still carry editor properties (the sibling bug the params section fixes); dirty after load = the listener fires on the manager's own writes.

#### `presetsaudio` (phase 4)
- For each factory preset: load, then `render (4 s, burst (220, 0.5, 0.1, 500 ms))`.
- PASS: finite; peak < 0.999; RMS over [0.9, 1.0] > -40 dBFS (something comes back at every preset's Blend); print RMS at 1 s and 3 s and a `runaway` flag (RMS at 3 s > RMS at 1 s - 3 dB) per preset, so a table edit that silences or blows up a preset shows as a number.

#### `bypass`
- Processor path (the crossfade lives in `processBlock`). 220 Hz sine amp 0.5 throughout. Decay 0.8, Blend 1.0, Filter 2000, Time 0.3. Reference run R without bypass; run B with the Bypass parameter -> 1 at 1.0 s and -> 0 at 1.5 s.
- PASS: B: `max |out - in| < 1e-6` over [1.03, 1.47] (true dry after the 20 ms fade); `maxStep` over [0.99, 1.10] and [1.49, 1.60] <= 1.5 x `maxStep` over [0.5, 0.99] (no click; a hard switch adds a step of order 0.5); `rms_B [1.522, 1.538] / rms_R [1.522, 1.538]` in [0.2, 0.65] (the window sits after the fade-in and before the first new tap at 1.5 + T/3 = 1.54 s, so it contains only the loop's old content: an engine that kept running on SILENCE while bypassed is 4.2 passes at 0.8 down, expected 0.39; one fed the input while bypassed reads about 1.0, and one that was stopped also reads about 1.0); wet present again over [1.6, 2.0] (`rms (out - in) > 0.01`); `getBypassParameter()` is the parameter with id `bypass` and it is declared last; a one-block bypass pulse (on at 2.0 s, off one block later) does not crash and produces no non-finite sample.
- FAIL means: residual during bypass = the crossfade never reaches 1; a ratio near 1 = the engine sees input while bypassed (or is paused); a click = a hard switch.

#### `readouts`
- For every float parameter at normalised {0, 0.25, 0.5, 0.75, 1}: `getText (norm, 64)`; plus Time with `timesync` on at the same five points; plus every `getLabel()` and `getName (64)`.
- PASS (regexes are ECMAScript, `std::regex`): no text matches `\d+\.\d{3,}` (the `juce::String (v, 0)` trap); Time free: `^\d+ ms$` below 100 ms and `^\d+\.\d{2} s$` at or above; Time synced: the text is one of `timemap::kDivisions[].name`; Filter: `^\d+ Hz$` at or above 100 Hz and `^\d+(\.\d{2})? Hz$` below (the params section chose integers throughout because the floor is 20 Hz; both forms accepted); Agit Speed: `^\d+\.\d s$` below 1 Hz (the period, Infinite Sustainer's Sweep Rate precedent and the modulation verdict's graft), `^\d+\.\d{2} Hz$` in [1, 100), `^\d+ Hz$` at or above 100; Blend, Absorb, Agitate, Time Mod, Resonance: `^\d+$` with label `%` (or `^\d+ ?%$` if baked); Tones Level `Off` at 0, Spread `Mono` at 0, otherwise `^\d+$`; Strength: `^\d+\.\d$` with label `dB`; Out: `-Inf` at the bottom else `^-?\d+\.\d$` with label `dB`; Decay: `^\d\.\d{2}$` up to 1.00 and `^\d\.\d{2} runaway$` above 1.005; Tones Pitch: `^[A-G]#?-?\d( [+-]\d+)?$`; the ms-style parameters have an empty label; no name is empty or longer than 19 characters; the UTF-8 bytes `E2 80 94` (em dash) appear in no name, label or text. Print the full table so the human can eyeball it against the UISnapshot images.
- expected: "28 ms", "0.31 s", "3.60 s"; "62.5 s", "4.00 Hz", "1000 Hz"; "1.15 runaway"; "-Inf".

#### `order`
- PASS: parameter IDs unique; declaration order begins `time, decay, filter, resonance, absorb, blend, agitate, agitspeed` (Push bank 1); `bypass` last; `getVersionHint()` strictly ascending in declaration order (the AU wrapper sorts by hint then hash-of-id, so equal hints scramble the Push order; `bypass` carries 1000); count >= 13 and reported (17 when the Phase 4 parameters exist).

#### `clearui`
- `proc.requestClear()` mid-runaway (Decay 1.15, 220 Hz burst then silence, request at 3.0 s through `perBlock`): RMS over [3.01, 3.5] < -80 dBFS with the engine trims clean (the fixture exposes `proc.engineTrimsForTests()` or constructs the processor with clean trims; if neither exists, use -60 dBFS with production trims and say so) and `proc.getClearsServed()` incremented by 1. Proves the mailbox is wired through the processor, not just the engine.

#### `synchost`
- `FixedTempo ph; proc.setPlayHead (&ph)`; `timesync` on; Time set to the normalised value whose `divisionIndexForTime01` is "1/4" (index 8 of 14: 8/13); input `burst (500, 0.5, 0.1, 6 ms)`; Decay 0, Blend 1, Filter 18k.
- PASS: `xcorrLag` in [0.45, 0.55] = 0.500 s +- 0.5 %; `proc.getDelaySeconds()` reads 0.500 +- 0.1 % after 0.2 s; bpm -> 100 at 1.0 s: `getDelaySeconds()` per block monotonic from 0.5 to 0.6 with per-block ratio <= 1.5 (the host tempo goes through the same smoother); `withBpm = false` (a position with no tempo, what the standalone reports) afterwards: the delay holds 0.600 (last known tempo), no non-finite; `setPlayHead (nullptr)`: still 0.600, still running; bpm 60 and Time = 1.0 ("1 bar"): `isSyncClamped()` true and `getDelaySeconds()` = `timemap::kDelayMaxSec`; bpm 120 in 3/4 with "1 bar": 1.500 s +- 0.5 %.
- expected: 0.500 / 0.600 / 3.600 / 1.500 s.
- FAIL means: no smear on the tempo step = the processor writes fs_chip directly instead of `delayTargetSec`; a jump to 120 when the tempo goes missing = `lastKnownBpm` not kept; 4.0 s = the clamp lives only in the readout.

---

## 7. UISnapshot

The UI section (`04-ui.md` section 14) owns the state list; the gate here is
that every image exists after `cmake --build build --target UISnapshot` and a
run from a scratch directory, and that each was opened and looked at:
`editor_snapshot.png`, `_active`, `_modulated`, `_runaway`, `_hover`, `_sync`,
`_sync_clamped`, `_gate`, `_parchment`, `_parchment_active`,
`_parchment_preset`, `_browser`, and `_preset_<name>` for the four factory
presets. The harness prints the readout line per image; `ProcessorTest
readouts` and those printed lines must agree (same strings for the same
values). A UI change that has not been looked at is not finished.

---

## 8. Manual gates (paste into `docs/PROGRESS.md`, replacing the template's `## Phase gates`)

Automated items name their scenario; the rest need hands and ears. Procedures
once, at the top of the section. (Headings use colons, not dashes.)

```markdown
## Phase gates

Procedures referenced below:
- Standalone rig: `open build/Dybbuk_artefacts/RelWithDebInfo/Standalone/Dybbuk.app`,
  Options -> Audio Settings -> pick the interface, input = guitar DI channel,
  48 kHz / 128. No rescan needed; quit and relaunch after every build.
- Live rig: Ableton Live 12 only rescans plugins at startup, so after
  `./build.sh` QUIT LIVE COMPLETELY AND REOPEN IT before testing. Insert
  Dybbuk (VST3 and AU are separate entries; test both) on an audio track
  with the DI, monitoring In. Null test = duplicate the track, bypass the
  copy, insert Utility (phase invert) after it, sum: silence.
- Listening tells come from the plan's tuning references (section 5). Each
  one states what a FAIL sounds like; if it fails, stop and tune, do not
  move on.

### Phase 0: Skeleton
- [ ] `./build.sh` completes with zero warnings from `Source/` and `Tests/`
- [ ] pluginval strictness 5 passes
- [ ] `auval -v aumf Dybk Fxci` passes
- [ ] Live rig: appears in the browser (both formats), passes audio unchanged with Blend 0 (null test silent)

### Phase 1: DSP core
- [ ] `EngineTest` green: delaytime multitap timesweep smear bandwidth thd noisefloor clockbleed firstblock runaway strength filter resonance absorb clear zipper passthrough nondeterminism nan blocksize samplerate cpu
- [ ] `EngineTest noisefloor` values recorded here: ____ / ____ / ____ / ____ / ____ dBFS, centroid at Time 1: ____ Hz (rumble or hiss?)
- [ ] CPU measured (`EngineTest cpu`, 48 k/128, busiest setting): ____ % of one core, max block ____ us
- [ ] Code review items the harness cannot see: phase accumulator wrapped every sample; every cross-thread flag is `std::atomic`; `quantStep` and its reciprocal recomputed as a pair, never ramped apart; `ScopedNoDenormals` at the top of `processBlock`; no allocation in `process` (grep for `resize`, `push_back`, `new`, `std::function` calls in the block path)
- [ ] Tell 1, standalone, Blend 50 %, Decay 0.6, Time 0.3: sweep Time slowly by hand while a chord rings. PASS: the repeats glide in pitch like a tape machine changing speed. FAIL sounds like: two clean delay times crossfading or stuttering with no pitch change.
- [ ] Tell 2, standalone, Time at max, Decay 0.5, Filter open: play one note. PASS: repeats are dark and hissy and there is a faint ticking/burbling under them that gets louder as Time is pushed further. FAIL: repeats stay bright, or the noise is a constant broadband hiss with no tick.
- [ ] Tell 3, standalone, Decay 1.15, any Time: play a chord, stop. PASS: the loop swells into a warm saturated drone that stays there. FAIL: a buzzing square-wave edge (digital clip), a rising howl that keeps getting louder, or the loop dying out.
- [ ] 1-vs-3 stage A/B (`-DDYBBUK_PT_STAGES=1` build, `EngineTest multitap` prints both): decision recorded here with the reason
- [ ] Feedback topology A/B (`kFeedbackFromTapSum` true/false): decision recorded here
- [ ] Write anti-alias A/B (`kGuardPoles` 0/1/2) at Time 1 with a guitar: decision recorded here
- [ ] Patch Corner "Echo-Verb" (short Time, moderate Decay, Filter dark): reads as a dark room, not a slapback
- [ ] Patch Corner "Wow and Flutter" (Blend 50 %, Absorb or Filter high): a picked chord comes back as aged, wobbling tape
- [ ] No clicks by hand: with a chord ringing, wiggle every knob fast in the standalone (`zipper` covers steps; this covers knob throw)
- [ ] Clear (double-click Decay or the button) silences a runaway loop instantly with no thump

### Phase 2: Parameters and state
- [ ] `EngineTest sync` green; `EngineTest pin` pinned (`kPinnedHash` set) and `kCurrentPhase = 2`
- [ ] `ProcessorTest` green: state presets bypass readouts order clearui synchost
- [ ] Live rig: every parameter appears in the automation chooser with the readout matching the UI (screenshot the chooser: Time in ms/s, Filter in Hz, Speed in s below 1 Hz, no three-decimal values)
- [ ] Live rig: first eight in Live's Configure panel and on Push are Time, Decay, Filter, Resonance, Absorb, Blend, Agitate, Speed (both formats)
- [ ] Live rig: set every knob off default, toggle Sync on with 1/8, switch theme, save the set, quit Live, reopen, values and theme restored (both formats)
- [ ] Live rig: Sync follows a tempo change from 120 to 90 with a smear, not a jump; 1 bar at 60 BPM shows the clamped readout
- [ ] Live rig: copy-paste the device to another track keeps its state
- [ ] Live rig: host bypass (device on/off) mid-runaway: no click either way, loop still there when re-enabled
- [ ] pluginval strictness 10 passes; `auval` passes

### Phase 3: Playability (modulation)
- [ ] `EngineTest` green with `kCurrentPhase = 3`: agitation follower interference timemod drifttime zippermod runaway (b) (d) nondeterminism (C) generative generative-agitated
- [ ] `EngineTest generative` numbers recorded here: bloom ____ s, cv(e) ____, cv(centroid) ____, autocorr ____, seeds xcorr ____
- [ ] Tell 4, standalone: same settings, play the same chord twice with a Clear in between. PASS: the two repeats are recognisably the same but not identical (drift, different hiss grain). FAIL: bit-identical repeats, or so different the settings feel random.
- [ ] Tell 5, standalone, Time Mod up, Speed in the audio range, Time short: play a single note. PASS: metallic ring-mod-like clang whose pitch tracks the note. FAIL: a wobble or vibrato (control-rate modulation) instead of clang. (Requires an audio-rate source routed to Time from the UI: a non-zero `kHeroAgitTime` or the Phase 4 Tones sub; until then this tell is test-only via `timemod`.)
- [ ] Generative milestone by ear: no input, Decay 1.1, Agitate full, Time 0.55, Filter 3 kHz. Leave it five minutes. PASS: it plays itself, it changes, nothing repeats verbatim, and the crackle audibly rides the loop's level. FAIL: a static drone, a loop you can hum along to, or silence after a minute.
- [ ] Agitation gate mode fires once per picked note and rests between notes; a re-pick while the previous note still rings also fires (decides whether the gate detector needs the HF-transient upgrade)
- [ ] Played through the standalone for a full session with guitar; the feel notes below are resolved: Time knob throw (fine control via shift-drag), Decay's runaway zone reachable but not accidental, Clear reachable without looking, Interference character (crackle vs wander balance, ring-down length) accepted or constants retuned

### Phase 4: Presets and control
- [ ] Preset save / load / rename / delete / star from the header; prev/next arrows step in list order
- [ ] Four Patch Corner factory presets ship; `ProcessorTest presets` loads them and `ProcessorTest presetsaudio` prints sane numbers for each
- [ ] Bypass state survives a preset load (a preset never takes the plugin out of circuit); Clear is momentary and never saved; the theme survives a preset load
- [ ] Live rig: Clear mapped to a MIDI button via Live's mapping works while a loop runs away

### Phase 5: UI
- [ ] `UISnapshot` renders, and every image was opened and looked at: idle brass, active (ember lit, OUT meter moving), modulated, runaway (red arc, readout "1.15 runaway"), hover strip, sync (note-value readout), sync clamped ("3.60 s max"), gate mode, parchment idle, parchment active, parchment after a preset load, browser, the four presets
- [ ] Value readouts checked on the screenshots for every knob at min, default, max: two decimals maximum, units baked in, no em dashes anywhere in UI copy (`grep -rn $'\xe2\x80\x94' Source/ui Source/Parameters.cpp` is empty)
- [ ] Readout strip shows name + value for every control on hover and drag (replaces the template's tooltip gate; the design has no floating tooltips)
- [ ] Window scales and stays aspect-locked; theme toggle persists across reopen
- [ ] Ember animation reads the loop energy: dark at Decay 0 after silence, glowing steadily in runaway, breathing with Agitation, black dip on Clear

### Phase 6: Validation matrix
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

## 9. Definition of done for any DSP change (the harness view)

1. `./build.sh` passes: zero warnings from `Source/` and `Tests/`, `EngineTest` and `ProcessorTest` exit 0, pluginval 10, auval.
2. A new or changed behaviour has a scenario line whose number would change if it broke; the PROGRESS.md entry quotes that number.
3. A deliberate retune of the sound updates `kPinnedHash` in the same commit and says what moved.
4. Anything that touched the editor: UISnapshot re-run and looked at, both themes.
5. Anything that touched state: `ProcessorTest state presets` re-run (the silent-save bug is found by this line or by the user, never by the compiler).

---

## 10. Judge fatal flaws and the line that catches each

| Verdict | Fatal flaw | Caught by |
|---|---|---|
| mod, design 0, 1 | control-rate destinations step once per tick with no intra-tick ramp | `zipper` / `zippermod` control-rate line at `sr / kControlBlock`; `follower` onset line |
| mod, design 0, 2 | Agitate / Time Mod macros unsmoothed | `zippermod` rows timeMod 0 -> 1 and agitate 0 -> 1 |
| mod, design 0, 3 | drift bound wrong by 2x (one-sided instead of rail-to-rail) | `drifttime` bound `2 x 1200 x kDriftTimeOct x 1.25` |
| pt, design 1, 1 | its noise floor (-70 dBFS at Time 0) cannot meet its own test | `noisefloor` `rms (0.0) <= -76 dBFS`, calibrated between the winner's budget (-80 to -89) and the flaw |
| pt, design 1, 2 | chip character computed at block start (block-size dependent) | `blocksize` hash identity across 32 / 512 / 2048 / 4096 |
| pt, design 1 (musician), 2 | SVF g, k, h ramped separately (inconsistent TPT solve while k < 0) | `resonance` guard (cutoff step at resonance 0.95), `zipper` resonance row |
| pt, design 2, 1 | no NaN / inf guard | `nan` |
| pt, design 2, 2 | hiss amplitude scaled `sqrt (48000 / sr)` (inverted) | `samplerate` noise column at time01 0.0, +- 2 dB, 192 k |
| pt, design 2, 3 | non-atomic cross-thread flag, editor reading engine floats | not measurable here: Phase 1 code-review checklist item; the contract in 1.2 makes every published value an atomic |
| pt, design 0 (musician), 1 | first-block chirp: smoother starts at the hard minimum | `firstblock` (a), (b), (d) |
| pt, design 0 (musician), 2 | unbounded phase accumulator | not observable in the budget (precision loss after hours): Phase 1 code-review item "phase wrapped every sample" |
| pt, design 0 (musician), 3 | noise-floor claim optimistic by about 4 dB | `noisefloor` bound leaves 9 dB to the -85 estimate; the expected curve is printed next to the measurement |

Non-fatal defects the judges named that a scenario also pins: the absolute
Clear step bound (replaced by the 1.5x relative criterion in `clear`), the
`fm` rationale about tap phases (replaced by the grid metric and the alias
line in `timemod`), the per-sub-block Agitation mean (caught by `blocksize`),
the per-block control cadence (`blocksize`), the generative gate that
contradicted its own autocorrelation line (split into two scenarios).

---

## 11. Named constants introduced by this section

| Constant | Value | Where |
|---|---|---|
| `kSr` / `kBlock` | 48000 / 128 | EngineTest rig |
| `kCurrentPhase` | 1 (bumped per phase) | EngineTest, ProcessorTest |
| `kPinnedHash` | 0 until the Phase 2 gate | EngineTest `pin` |
| `kBudgetWarnAudioSeconds` | 2000 | `EngineTest list` |
| `kGenerativeSec` / `kQuickGenerativeSec` | 60 / 30 | `generative*` |
| `kZipperToneHz` | 48000 / 208 = 230.77 Hz | `zipper`, `zippermod`, `follower` |
| `kClearSettleSec` | 0.010 | `clear`, `clearui` |
| `kTimeModTestSwingOct` | 0.10 | `timemod` (A), (B), (C) |
| `kTimeModAliasSwingOct` | 0.40 | `timemod` (D) |
| `kSidebandSwingOct` | 0.70 | `timemod` (E) |
| `kDriftBoundMargin` | 1.25 | `drifttime` |
| `kNoiseFloorMaxDb` | -76 dBFS | `noisefloor` |
| `kCpuBudgetPct` / `kCpuGatePct` | 5 / 10 | `cpu` |
| `kProbeSeed` | 42 | every seeded rig |

---

## 12. Assumptions other sections must honour, and open questions

Assumptions (each is one identifier or one constant; the harness breaks
loudly, not silently, if it is missing):

1. `DybbukEngine::Params` carries `delayTargetSec` (seconds, resolved by the processor); the engine clamps it into `[timemap::kDelayMinSec, kDelayMaxSec]`, ignores non-finite values, and smooths `log2 (kMemorySamples / delayTargetSec)` with `kTimeSmoothMs`; audio-rate modulation is added AFTER that smoother.
2. `DybbukEngine::Trims` with the seven fields of 1.2 and the two factories; `Trims::loopSat = 0` makes the saturator an identity and `inputDrive = 0` makes Strength a pure gain (`thd` and `strength` cannot be written otherwise).
3. `seedForTests (uint32)` reaches every RNG; default construction seeds from `juce::Random::getSystemRandom()`.
4. `setRoutingForTests (const ModMatrix::Routing&)` and the `ModMatrix` enums named in 1.2; `modk::kDstScaleTimeOct` is the full-scale of the Time column in octaves and Time Mod multiplies that column.
5. The atomics of 1.2 with those names and units; `uiLoopEnergy` is 0..1 log-mapped, above 0.5 in runaway and under 0.05 within 1.5 s of a flush to silence.
6. `prepare()` snaps every smoother to the first block's `Params` (no start-up ramp), is repeat-safe, and `process()` chunks internally above `maxBlockSize`.
7. Control ticks are stream-aligned (persistent countdown across blocks) at `modk::kControlBlock`; the PT core's slow refresh at `pt::kCtrlInterval` likewise; nothing is computed per host block.
8. Header-only `TimeMap.h`, `ChipConstants.h`, `ModConstants.h`, `ModMatrix.h` are includable from `Tests/` without dragging in `Parameters.cpp` or the editor.
9. Processor accessors: `getDelaySeconds()`, `isSyncClamped()`, `getClearsServed()`, `requestClear()`, `getLoopEnergy()`, `getOutputLevel()`; `setPlayHead` resolution keeps `lastKnownBpm`; parameter ids and order per the params section; `params::editorOnlyProperties() = { "theme", "uiScale" }`; preset root tag `"DybbukPreset"`.
10. The engine keeps running while bypassed and is fed silence (the processor crossfades over 20 ms).

Open questions (resolved here with the cheapest-to-change option; revisit
when the listening pass says so):

1. Agitate scaling the Time column: `modk::kMacroScalesTimeColumn` (assumed `true`, the winner's default). Every scenario is written to pass under both values by routing through `setRoutingForTests` and setting agitate 1.0 where the Time column must be alive; `interference` prints the spread at agitate 0 so PROGRESS.md records which shipped.
2. Which audio-rate source reaches Time from the UI (tell 5): assumed none in v1 (`kHeroAgitTime = 0`); `timemod` opens the cell itself. If a non-zero hidden depth or the Tones sub ships, `timemod` gains a run using the shipped path and the manual tell becomes reachable.
3. FM law (exponential in octaves vs linear in clock rate, `kFmLawLinear`): `timemod` (E) is law-independent; (A)/(B) use a ratio window wide enough for both. Assumed exponential.
4. Feedback topology (`kFeedbackFromTapSum`): `multitap`'s decay pass and `delaytime`'s repeat period pass under both; assumed `true` (plan literal). The A/B is a Phase 1 manual gate item.
5. Absorb touching feedback (`kAbsorbFeedbackShare`): assumed the winner's 4 dB at full; if 0 ships, the two loop-pass lines in `absorb` are dropped and PROGRESS.md says so.
6. Clock-bleed model (fs_chip tick train + fs/2 square, assumed, plan literal) vs memory-wrap ticks: `clockbleed` states the replacement line if the tick train is zeroed.
7. Memory size and range: `timemap::kMemorySamples = 5504`, `kDelayMaxSec = 3.6` (params and UI sections agree); a 5502 / 3.668 s variant changes no tolerance because every expectation is computed from `timemap` at run time.
8. Time on preset load, snap vs smear: not asserted; `presets` reports the `maxStep` and the `getDelaySeconds()` trajectory after a load.
9. Interference character, energy floor vs idle hiss at long Time, crackle density: only correlation, boundedness and rest are asserted; the numbers are printed for the tuning pass (`kEnergyFloorDb` re-check is a Phase 3 gate item).
10. `kNumStages` A/B and the write anti-alias poles (`kGuardPoles`): decided by ear at the Phase 1 gate; `multitap` and `bandwidth` print the numbers for both builds.
11. The exact resonance-0 damping (Q 0.5 vs 0.707): `filter` accepts both; the core section decides and PROGRESS.md records the measured -3 dB point.
12. Gate detector flavour on legato sources: `agitation`'s gate pass uses silence between bursts so both detectors pass; the re-pick-while-ringing case is a Phase 3 manual item.
