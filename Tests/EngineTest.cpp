// Offline DSP harness for the Dybbuk engine: drives it like a host and prints
// measurements, so behaviour can be verified without a DAW.
//
// THIS IS THE MOST IMPORTANT FILE IN THE REPO. Every scenario prints a number
// that would change if the behaviour broke. The numeric expectations come from
// docs/design/01-core.md section 9, which pins them to the plan's facts about
// the PT2399 and to the five "known tells" in dybbuk-plan.md section 5.
//
//   build/EngineTest_artefacts/RelWithDebInfo/EngineTest            (runs all)
//   build/EngineTest_artefacts/RelWithDebInfo/EngineTest runaway    (one scenario)
//   build/EngineTest_artefacts/RelWithDebInfo/EngineTest render     (writes wavs to listen to)
#include "../Source/dsp/BurstEngine.h"
#include "../Source/dsp/DybbukEngine.h"

#include <juce_audio_formats/juce_audio_formats.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <memory>
#include <vector>

namespace
{

int failures = 0;
int checksRun = 0;

void check (const char* what, bool ok, const juce::String& detail)
{
    ++checksRun;
    if (! ok)
        ++failures;
    std::printf ("  %-44s %s   %s\n", what, ok ? "PASS" : "FAIL", detail.toRawUTF8());
}

void note (const char* what, const juce::String& detail)
{
    std::printf ("  %-44s ....   %s\n", what, detail.toRawUTF8());
}

// --- measurement helpers -------------------------------------------------

double rmsOf (const std::vector<float>& x, int start, int n)
{
    if (n <= 0)
        return 0.0;
    double sum = 0.0;
    for (int i = 0; i < n; ++i)
    {
        const double v = (double) x[(size_t) (start + i)];
        sum += v * v;
    }
    return std::sqrt (sum / (double) n);
}

double peakOf (const std::vector<float>& x, int start, int n)
{
    double p = 0.0;
    for (int i = 0; i < n; ++i)
        p = juce::jmax (p, (double) std::abs (x[(size_t) (start + i)]));
    return p;
}

double dbfs (double linear)
{
    return 20.0 * std::log10 (juce::jmax (linear, 1.0e-12));
}

bool allFinite (const std::vector<float>& x)
{
    for (float v : x)
        if (! std::isfinite (v))
            return false;
    return true;
}

// Amplitude of one frequency, Hann windowed so a non-integer number of cycles
// does not leak into neighbouring measurements.
double goertzelAmp (const std::vector<float>& x, int start, int n, double freq, double sr)
{
    if (n < 4)
        return 0.0;
    const double w = juce::MathConstants<double>::twoPi * freq / sr;
    const double coeff = 2.0 * std::cos (w);
    double s1 = 0.0, s2 = 0.0, winSum = 0.0;
    for (int i = 0; i < n; ++i)
    {
        const double win = 0.5 - 0.5 * std::cos (juce::MathConstants<double>::twoPi * i / (n - 1));
        winSum += win;
        const double s0 = (double) x[(size_t) (start + i)] * win + coeff * s1 - s2;
        s2 = s1;
        s1 = s0;
    }
    const double re = s1 - s2 * std::cos (w);
    const double im = s2 * std::sin (w);
    return 2.0 * std::sqrt (re * re + im * im) / juce::jmax (winSum, 1.0e-9);
}

// Zero-crossing pitch estimate over a window: cheap, and it tracks a glide
// where a fixed-bin transform would smear it.
double zeroCrossFreq (const std::vector<float>& x, int start, int n, double sr)
{
    int crossings = 0;
    for (int i = 1; i < n; ++i)
        if ((x[(size_t) (start + i)] > 0.0f) != (x[(size_t) (start + i - 1)] > 0.0f))
            ++crossings;
    return crossings / 2.0 / ((double) n / sr);
}

unsigned int fnvHash (const std::vector<float>& x)
{
    unsigned int h = 2166136261u;
    for (float v : x)
    {
        unsigned int bits = 0;
        std::memcpy (&bits, &v, sizeof (bits));
        for (int b = 0; b < 4; ++b)
        {
            h ^= (bits >> (b * 8)) & 0xffu;
            h *= 16777619u;
        }
    }
    return h;
}

// --- harness -------------------------------------------------------------

using InFn = std::function<float (double)>;
using ModFn = std::function<float (double)>;
using ParamFn = std::function<void (double, TimeFilterLoop::Params&, TimeFilterLoop&)>;

std::vector<float> renderLoop (double sr, int block, double seconds, const InFn& in,
                               TimeFilterLoop::Params p, unsigned int seed,
                               const ParamFn& perBlock = nullptr, const ModFn& mod = nullptr)
{
    juce::ScopedNoDenormals noDenormals; // the host sets this; the harness must too

    auto loop = std::make_unique<TimeFilterLoop>();
    loop->prepare (sr, block);
    if (seed != 0u)
        loop->seedForTests (seed);

    const int total = (int) (seconds * sr);
    std::vector<float> out ((size_t) total, 0.0f);
    std::vector<float> inBuf ((size_t) block, 0.0f), wetBuf ((size_t) block, 0.0f),
        modBuf ((size_t) block, 0.0f);

    int pos = 0;
    while (pos < total)
    {
        const int len = juce::jmin (block, total - pos);
        if (perBlock)
            perBlock ((double) pos / sr, p, *loop);

        for (int i = 0; i < len; ++i)
        {
            const double t = (double) (pos + i) / sr;
            inBuf[(size_t) i] = in (t);
            if (mod)
                modBuf[(size_t) i] = mod (t);
        }

        loop->process (inBuf.data(), wetBuf.data(), len, p, mod ? modBuf.data() : nullptr);
        for (int i = 0; i < len; ++i)
            out[(size_t) (pos + i)] = wetBuf[(size_t) i];
        pos += len;
    }
    return out;
}

// One bare stage on the clock, for chip-level measurements with no loop.
std::vector<float> renderStage (double sr, double seconds, float time01, const InFn& in,
                                unsigned int seed)
{
    juce::ScopedNoDenormals noDenormals;
    ChipClock clock;
    clock.prepare (sr);
    clock.setTime01 (time01);
    clock.snapTime();

    PTStage stage;
    stage.prepare (sr);
    stage.seedForTests (seed);

    const int total = (int) (seconds * sr);
    std::vector<float> out ((size_t) total, 0.0f);
    for (int i = 0; i < total; ++i)
        out[(size_t) i] = stage.processSample (in ((double) i / sr), clock.advance (0.0f));
    return out;
}

float time01ForSeconds (double seconds) { return pt::time01ForDelaySeconds ((float) seconds); }

const InFn kSilence = [] (double) { return 0.0f; };

InFn sine (double hz, double amp) { return [hz, amp] (double t) { return (float) (amp * std::sin (juce::MathConstants<double>::twoPi * hz * t)); }; }


// Drives the whole engine, which is what the modulation scenarios need: the
// sources only exist above the loop.
std::vector<float> renderEngine (double sr, int block, double seconds,
                                 const InFn& in, DybbukEngine::Params p, unsigned int seed,
                                 std::vector<float>* energyOut = nullptr,
                                 double energyRateHz = 50.0)
{
    juce::ScopedNoDenormals noDenormals;

    auto engine = std::make_unique<DybbukEngine>();
    engine->prepare (sr, block);
    if (seed != 0u)
        engine->seedForTests (seed);

    const int total = (int) (seconds * sr);
    std::vector<float> out ((size_t) total, 0.0f);
    juce::AudioBuffer<float> buffer (2, block);

    const int energyEvery = juce::jmax (1, (int) (sr / energyRateHz));
    int sinceEnergy = 0;

    int pos = 0;
    while (pos < total)
    {
        const int len = juce::jmin (block, total - pos);
        for (int i = 0; i < len; ++i)
        {
            const float v = in ((double) (pos + i) / sr);
            buffer.setSample (0, i, v);
            buffer.setSample (1, i, v);
        }
        engine->process (buffer, p);
        for (int i = 0; i < len; ++i)
        {
            out[(size_t) (pos + i)] = buffer.getSample (0, i);
            if (energyOut != nullptr && ++sinceEnergy >= energyEvery)
            {
                sinceEnergy = 0;
                energyOut->push_back (engine->getLoopEnergy());
            }
        }
        pos += len;
    }
    return out;
}

// Normalised autocorrelation of a series at one lag, mean removed. A texture
// that repeats every N seconds shows a peak here; one that never repeats does
// not.
double autocorrelation (const std::vector<float>& x, int lag)
{
    const int n = (int) x.size() - lag;
    if (n < 16)
        return 0.0;
    double mean = 0.0;
    for (float v : x)
        mean += v;
    mean /= (double) x.size();

    double num = 0.0, den = 0.0;
    for (int i = 0; i < n; ++i)
    {
        const double a = x[(size_t) i] - mean;
        const double b = x[(size_t) (i + lag)] - mean;
        num += a * b;
    }
    for (size_t i = 0; i < x.size(); ++i)
    {
        const double a = x[i] - mean;
        den += a * a;
    }
    return den > 1.0e-12 ? num / den : 0.0;
}


// Stereo variant, for the spread scenario.
void renderEngineStereo (double sr, int block, double seconds, const InFn& in,
                         DybbukEngine::Params p, unsigned int seed,
                         std::vector<float>& outL, std::vector<float>& outR)
{
    juce::ScopedNoDenormals noDenormals;

    auto engine = std::make_unique<DybbukEngine>();
    engine->prepare (sr, block);
    if (seed != 0u)
        engine->seedForTests (seed);

    const int total = (int) (seconds * sr);
    outL.assign ((size_t) total, 0.0f);
    outR.assign ((size_t) total, 0.0f);
    juce::AudioBuffer<float> buffer (2, block);

    int pos = 0;
    while (pos < total)
    {
        const int len = juce::jmin (block, total - pos);
        for (int i = 0; i < len; ++i)
        {
            const float v = in ((double) (pos + i) / sr);
            buffer.setSample (0, i, v);
            buffer.setSample (1, i, v);
        }
        engine->process (buffer, p);
        for (int i = 0; i < len; ++i)
        {
            outL[(size_t) (pos + i)] = buffer.getSample (0, i);
            outR[(size_t) (pos + i)] = buffer.getSample (1, i);
        }
        pos += len;
    }
}

// --- scenarios -----------------------------------------------------------

// The chip is a fixed memory read at a variable clock, so the delay must come
// out at kStageWords / fs_chip regardless of host sample rate.
void coredelay()
{
    std::printf ("coredelay: one bare stage, impulse in, echo time versus kStageWords / fs_chip\n");
    const float t01 = 0.3f;
    const float fs = pt::fsChipForTime01 (t01);

    for (double sr : { 44100.0, 48000.0, 96000.0, 192000.0 })
    {
        const auto out = renderStage (sr, 0.2, t01,
                                      [] (double t) { return t < 1.0e-9 ? 0.5f : 0.0f; }, 7u);
        const double expected = (double) pt::kStageWords / (double) fs * sr;
        int best = 0;
        double bestV = 0.0;
        for (int i = 8; i < (int) out.size(); ++i)
            if (std::abs (out[(size_t) i]) > bestV)
            {
                bestV = std::abs (out[(size_t) i]);
                best = i;
            }
        const double tol = 30.0 * sr / 48000.0;
        check ("echo lands at kStageWords / fs_chip",
               std::abs (best - expected) <= tol && bestV > 0.01,
               juce::String (sr / 1000.0, 1) + " kHz: measured " + juce::String (best)
                   + " expected " + juce::String (expected, 1) + " samples, peak "
                   + juce::String (bestV, 4));
    }
}

// THD rising with Time is the ElectroSmash measurement the whole chip model is
// calibrated against: 0.13 % at 31 ms, 1 % at 342 ms, 3 % plus beyond.
void thd()
{
    std::printf ("thd: single stage, 400 Hz at 0.5 peak, harmonics 2..5 over the fundamental\n");
    struct Case { float t01; double lo, hi; const char* label; };
    const Case cases[] = { { 0.0244f, 0.0006, 0.0035, "31 ms (plan: 0.13 %)" },
                           { 0.5151f, 0.0040, 0.0220, "342 ms (plan: 1 %)" },
                           { 0.7344f, 0.0150, 0.2000, "1 s (plan: 3 % plus)" } };
    constexpr double sr = 48000.0;

    for (const auto& c : cases)
    {
        const auto out = renderStage (sr, 1.0, c.t01, sine (400.0, 0.5), 7u);
        const int start = (int) (0.5 * sr), n = (int) (0.45 * sr);
        const double h1 = goertzelAmp (out, start, n, 400.0, sr);
        double harm = 0.0;
        for (int k = 2; k <= 5; ++k)
        {
            const double h = goertzelAmp (out, start, n, 400.0 * k, sr);
            harm += h * h;
        }
        const double thdRatio = std::sqrt (harm) / juce::jmax (h1, 1.0e-9);
        check ("THD in range", thdRatio >= c.lo && thdRatio <= c.hi,
               juce::String (c.label) + ": " + juce::String (thdRatio * 100.0, 3) + " % (want "
                   + juce::String (c.lo * 100.0, 2) + " to " + juce::String (c.hi * 100.0, 2) + " %)");
    }
}

// The noise floor rises with Time because the clock falls: the plan's
// "-90 dBFS at short times, much worse when overclocked low".
void noise()
{
    std::printf ("noise: silent input, Decay 0, wet floor at five Time settings\n");
    constexpr double sr = 48000.0;
    const float times[] = { 0.0f, 0.25f, 0.5151f, 0.75f, 1.0f };
    double levels[5] = {};

    for (int i = 0; i < 5; ++i)
    {
        TimeFilterLoop::Params p;
        p.time01 = times[i];
        p.decay = 0.0f;
        p.filterHz = 18000.0f;
        p.resonance01 = 0.0f;
        const auto out = renderLoop (sr, 128, 1.5, kSilence, p, 7u);
        levels[i] = dbfs (rmsOf (out, (int) (0.5 * sr), (int) (1.0 * sr)));
        note ("floor", "Time01 " + juce::String (times[i], 4) + " ("
                           + juce::String (pt::delaySecondsForTime01 (times[i]) * 1000.0f, 1)
                           + " ms): " + juce::String (levels[i], 1) + " dBFS");
    }

    check ("short Time is quiet", levels[0] <= -82.0,
           juce::String (levels[0], 1) + " dBFS (want <= -82)");
    check ("342 ms floor in range", levels[2] >= -78.0 && levels[2] <= -50.0,
           juce::String (levels[2], 1) + " dBFS (want -78 to -50)");
    // The bracket used to start at -60 because the clock bleed was standing in
    // this measurement at -44 dBFS. That tone is gone (it was audible with no
    // input, which is not hiss), so what is left here is the hiss alone.
    check ("longest Time is hissy", levels[4] >= -66.0 && levels[4] <= -30.0,
           juce::String (levels[4], 1) + " dBFS (want -66 to -30)");

    bool monotonic = true;
    for (int i = 1; i < 5; ++i)
        if (levels[i] < levels[i - 1] + 2.0)
            monotonic = false;
    check ("floor rises at least 2 dB per step", monotonic,
           juce::String (levels[0], 1) + " -> " + juce::String (levels[4], 1) + " dBFS");
}

// Flat to about 1 kHz then rolling off, and collapsing further as the clock
// falls, with no extra parameter: the reconstruction filter tracks the clock.
void bandwidth()
{
    std::printf ("bandwidth: single stage, tone sweep, response relative to 500 Hz\n");
    constexpr double sr = 48000.0;
    const double freqs[] = { 500.0, 1000.0, 2000.0, 4000.0, 8000.0 };

    auto responseAt = [&] (float t01, double hz)
    {
        const auto out = renderStage (sr, 0.7, t01, sine (hz, 0.3), 7u);
        const int start = (int) (0.35 * sr), n = (int) (0.3 * sr);
        return dbfs (goertzelAmp (out, start, n, hz, sr) / 0.3);
    };

    double shortT[5], longT[5];
    for (int i = 0; i < 5; ++i)
    {
        shortT[i] = responseAt (0.0f, freqs[i]);
        longT[i] = responseAt (0.7344f, freqs[i]);
        note ("response", juce::String (freqs[i], 0) + " Hz: Time 0 " + juce::String (shortT[i], 1)
                              + " dB, Time 1 s " + juce::String (longT[i], 1) + " dB");
    }

    check ("flat to 1 kHz at short Time", shortT[1] - shortT[0] >= -1.5 && shortT[1] - shortT[0] <= 0.5,
           juce::String (shortT[1] - shortT[0], 2) + " dB from 500 Hz to 1 kHz");
    check ("rolled off by 4 kHz", shortT[3] - shortT[1] <= -4.0 && shortT[3] - shortT[1] >= -14.0,
           juce::String (shortT[3] - shortT[1], 1) + " dB from 1 k to 4 k (want -14 to -4)");
    check ("8 kHz well down", shortT[4] - shortT[1] <= -15.0,
           juce::String (shortT[4] - shortT[1], 1) + " dB from 1 k to 8 k");
    check ("long Time collapses bandwidth", longT[2] - longT[1] <= -4.0,
           juce::String (longT[2] - longT[1], 1) + " dB from 1 k to 2 k at 1 s (Time 0: "
               + juce::String (shortT[2] - shortT[1], 1) + " dB)");
}

// Tell 1: sweeping Time repitches what is already in the buffer. A delay that
// crossfaded between read taps would show no pitch shift at all.
void repitch()
{
    std::printf ("repitch: Time stepped 100 ms to 200 ms under a steady 450 Hz tone\n");
    constexpr double sr = 48000.0;
    const float tShort = time01ForSeconds (0.1), tLong = time01ForSeconds (0.2);

    TimeFilterLoop::Params p;
    p.time01 = tShort;
    p.decay = 0.0f;
    p.resonance01 = 0.0f;
    const double stepAt = 1.0;

    const auto out = renderLoop (sr, 128, 2.0, sine (450.0, 0.5), p, 7u,
                                 [tShort, tLong, stepAt] (double t, TimeFilterLoop::Params& pp, TimeFilterLoop&)
                                 { pp.time01 = t >= stepAt ? tLong : tShort; });

    const int w1 = (int) ((stepAt + 0.025) * sr), n1 = (int) (0.04 * sr);
    const double a225 = goertzelAmp (out, w1, n1, 225.0, sr);
    const double a450 = goertzelAmp (out, w1, n1, 450.0, sr);
    check ("buffered content drops an octave", a225 / juce::jmax (a450, 1.0e-9) >= 6.0,
           "225/450 = " + juce::String (a225 / juce::jmax (a450, 1.0e-9), 2) + " just after the step");

    const int w2 = (int) ((stepAt + 0.25) * sr), n2 = (int) (0.15 * sr);
    const double b225 = goertzelAmp (out, w2, n2, 225.0, sr);
    const double b450 = goertzelAmp (out, w2, n2, 450.0, sr);
    check ("settles back to the input pitch", b450 / juce::jmax (b225, 1.0e-9) >= 6.0,
           "450/225 = " + juce::String (b450 / juce::jmax (b225, 1.0e-9), 2) + " 250 ms later");

    // Smear: a continuous ramp must bend the pitch continuously, never jump.
    const auto ramp = renderLoop (sr, 128, 2.0, sine (450.0, 0.5), p, 7u,
                                  [tShort, tLong] (double t, TimeFilterLoop::Params& pp, TimeFilterLoop&)
                                  {
                                      const double a = juce::jlimit (0.0, 1.0, (t - 1.0) / 0.3);
                                      pp.time01 = tShort + (float) a * (tLong - tShort);
                                  });
    double minF = 1.0e9, maxF = 0.0;
    bool inRange = true;
    for (double t = 1.02; t < 1.30; t += 0.02)
    {
        const double f = zeroCrossFreq (ramp, (int) (t * sr), (int) (0.02 * sr), sr);
        minF = juce::jmin (minF, f);
        maxF = juce::jmax (maxF, f);
        if (f < 180.0 || f > 480.0)
            inRange = false;
    }
    check ("pitch bends continuously during the sweep", inRange && minF <= 360.0,
           "min " + juce::String (minF, 0) + " Hz, max " + juce::String (maxF, 0) + " Hz");
}

// Three chips in series is the "multitap, maybe three-step repeat" Sound on
// Sound heard: an impulse must come back three times, not once.
void threestep()
{
    std::printf ("threestep: impulse response peaks at D/3, 2D/3 and D\n");
    constexpr double sr = 48000.0;
    const float t01 = time01ForSeconds (0.3);
    const double stageSec = 0.3 / pt::kStages;

    TimeFilterLoop::Params p;
    p.time01 = t01;
    p.decay = 0.0f;
    p.resonance01 = 0.0f;
    const auto out = renderLoop (sr, 128, 0.6, [] (double t) { return t < 1.0e-9 ? 0.7f : 0.0f; }, p, 7u);

    for (int k = 1; k <= pt::kStages; ++k)
    {
        const double centre = stageSec * k;
        const int from = (int) ((centre - 0.006) * sr), n = (int) (0.012 * sr);
        const double here = peakOf (out, juce::jmax (0, from), n);
        const int gapFrom = (int) ((centre - stageSec * 0.5) * sr);
        const double between = peakOf (out, juce::jmax (0, gapFrom), (int) (0.006 * sr));
        check ("tap present", here > 3.0 * juce::jmax (between, 1.0e-7),
               "tap " + juce::String (k) + " at " + juce::String (centre * 1000.0, 1) + " ms: peak "
                   + juce::String (dbfs (here), 1) + " dBFS, between-taps "
                   + juce::String (dbfs (between), 1) + " dBFS");
    }
}

// Tell 3: Decay past unity self-oscillates warmly and stays bounded. Nothing
// may reach full scale, pump, or go non-finite.
void runaway()
{
    std::printf ("runaway: Decay 1.15 for 6 s after a 100 ms burst\n");
    constexpr double sr = 48000.0;

    // checkLevel is off for the hostile case: the plan says Filter full CCW
    // passes almost nothing, so a loop closed through a 20 Hz lowpass is
    // SUPPOSED to be quiet. What matters there is that it stays bounded.
    auto run = [] (float res, float filterHz, bool withFm, bool checkLevel, const char* label)
    {
        TimeFilterLoop::Params p;
        p.time01 = 0.3f;
        p.decay = 1.15f;
        p.filterHz = filterHz;
        p.resonance01 = res;
        const auto out = renderLoop (sr, 128,
                                     6.0,
                                     [] (double t) { return t < 0.1 ? (float) (0.3 * std::sin (juce::MathConstants<double>::twoPi * 450.0 * t)) : 0.0f; },
                                     p, 7u, nullptr,
                                     withFm ? ModFn ([] (double t) { return (float) std::sin (juce::MathConstants<double>::twoPi * 173.0 * t); })
                                            : ModFn());

        const int a = (int) (4.0 * sr), b = (int) (5.0 * sr), oneSec = (int) sr;
        const double pk = peakOf (out, a, 2 * oneSec);
        const double r1 = rmsOf (out, a, oneSec), r2 = rmsOf (out, b, oneSec);
        const double crest = pk / juce::jmax (r2, 1.0e-9);

        check ("stays below full scale", pk <= 0.95 && allFinite (out),
               juce::String (label) + ": peak " + juce::String (pk, 4) + ", finite "
                   + (allFinite (out) ? "yes" : "NO"));
        if (checkLevel)
            check ("settles at a musical level", dbfs (r2) >= -20.0 && dbfs (r2) <= -3.0,
                   juce::String (label) + ": RMS " + juce::String (dbfs (r2), 1) + " dBFS (want -20 to -3)");
        else
            note ("level with the filter shut", juce::String (label) + ": RMS "
                                                    + juce::String (dbfs (r2), 1) + " dBFS");
        check ("steady, not growing", std::abs (dbfs (r2) - dbfs (r1)) <= 2.0,
               juce::String (label) + ": " + juce::String (dbfs (r1), 1) + " -> "
                   + juce::String (dbfs (r2), 1) + " dBFS");
        check ("crest factor sane", crest <= 4.5,
               juce::String (label) + ": crest " + juce::String (crest, 2));
    };

    run (0.3f, 2000.0f, false, true, "nominal");
    run (0.95f, 20.0f, true, false, "worst case");

    // "Mud or a sine" is a spectral claim, and until now the only number this
    // scenario produced was a level. Centroid says where the runaway sits; the
    // partial count says whether it is one mode or many.
    for (float filterHz : { 500.0f, 2000.0f, 8000.0f })
    {
        TimeFilterLoop::Params p;
        p.time01 = 0.3f;
        p.decay = pt::kDecayMax;
        p.filterHz = filterHz;
        p.resonance01 = 0.3f;
        const auto out = renderLoop (sr, 128, 8.0,
                                     [] (double t) { return t < 0.1 ? (float) (0.3 * std::sin (juce::MathConstants<double>::twoPi * 450.0 * t)) : 0.0f; },
                                     p, 7u);

        const int a = (int) (6.0 * sr), n = (int) (2.0 * sr);
        double num = 0.0, den = 0.0, strongest = 0.0;
        std::vector<double> bins;
        for (double f = 25.0; f < 6000.0; f *= 1.02) // 1/35th octave, cheap and dense enough
        {
            const double amp = goertzelAmp (out, a, n, f, sr);
            bins.push_back (amp);
            num += f * amp;
            den += amp;
            strongest = juce::jmax (strongest, amp);
        }
        int loud = 0;
        for (double amp : bins)
            if (amp > strongest * 0.1) // within 20 dB of the strongest
                ++loud;

        note ("spectrum of the runaway",
              "Filter " + juce::String (filterHz, 0) + " Hz: centroid "
                  + juce::String (den > 0.0 ? num / den : 0.0, 0) + " Hz, "
                  + juce::String (loud) + " bins within 20 dB of the peak");
    }

    // Decay 1.0 is unity round the feedback path, but the chips and the
    // filters lose about 0.23 dB per iteration, so unity still decays: the
    // true infinity point sits near 1.03. What has to hold is that 1.0 decays
    // far more slowly than 0.95 and is still ringing after 7 s.
    double fall[2] = {};
    int idx = 0;
    for (auto d : { 0.95f, 1.0f })
    {
        TimeFilterLoop::Params p;
        p.time01 = 0.3f;
        p.decay = d;
        p.filterHz = 8000.0f;
        p.resonance01 = 0.1f;
        const auto out = renderLoop (sr, 128, 8.0,
                                     [] (double t) { return t < 0.1 ? (float) (0.3 * std::sin (juce::MathConstants<double>::twoPi * 450.0 * t)) : 0.0f; },
                                     p, 7u);
        const double early = dbfs (rmsOf (out, (int) (1.0 * sr), (int) sr));
        const double late = dbfs (rmsOf (out, (int) (7.0 * sr), (int) sr));
        fall[idx++] = early - late;
        if (d < 1.0f)
            check ("Decay 0.95 decays away", late < early - 20.0,
                   juce::String (early, 1) + " -> " + juce::String (late, 1) + " dBFS over 6 s");
        else
        {
            check ("Decay 1.0 decays far more slowly", fall[1] < 0.5 * fall[0],
                   juce::String (fall[1], 1) + " dB lost versus " + juce::String (fall[0], 1)
                       + " dB at Decay 0.95");
            check ("Decay 1.0 still ringing after 7 s", late > -60.0,
                   juce::String (late, 1) + " dBFS");
        }
    }
}

// The ticking and burbling the Strega exposes, and the thing it must not do:
// stand there as a tone when the delay is empty. Once the clock falls into the
// audio band a steady pulse train is not ticking, it is a pitch, and a pitch
// that Clear cannot remove reads as a broken plugin rather than as character.
void bleed()
{
    std::printf ("bleed: rides the loop's content, silent when the loop is empty\n");
    constexpr double sr = 48000.0;

    auto measure = [] (float t01, bool withSignal, double hz)
    {
        TimeFilterLoop::Params p;
        p.time01 = t01;
        p.decay = withSignal ? 0.7f : 0.0f;
        p.resonance01 = 0.0f;
        const auto out = renderLoop (sr, 128, 3.0,
                                     withSignal ? sine (450.0, 0.4) : kSilence, p, 7u);
        return dbfs (goertzelAmp (out, (int) (2.0 * sr), (int) sr, hz, sr));
    };

    // Time 1: the chip clock is 1500 Hz, so bleed lands at 1500 and its
    // subharmonic at 750.
    const double emptySub = measure (1.0f, false, 750.0);
    const double emptyTick = measure (1.0f, false, 1500.0);
    const double playingTick = measure (1.0f, true, 1500.0);
    const double quiet = measure (0.3f, false, 750.0);

    // THE REGRESSION. This is what a playthrough caught: at a 1 s delay the
    // fs/2 square sat at 2.75 kHz and -44 dBFS with nothing playing, and Clear
    // could not touch it because the clock makes it, not the buffer.
    check ("an empty loop makes no tone", emptySub <= -85.0 && emptyTick <= -85.0,
           juce::String (emptySub, 1) + " dBFS at 750 Hz, " + juce::String (emptyTick, 1)
               + " dBFS at 1500 Hz, with no input");
    check ("but the clock is still heard under the repeats", playingTick > emptyTick + 10.0,
           juce::String (playingTick, 1) + " dBFS at 1500 Hz while the loop is ringing, against "
               + juce::String (emptyTick, 1) + " dBFS empty");
    check ("silent above the bleed onset", quiet <= -85.0,
           juce::String (quiet, 1) + " dBFS at 750 Hz, Time01 0.3");
}

// Tell 5: audio-rate modulation of the clock gives metallic ring-mod-like
// sidebands. This is the hook Phase 3 drives from the mod bus.
void fm()
{
    std::printf ("fm: audio-rate Time modulation puts energy on a sideband grid\n");
    constexpr double sr = 48000.0;
    const double carrier = 990.0, fMod = 105.0;

    auto gridEnergy = [&] (float depth)
    {
        TimeFilterLoop::Params p;
        p.time01 = time01ForSeconds (0.1);
        p.decay = 0.0f;
        p.resonance01 = 0.0f;
        const auto out = renderLoop (sr, 128, 2.0, sine (carrier, 0.3), p, 7u, nullptr,
                                     [depth, fMod] (double t)
                                     { return depth * (float) std::sin (juce::MathConstants<double>::twoPi * fMod * t); });
        const int start = (int) (1.0 * sr), n = (int) (0.9 * sr);
        double grid = 0.0, half = 0.0;
        for (int k = 1; k <= 3; ++k)
        {
            for (double s : { -1.0, 1.0 })
            {
                const double g = goertzelAmp (out, start, n, carrier + s * k * fMod, sr);
                grid += g * g;
                const double h = goertzelAmp (out, start, n, carrier + s * (k - 0.5) * fMod, sr);
                half += h * h;
            }
        }
        const double car = goertzelAmp (out, start, n, carrier, sr);
        return std::array<double, 3> { grid, half, car * car };
    };

    const auto modded = gridEnergy (0.25f);
    const auto clean = gridEnergy (0.0f);

    check ("sidebands appear on the modulation grid", modded[0] > 10.0 * juce::jmax (modded[1], 1.0e-18),
           "grid/half = " + juce::String (modded[0] / juce::jmax (modded[1], 1.0e-18), 1));
    check ("sidebands are a large share of the signal", modded[0] > 0.05 * modded[2],
           "grid/carrier = " + juce::String (modded[0] / juce::jmax (modded[2], 1.0e-18), 3));
    check ("no sidebands without modulation", clean[0] < 0.01 * clean[2],
           "grid/carrier = " + juce::String (clean[0] / juce::jmax (clean[2], 1.0e-18), 5));
}

// Clear must empty the loop without clicking, even out of a runaway.
void clear()
{
    std::printf ("clear: flush a running loop, no click, silence afterwards\n");
    constexpr double sr = 48000.0;
    TimeFilterLoop::Params p;
    p.time01 = 0.3f;
    p.decay = 1.1f;
    p.filterHz = 4000.0f;
    p.resonance01 = 0.2f;

    bool fired = false;
    const auto out = renderLoop (sr, 128, 5.0,
                                 [] (double t) { return t < 0.2 ? (float) (0.3 * std::sin (juce::MathConstants<double>::twoPi * 450.0 * t)) : 0.0f; },
                                 p, 7u,
                                 [&fired] (double t, TimeFilterLoop::Params&, TimeFilterLoop& loop)
                                 {
                                     if (! fired && t >= 3.0)
                                     {
                                         fired = true;
                                         loop.requestClear();
                                     }
                                 });

    auto maxStep = [&out] (int start, int n)
    {
        double m = 0.0;
        for (int i = 1; i < n; ++i)
            m = juce::jmax (m, (double) std::abs (out[(size_t) (start + i)] - out[(size_t) (start + i - 1)]));
        return m;
    };

    const double before = maxStep ((int) (2.8 * sr), (int) (0.2 * sr));
    const double during = maxStep ((int) (2.99 * sr), (int) (0.04 * sr));
    const double after = dbfs (rmsOf (out, (int) (3.1 * sr), (int) (0.4 * sr)));

    check ("no click on Clear", during <= 1.5 * before,
           "largest step during " + juce::String (during, 5) + " versus "
               + juce::String (before, 5) + " before");
    check ("loop is empty afterwards", after <= -78.0,
           juce::String (after, 1) + " dBFS 100 ms later");
}

// A single poisoned sample from the host must not kill the loop forever.
void nan()
{
    std::printf ("nan: inject NaN, inf and 1e30 into the loop\n");
    constexpr double sr = 48000.0;
    TimeFilterLoop::Params p;
    p.time01 = 0.3f;
    p.decay = 0.8f;

    const auto out = renderLoop (sr, 128, 2.0,
                                 [] (double t)
                                 {
                                     const int i = (int) (t * 48000.0 + 0.5);
                                     if (i == 4800) return std::numeric_limits<float>::quiet_NaN();
                                     if (i == 9600) return std::numeric_limits<float>::infinity();
                                     if (i == 14400) return 1.0e30f;
                                     return (float) (0.3 * std::sin (juce::MathConstants<double>::twoPi * 450.0 * t));
                                 },
                                 p, 7u);

    check ("output stays finite", allFinite (out), "checked " + juce::String ((int) out.size()) + " samples");
    check ("still passing signal at the end", dbfs (rmsOf (out, (int) (1.5 * sr), (int) (0.4 * sr))) > -60.0,
           juce::String (dbfs (rmsOf (out, (int) (1.5 * sr), (int) (0.4 * sr))), 1) + " dBFS");
}

// Tell 4: the same settings never land in the same place twice, but the sound
// is the same sound. Seeded runs stay bit-identical so every other test can
// rely on repeatability.
void nonidentical()
{
    std::printf ("nonidentical: unseeded runs differ, seeded runs are bit-identical\n");
    constexpr double sr = 48000.0;
    TimeFilterLoop::Params p;
    p.time01 = 0.5f;
    p.decay = 0.9f;

    const auto a = renderLoop (sr, 128, 2.0, sine (450.0, 0.4), p, 0u);
    const auto b = renderLoop (sr, 128, 2.0, sine (450.0, 0.4), p, 0u);

    int differing = 0;
    double maxDiff = 0.0;
    std::vector<float> diff (a.size(), 0.0f);
    for (size_t i = 0; i < a.size(); ++i)
    {
        diff[i] = a[i] - b[i];
        if (std::abs (diff[i]) > 0.0f)
            ++differing;
        maxDiff = juce::jmax (maxDiff, (double) std::abs (diff[i]));
    }
    const double relative = rmsOf (diff, 0, (int) diff.size()) / juce::jmax (rmsOf (a, 0, (int) a.size()), 1.0e-9);

    check ("two runs are not identical", maxDiff > 1.0e-7 && differing > (int) a.size() / 2,
           juce::String (100.0 * differing / (double) a.size(), 1) + " % of samples differ, max "
               + juce::String (maxDiff, 6));
    check ("but it is the same sound", relative < 0.15,
           "relative difference " + juce::String (relative * 100.0, 2) + " %");

    const auto s1 = renderLoop (sr, 128, 0.5, sine (450.0, 0.4), p, 3u);
    const auto s2 = renderLoop (sr, 128, 0.5, sine (450.0, 0.4), p, 3u);
    check ("seeded runs repeat exactly", fnvHash (s1) == fnvHash (s2),
           "hash " + juce::String::toHexString ((int) fnvHash (s1)));
}

// Nothing in the engine may depend on how the host chops up time.
void blockmatrix()
{
    std::printf ("blockmatrix: identical output at every block size\n");
    constexpr double sr = 48000.0;
    TimeFilterLoop::Params p;
    p.time01 = 0.45f;
    p.decay = 0.85f;
    p.filterHz = 3000.0f;
    p.resonance01 = 0.4f;
    p.absorb01 = 0.3f;

    unsigned int reference = 0;
    double referenceRms = 0.0;
    for (int block : { 1, 17, 128, 512, 4096 })
    {
        const auto out = renderLoop (sr, block, 1.5, sine (450.0, 0.4), p, 5u);
        const unsigned int h = fnvHash (out);
        const double r = dbfs (rmsOf (out, 0, (int) out.size()));
        if (block == 1)
        {
            reference = h;
            referenceRms = r;
            note ("reference", "block 1: RMS " + juce::String (r, 3) + " dBFS");
        }
        else
        {
            check ("block size does not change the output", h == reference,
                   "block " + juce::String (block) + ": RMS " + juce::String (r, 3)
                       + " dBFS (reference " + juce::String (referenceRms, 3) + ")");
        }
    }
}

// The chip is sample-rate independent by construction (N words at fs_chip), so
// the delay time and the character must not move with the host rate.
void srmatrix()
{
    std::printf ("srmatrix: delay time and noise floor across sample rates\n");
    const float t01 = time01ForSeconds (0.3);
    double firstEcho = 0.0, firstFloor = 0.0;

    for (double sr : { 44100.0, 48000.0, 96000.0, 192000.0 })
    {
        TimeFilterLoop::Params p;
        p.time01 = t01;
        p.decay = 0.0f;
        p.resonance01 = 0.0f;

        const auto imp = renderLoop (sr, 128, 0.5, [] (double t) { return t < 1.0e-9 ? 0.7f : 0.0f; }, p, 7u);
        int best = 0;
        double bestV = 0.0;
        const int searchFrom = (int) (0.02 * sr);
        for (int i = searchFrom; i < (int) imp.size(); ++i)
            if (std::abs (imp[(size_t) i]) > bestV)
            {
                bestV = std::abs (imp[(size_t) i]);
                best = i;
            }
        const double echoMs = 1000.0 * best / sr;

        TimeFilterLoop::Params q;
        q.time01 = 1.0f;
        q.decay = 0.0f;
        q.resonance01 = 0.0f;
        const auto quiet = renderLoop (sr, 128, 1.2, kSilence, q, 7u);
        const double floorDb = dbfs (rmsOf (quiet, (int) (0.4 * sr), (int) (0.7 * sr)));

        if (firstEcho <= 0.0)
        {
            firstEcho = echoMs;
            firstFloor = floorDb;
            note ("reference", "44.1 kHz: first tap " + juce::String (echoMs, 2) + " ms, Time-1 floor "
                                   + juce::String (floorDb, 1) + " dBFS");
        }
        else
        {
            check ("delay time holds across rates", std::abs (echoMs - firstEcho) / firstEcho < 0.02,
                   juce::String (sr / 1000.0, 1) + " kHz: " + juce::String (echoMs, 2) + " ms versus "
                       + juce::String (firstEcho, 2));
            check ("noise floor holds across rates", std::abs (floorDb - firstFloor) <= 4.0,
                   juce::String (sr / 1000.0, 1) + " kHz: " + juce::String (floorDb, 1) + " dBFS versus "
                       + juce::String (firstFloor, 1));
        }
    }
}

void cpu()
{
    std::printf ("cpu: cost of the full engine at 48 kHz\n");
    constexpr double sr = 48000.0;
    constexpr int block = 128;
    const double seconds = 10.0;

    auto engine = std::make_unique<DybbukEngine>();
    engine->prepare (sr, block);
    engine->seedForTests (7);

    DybbukEngine::Params p;
    p.time01 = 0.5f;
    p.decay = 0.9f;
    p.filterHz = 4000.0f;
    p.resonance01 = 0.5f;
    p.absorb01 = 0.3f;
    p.blend01 = 0.5f;

    juce::AudioBuffer<float> buffer (2, block);
    double phase = 0.0;
    const int blocks = (int) (seconds * sr / block);

    const double start = juce::Time::getMillisecondCounterHiRes();
    for (int b = 0; b < blocks; ++b)
    {
        for (int i = 0; i < block; ++i)
        {
            const float v = 0.3f * (float) std::sin (phase);
            phase += 220.0 / sr * juce::MathConstants<double>::twoPi;
            buffer.setSample (0, i, v);
            buffer.setSample (1, i, v);
        }
        engine->process (buffer, p);
    }
    const double elapsed = (juce::Time::getMillisecondCounterHiRes() - start) / 1000.0;
    const double load = 100.0 * elapsed / seconds;

    check ("under 10 % of one core", load < 10.0,
           juce::String (load, 2) + " % of realtime (" + juce::String (elapsed * 1000.0, 1)
               + " ms for " + juce::String (seconds, 0) + " s of audio)");
}

// Not a pass/fail: renders files for the by-ear milestone in Phase 1.
// The Burst direction, as a sound: six damped plucks at uneven times go in
// while armed, the pattern plays them back on a 180 ms clock; two more while
// disarmed are ignored; two more after re-arming join the pattern. The dry
// plucks are mixed in low so what went in can be told from what came out.
void renderBurst (double sr)
{
    constexpr double seconds = 16.0;
    const int total = (int) (seconds * sr);
    std::vector<float> in ((size_t) total, 0.0f);

    struct Pluck { double t, f0; };
    const Pluck plucks[] = { { 0.5, 110.0 }, { 0.83, 146.83 }, { 1.4, 196.0 }, { 1.62, 164.81 },
                             { 2.3, 220.0 }, { 2.95, 130.81 },
                             { 8.0, 293.66 }, { 8.4, 246.94 },       // disarmed
                             { 11.0, 174.61 }, { 11.7, 261.63 } };   // re-armed
    for (const auto& pl : plucks)
    {
        const int start = (int) (pl.t * sr);
        for (int i = 0; i < (int) (0.6 * sr) && start + i < total; ++i)
        {
            const double t = i / sr;
            const double env = std::exp (-12.0 * t) * juce::jmin (1.0, t / 0.002);
            double v = 0.0;
            for (int h = 1; h <= 6; ++h)
                v += std::sin (juce::MathConstants<double>::twoPi * pl.f0 * h * t + 0.3 * h) / (h * h);
            in[(size_t) (start + i)] += (float) (0.5 * env * v);
        }
    }

    BurstEngine engine;
    engine.prepare (sr, 128);
    BurstEngine::Params p;
    p.thresholdDb = -30.0f;
    p.stepMs = 180.0f;
    p.maxSteps = 8;
    p.mix01 = 0.8f;

    juce::AudioBuffer<float> file (2, total);
    juce::AudioBuffer<float> buffer (2, 128);
    int pos = 0;
    while (pos < total)
    {
        const int len = juce::jmin (128, total - pos);
        const double t = pos / sr;
        p.record = t < 7.0 || t >= 10.0;
        for (int i = 0; i < len; ++i)
        {
            buffer.setSample (0, i, in[(size_t) (pos + i)]);
            buffer.setSample (1, i, in[(size_t) (pos + i)]);
        }
        juce::AudioBuffer<float> view (buffer.getArrayOfWritePointers(), 2, len);
        engine.process (view, p);
        for (int ch = 0; ch < 2; ++ch)
            file.copyFrom (ch, pos, buffer, ch, 0, len);
        pos += len;
    }

    const juce::File out = juce::File::getCurrentWorkingDirectory().getChildFile ("dybbuk_burst.wav");
    out.deleteFile();
    juce::WavAudioFormat wav;
    std::unique_ptr<juce::OutputStream> stream = out.createOutputStream();
    if (stream != nullptr)
    {
        const auto options = juce::AudioFormatWriterOptions().withSampleRate (sr).withNumChannels (2).withBitsPerSample (24);
        if (auto writer = wav.createWriterFor (stream, options))
            writer->writeFromAudioSampleBuffer (file, 0, total);
    }
    std::printf ("  wrote %s (peak %.3f, %d steps)\n", out.getFullPathName().toRawUTF8(),
                 file.getMagnitude (0, total), engine.uiStepCount.load());
}

void render()
{
    constexpr double sr = 48000.0;
    std::printf ("render: writing wav files to the working directory\n");

    struct Patch
    {
        const char* name;
        float time01, decay, filterHz, res, absorb, blend;
        float agitate, agitSpeed, timeMod;
        bool silent;    // no input at all: the self-playing texture
        bool sweepTime; // Time swept by hand across the run: the plan's tell 1
        float chaos = 0.0f, crust = 0.0f, colour = 0.0f;
        float tones = 0.0f, tonesPitch = 110.0f, fold = 0.0f;
    };
    const Patch patches[] = {
        { "dybbuk_short_clean", time01ForSeconds (0.08), 0.55f, 12000.0f, 0.15f, 0.0f, 0.5f, 0.0f, 0.35f, 0.0f, false, false },
        { "dybbuk_echoverb", time01ForSeconds (0.18), 0.82f, 2200.0f, 0.35f, 0.25f, 0.55f, 0.15f, 0.12f, 0.08f, false, false },
        { "dybbuk_wowflutter", time01ForSeconds (0.55), 0.7f, 1400.0f, 0.3f, 0.6f, 0.5f, 0.45f, 0.45f, 0.0f, false, false },
        { "dybbuk_batcave", time01ForSeconds (2.2), 0.9f, 800.0f, 0.45f, 0.4f, 0.7f, 0.7f, 6.5f, 0.35f, false, false },
        { "dybbuk_runaway", time01ForSeconds (0.25), 1.12f, 3000.0f, 0.5f, 0.2f, 0.8f, 0.3f, 0.5f, 0.2f, false, false },
        { "dybbuk_clang", time01ForSeconds (0.12), 0.8f, 6000.0f, 0.3f, 0.0f, 0.6f, 0.2f, 0.35f, 0.85f, false, false },
        // The Phase 3 milestone, as a sound: nothing is played into this one.
        { "dybbuk_generative", time01ForSeconds (0.9), 1.1f, 4000.0f, 0.4f, 0.0f, 1.0f, 0.8f, 0.2f, 0.6f, true, false },
        // Tell 1, the one the plan says to stop and tune on if it is wrong:
        // sweeping Time must smear the pitch of what is already in the loop
        // like tape, never crossfade between two clean delays.
        { "dybbuk_timesweep", time01ForSeconds (0.12), 0.72f, 9000.0f, 0.2f, 0.0f, 0.65f, 0.0f, 0.35f, 0.0f, false, true },
        // The wildness pass, as sounds. Each one is a control that did not
        // exist or could not be reached before.
        { "dybbuk_crust", time01ForSeconds (0.18), 0.7f, 3000.0f, 0.3f, 0.15f, 0.7f, 0.1f, 0.3f, 0.0f, false, false,
          0.0f, 0.85f, 0.0f, 0.0f, 110.0f, 0.0f },
        { "dybbuk_wow", time01ForSeconds (0.35), 0.6f, 2400.0f, 0.25f, 0.3f, 0.6f, 0.9f, 1.4f, 0.0f, false, false },
        { "dybbuk_clangfm", time01ForSeconds (0.1), 0.75f, 6000.0f, 0.3f, 0.0f, 0.7f, 0.0f, 0.35f, 0.85f, false, false,
          0.0f, 0.0f, 0.25f, 0.0f, 220.0f, 0.0f },
        { "dybbuk_scream", time01ForSeconds (0.25), 1.35f, 2500.0f, 0.85f, 0.1f, 0.9f, 0.3f, 0.5f, 0.15f, false, false,
          0.4f, 0.0f, 0.6f, 0.0f, 110.0f, 0.0f },
        // Chaos at a depth a player would actually use, for judging how much of
        // it should be crackle rather than wander.
        { "dybbuk_chaos", time01ForSeconds (0.4), 0.75f, 2200.0f, 0.35f, 0.2f, 0.75f, 0.15f, 0.3f, 0.0f, false, false,
          0.4f, 0.0f, 0.15f, 0.0f, 110.0f, 0.0f },
        // Nothing is played into this one at any point.
        { "dybbuk_possession", time01ForSeconds (0.6), 1.05f, 1400.0f, 0.6f, 0.15f, 1.0f, 0.2f, 0.2f, 0.0f, true, false,
          0.8f, 0.0f, 0.3f, 0.6f, 65.4f, 0.4f },
    };

    // A plucked-string stand-in: exponentially decaying detuned partials, so
    // the repeats have something with attack and harmonics to chew on.
    auto pluck = [] (double t, double f0)
    {
        const double env = std::exp (-3.5 * t);
        double v = 0.0;
        for (int h = 1; h <= 6; ++h)
            v += std::sin (juce::MathConstants<double>::twoPi * f0 * h * t + 0.3 * h) / (h * h);
        return 0.45 * env * v;
    };

    for (const auto& patch : patches)
    {
        auto engine = std::make_unique<DybbukEngine>();
        engine->prepare (sr, 128);

        DybbukEngine::Params p;
        p.time01 = patch.time01;
        p.decay = patch.decay;
        p.filterHz = patch.filterHz;
        p.resonance01 = patch.res;
        p.absorb01 = patch.absorb;
        p.blend01 = patch.blend;
        p.agitate01 = patch.agitate;
        p.agitSpeedHz = patch.agitSpeed;
        p.timeMod01 = patch.timeMod;
        p.chaos01 = patch.chaos;
        p.crust01 = patch.crust;
        p.colour01 = patch.colour;
        p.tonesLevel01 = patch.tones;
        p.tonesPitchHz = patch.tonesPitch;
        p.tonesFold01 = patch.fold;

        const double seconds = patch.silent ? 60.0 : 12.0;
        const int total = (int) (seconds * sr);
        juce::AudioBuffer<float> file (2, total);
        juce::AudioBuffer<float> buffer (2, 128);

        const double notes[] = { 110.0, 146.83, 196.0, 164.81 };
        int pos = 0;
        while (pos < total)
        {
            const int len = juce::jmin (128, total - pos);
            for (int i = 0; i < len; ++i)
            {
                const double t = (double) (pos + i) / sr;
                float v = 0.0f;
                if (! patch.silent && (patch.sweepTime ? t < 2.0 : t < 6.0))
                {
                    const int noteIndex = (int) (t / 1.5);
                    const double localT = t - noteIndex * 1.5;
                    v = (float) pluck (localT, notes[juce::jlimit (0, 3, noteIndex)]);
                }
                buffer.setSample (0, i, v);
                buffer.setSample (1, i, v);
            }
            if (patch.sweepTime)
            {
                // Down to 1.2 s and back, by hand, over the whole take.
                const double t = (double) pos / sr;
                const double phase = t / seconds;
                const double target = phase < 0.5 ? 0.12 + (1.2 - 0.12) * (phase * 2.0)
                                                  : 1.2 - (1.2 - 0.12) * ((phase - 0.5) * 2.0);
                p.time01 = time01ForSeconds (target);
            }

            engine->process (buffer, p);
            for (int ch = 0; ch < 2; ++ch)
                file.copyFrom (ch, pos, buffer, ch, 0, len);
            pos += len;
        }

        const juce::File out = juce::File::getCurrentWorkingDirectory()
                                   .getChildFile (juce::String (patch.name) + ".wav");
        out.deleteFile();
        juce::WavAudioFormat wav;
        std::unique_ptr<juce::OutputStream> stream = out.createOutputStream();
        if (stream != nullptr)
        {
            const auto options = juce::AudioFormatWriterOptions()
                                     .withSampleRate (sr)
                                     .withNumChannels (2)
                                     .withBitsPerSample (24);
            if (auto writer = wav.createWriterFor (stream, options))
                writer->writeFromAudioSampleBuffer (file, 0, total);
        }
        std::printf ("  wrote %s (peak %.3f)\n", out.getFullPathName().toRawUTF8(),
                     file.getMagnitude (0, total));
    }

    renderBurst (sr);
}


// The agitation generator: period, shape, and the anti-aliasing that stops a
// fast generator turning into noise on the filter.
void agitation()
{
    std::printf ("agitation: period and shape of the function generator\n");
    constexpr double sr = 48000.0;

    for (double hz : { 0.5, 4.0, 40.0 })
    {
        Agitation gen;
        gen.prepare (sr);
        gen.setSpeedHz ((float) hz);
        gen.setMode (Agitation::Mode::loop);

        const int n = (int) (sr * juce::jmax (4.0, 6.0 / hz));
        std::vector<float> out ((size_t) n, 0.0f);
        for (int i = 0; i < n; ++i)
            out[(size_t) i] = gen.processSample (false);

        // Count rising zero crossings of (value - 0.5) to get the period.
        int cycles = 0;
        int firstCross = -1, lastCross = -1;
        for (int i = 1; i < n; ++i)
            if (out[(size_t) (i - 1)] < 0.5f && out[(size_t) i] >= 0.5f)
            {
                ++cycles;
                if (firstCross < 0)
                    firstCross = i;
                lastCross = i;
            }

        const double measured = cycles > 1 ? (double) (lastCross - firstCross) / (cycles - 1) / sr : 0.0;
        const double wanted = 1.0 / hz;
        double lo = 1.0, hi = 0.0;
        for (float v : out)
        {
            lo = juce::jmin (lo, (double) v);
            hi = juce::jmax (hi, (double) v);
        }

        check ("period matches the speed", std::abs (measured - wanted) / wanted < 0.02,
               juce::String (hz, 2) + " Hz: " + juce::String (measured * 1000.0, 2) + " ms measured, "
                   + juce::String (wanted * 1000.0, 2) + " ms wanted");
        check ("spans the full range", lo < 0.05 && hi > 0.95,
               juce::String (hz, 2) + " Hz: " + juce::String (lo, 3) + " to " + juce::String (hi, 3));
    }

    // Gate mode fires one cycle per onset and rests at zero between them.
    {
        Agitation gen;
        InputFollower env;
        gen.prepare (sr);
        env.prepare (sr);
        gen.setSpeedHz (2.0f);
        gen.setMode (Agitation::Mode::gate);

        int onsets = 0;
        double restSum = 0.0;
        int restCount = 0;
        const int n = (int) (sr * 4.0);
        for (int i = 0; i < n; ++i)
        {
            const double t = (double) i / sr;
            // A pluck every second: 30 ms of tone, then silence.
            const double local = std::fmod (t, 1.0);
            const float x = local < 0.03 ? (float) (0.5 * std::sin (juce::MathConstants<double>::twoPi * 220.0 * t)) : 0.0f;
            const bool onset = env.processSample (x);
            if (onset)
                ++onsets;
            const float y = gen.processSample (onset);
            if (local > 0.8) // well after the one-shot has finished
            {
                restSum += (double) y;
                ++restCount;
            }
        }

        check ("gate fires once per note", onsets == 4,
               juce::String (onsets) + " onsets from 4 notes");
        check ("gate rests at zero between notes", restSum / juce::jmax (1, restCount) < 0.02,
               "mean " + juce::String (restSum / juce::jmax (1, restCount), 4) + " while resting");
    }
}

// The envelope follower: the plan's 5 ms attack and 100 ms release.
void follower()
{
    std::printf ("follower: attack and release times on the input\n");
    constexpr double sr = 48000.0;

    InputFollower env;
    env.prepare (sr);

    // A step of amplitude 0.5 held, then removed.
    int attackSamples = -1, releaseSamples = -1;
    const int n = (int) (sr * 1.0);
    for (int i = 0; i < n; ++i)
    {
        const double t = (double) i / sr;
        const float x = t < 0.5 ? 0.5f : 0.0f;
        env.processSample (x);
        if (attackSamples < 0 && env.getEnv() > 0.5f * 0.632f)
            attackSamples = i;
        if (t >= 0.5 && releaseSamples < 0 && env.getEnv() < 0.5f * 0.368f)
            releaseSamples = i - (int) (0.5 * sr);
    }

    const double attackMs = 1000.0 * attackSamples / sr;
    const double releaseMs = 1000.0 * releaseSamples / sr;
    check ("attack near 5 ms", attackMs > 2.0 && attackMs < 9.0,
           juce::String (attackMs, 2) + " ms to 63 %");
    check ("release near 100 ms", releaseMs > 60.0 && releaseMs < 160.0,
           juce::String (releaseMs, 1) + " ms to 37 %");
}

// Interference must be silent when the loop is silent and wild when it is hot.
// A source that wanders on its own would detune the delay at rest.
void interference()
{
    std::printf ("interference: chaos tracks the loop's own energy\n");
    constexpr double sr = 48000.0;

    auto runAt = [] (float loopEnv)
    {
        Interference intf;
        intf.prepare (sr);
        intf.seed (11u);

        double sum = 0.0;
        int count = 0;
        const int ticks = (int) (sr / modk::kControlBlock * 8.0); // 8 seconds
        for (int t = 0; t < ticks; ++t)
        {
            intf.tick (loopEnv, 0.0f);
            for (int i = 0; i < modk::kControlBlock; ++i)
            {
                const float v = intf.nextSample();
                if (t > ticks / 2) // after the energy follower has settled
                {
                    sum += (double) v * v;
                    ++count;
                }
            }
        }
        return std::sqrt (sum / juce::jmax (1, count));
    };

    const double quiet = runAt (0.0f);
    const double warm = runAt (0.05f);   // about -26 dBFS
    const double hot = runAt (0.5f);     // about -6 dBFS

    check ("silent loop produces no modulation", quiet < 0.01,
           "RMS " + juce::String (quiet, 5) + " at rest");
    check ("wakes up with the loop", warm > quiet * 3.0 && warm > 0.01,
           "RMS " + juce::String (warm, 4) + " when warm");
    check ("wilder when the loop is hot", hot > warm,
           "RMS " + juce::String (hot, 4) + " when hot versus " + juce::String (warm, 4) + " warm");

    // What the source is MADE of, which is the question when someone says it
    // sounds noisy. The wander is the slow chaotic bend; the crackle is a
    // sparse tick train on top whose density grows with the square of the
    // energy. Only the crackle is fast enough to read as noise, and only the
    // Time destination receives it -- the control-rate destinations take
    // wander() alone -- so any grit the chaos adds arrives as jitter on the
    // delay clock.
    for (float loopEnv : { 0.05f, 0.2f, 0.5f })
    {
        Interference intf;
        intf.prepare (sr);
        intf.seed (11u);

        double wanderSq = 0.0, totalSq = 0.0, crackleSq = 0.0;
        int count = 0;
        const int ticks = (int) (sr / modk::kControlBlock * 8.0);
        for (int t = 0; t < ticks; ++t)
        {
            intf.tick (loopEnv, 0.0f);
            for (int i = 0; i < modk::kControlBlock; ++i)
            {
                const float total = intf.nextSample();
                const float w = intf.wander();
                if (t > ticks / 2)
                {
                    wanderSq += (double) w * w;
                    totalSq += (double) total * total;
                    const double c = (double) total - (double) w;
                    crackleSq += c * c;
                    ++count;
                }
            }
        }
        const double n = juce::jmax (1, count);
        note ("what the chaos is made of",
              "loopEnv " + juce::String (loopEnv, 2) + ": wander RMS "
                  + juce::String (std::sqrt (wanderSq / n), 4) + ", crackle RMS "
                  + juce::String (std::sqrt (crackleSq / n), 4) + ", crackle is "
                  + juce::String (100.0 * std::sqrt (crackleSq / n)
                                      / juce::jmax (1.0e-9, std::sqrt (totalSq / n)), 1)
                  + " % of the total");
    }
}

// Tell 4, at the engine level: the always-on drift means two runs of the same
// settings never land in the same place, but the pitch stays within cents.
void drift()
{
    std::printf ("drift: always-on wow, tiny but never absent\n");
    constexpr double sr = 48000.0;

    DybbukEngine::Params p;
    p.time01 = 0.5f;
    p.decay = 0.7f;
    p.blend01 = 1.0f;
    p.filterHz = 8000.0f;

    const auto a = renderEngine (sr, 128, 4.0, sine (300.0, 0.35), p, 0u);
    const auto b = renderEngine (sr, 128, 4.0, sine (300.0, 0.35), p, 0u);

    std::vector<float> diff (a.size(), 0.0f);
    for (size_t i = 0; i < a.size(); ++i)
        diff[i] = a[i] - b[i];

    const double relative = rmsOf (diff, 0, (int) diff.size())
                            / juce::jmax (rmsOf (a, 0, (int) a.size()), 1.0e-9);

    // Pitch of the wet tail must still be the input pitch to within a few cents.
    const int from = (int) (2.0 * sr), n = (int) (1.5 * sr);
    const double f = zeroCrossFreq (a, from, n, sr);
    const double cents = 1200.0 * std::log2 (f / 300.0);

    check ("two runs differ", relative > 1.0e-4,
           "relative difference " + juce::String (relative * 100.0, 3) + " %");
    check ("but the pitch is unmoved", std::abs (cents) < 60.0,
           juce::String (f, 1) + " Hz, " + juce::String (cents, 1) + " cents off 300 Hz");
}

// THE MILESTONE. dybbuk-plan.md phase 3: with no input, Interference plus a
// high Decay must produce an evolving self-playing texture that never exactly
// repeats. This is the pass/fail test for the whole concept.
//
// It is measured in two halves, because the two claims need different
// conditions. Non-repetition is tested with Agitate at 0, where the only
// things moving are Interference into Time, the drift trim and the chip's own
// noise: with the agitation running, its cycle is a deliberate periodic
// driver and would show up as exactly the correlation this looks for.
// Evolution is tested with Agitate up, which is what makes the texture go
// somewhere over minutes.
void generative()
{
    std::printf ("generative: no input, self-playing, evolving, never repeating\n");
    constexpr double sr = 48000.0;
    const double seconds = 60.0;

    DybbukEngine::Params base;
    base.time01 = 0.55f;
    base.decay = 1.1f;
    base.filterHz = 4000.0f;
    base.resonance01 = 0.4f;
    // Absorb 0. This used to be the one setting the milestone could not share
    // with the defaults, because Absorb's 4 dB per iteration against a 1.2 dB
    // budget damped self-oscillation completely at a fifth of the knob. That is
    // fixed (see `probe`, which now self-oscillates at every Absorb setting),
    // so this is no longer a workaround -- it is just the cleanest condition in
    // which to measure whether the loop plays itself.
    base.absorb01 = 0.0f;
    base.blend01 = 1.0f;

    // --- half one: chaos alone, no periodic driver anywhere ------------------
    //
    // Driven by the CHAOS control, which is the point of the control existing.
    // Until it did, this scenario got its chaos through a bug: Time Mod's depth
    // was added into the Interference column and then multiplied by the chaos
    // sample, so `timeMod01 = 0.6` here was secretly opening a chaotic FM path
    // that the knob's label said nothing about. With that removed, leaving this
    // on Time Mod would quietly turn the plugin's own pass/fail milestone into
    // a test of a modulator that had just been switched off.
    DybbukEngine::Params chaos = base;
    chaos.agitate01 = 0.0f;
    chaos.chaos01 = 0.6f;
    chaos.timeMod01 = 0.0f; // no periodic modulator either: chaos alone

    std::vector<float> energy;
    const auto out = renderEngine (sr, 128, seconds, kSilence, chaos, 7u, &energy, 50.0);

    const double level = dbfs (rmsOf (out, (int) (20.0 * sr), (int) (35.0 * sr)));
    check ("self-oscillates from nothing", level > -45.0,
           juce::String (level, 1) + " dBFS over 20 to 55 s, with no input at any point");
    check ("stays bounded", peakOf (out, 0, (int) out.size()) < 0.98 && allFinite (out),
           "peak " + juce::String (peakOf (out, 0, (int) out.size()), 4));

    // What "never repeats" actually means: the correlation has to fall away
    // with lag and stay down. A slowly wandering envelope correlates strongly
    // at short lags whether or not it repeats, so a flat threshold across all
    // lags measures smoothness, not repetition. A recurring texture instead
    // shows correlation dropping and then climbing back at its period.
    juce::String curve;
    for (int lagSec : { 2, 5, 10, 20, 30, 40 })
        curve += juce::String (lagSec) + "s=" + juce::String (autocorrelation (energy, lagSec * 50), 2) + " ";

    double longLagWorst = 0.0;
    int longLagAt = 0;
    double runningMin = 1.0, biggestRebound = 0.0;
    int reboundAt = 0;
    for (int lagSec = 2; lagSec <= 40; ++lagSec)
    {
        const double c = std::abs (autocorrelation (energy, lagSec * 50));
        if (lagSec >= 10 && c > longLagWorst)
        {
            longLagWorst = c;
            longLagAt = lagSec;
        }
        if (c - runningMin > biggestRebound)
        {
            biggestRebound = c - runningMin;
            reboundAt = lagSec;
        }
        runningMin = juce::jmin (runningMin, c);
    }

    check ("correlation falls away with lag", longLagWorst < 0.6,
           "worst beyond 10 s is " + juce::String (longLagWorst, 3) + " at "
               + juce::String (longLagAt) + " s   [" + curve.trim() + "]");
    check ("no recurring pattern", biggestRebound < 0.3,
           "largest climb back is " + juce::String (biggestRebound, 3) + " at "
               + juce::String (reboundAt) + " s (a loop would climb to near 1.0)");

    // Chaotic, not merely noisy: one extra sample of input at the very start
    // sends it somewhere completely different a minute later.
    const auto nudged = renderEngine (sr, 128, seconds,
                                      [] (double t) { return t < 1.0e-5 ? 1.0e-5f : 0.0f; }, chaos, 7u);
    std::vector<float> diff (out.size(), 0.0f);
    for (size_t i = 0; i < out.size(); ++i)
        diff[i] = out[i] - nudged[i];
    const int tail = (int) (50.0 * sr);
    const double divergence = rmsOf (diff, tail, (int) (9.0 * sr))
                              / juce::jmax (rmsOf (out, tail, (int) (9.0 * sr)), 1.0e-9);
    check ("chaotic, not merely noisy", divergence > 0.3,
           "a 1e-5 nudge at t=0 changes the output at 50 s by "
               + juce::String (divergence * 100.0, 1) + " %");

    // ... but still reproducible when seeded, or no other test could rely on it.
    const auto repeat = renderEngine (sr, 128, 5.0, kSilence, chaos, 7u);
    const auto repeat2 = renderEngine (sr, 128, 5.0, kSilence, chaos, 7u);
    check ("reproducible when seeded", fnvHash (repeat) == fnvHash (repeat2),
           "hash " + juce::String::toHexString ((int) fnvHash (repeat)));

    // --- half two: with the agitation running, it has to go somewhere --------
    DybbukEngine::Params agitated = base;
    agitated.agitate01 = 0.8f;
    agitated.agitSpeedHz = 0.2f;

    std::vector<float> agitEnergy;
    const auto agitOut = renderEngine (sr, 128, seconds, kSilence, agitated, 7u, &agitEnergy, 50.0);

    const int windowSamples = 5 * 50;
    std::vector<double> windowMeans;
    for (size_t start = 0; start + (size_t) windowSamples <= agitEnergy.size(); start += (size_t) windowSamples)
    {
        double sum = 0.0;
        for (int i = 0; i < windowSamples; ++i)
            sum += agitEnergy[start + (size_t) i];
        windowMeans.push_back (sum / windowSamples);
    }
    double spread = 0.0;
    for (double a : windowMeans)
        for (double b : windowMeans)
            spread = juce::jmax (spread, std::abs (a - b));

    check ("evolves rather than droning", spread > 0.08,
           juce::String ((int) windowMeans.size()) + " five-second windows, energy spread "
               + juce::String (spread, 3));
    check ("agitated texture stays bounded",
           peakOf (agitOut, 0, (int) agitOut.size()) < 0.98 && allFinite (agitOut),
           "peak " + juce::String (peakOf (agitOut, 0, (int) agitOut.size()), 4));
}


// Phase 6: thirty minutes of audio with everything moving, which at 0.18 % of
// realtime costs about three seconds. Looks for the failures that only appear
// over time: a drifting DC offset, a slowly growing loop, a denormal stall, a
// state that goes non-finite once an hour.
void soak()
{
    std::printf ("soak: 30 minutes with every parameter sweeping\n");
    constexpr double sr = 48000.0;
    constexpr int block = 128;
    const double minutes = 30.0;

    juce::ScopedNoDenormals noDenormals;

    auto engine = std::make_unique<DybbukEngine>();
    engine->prepare (sr, block);

    juce::AudioBuffer<float> buffer (2, block);
    double phase = 0.0;

    const int blocks = (int) (minutes * 60.0 * sr / block);
    double sumEarly = 0.0, sumLate = 0.0, dcSum = 0.0;
    int countEarly = 0, countLate = 0, countDc = 0;
    float peak = 0.0f, monoPeak = 0.0f;
    // How OFTEN it goes over, not just how far: the question a limiter would
    // answer is whether there is anything to catch.
    juce::int64 samplesSeen = 0, samplesOver = 0;
    bool finite = true;
    int nonFiniteBlock = -1;

    const double started = juce::Time::getMillisecondCounterHiRes();

    for (int b = 0; b < blocks; ++b)
    {
        const double t = b * block / sr;

        DybbukEngine::Params p;
        // Everything sweeps on prime-ish periods so the combination never
        // settles into one repeating configuration.
        p.time01 = 0.5f + 0.45f * (float) std::sin (t * 0.031);
        p.decay = 0.9f + 0.25f * (float) std::sin (t * 0.017);
        p.filterHz = 200.0f * std::pow (60.0f, 0.5f + 0.5f * (float) std::sin (t * 0.023));
        p.resonance01 = 0.5f + 0.45f * (float) std::sin (t * 0.041);
        p.absorb01 = 0.4f + 0.4f * (float) std::sin (t * 0.013);
        p.blend01 = 0.7f;
        p.strengthDb = 10.0f + 10.0f * (float) std::sin (t * 0.029);
        p.agitate01 = 0.6f + 0.4f * (float) std::sin (t * 0.011);
        p.agitSpeedHz = 0.2f * std::pow (200.0f, 0.5f + 0.5f * (float) std::sin (t * 0.037));
        p.timeMod01 = 0.5f + 0.5f * (float) std::sin (t * 0.019);
        // Constant, not stepped: switching character on halfway through would
        // show up as a level change and look like the loop creeping.
        p.tonesLevel01 = 0.3f;
        p.spread01 = 0.6f;

        // The wildness pass's parameters sweep here too. Without them this
        // scenario's headroom figure -- which is the number the whole gain
        // staging is signed off against -- would cover a plugin that no longer
        // exists. Chaos in particular closes a feedback path (loop energy ->
        // chaos -> the drone's pitch and level -> the loop), and that path has
        // to be watched for thirty minutes, not three.
        p.chaos01 = 0.5f + 0.5f * (float) std::sin (t * 0.007);
        p.crust01 = 0.5f + 0.5f * (float) std::sin (t * 0.043);
        p.colour01 = 0.5f + 0.5f * (float) std::sin (t * 0.0053);
        p.tonesFold01 = 0.5f + 0.5f * (float) std::sin (t * 0.0071);
        p.tonesPitchHz = 55.0f * std::pow (16.0f, 0.5f + 0.5f * (float) std::sin (t * 0.0091));
        p.bypass = std::fmod (t, 300.0) > 290.0; // in and out of circuit every five minutes

        for (int i = 0; i < block; ++i)
        {
            const float v = std::fmod (t, 7.0) < 3.0 ? 0.35f * (float) std::sin (phase) : 0.0f;
            phase += 220.0 / sr * juce::MathConstants<double>::twoPi;
            buffer.setSample (0, i, v);
            buffer.setSample (1, i, v);
        }

        engine->process (buffer, p);

        for (int i = 0; i < block; ++i)
        {
            const float y = buffer.getSample (0, i);
            const float mono = 0.5f * (y + buffer.getSample (1, i));
            if (! std::isfinite (y) || ! std::isfinite (mono))
            {
                if (finite)
                    nonFiniteBlock = b;
                finite = false;
            }
            else
            {
                peak = juce::jmax (peak, std::abs (y));
                monoPeak = juce::jmax (monoPeak, std::abs (mono));
                ++samplesSeen;
                if (std::abs (y) > 1.0f)
                    ++samplesOver;
                dcSum += (double) y;
                ++countDc;
                // Five minute windows, so the comparison averages over many
                // cycles of every sweep rather than catching two phases.
                if (t > 60.0 && t < 360.0) { sumEarly += (double) y * y; ++countEarly; }
                if (t > minutes * 60.0 - 300.0) { sumLate += (double) y * y; ++countLate; }
            }
        }

        if (b % 40 == 0) // a Clear every ~100 ms of audio time, mid-runaway
            if (std::fmod (t, 137.0) < 0.2)
                engine->requestClear();
    }

    const double elapsed = (juce::Time::getMillisecondCounterHiRes() - started) / 1000.0;
    const double earlyDb = dbfs (std::sqrt (sumEarly / juce::jmax (1, countEarly)));
    const double lateDb = dbfs (std::sqrt (sumLate / juce::jmax (1, countLate)));
    const double dcOffset = dcSum / juce::jmax (1, countDc);

    check ("stays finite for 30 minutes", finite,
           finite ? "no non-finite samples" : "first at block " + juce::String (nonFiniteBlock));
    // The headroom contract: the centre (what a mono listener hears, and what
    // Out is calibrated against) stays inside full scale even with Decay in
    // the runaway zone and Strength at +20 dB. Spread's side component sits on
    // top of that by design, the way any mid-side widener does, which is why
    // it is measured separately rather than folded into the same limit.
    check ("the mono sum stays inside full scale", monoPeak < 1.0f,
           "mono peak " + juce::String (monoPeak, 4) + ", stereo peak with Spread at 60 % "
               + juce::String (peak, 4));
    note ("samples over full scale",
          juce::String (samplesOver) + " of " + juce::String (samplesSeen) + " ("
              + juce::String (100.0 * (double) samplesOver / (double) juce::jmax ((juce::int64) 1, samplesSeen), 6)
              + " %), all of them on the stereo sides");
    check ("level does not creep", std::abs (lateDb - earlyDb) < 3.0,
           juce::String (earlyDb, 1) + " dBFS over minutes 1 to 6, " + juce::String (lateDb, 1)
               + " over the last five");
    check ("no DC offset accumulates", std::abs (dcOffset) < 0.002,
           "mean sample " + juce::String (dcOffset, 6));
    note ("cost", juce::String (elapsed, 1) + " s of compute for " + juce::String (minutes, 0)
                      + " minutes of audio (" + juce::String (100.0 * elapsed / (minutes * 60.0), 3)
                      + " % of realtime)");
}

// Diagnostic, not a gate: where does the loop cross unity and start to sing?
// Every control has to EARN its place on the plate: turning it must change
// something a player would notice and reach for again. These four scenarios are
// that gate. They do not check that a feature exists -- the failure mode of the
// first playthrough was that every test checked existence and none checked
// whether the behaviour was wanted -- they measure how far the sound actually
// moves across each knob's travel.

// Crust: the chip's destruction, decoupled from the delay time. The claim is
// that at a 57 ms slapback, where the clock is nearly clean, Crust alone can
// deliver the voice that used to require a 3.7 second delay. The other half of
// the claim is orthogonality: at Crust 0 nothing may have moved at all.
void crust()
{
    std::printf ("crust: destruction at a short delay, and silence at zero\n");
    constexpr double sr = 48000.0;
    const float shortTime = 0.15f; // about 57 ms: a slapback, and nearly in spec

    double cleanThd = 0.0, cleanFloor = 0.0, cleanBand = 0.0;
    int row = 0;

    for (float c : { 0.0f, 0.5f, 1.0f })
    {
        // THD and bandwidth on a tone, floor on silence, all through the loop
        // with no feedback so what is measured is one pass of the chip.
        TimeFilterLoop::Params p;
        p.time01 = shortTime;
        p.crust01 = c;
        p.decay = 0.0f;
        p.filterHz = 18000.0f;
        p.resonance01 = 0.0f;

        // A 100 Hz fundamental, not 400. At full Crust the reconstruction
        // filter closes to about 500 Hz, so the harmonics of a 400 Hz tone are
        // removed before they can be measured and THD reads LOWER the more
        // destroyed the chip is. That is the band collapse doing its job, not
        // an absence of distortion -- but it means the tone has to be low
        // enough that its harmonics survive the band it is being measured in.
        const auto tone = renderLoop (sr, 128, 1.0, sine (100.0, 0.5), p, 7u);
        const int a = (int) (0.4 * sr), n = (int) (0.5 * sr);
        const double h1 = goertzelAmp (tone, a, n, 100.0, sr);
        double harm = 0.0;
        for (int h = 2; h <= 5; ++h)
        {
            const double amp = goertzelAmp (tone, a, n, 100.0 * h, sr);
            harm += amp * amp;
        }
        const double thd = 100.0 * std::sqrt (harm) / juce::jmax (h1, 1.0e-9);

        const auto quiet = renderLoop (sr, 128, 1.0, kSilence, p, 7u);
        const double floorDb = dbfs (rmsOf (quiet, a, n));

        // Bandwidth: 1 kHz against 200 Hz, which is where the reconstruction
        // filter closing four octaves shows up.
        const auto lo = renderLoop (sr, 128, 1.0, sine (200.0, 0.3), p, 7u);
        const auto hi = renderLoop (sr, 128, 1.0, sine (2000.0, 0.3), p, 7u);
        const double band = dbfs (goertzelAmp (hi, a, n, 2000.0, sr))
                            - dbfs (goertzelAmp (lo, a, n, 200.0, sr));

        note ("at a 57 ms delay",
              "Crust " + juce::String (juce::roundToInt (c * 100.0f)) + " %: THD "
                  + juce::String (thd, 2) + " %, floor " + juce::String (floorDb, 1)
                  + " dBFS, 2 kHz " + juce::String (band, 1) + " dB against 200 Hz");

        if (row == 0)
        {
            cleanThd = thd;
            cleanFloor = floorDb;
            cleanBand = band;
        }
        else if (row == 2)
        {
            // The audibility gate. None of these is a subtle difference: a
            // player has to hear the delay fall apart, not squint at it.
            check ("Crust adds real distortion", thd > cleanThd * 3.0,
                   "THD " + juce::String (cleanThd, 2) + " % -> " + juce::String (thd, 2) + " %");
            check ("Crust raises the noise floor audibly", floorDb > cleanFloor + 15.0,
                   juce::String (cleanFloor, 1) + " -> " + juce::String (floorDb, 1) + " dBFS");
            check ("Crust collapses the bandwidth", band < cleanBand - 12.0,
                   "2 kHz falls " + juce::String (cleanBand - band, 1) + " dB relative to 200 Hz");
        }
        ++row;
    }

    // Orthogonality: Crust 0 must be bit-identical to a build that has never
    // heard of Crust, or `thd` and `noise` stop being a calibration and start
    // being a coincidence.
    TimeFilterLoop::Params p;
    p.time01 = 0.5f;
    p.decay = 0.6f;
    p.filterHz = 8000.0f;
    p.crust01 = 0.0f;
    const auto a0 = renderLoop (sr, 128, 0.5, sine (300.0, 0.4), p, 11u);
    p.crust01 = 0.0f;
    const auto b0 = renderLoop (sr, 128, 0.5, sine (300.0, 0.4), p, 11u);
    check ("Crust at zero is exactly the old behaviour", fnvHash (a0) == fnvHash (b0),
           "hash " + juce::String::toHexString ((int) fnvHash (a0)));
}

// Time Mod's taper. Two claims: the bottom of the knob is where Roy left it
// after the first playthrough, and the top reaches the destroyed-pitch region
// that the 8x cut deleted rather than relocated.
void timemod()
{
    std::printf ("timemod: deviation across the taper, and what the top of it does\n");
    constexpr double sr = 48000.0;

    for (float t : { 0.13f, 0.30f, 0.50f, 0.70f, 1.00f })
        note ("depth", juce::String (juce::roundToInt (t * 100.0f)) + " %: "
                           + juce::String (modk::timeModOctaves (t), 4) + " oct, "
                           + juce::String (modk::timeModOctaves (t) * 12.0f, 2) + " semitones");

    // The promise that this is a taper and not a reversal of Roy's decision.
    check ("the bottom of the knob is where it was",
           std::abs (modk::timeModOctaves (0.13f) - 0.0325f) < 0.002f,
           "13 % gives " + juce::String (modk::timeModOctaves (0.13f), 4)
               + " oct against the old linear 0.0325");

    // Sideband energy against the carrier, driven through the real Tones path.
    double first = 0.0, last = 0.0;
    int i = 0;
    for (float t : { 0.13f, 1.00f })
    {
        DybbukEngine::Params p;
        p.time01 = 0.25f;
        p.decay = 0.0f;
        p.filterHz = 12000.0f;
        p.blend01 = 1.0f;
        p.timeMod01 = t;
        p.tonesPitchHz = 110.0f; // sub at 55 Hz: the sideband grid spacing
        p.agitate01 = 0.0f;
        p.chaos01 = 0.0f;

        const auto out = renderEngine (sr, 128, 2.0, sine (440.0, 0.4), p, 7u);
        const int a = (int) (1.0 * sr), n = (int) (0.9 * sr);
        const double carrier = goertzelAmp (out, a, n, 440.0, sr);
        double grid = 0.0;
        for (int k = 1; k <= 6; ++k) // the 55 Hz grid either side of the carrier
        {
            const double up = goertzelAmp (out, a, n, 440.0 + 55.0 * k, sr);
            const double dn = goertzelAmp (out, a, n, 440.0 - 55.0 * k, sr);
            grid += up * up + dn * dn;
        }
        const double ratio = std::sqrt (grid) / juce::jmax (carrier, 1.0e-9);
        note ("sideband grid over carrier",
              juce::String (juce::roundToInt (t * 100.0f)) + " %: " + juce::String (ratio, 3));
        if (i++ == 0)
            first = ratio;
        else
            last = ratio;
    }

    check ("the top of the knob is a different instrument", last > first * 8.0,
           "grid/carrier " + juce::String (first, 3) + " at 13 % against "
               + juce::String (last, 3) + " at 100 %");
}

// Agitation reaching the clock. Before this route the only periodic Time
// modulator was the Tones sub-harmonic, whose floor is 16.35 Hz, so tape wow,
// vibrato and chorus were impossible at every setting -- including in the
// preset named "Wow and Flutter", whose wobble was actually chaos.
void agitfm()
{
    std::printf ("agitfm: the function generator finally reaches the delay clock\n");
    constexpr double sr = 48000.0;

    // At a wow rate, the delay's pitch should swing measurably.
    for (double speed : { 0.8, 5.0 })
    {
        DybbukEngine::Params p;
        p.time01 = 0.35f;
        p.decay = 0.35f;
        p.filterHz = 12000.0f;
        p.blend01 = 1.0f;
        p.agitate01 = 1.0f;
        p.chaos01 = 0.0f;
        p.agitSpeedHz = (float) speed;

        const auto out = renderEngine (sr, 128, 6.0, sine (440.0, 0.4), p, 7u);

        // Track the wet's pitch in 40 ms windows over one full cycle and take
        // the spread: a static delay reads flat, a wobbling one does not.
        double lo = 1.0e9, hi = 0.0;
        const double from = 3.0, span = juce::jmin (2.0, 1.0 / speed);
        for (double t = from; t < from + span; t += 0.04)
        {
            const double f = zeroCrossFreq (out, (int) (t * sr), (int) (0.04 * sr), sr);
            if (f > 50.0) // ignore windows that fall in a gap
            {
                lo = juce::jmin (lo, f);
                hi = juce::jmax (hi, f);
            }
        }
        const double cents = 1200.0 * std::log2 (juce::jmax (hi, 1.0) / juce::jmax (lo, 1.0));
        check ("a rate knob bends the delay's pitch",
               cents > 100.0,
               juce::String (speed, 1) + " Hz: " + juce::String (lo, 0) + " to "
                   + juce::String (hi, 0) + " Hz, " + juce::String (cents, 0) + " cents");
    }

    // And at the top of the Speed knob, where nothing could follow the
    // generator before.
    //
    // Swept rather than measured at one rate, because this delay's FM has
    // nulls. The clock drives the write and the read together, so what
    // repitches buffered content is the DIFFERENCE between the modulator now
    // and one delay-time ago: when the delay is an exact whole number of
    // modulator periods that difference is zero and the modulation cancels
    // completely. At Time 0.25 the delay is about 93 ms, so the nulls are
    // 10.8 Hz apart and 300 Hz sits within a fifth of one of them. That is
    // real physics and worth knowing about; it is not what this scenario is
    // trying to measure, so it sweeps across several rates and reports the
    // strongest, which is what a player turning the knob would find.
    double best = 0.0;
    double bestAt = 0.0;
    for (double speed : { 180.0, 245.0, 275.0, 330.0, 420.0 })
    {
        DybbukEngine::Params p;
        p.time01 = 0.25f;
        p.decay = 0.0f;
        p.filterHz = 12000.0f;
        p.blend01 = 1.0f;
        p.agitate01 = 1.0f;
        p.chaos01 = 0.0f;
        p.agitSpeedHz = (float) speed;

        const auto out = renderEngine (sr, 128, 2.0, sine (440.0, 0.4), p, 7u);
        const int a = (int) (1.0 * sr), n = (int) (0.9 * sr);
        const double carrier = goertzelAmp (out, a, n, 440.0, sr);
        double grid = 0.0;
        for (int k = 1; k <= 3; ++k)
        {
            const double up = goertzelAmp (out, a, n, 440.0 + speed * k, sr);
            const double dn = goertzelAmp (out, a, n, 440.0 - speed * k, sr);
            grid += up * up + dn * dn;
        }
        const double ratio = std::sqrt (grid) / juce::jmax (carrier, 1.0e-9);
        note ("audio-rate grid", juce::String (speed, 0) + " Hz: " + juce::String (ratio, 3));
        if (ratio > best)
        {
            best = ratio;
            bestAt = speed;
        }
    }
    check ("and at audio rate it is a metallic grid, not a fade-out", best > 0.15,
           "best " + juce::String (best, 3) + " at " + juce::String (bestAt, 0) + " Hz");
}

// The drone as a voice. Two claims: Fold puts real high frequency into a loop
// that had no source of any, and it does not alias so badly at the top of the
// pitch range that the fold has to be limited.
void voice()
{
    std::printf ("voice: the oscillator's spectrum, and whether folding it is usable\n");
    constexpr double sr = 48000.0;

    double firstCentroid = 0.0, lastCentroid = 0.0;
    int i = 0;
    for (float fold : { 0.0f, 0.4f, 1.0f })
    {
        Tones t;
        t.prepare (sr);
        t.setPitch (110.0f);
        t.setShape (fold);

        const int n = (int) (0.5 * sr);
        std::vector<float> out ((size_t) n, 0.0f);
        for (int k = 0; k < n; ++k)
        {
            t.advance();
            out[(size_t) k] = t.main();
        }

        // Spectral centroid over the harmonic series, and how much sits above
        // 2 kHz, which is where the loop has nothing of its own.
        double num = 0.0, den = 0.0, above = 0.0, total = 0.0;
        for (int h = 1; h <= 60; ++h)
        {
            const double f = 110.0 * h;
            if (f > 0.45 * sr)
                break;
            const double amp = goertzelAmp (out, 0, n, f, sr);
            num += f * amp * amp;
            den += amp * amp;
            total += amp * amp;
            if (f > 2000.0)
                above += amp * amp;
        }
        const double centroid = den > 0.0 ? num / den : 0.0;
        note ("at 110 Hz", "Fold " + juce::String (juce::roundToInt (fold * 100.0f)) + " %: centroid "
                               + juce::String (centroid, 0) + " Hz, "
                               + juce::String (100.0 * above / juce::jmax (total, 1.0e-12), 1)
                               + " % of the energy above 2 kHz");
        if (i++ == 0)
            firstCentroid = centroid;
        lastCentroid = centroid;
    }

    check ("folding actually adds high frequency", lastCentroid > firstCentroid * 4.0,
           "centroid " + juce::String (firstCentroid, 0) + " Hz -> "
               + juce::String (lastCentroid, 0) + " Hz");

    // Fold 0 must be the triangle it always was, or "default 0" is a quiet
    // character change rather than a new control.
    {
        Tones a, b;
        a.prepare (sr);
        b.prepare (sr);
        a.setPitch (220.0f);
        b.setPitch (220.0f);
        a.setShape (0.0f);
        const int n = 2048;
        double worst = 0.0;
        for (int k = 0; k < n; ++k)
        {
            a.advance();
            b.advance();
            worst = juce::jmax (worst, (double) std::abs (a.main() - b.main()));
        }
        check ("Fold at zero is exactly the old triangle", worst == 0.0,
               "largest difference " + juce::String (worst, 12));
    }

    // The alias gate. At the top of the pitch range a folder is broadband
    // against a 48 kHz clock, and what folds back lands where no harmonic of
    // the fundamental belongs. If this is ugly rather than characterful the fix
    // is to limit the fold with pitch, so it is measured rather than assumed.
    {
        Tones t;
        t.prepare (sr);
        t.setPitch (2093.0f);
        t.setShape (1.0f);
        const int n = (int) (0.5 * sr);
        std::vector<float> out ((size_t) n, 0.0f);
        for (int k = 0; k < n; ++k)
        {
            t.advance();
            out[(size_t) k] = t.main();
        }
        const double fundamental = goertzelAmp (out, 0, n, 2093.0, sr);
        double alias = 0.0;
        for (double f = 100.0; f < 2000.0; f += 50.0) // nothing harmonic lives here
            alias = juce::jmax (alias, goertzelAmp (out, 0, n, f, sr));
        const double ratio = dbfs (alias / juce::jmax (fundamental, 1.0e-9));
        check ("folding at the top of the pitch range does not spray aliases",
               ratio < -30.0,
               "worst non-harmonic bin is " + juce::String (ratio, 1) + " dB under the fundamental");
    }
}

// Colour: whether mixing bandpass into the loop's lowpass actually moves the
// runaway's spectrum, and whether it is still the same filter at zero.
void colour()
{
    std::printf ("colour: the loop's own filter, and whether it can be bright\n");
    constexpr double sr = 48000.0;

    double lowCentroid = 0.0, highCentroid = 0.0;
    int i = 0;
    for (float c : { 0.0f, 1.0f })
    {
        TimeFilterLoop::Params p;
        p.time01 = 0.3f;
        p.decay = pt::kDecayMax;
        p.filterHz = 2500.0f;
        p.resonance01 = 0.5f;
        p.colour01 = c;

        const auto out = renderLoop (sr, 128, 8.0,
                                     [] (double t) { return t < 0.1 ? (float) (0.3 * std::sin (juce::MathConstants<double>::twoPi * 450.0 * t)) : 0.0f; },
                                     p, 7u);
        const int a = (int) (6.0 * sr), n = (int) (2.0 * sr);
        double num = 0.0, den = 0.0;
        for (double f = 25.0; f < 8000.0; f *= 1.03)
        {
            const double amp = goertzelAmp (out, a, n, f, sr);
            num += f * amp;
            den += amp;
        }
        const double centroid = den > 0.0 ? num / den : 0.0;
        note ("runaway spectrum", "Colour " + juce::String (juce::roundToInt (c * 100.0f))
                                      + " %: centroid " + juce::String (centroid, 0) + " Hz, RMS "
                                      + juce::String (dbfs (rmsOf (out, a, n)), 1) + " dBFS");
        if (i++ == 0)
            lowCentroid = centroid;
        else
            highCentroid = centroid;
    }

    check ("Colour moves where the runaway sits", highCentroid > lowCentroid * 1.5,
           "centroid " + juce::String (lowCentroid, 0) + " Hz -> "
               + juce::String (highCentroid, 0) + " Hz");

    // And it must be the same filter it always was at zero.
    TimeFilterLoop::Params p;
    p.time01 = 0.4f;
    p.decay = 0.6f;
    p.filterHz = 3000.0f;
    p.resonance01 = 0.3f;
    p.colour01 = 0.0f;
    const auto a0 = renderLoop (sr, 128, 0.5, sine (300.0, 0.4), p, 13u);
    const auto b0 = renderLoop (sr, 128, 0.5, sine (300.0, 0.4), p, 13u);
    check ("Colour at zero is exactly the old lowpass", fnvHash (a0) == fnvHash (b0),
           "hash " + juce::String::toHexString ((int) fnvHash (a0)));
}

// The instrument playing itself: no input at any point, the drone on, and the
// loop's own state moving the drone's pitch. The claim is that new material
// keeps being WRITTEN, not that an existing sine is smeared harder -- so what
// is counted is distinct pitch plateaux in the output, which a smeared sine
// cannot produce.
void chaosloop()
{
    std::printf ("chaosloop: with no input, does it write new material or smear one tone\n");
    constexpr double sr = 48000.0;

    DybbukEngine::Params p;
    p.time01 = 0.55f;
    p.decay = 1.05f;
    p.filterHz = 1400.0f;
    p.resonance01 = 0.6f;
    p.absorb01 = 0.15f;
    p.blend01 = 1.0f;
    p.chaos01 = 0.8f;
    p.tonesLevel01 = 0.6f;
    p.tonesPitchHz = 65.4f;
    p.tonesFold01 = 0.4f;
    p.colour01 = 0.3f; // some bandpass, so the loop can lock somewhere bright
    p.agitate01 = 0.2f;
    p.agitSpeedHz = 0.2f;

    const auto out = renderEngine (sr, 128, 90.0, kSilence, p, 7u);

    check ("it makes sound with nothing played into it",
           dbfs (rmsOf (out, (int) (20.0 * sr), (int) (60.0 * sr))) > -40.0,
           juce::String (dbfs (rmsOf (out, (int) (20.0 * sr), (int) (60.0 * sr))), 1) + " dBFS");
    check ("and stays bounded", peakOf (out, 0, (int) out.size()) < 1.0 && allFinite (out),
           "peak " + juce::String (peakOf (out, 0, (int) out.size()), 4));

    // Track the fundamental in 250 ms windows and count how many distinct
    // pitch plateaux it visits. A loop smearing one tone reads as one.
    std::vector<double> pitches;
    for (double t = 5.0; t < 88.0; t += 0.25)
    {
        const double f = zeroCrossFreq (out, (int) (t * sr), (int) (0.25 * sr), sr);
        if (f > 20.0 && f < 4000.0)
            pitches.push_back (f);
    }
    int plateaux = 0;
    double held = -1.0;
    for (double f : pitches)
        if (held < 0.0 || std::abs (1200.0 * std::log2 (f / held)) > 200.0) // a whole tone apart
        {
            ++plateaux;
            held = f;
        }
    check ("it visits many pitches rather than one", plateaux >= 8,
           juce::String (plateaux) + " distinct pitch plateaux over 83 s");

    // The TIMBRE has to move too, or it is one voice changing pitch rather than
    // a performance.
    //
    // Deliberately not a level check. A self-oscillating loop's amplitude is
    // regulated by the saturator -- that is what stops it running away -- so
    // everything downstream of it can only move the level a little, and the
    // measurement bears that out: the 200 ms spread is 0.8 dB and stays 0.8 dB
    // whether the Interference-to-Blend depth is 0.25, 0.6 or 1.0. Making that
    // number bigger would mean letting one route dominate the output gain,
    // which reads as a fault rather than a gesture. What actually moves here is
    // pitch and colour, so that is what is measured.
    std::vector<double> centroids;
    for (double t = 5.0; t < 88.0; t += 0.5)
    {
        double num = 0.0, den = 0.0;
        for (double f = 40.0; f < 5000.0; f *= 1.06)
        {
            const double amp = goertzelAmp (out, (int) (t * sr), (int) (0.5 * sr), f, sr);
            num += f * amp;
            den += amp;
        }
        if (den > 1.0e-9)
            centroids.push_back (num / den);
    }
    std::sort (centroids.begin(), centroids.end());
    const double lo = centroids[(size_t) (centroids.size() * 10 / 100)];
    const double hi = centroids[(size_t) (centroids.size() * 90 / 100)];
    check ("and its colour moves", hi > lo * 1.4,
           "spectral centroid spans " + juce::String (lo, 0) + " to " + juce::String (hi, 0)
               + " Hz across the run");
}

// Which destinations each source actually reaches, in that destination's own
// units. This is the isolation check that catches a mis-wired cell: a route
// that should be silent must read exactly zero, not merely small.
void routes()
{
    std::printf ("routes: what each source reaches, and what it must not\n");
    constexpr double sr = 48000.0;

    struct Probe { const char* name; float agitate, chaos; };
    const Probe probes[] = { { "Agitate alone", 1.0f, 0.0f }, { "Chaos alone", 0.0f, 1.0f } };

    for (const auto& probe : probes)
    {
        auto engine = std::make_unique<DybbukEngine>();
        engine->prepare (sr, 128);
        engine->seedForTests (7);

        DybbukEngine::Params p;
        p.time01 = 0.35f;
        p.decay = 0.9f;         // hot enough that Interference wakes up
        p.filterHz = 2000.0f;
        p.resonance01 = 0.4f;
        p.blend01 = 1.0f;
        p.agitate01 = probe.agitate;
        p.chaos01 = probe.chaos;
        p.agitSpeedHz = 2.0f;
        p.strengthDb = 6.0f;

        juce::AudioBuffer<float> buffer (2, 128);
        float filterPeak = 0.0f, decayPeak = 0.0f, timePeak = 0.0f;
        for (int b = 0; b < (int) (6.0 * sr / 128); ++b)
        {
            for (int i = 0; i < 128; ++i)
            {
                const double t = (double) (b * 128 + i) / sr;
                const float v = (float) (0.4 * std::sin (juce::MathConstants<double>::twoPi * 220.0 * t));
                buffer.setSample (0, i, v);
                buffer.setSample (1, i, v);
            }
            engine->process (buffer, p);
            filterPeak = juce::jmax (filterPeak, std::abs (engine->getFilterModOct()));
            decayPeak = juce::jmax (decayPeak, std::abs (engine->getDecayModLinear()));
            timePeak = juce::jmax (timePeak, std::abs (engine->getTimeModDepthOct()));
        }

        note (probe.name, "filter " + juce::String (filterPeak, 2) + " oct, decay +"
                              + juce::String (decayPeak, 3) + ", Time column "
                              + juce::String (timePeak, 3) + " oct");
        check ("this source moves the filter", filterPeak > 0.3f,
               juce::String (probe.name) + ": " + juce::String (filterPeak, 2) + " octaves");
        check ("and it reaches the clock", timePeak > 0.05f,
               juce::String (probe.name) + ": " + juce::String (timePeak, 3) + " octaves");
    }

    // With both macros at zero the only thing left must be Drift's trim.
    auto engine = std::make_unique<DybbukEngine>();
    engine->prepare (sr, 128);
    engine->seedForTests (7);
    DybbukEngine::Params p;
    p.time01 = 0.35f;
    p.decay = 0.9f;
    p.blend01 = 1.0f;
    p.agitate01 = 0.0f;
    p.chaos01 = 0.0f;
    juce::AudioBuffer<float> buffer (2, 128);
    float anyFilter = 0.0f, anyDecay = 0.0f;
    for (int b = 0; b < (int) (3.0 * sr / 128); ++b)
    {
        buffer.clear();
        engine->process (buffer, p);
        anyFilter = juce::jmax (anyFilter, std::abs (engine->getFilterModOct()));
        anyDecay = juce::jmax (anyDecay, std::abs (engine->getDecayModLinear()));
    }
    check ("both macros at zero really is silent", anyFilter == 0.0f && anyDecay == 0.0f,
           "filter " + juce::String (anyFilter, 6) + ", decay " + juce::String (anyDecay, 6));
}

// Where the self-oscillation settles as a function of Decay. The runaway zone
// is the top of the Decay knob and the whole point of the red hatching, so the
// size of that zone has to be chosen from a measured equilibrium curve rather
// than from a dB budget: the saturator's tanh is compressive, so past some
// excess gain more Decay buys knob travel and no sound.
//
// Read the crest column as well as the level. A crest of 1.41 is a sine, which
// is what a loop with one soft nonlinearity and ten lowpass poles per iteration
// settles into when it has almost no excess to work with. Lower is squarer.
void sustain()
{
    std::printf ("sustain: self-oscillation equilibrium against Decay, 40 s of silence\n");
    constexpr double sr = 48000.0;

    // The sweep is expressed against the live ceiling, because TimeFilterLoop
    // clamps its target to kDecayMax: asking for 1.80 while the constant says
    // 1.15 measures the clamp, not the loop, and every row past the ceiling
    // comes back identical. Reading the range off the constant means this
    // scenario re-points itself whenever the ceiling moves.
    const float top = pt::kDecayMax;
    const float sweep[] = { 1.0f, 1.0f + 0.25f * (top - 1.0f), 1.0f + 0.5f * (top - 1.0f),
                            1.0f + 0.75f * (top - 1.0f), top };
    note ("ceiling", "kDecayMax is " + juce::String (top, 2) + ", so the runaway zone is "
                         + juce::String (20.0 * std::log10 (top), 2) + " dB of excess gain");

    double lastRms = -200.0;
    for (float decay : sweep)
    {
        DybbukEngine::Params p;
        p.time01 = 0.30f;
        p.decay = decay;
        p.filterHz = 2000.0f;
        p.resonance01 = 0.30f;
        p.absorb01 = 0.0f;
        p.blend01 = 1.0f;

        // A short burst to start it, then nothing: what is left at 35 s is the
        // loop feeding itself.
        const auto out = renderEngine (sr, 128, 40.0,
                                       [] (double t) { return t < 0.1 ? (float) (0.3 * std::sin (juce::MathConstants<double>::twoPi * 450.0 * t)) : 0.0f; },
                                       p, 7u);

        const int a = (int) (35.0 * sr), n = (int) (4.0 * sr);
        const double pk = peakOf (out, a, n), r = rmsOf (out, a, n);
        const double crest = pk / juce::jmax (r, 1.0e-9);
        note ("equilibrium",
              "Decay " + juce::String (decay, 2) + ": peak " + juce::String (pk, 4) + ", RMS "
                  + juce::String (dbfs (r), 2) + " dBFS, crest " + juce::String (crest, 2)
                  + (lastRms > -190.0 ? ", +" + juce::String (dbfs (r) - lastRms, 2) + " dB on the last"
                                      : juce::String()));
        check ("bounded and finite", pk < 1.0 && allFinite (out),
               "Decay " + juce::String (decay, 2) + ": peak " + juce::String (pk, 4));
        lastRms = dbfs (r);
    }
}

// The loop filter's resonance, measured the way it is actually used. The state
// limit inside TptSvf is an ABSOLUTE clamp on the bandpass integrator, so the
// resonant gain depends on how hot the signal is: the same knob is worth tens
// of dB on a whisper and nothing at all at the level the loop runs at. That is
// a compressor hiding inside a filter, and (a) is the print that shows it.
//
// (b) is the free ring with Decay at zero, which is the filter as a voice in
// its own right. It is also the number that decides how far the negative
// damping can go, so it is measured before kResOverdrive is ever touched.
void resonance()
{
    std::printf ("resonance: gain at cutoff against level, and the filter's own free ring\n");
    constexpr double sr = 48000.0;
    constexpr double fc = 1000.0;

    for (float res : { 0.0f, 0.5f, 0.85f, 0.95f, 1.0f })
    {
        juce::String row;
        for (double amp : { 0.001, 0.01, 0.1, 0.3, 0.5 })
        {
            TptSvf svf;
            svf.setStateLimit (pt::kSvfSatLimit);
            svf.setG ((float) std::tan (juce::MathConstants<double>::pi * fc / sr));

            const float over = juce::jlimit (0.0f, 1.0f,
                                             (res - pt::kResOverdriveStart) / (1.0f - pt::kResOverdriveStart));
            svf.setK (2.0f * std::pow (1.0f - res, pt::kResCurve)
                      - pt::kResOverdrive * (over * over * (3.0f - 2.0f * over)));

            const int n = (int) (2.0 * sr);
            std::vector<float> out ((size_t) n, 0.0f);
            for (int i = 0; i < n; ++i)
                out[(size_t) i] = svf.lowpass ((float) (amp * std::sin (juce::MathConstants<double>::twoPi * fc * i / sr)));

            const double g = goertzelAmp (out, n / 2, n / 2, fc, sr) / amp;
            row += juce::String (dbfs (g), 2) + "  ";
        }
        note ("gain at fc, A = .001/.01/.1/.3/.5", "Res " + juce::String ((int) (res * 100.0f)) + " %: " + row);
    }

    // Free ring: excite once, then let the filter alone for 4 s. Anything that
    // survives is the filter oscillating on its own.
    for (float res : { 0.90f, 0.95f, 1.0f })
    {
        juce::String row;
        double worst = 0.0;
        for (double f : { 200.0, 1000.0, 4000.0, 8000.0 })
        {
            TptSvf svf;
            svf.setStateLimit (pt::kSvfSatLimit);
            svf.setG ((float) std::tan (juce::MathConstants<double>::pi * f / sr));

            const float over = juce::jlimit (0.0f, 1.0f,
                                             (res - pt::kResOverdriveStart) / (1.0f - pt::kResOverdriveStart));
            svf.setK (2.0f * std::pow (1.0f - res, pt::kResCurve)
                      - pt::kResOverdrive * (over * over * (3.0f - 2.0f * over)));

            LoopSaturator sat;
            sat.prepare (sr);
            sat.setDrive (pt::kSatDrive);

            const int n = (int) (4.0 * sr);
            std::vector<float> out ((size_t) n, 0.0f);
            for (int i = 0; i < n; ++i)
                out[(size_t) i] = sat.process (svf.lowpass (i < 64 ? 0.5f : 0.0f));

            const double pk = peakOf (out, n - (int) (0.5 * sr), (int) (0.5 * sr));
            worst = juce::jmax (worst, pk);
            row += juce::String (pk, 4) + "  ";
        }
        note ("free ring at 200/1k/4k/8k Hz", "Res " + juce::String ((int) (res * 100.0f)) + " %: " + row);
        check ("the filter cannot ring to full scale on its own", worst < 1.0,
               "Res " + juce::String ((int) (res * 100.0f)) + " %: worst " + juce::String (worst, 4));
    }
}

// Strength is one knob for gain and drive, so what matters is whether the top
// of it still changes anything. If the input peak freezes, the knob has become
// a hard clipper and its remaining travel only changes duty cycle.
void strength()
{
    std::printf ("strength: how far up the drive knob the texture keeps changing\n");
    constexpr double sr = 48000.0, f0 = 300.0;

    // Driven through the real engine, not through a copy of softClip: a local
    // mirror of a private function is a trap that passes forever while the
    // thing it mirrors rots. Decay 0 and Blend full, so what comes out is one
    // pass of the drive stage and the chip with no feedback on top.
    //
    // The source is -30 dBFS, which is a guitar DI, not the -10 dBFS a
    // synth patch would give: this knob's 40 dB range exists for quiet
    // sources, so measuring it with a hot one would call it saturated at a
    // third of its travel and prove nothing.
    double first = 0.0, last = 0.0;
    int step = 0;
    for (float db : { 0.0f, 10.0f, 20.0f, 30.0f, 40.0f })
    {
        DybbukEngine::Params p;
        p.strengthDb = db;
        p.time01 = 0.0f;
        p.decay = 0.0f;
        p.filterHz = 18000.0f;
        p.resonance01 = 0.0f;
        p.absorb01 = 0.0f;
        p.blend01 = 1.0f;

        const auto out = renderEngine (sr, 128, 0.6, sine (f0, 0.0316), p, 7u);
        const int a = (int) (0.3 * sr), n = (int) (0.25 * sr);

        const double h1 = goertzelAmp (out, a, n, f0, sr);
        double harm = 0.0;
        for (int h = 2; h <= 9; ++h)
        {
            const double amp = goertzelAmp (out, a, n, f0 * h, sr);
            harm += amp * amp;
        }
        const double thd = 100.0 * std::sqrt (harm) / juce::jmax (h1, 1.0e-9);
        note ("through the drive stage",
              "Strength +" + juce::String (db, 0) + " dB: wet peak "
                  + juce::String (peakOf (out, a, n), 4) + ", fundamental "
                  + juce::String (dbfs (h1), 1) + " dBFS, THD " + juce::String (thd, 1) + " %");

        if (step == 2) // +20 dB, the old hard clipper's freezing point
            first = thd;
        last = thd;
        ++step;
    }

    // The bug this exists for: softClip used pt::fastTanh, whose argument is
    // clamped to +-3 where it returns EXACTLY 1.0, so the drive stage was a
    // hard limiter above |x| = 1.6 and the top 25 dB of the knob only changed
    // the duty cycle of an already-square wave. std::tanh keeps generating
    // harmonics, so the distortion must still be climbing over the top half.
    check ("the top half of the knob still changes the texture", last > first * 1.25,
           "THD " + juce::String (first, 1) + " % at +20 dB rising to "
               + juce::String (last, 1) + " % at +40 dB");
}

void probe()
{
    std::printf ("probe: level after 30 s with no input, across Decay / Absorb / Filter\n");
    constexpr double sr = 48000.0;

    struct Case { float decay, absorb, filterHz, time01, res; };
    const Case cases[] = {
        { 1.15f, 0.0f, 8000.0f, 0.30f, 0.3f },
        { 1.15f, 0.0f, 4000.0f, 0.55f, 0.4f },
        { 1.15f, 0.2f, 4000.0f, 0.55f, 0.4f },
        { 1.10f, 0.0f, 4000.0f, 0.55f, 0.4f },
        { 1.10f, 0.0f, 8000.0f, 0.55f, 0.4f },
        { 1.10f, 0.0f, 8000.0f, 0.30f, 0.4f },
        { 1.05f, 0.0f, 8000.0f, 0.30f, 0.4f },
        { 1.15f, 0.0f, 18000.0f, 0.30f, 0.0f },
    };

    for (const auto& c : cases)
    {
        DybbukEngine::Params p;
        p.time01 = c.time01;
        p.decay = c.decay;
        p.filterHz = c.filterHz;
        p.resonance01 = c.res;
        p.absorb01 = c.absorb;
        p.blend01 = 1.0f;
        const auto out = renderEngine (sr, 128, 30.0, kSilence, p, 7u);
        note ("level",
              "Decay " + juce::String (c.decay, 2) + " Absorb " + juce::String (c.absorb, 2)
                  + " Filter " + juce::String (c.filterHz, 0) + " Time " + juce::String (c.time01, 2)
                  + " Res " + juce::String (c.res, 1) + ": "
                  + juce::String (dbfs (rmsOf (out, (int) (25.0 * sr), (int) (4.0 * sr))), 1) + " dBFS");
    }

    // Absorb against the runaway zone, which is the question docs/PROGRESS.md
    // leaves open. Absorb takes a fixed number of dB per iteration out of the
    // feedback, so what matters is that number against the loop's whole budget
    // above unity: if it is larger, a fraction of the Absorb knob vetoes the
    // top of the Decay knob entirely and the red hatching means nothing.
    //
    // The closed-form loop gain undershoots here because it omits the in-loop
    // Absorb shelf, so this is the authority for kAbsorbFbMaxDb, not algebra.
    std::printf ("\n  -- Absorb against the top of Decay (every row should self-oscillate) --\n");
    for (float absorb : { 0.0f, 0.2f, 0.5f, 1.0f })
    {
        DybbukEngine::Params p;
        p.time01 = 0.30f;
        p.decay = pt::kDecayMax;
        p.filterHz = 2000.0f;
        p.resonance01 = 0.3f;
        p.absorb01 = absorb;
        p.blend01 = 1.0f;
        const auto out = renderEngine (sr, 128, 30.0,
                                       [] (double t) { return t < 0.1 ? (float) (0.3 * std::sin (juce::MathConstants<double>::twoPi * 450.0 * t)) : 0.0f; },
                                       p, 7u);
        const double r = dbfs (rmsOf (out, (int) (25.0 * sr), (int) (4.0 * sr)));
        check ("runaway survives this much Absorb", r > -30.0,
               "Decay " + juce::String (pt::kDecayMax, 2) + " Absorb "
                   + juce::String (juce::roundToInt (absorb * 100.0f)) + " %: "
                   + juce::String (r, 1) + " dBFS");
    }
}


// The optional character: a drone that leaks into the delay, and its
// sub-harmonic driving the clock. Both off by default.
void tones()
{
    std::printf ("tones: the internal oscillator and its sub-harmonic\n");
    constexpr double sr = 48000.0;

    DybbukEngine::Params p;
    p.time01 = 0.35f;
    p.decay = 0.4f;
    p.filterHz = 12000.0f;
    p.blend01 = 1.0f;
    p.tonesPitchHz = 110.0f;

    const auto off = renderEngine (sr, 128, 2.0, kSilence, p, 7u);
    check ("silent with Tones at zero", dbfs (rmsOf (off, (int) sr, (int) sr)) < -60.0,
           juce::String (dbfs (rmsOf (off, (int) sr, (int) sr)), 1) + " dBFS");

    p.tonesLevel01 = 0.6f;
    const auto on = renderEngine (sr, 128, 2.0, kSilence, p, 7u);
    const int from = (int) (1.0 * sr), n = (int) (0.9 * sr);
    const double fundamental = goertzelAmp (on, from, n, 110.0, sr);
    const double subTone = goertzelAmp (on, from, n, 55.0, sr);
    const double level = dbfs (rmsOf (on, from, n));

    check ("drone appears at the set pitch", dbfs (fundamental) > -40.0,
           "110 Hz at " + juce::String (dbfs (fundamental), 1) + " dBFS, overall "
               + juce::String (level, 1) + " dBFS");
    check ("the sub-harmonic is under it", dbfs (subTone) > -50.0,
           "55 Hz at " + juce::String (dbfs (subTone), 1) + " dBFS");

    // Time Mod is normalled to the sub, so it should put sidebands a
    // sub-harmonic apart around a played tone, with no drone in the mix.
    DybbukEngine::Params fmParams;
    fmParams.time01 = time01ForSeconds (0.1);
    fmParams.decay = 0.0f;
    fmParams.blend01 = 1.0f;
    fmParams.filterHz = 18000.0f;
    fmParams.tonesPitchHz = 200.0f; // sub at 100 Hz
    // A modest index: at full depth the deviation is two octaves and the
    // energy spreads so far that "sidebands" stops being the right word.
    fmParams.timeMod01 = 0.12f;

    const double subHz = 100.0;
    auto measure = [&] (float depth)
    {
        DybbukEngine::Params q = fmParams;
        q.timeMod01 = depth;
        const auto fmOut = renderEngine (sr, 128, 2.0, sine (990.0, 0.3), q, 7u);
        double grid = 0.0, halfGrid = 0.0;
        for (int k = 1; k <= 4; ++k)
            for (double sign : { -1.0, 1.0 })
            {
                const double g = goertzelAmp (fmOut, from, n, 990.0 + sign * k * subHz, sr);
                grid += g * g;
                const double h = goertzelAmp (fmOut, from, n, 990.0 + sign * (k - 0.5) * subHz, sr);
                halfGrid += h * h;
            }
        const double car = std::pow (goertzelAmp (fmOut, from, n, 990.0, sr), 2.0);
        return std::array<double, 3> { grid, halfGrid, car };
    };

    const auto modded = measure (0.12f);
    const auto clean = measure (0.0f);

    // The comparison that cannot be fooled: the same measurement with the
    // depth at zero. The grid-to-half-grid ratio alone is muddied by the
    // three-tap comb, which puts structure every 10 Hz at this delay.
    check ("Time Mod puts sidebands on the sub-harmonic grid",
           modded[0] > 0.02 * modded[2] && modded[0] > 100.0 * clean[0],
           "grid/carrier " + juce::String (modded[0] / juce::jmax (modded[2], 1.0e-18), 3)
               + " with Time Mod, " + juce::String (clean[0] / juce::jmax (clean[2], 1.0e-18), 6)
               + " without; grid/half-grid "
               + juce::String (modded[0] / juce::jmax (modded[1], 1.0e-18), 1));
}

// Hardware-true mono is the default. Spread widens without ever breaking the
// mono sum, because the side signal is a difference.
void spread()
{
    std::printf ("spread: wide in stereo, unchanged in mono\n");
    constexpr double sr = 48000.0;

    DybbukEngine::Params p;
    p.time01 = 0.35f;
    p.decay = 0.7f;
    p.filterHz = 9000.0f;
    p.blend01 = 1.0f;

    std::vector<float> monoL, monoR, wideL, wideR;
    renderEngineStereo (sr, 128, 2.0, sine (330.0, 0.35), p, 7u, monoL, monoR);
    p.spread01 = 1.0f;
    renderEngineStereo (sr, 128, 2.0, sine (330.0, 0.35), p, 7u, wideL, wideR);

    double monoDiff = 0.0, wideDiff = 0.0, sumDiff = 0.0;
    double sumRef = 0.0;
    for (size_t i = 0; i < monoL.size(); ++i)
    {
        monoDiff = juce::jmax (monoDiff, (double) std::abs (monoL[i] - monoR[i]));
        wideDiff = juce::jmax (wideDiff, (double) std::abs (wideL[i] - wideR[i]));
        const double a = 0.5 * ((double) monoL[i] + monoR[i]);
        const double b = 0.5 * ((double) wideL[i] + wideR[i]);
        sumDiff = juce::jmax (sumDiff, std::abs (a - b));
        sumRef = juce::jmax (sumRef, std::abs (a));
    }

    check ("Spread 0 is true mono", monoDiff < 1.0e-6,
           "largest L minus R is " + juce::String (monoDiff, 9));
    check ("Spread 1 actually widens", wideDiff > 0.02,
           "largest L minus R is " + juce::String (wideDiff, 4));
    check ("the mono sum is untouched", sumDiff < 1.0e-5 * juce::jmax (sumRef, 1.0e-6),
           "mono sums differ by at most " + juce::String (sumDiff, 9) + " against a peak of "
               + juce::String (sumRef, 4));
}


// --- the Burst direction ---------------------------------------------------
//
// A gated step recorder driving a steady step sequencer (docs/BURST.md).
// Distinct tone bursts go in, so every step can be identified by its pitch on
// the way out, and the step clock can be measured to the sample.

struct ToneBurst { double t0, dur, freq, amp; };

std::vector<float> burstInput (double sr, double seconds, const std::vector<ToneBurst>& bursts)
{
    std::vector<float> x ((size_t) (seconds * sr), 0.0f);
    const int fade = (int) (0.002 * sr);
    for (const auto& b : bursts)
    {
        const int start = (int) (b.t0 * sr), len = (int) (b.dur * sr);
        for (int i = 0; i < len && start + i < (int) x.size(); ++i)
        {
            float g = 1.0f;
            if (i < fade)            g = (float) i / (float) fade;
            if (len - i < fade)      g = juce::jmin (g, (float) (len - i) / (float) fade);
            x[(size_t) (start + i)] += (float) (b.amp * g * std::sin (juce::MathConstants<double>::twoPi * b.freq * i / sr));
        }
    }
    return x;
}

struct BurstRun
{
    std::vector<float> out;
    int stepCount = 0;
};

// Drives the engine like a host. `atBlock` lets a scenario poke the engine
// (clear, a parameter flip) at a given time.
BurstRun runBurst (const std::vector<float>& input, BurstEngine::Params params, double sr, int blockSize,
                   const std::function<void (BurstEngine&, BurstEngine::Params&, double)>& atBlock = {})
{
    BurstEngine engine;
    engine.prepare (sr, blockSize);
    juce::AudioBuffer<float> buf (2, blockSize);
    BurstRun r;
    r.out.resize (input.size());
    for (size_t pos = 0; pos < input.size(); pos += (size_t) blockSize)
    {
        const int n = (int) juce::jmin ((size_t) blockSize, input.size() - pos);
        if (atBlock)
            atBlock (engine, params, (double) pos / sr);
        for (int i = 0; i < n; ++i)
        {
            buf.setSample (0, i, input[pos + (size_t) i]);
            buf.setSample (1, i, input[pos + (size_t) i]);
        }
        juce::AudioBuffer<float> view (buf.getArrayOfWritePointers(), 2, n);
        engine.process (view, params);
        for (int i = 0; i < n; ++i)
            r.out[pos + (size_t) i] = buf.getSample (0, i);
    }
    r.stepCount = engine.uiStepCount.load();
    return r;
}

// Sample-accurate note onsets in the output: the first sample over the
// level after at least 20 ms under it, plus the pitch of the 30 ms after.
struct NoteOnset { int sample; double freq; int soundedFor; };

std::vector<NoteOnset> onsetsOf (const std::vector<float>& x, double sr, int from = 0)
{
    std::vector<NoteOnset> v;
    const int quiet = (int) (0.02 * sr);
    int silentRun = from > 0 ? 0 : quiet;   // scanning from mid-stream, wait for real silence first
    for (int i = from; i < (int) x.size(); ++i)
    {
        const float a = std::abs (x[(size_t) i]);
        if (a > 0.02f)
        {
            if (silentRun >= quiet)
            {
                NoteOnset o;
                o.sample = i;
                const int win = juce::jmin ((int) (0.03 * sr), (int) x.size() - i);
                o.freq = zeroCrossFreq (x, i, win, sr);
                int last = i, gap = 0;
                for (int j = i; j < (int) x.size() && gap < quiet; ++j)
                {
                    if (std::abs (x[(size_t) j]) > 0.02f) { last = j; gap = 0; }
                    else                                    ++gap;
                }
                o.soundedFor = last - i;
                v.push_back (o);
            }
            silentRun = 0;
        }
        else if (a < 0.002f)
        {
            ++silentRun;
        }
    }
    return v;
}

bool near (double a, double b, double tol) { return std::abs (a - b) <= tol * b; }

void burst()
{
    std::printf ("burst: a gated step recorder feeding a steady step sequencer\n");
    const double sr = 48000.0;
    const std::vector<ToneBurst> four = { { 0.10, 0.080, 220.0, 0.5 }, { 0.40, 0.120, 440.0, 0.5 },
                                          { 0.75, 0.060, 660.0, 0.5 }, { 1.10, 0.100, 880.0, 0.5 } };
    BurstEngine::Params p;
    p.thresholdDb = -30.0f;
    p.stepMs = 200.0f;
    p.maxSteps = 8;
    p.record = true;
    p.mix01 = 1.0f;

    const auto in = burstInput (sr, 3.4, four);
    const auto r = runBurst (in, p, sr, 128);
    if (std::getenv ("BURST_DEBUG") != nullptr)
        for (const auto& o : onsetsOf (r.out, sr))
            std::printf ("    onset %8d  %6.1f Hz  sounded %6.1f ms\n", o.sample, o.freq, o.soundedFor * 1000.0 / sr);
    const int stepSamples = juce::roundToInt (0.2 * sr);

    check ("nothing plays until the first step commits", rmsOf (r.out, 0, (int) (0.18 * sr)) == 0.0,
           "rms over the first 180 ms is " + juce::String (rmsOf (r.out, 0, (int) (0.18 * sr)), 9));
    check ("four bursts make four steps", r.stepCount == 4, juce::String (r.stepCount) + " steps");

    const auto on = onsetsOf (r.out, sr, (int) (1.5 * sr));
    bool spacingOk = on.size() >= 8, cyclicOk = on.size() >= 8;
    int worstAdjacent = 0;
    for (size_t k = 0; k + 1 < on.size(); ++k)
        worstAdjacent = juce::jmax (worstAdjacent, std::abs (on[k + 1].sample - on[k].sample - stepSamples));
    for (size_t k = 0; k + 4 < on.size(); ++k)
        cyclicOk = cyclicOk && (on[k + 4].sample - on[k].sample == 4 * stepSamples);
    spacingOk = spacingOk && worstAdjacent <= (int) (0.003 * sr);
    check ("steps land on a 200 ms clock", spacingOk,
           juce::String ((int) on.size()) + " onsets after 1.5 s, adjacent spacing within "
               + juce::String (worstAdjacent) + " samples of " + juce::String (stepSamples));
    check ("the same step recurs to the sample", cyclicOk, "every 4th onset is exactly 800 ms apart");

    size_t first = 0;
    while (first < on.size() && ! near (on[first].freq, 220.0, 0.08))
        ++first;
    const double expect[] = { 220.0, 440.0, 660.0, 880.0 };
    bool orderOk = first + 8 <= on.size();
    juce::String seq;
    for (size_t k = first; k < on.size() && k < first + 8; ++k)
    {
        orderOk = orderOk && near (on[k].freq, expect[(k - first) % 4], 0.08);
        seq += juce::String (juce::roundToInt (on[k].freq)) + " ";
    }
    check ("the pattern cycles in the order it was played", orderOk, seq.trim() + " Hz");

    bool lengthOk = orderOk;
    if (orderOk)
    {
        const double heard = on[first].soundedFor / sr;
        lengthOk = std::abs (heard - 0.080) < 0.006;
        check ("a step holds exactly what was gated", lengthOk,
               "the 80 ms burst sounds for " + juce::String (heard * 1000.0, 1) + " ms");
        const int gapStart = on[first + 2].sample + (int) (0.12 * sr);
        const double gapRms = rmsOf (r.out, gapStart, (int) (0.07 * sr));
        check ("a short step leaves a gap", gapRms == 0.0,
               "rms 120..190 ms into the 60 ms step is " + juce::String (gapRms, 9));
    }

    // The clock must not care how the host chops the block, or what the rate is.
    const auto r1 = runBurst (in, p, sr, 1);
    const auto r512 = runBurst (in, p, sr, 512);
    check ("block size does not change a sample", fnvHash (r.out) == fnvHash (r1.out) && fnvHash (r.out) == fnvHash (r512.out),
           "hashes at 1 / 128 / 512: " + juce::String (fnvHash (r1.out)) + " / " + juce::String (fnvHash (r.out)) + " / "
               + juce::String (fnvHash (r512.out)));
    for (double rate : { 44100.0, 96000.0 })
    {
        const auto rr = runBurst (burstInput (rate, 3.4, four), p, rate, 128);
        const auto onr = onsetsOf (rr.out, rate, (int) (1.5 * rate));
        bool ok = onr.size() >= 8 && rr.stepCount == 4;
        for (size_t k = 0; k + 4 < onr.size(); ++k)
            ok = ok && onr[k + 4].sample - onr[k].sample == 4 * juce::roundToInt (0.2 * rate);
        check (rate < 50000.0 ? "44.1 kHz keeps the same clock" : "96 kHz keeps the same clock", ok,
               juce::String ((int) onr.size()) + " onsets, " + juce::String (rr.stepCount) + " steps");
    }

    // A fifth burst while the pattern runs is a fifth step while armed, and
    // nothing at all once disarmed.
    auto five = four;
    five.push_back ({ 3.0, 0.090, 1100.0, 0.5 });
    const auto in5 = burstInput (sr, 4.6, five);
    {
        const auto ro = runBurst (in5, p, sr, 128);
        const auto ono = onsetsOf (ro.out, sr, (int) (3.3 * sr));
        int hits = 0;
        for (const auto& o : ono)
            hits += near (o.freq, 1100.0, 0.08) ? 1 : 0;
        check ("armed, a fifth burst is a fifth step", ro.stepCount == 5 && hits >= 1,
               juce::String (ro.stepCount) + " steps, the new tone heard " + juce::String (hits) + " times");
        // Disarm at 2 s, once the four are in.
        const auto rf = runBurst (in5, p, sr, 128, [] (BurstEngine&, BurstEngine::Params& q, double t) {
            if (t >= 2.0)
                q.record = false;
        });
        const auto onf = onsetsOf (rf.out, sr, (int) (3.3 * sr));
        int hitsOff = 0;
        for (const auto& o : onf)
            hitsOff += near (o.freq, 1100.0, 0.08) ? 1 : 0;
        check ("disarmed, the pattern is left alone", rf.stepCount == 4 && hitsOff == 0,
               juce::String (rf.stepCount) + " steps, the new tone heard " + juce::String (hitsOff) + " times");
    }

    // The ceiling: past it the oldest step goes, so the pattern is the last
    // N things played.
    {
        BurstEngine::Params three = p;
        three.maxSteps = 3;
        const auto rc = runBurst (in, three, sr, 128);
        const auto onc = onsetsOf (rc.out, sr, (int) (1.5 * sr));
        int low = 0;
        bool cyc = onc.size() >= 6;
        for (const auto& o : onc)
            low += near (o.freq, 220.0, 0.08) ? 1 : 0;
        for (size_t k = 0; k + 3 < onc.size(); ++k)
            cyc = cyc && onc[k + 3].sample - onc[k].sample == 3 * stepSamples;
        check ("past the ceiling the oldest step is dropped", rc.stepCount == 3 && low == 0 && cyc,
               juce::String (rc.stepCount) + " steps, 220 Hz heard " + juce::String (low) + " times, 3-cycle "
                   + (cyc ? "holds" : "broken"));
    }

    // Start over: empties the pattern mid-flight, and the next thing played
    // starts a fresh one.
    {
        auto again = four;
        again.push_back ({ 2.8, 0.090, 1100.0, 0.5 });
        const auto inA = burstInput (sr, 4.0, again);
        int countAtClear = -1;
        const auto rx = runBurst (inA, p, sr, 128, [&] (BurstEngine& e, BurstEngine::Params&, double t) {
            if (t >= 2.0 && countAtClear < 0)
            {
                countAtClear = e.uiStepCount.load();
                e.requestClear();
            }
        });
        const double afterClear = rmsOf (rx.out, (int) (2.01 * sr), (int) (0.7 * sr));
        const auto onx = onsetsOf (rx.out, sr, (int) (2.9 * sr));
        bool fresh = onx.size() >= 3 && rx.stepCount == 1;
        for (const auto& o : onx)
            fresh = fresh && near (o.freq, 1100.0, 0.08);
        check ("start over empties the pattern", countAtClear == 4 && afterClear == 0.0,
               "rms for 700 ms after the clear is " + juce::String (afterClear, 9));
        check ("the next thing played starts a fresh pattern", fresh,
               juce::String (rx.stepCount) + " step, " + juce::String ((int) onx.size()) + " onsets of "
                   + juce::String (onx.empty() ? 0.0 : onx[0].freq, 0) + " Hz");
    }

    // Material longer than the step is cut at the boundary, without a click.
    {
        BurstEngine::Params fast = p;
        fast.stepMs = 150.0f;
        const auto inL = burstInput (sr, 2.0, { { 0.1, 0.400, 330.0, 0.5 } });
        const auto rl = runBurst (inL, fast, sr, 128);
        double minWindowRms = 1.0, maxJump = 0.0;
        const int w = (int) (0.02 * sr);
        for (int s = (int) (0.7 * sr); s + w < (int) rl.out.size(); s += w)
            minWindowRms = juce::jmin (minWindowRms, rmsOf (rl.out, s, w));
        for (int s = (int) (0.7 * sr) + 1; s < (int) rl.out.size(); ++s)
            maxJump = juce::jmax (maxJump, (double) std::abs (rl.out[(size_t) s] - rl.out[(size_t) s - 1]));
        check ("a long step is cut at the boundary and keeps sounding", rl.stepCount == 1 && minWindowRms > 0.2,
               "quietest 20 ms window is " + juce::String (dbfs (minWindowRms), 1) + " dBFS");
        check ("no click at the cut", maxJump < 0.04, "largest sample step " + juce::String (maxJump, 4));
    }

    // A re-attack inside an open gate splits the step; a steady note does not.
    {
        const auto inS = burstInput (sr, 1.5, { { 0.1, 0.200, 220.0, 0.15 }, { 0.3, 0.200, 220.0, 0.5 } });
        const auto rs = runBurst (inS, p, sr, 128);
        const auto inH = burstInput (sr, 1.5, { { 0.1, 0.400, 220.0, 0.5 } });
        const auto rh = runBurst (inH, p, sr, 128);
        check ("a louder re-attack splits the step", rs.stepCount == 2, juce::String (rs.stepCount) + " steps");
        check ("a held note stays one step", rh.stepCount == 1, juce::String (rh.stepCount) + " steps");
    }

    check ("output is finite", allFinite (r.out), "");
}

struct Scenario { const char* name; void (*fn)(); };

const Scenario kScenarios[] = {
    { "coredelay", coredelay }, { "thd", thd },           { "noise", noise },
    { "bandwidth", bandwidth }, { "repitch", repitch },   { "threestep", threestep },
    { "runaway", runaway },     { "bleed", bleed },       { "fm", fm },
    { "clear", clear },         { "nan", nan },           { "nonidentical", nonidentical },
    { "blockmatrix", blockmatrix }, { "srmatrix", srmatrix },
    { "agitation", agitation }, { "follower", follower }, { "interference", interference },
    { "drift", drift },         { "generative", generative },
    { "tones", tones },         { "spread", spread },
    { "cpu", cpu },             { "soak", soak },       { "probe", probe },
    { "sustain", sustain },     { "resonance", resonance }, { "strength", strength },
    { "crust", crust },         { "timemod", timemod },     { "agitfm", agitfm },
    { "routes", routes },       { "voice", voice },         { "colour", colour },
    { "chaosloop", chaosloop }, { "burst", burst },
};

} // namespace

int main (int argc, char* argv[])
{
    const juce::String wanted = argc > 1 ? juce::String (argv[1]) : juce::String();

    if (wanted == "render")
    {
        render();
        return 0;
    }

    const double started = juce::Time::getMillisecondCounterHiRes();
    bool ranSomething = false;

    for (const auto& s : kScenarios)
    {
        if (wanted.isNotEmpty() && wanted != s.name)
            continue;
        ranSomething = true;
        s.fn();
        std::printf ("\n");
    }

    if (! ranSomething)
    {
        std::printf ("unknown scenario '%s'. Available:", wanted.toRawUTF8());
        for (const auto& s : kScenarios)
            std::printf (" %s", s.name);
        std::printf (" render\n");
        return 2;
    }

    std::printf ("%d checks, %d failures, %.1f s\n", checksRun, failures,
                 (juce::Time::getMillisecondCounterHiRes() - started) / 1000.0);
    return failures == 0 ? 0 : 1;
}
