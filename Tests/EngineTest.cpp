// Offline DSP harness for the Dybbuk engine: drives it like a host and prints
// measurements, so behaviour can be verified without a DAW.
//
// THIS IS THE MOST IMPORTANT FILE IN THE REPO. Every scenario prints a number
// that would change if the behaviour broke. The model under test is in
// docs/BURST.md.
//
//   build/EngineTest_artefacts/RelWithDebInfo/EngineTest            (runs all)
//   build/EngineTest_artefacts/RelWithDebInfo/EngineTest burst      (one scenario)
//   build/EngineTest_artefacts/RelWithDebInfo/EngineTest render     (writes wavs to listen to)
#include "../Source/dsp/BurstEngine.h"

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

// Pitch from the time between the first and last zero crossing in a window,
// not from a crossing count over the window: a count is quantised to one
// crossing, which at 110 Hz over 30 ms is a 9 % error.
double cycleFreq (const std::vector<float>& x, int start, int n, double sr)
{
    int first = -1, last = -1, crossings = 0;
    for (int i = 1; i < n; ++i)
    {
        if ((x[(size_t) (start + i)] > 0.0f) != (x[(size_t) (start + i - 1)] > 0.0f))
        {
            if (first < 0) first = i;
            last = i;
            ++crossings;
        }
    }
    if (crossings < 3)
        return zeroCrossFreq (x, start, n, sr);
    return (double) (crossings - 1) / 2.0 / ((double) (last - first) / sr);
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
                o.freq = cycleFreq (x, i, win, sr);
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
    p.stepSeconds = 0.2;
    p.maxSteps = 8;
    p.record = true;
    p.blend01 = 1.0f;

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
        fast.stepSeconds = 0.15;
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
    p.stepSeconds = 0.18;
    p.maxSteps = 8;
    p.blend01 = 0.8f;

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
    renderBurst (sr);
}


// The four-burst phrase every scenario below starts from: four distinct
// pitches, so each step can be told apart on the way out.
const std::vector<ToneBurst> kFour = { { 0.10, 0.080, 220.0, 0.5 }, { 0.40, 0.120, 440.0, 0.5 },
                                       { 0.75, 0.060, 660.0, 0.5 }, { 1.10, 0.100, 880.0, 0.5 } };

BurstEngine::Params wetParams()
{
    BurstEngine::Params p;
    p.thresholdDb = -30.0f;
    p.stepSeconds = 0.2;
    p.maxSteps = 8;
    p.record = true;
    p.blend01 = 1.0f;
    return p;
}

int stepOf (double freq)
{
    const double table[] = { 220.0, 440.0, 660.0, 880.0 };
    for (int i = 0; i < 4; ++i)
        if (near (freq, table[i], 0.08))
            return i;
    return -1;
}

juce::String stepsHeard (const std::vector<NoteOnset>& on, size_t from = 0, size_t max = 12)
{
    juce::String s;
    for (size_t k = from; k < on.size() && k < from + max; ++k)
        s += juce::String (stepOf (on[k].freq) + 1) + " ";
    return s.trim();
}

// Transport mode: the processor hands the engine the distance to the next
// grid line every block; the ticks must land on that grid whatever the
// pattern's own history, and follow it when it moves.
void sync()
{
    std::printf ("sync: transport mode puts every tick on the host's grid\n");
    const double sr = 48000.0;
    const int div = 6000;                 // a sixteenth at 120 BPM
    auto p = wetParams();
    p.stepSeconds = div / sr;

    int origin = 1234;                    // an odd phase, so free-running could not match it by luck
    const auto in = burstInput (sr, 4.0, kFour);
    const auto r = runBurst (in, p, sr, 128, [&] (BurstEngine&, BurstEngine::Params& q, double t) {
        if (t >= 2.5)
            origin = 1234 + 3000;         // a relocate half a step away
        const int pos = (int) std::llround (t * sr);
        int off = (origin - pos) % div;
        if (off < 0)
            off += div;
        q.gridOffsetSamples = off;
    });

    const auto early = onsetsOf (r.out, sr, (int) (1.4 * sr));
    bool onGrid = ! early.empty(), moved = false;
    int phase = -1, worst = 0;
    juce::String phases;
    for (const auto& o : early)
    {
        if (o.sample >= (int) (2.5 * sr))
            break;
        const int ph = (o.sample - 1234) % div;
        if (phase < 0) phase = ph;
        worst = juce::jmax (worst, std::abs (ph - phase));
        phases += juce::String (ph) + " ";
    }
    onGrid = onGrid && worst <= 2 && phase < (int) (0.008 * sr);
    check ("ticks sit on the grid, not on the first note", onGrid,
           "offsets past the grid line: " + phases.trim() + " samples (detection lag included)");

    const auto late = onsetsOf (r.out, sr, (int) (2.7 * sr));
    int worstLate = 0;
    for (const auto& o : late)
        worstLate = juce::jmax (worstLate, std::abs ((o.sample - 4234) % div - phase));
    moved = ! late.empty() && worstLate <= 2;
    check ("a relocate moves the ticks within a block", moved,
           juce::String ((int) late.size()) + " onsets after the move, worst " + juce::String (worstLate)
               + " samples off the new grid");
    check ("the pattern is intact", r.stepCount == 4, juce::String (r.stepCount) + " steps");
}

void direction()
{
    std::printf ("direction: the order the sequencer walks the pattern\n");
    const double sr = 48000.0;
    const auto in = burstInput (sr, 4.6, kFour);

    auto run = [&] (BurstEngine::Direction d, unsigned seed) {
        auto p = wetParams();
        return runBurst (in, p, sr, 128, [&, d, seed] (BurstEngine& e, BurstEngine::Params& q, double t) {
            if (t == 0.0) e.seedForTests (seed);
            if (t >= 1.5) q.direction = d;
        });
    };

    {
        const auto on = onsetsOf (run (BurstEngine::Direction::reverse, 1).out, sr, (int) (1.7 * sr));
        size_t k = 0;
        while (k < on.size() && stepOf (on[k].freq) != 3) ++k;
        const int expect[] = { 3, 2, 1, 0 };
        bool ok = k + 8 <= on.size();
        for (size_t i = k; ok && i < k + 8; ++i)
            ok = stepOf (on[i].freq) == expect[(i - k) % 4];
        check ("Reverse walks 4 3 2 1", ok, stepsHeard (on, k, 8));
    }
    {
        const auto on = onsetsOf (run (BurstEngine::Direction::pendulum, 1).out, sr, (int) (1.7 * sr));
        size_t k = 0;
        while (k + 1 < on.size() && ! (stepOf (on[k].freq) == 0 && stepOf (on[k + 1].freq) == 1)) ++k;
        const int expect[] = { 0, 1, 2, 3, 2, 1 };
        bool ok = k + 12 <= on.size();
        for (size_t i = k; ok && i < k + 12; ++i)
            ok = stepOf (on[i].freq) == expect[(i - k) % 6];
        check ("Pendulum bounces 1 2 3 4 3 2", ok, stepsHeard (on, k, 12));
    }
    {
        const auto on = onsetsOf (run (BurstEngine::Direction::random, 7).out, sr, (int) (1.7 * sr));
        bool inSet = on.size() >= 10, isForward = on.size() >= 10;
        for (size_t i = 0; i < on.size(); ++i)
        {
            inSet = inSet && stepOf (on[i].freq) >= 0;
            if (i > 0)
                isForward = isForward && stepOf (on[i].freq) == (stepOf (on[i - 1].freq) + 1) % 4;
        }
        check ("Random stays inside the pattern and is not the forward order", inSet && ! isForward, stepsHeard (on));
    }
    {
        const auto on = onsetsOf (run (BurstEngine::Direction::drunk, 3).out, sr, (int) (1.7 * sr));
        bool steps1 = on.size() >= 10, wanders = false;
        for (size_t i = 1; i < on.size(); ++i)
        {
            const int a = stepOf (on[i - 1].freq), b = stepOf (on[i].freq);
            const int d = ((b - a) % 4 + 4) % 4;
            steps1 = steps1 && (d == 1 || d == 3);
            wanders = wanders || d == 3;
        }
        check ("Drunk moves one step either way", steps1 && wanders, stepsHeard (on));
    }
}

void length()
{
    std::printf ("length: the choke, as a fraction of the step\n");
    const double sr = 48000.0;
    const auto in = burstInput (sr, 2.0, { { 0.1, 0.400, 330.0, 0.5 } });
    for (float len : { 0.5f, 0.25f })
    {
        auto p = wetParams();
        p.length01 = len;
        const auto r = runBurst (in, p, sr, 128);
        const auto on = onsetsOf (r.out, sr, (int) (0.7 * sr));
        const double heard = on.empty() ? 0.0 : on[0].soundedFor / sr;
        check (len == 0.5f ? "Length 50 % sounds for half the step" : "Length 25 % sounds for a quarter",
               // The 4 ms pre-roll at the head of the slice is silence, so the
               // audible part is the choke less the pre-roll.
               ! on.empty() && std::abs (heard - (0.2 * len - 0.004)) < 0.003,
               "sounds for " + juce::String (heard * 1000.0, 1) + " ms of a 200 ms step (choke less the 4 ms pre-roll)");
    }
}

void fade()
{
    std::printf ("fade: every play costs level, and a step that fades out leaves\n");
    const double sr = 48000.0;
    const auto in = burstInput (sr, 4.0, kFour);
    auto p = wetParams();
    p.fade01 = 1.0f;
    const auto r = runBurst (in, p, sr, 128);
    const auto on = onsetsOf (r.out, sr);
    const int step = juce::roundToInt (0.2 * sr);
    double first = 0.0, second = 0.0;
    if (! on.empty())
    {
        first = peakOf (r.out, on[0].sample, step - 300);
        second = peakOf (r.out, on[0].sample + step, step - 300);
    }
    check ("the second play is 18 dB down", first > 0.4 && std::abs (dbfs (second / juce::jmax (first, 1e-9)) + 18.0) < 1.0,
           "first play " + juce::String (dbfs (first), 1) + " dBFS, second " + juce::String (dbfs (second), 1));
    const double tail = rmsOf (r.out, (int) (3.5 * sr), (int) (0.5 * sr));
    check ("the pattern dies away to nothing", r.stepCount == 0 && tail == 0.0,
           juce::String (r.stepCount) + " steps left, tail rms " + juce::String (tail, 9));

    auto keep = wetParams();
    const auto rk = runBurst (in, keep, sr, 128);
    check ("Fade at zero keeps every step", rk.stepCount == 4 && peakOf (rk.out, (int) (3.5 * sr), (int) (0.5 * sr)) > 0.4,
           juce::String (rk.stepCount) + " steps");
}

void fills()
{
    std::printf ("fills: disarmed, an onset scrambles the order for one cycle\n");
    const double sr = 48000.0;
    auto phrase = kFour;
    phrase.push_back ({ 2.5, 0.090, 1100.0, 0.5 });   // the trigger, after disarming
    const auto in = burstInput (sr, 5.5, phrase);
    for (float depth : { 1.0f, 0.0f })
    {
        auto p = wetParams();
        p.fills01 = depth;
        const auto r = runBurst (in, p, sr, 128, [] (BurstEngine& e, BurstEngine::Params& q, double t) {
            if (t == 0.0) e.seedForTests (11);
            if (t >= 1.5) q.record = false;
        });
        const auto during = onsetsOf (r.out, sr, (int) (2.55 * sr));
        std::vector<int> cycle;
        for (const auto& o : during)
            if (o.sample < (int) (3.4 * sr))
                cycle.push_back (stepOf (o.freq));
        bool permutation = cycle.size() == 4, forward = cycle.size() == 4;
        for (int st = 0; st < 4; ++st)
            permutation = permutation && std::count (cycle.begin(), cycle.end(), st) == 1;
        for (size_t i = 1; i < cycle.size(); ++i)
            forward = forward && cycle[i] == (cycle[i - 1] + 1) % 4;

        const auto after = onsetsOf (r.out, sr, (int) (3.5 * sr));
        bool restored = after.size() >= 6;
        for (size_t i = 1; i < after.size(); ++i)
            restored = restored && stepOf (after[i].freq) == (stepOf (after[i - 1].freq) + 1) % 4;

        if (depth > 0.0f)
        {
            check ("the trigger is not captured", r.stepCount == 4, juce::String (r.stepCount) + " steps");
            check ("the next cycle is a scramble of the same four", permutation && ! forward, stepsHeard (during, 0, 4));
            check ("then the order comes back", restored, stepsHeard (after, 0, 8));
        }
        else
        {
            check ("Fills at zero leaves the order alone", forward && restored, stepsHeard (during, 0, 4) + " | " + stepsHeard (after, 0, 8));
        }
    }
}

void chaos()
{
    std::printf ("chaos: skips, ratchets, reverses and repeats, seeded\n");
    const double sr = 48000.0;
    const auto in = burstInput (sr, 6.0, kFour);
    auto still = wetParams();
    const auto a = runBurst (in, still, sr, 128, [] (BurstEngine& e, BurstEngine::Params&, double t) { if (t == 0.0) e.seedForTests (5); });
    const auto b = runBurst (in, still, sr, 128, [] (BurstEngine& e, BurstEngine::Params&, double t) { if (t == 0.0) e.seedForTests (5); });
    check ("Chaos at zero is deterministic", fnvHash (a.out) == fnvHash (b.out), juce::String (fnvHash (a.out)));

    auto wild = wetParams();
    wild.chaos01 = 1.0f;
    const auto c = runBurst (in, wild, sr, 128, [] (BurstEngine& e, BurstEngine::Params&, double t) { if (t == 0.0) e.seedForTests (5); });
    const auto onA = onsetsOf (a.out, sr, (int) (1.5 * sr));
    const auto onC = onsetsOf (c.out, sr, (int) (1.5 * sr));
    // Chaos may pitch a step by a musical interval, so what is heard must be
    // one of the four notes or one of them transposed by such an interval.
    bool inSet = true;
    for (const auto& o : onC)
    {
        bool found = false;
        for (const double base : { 220.0, 440.0, 660.0, 880.0 })
            for (const int st : { -12, -7, -5, -3, -2, 0, 2, 3, 5, 7, 12 })
                found = found || near (o.freq, base * std::pow (2.0, st / 12.0), 0.03);
        inSet = inSet && found;
    }
    // A reversed slice sounds late in its step (its silent tail plays first),
    // so onset phase is not a fair clock test here; the material is.
    check ("Chaos at full changes the output", fnvHash (a.out) != fnvHash (c.out) && allFinite (c.out),
           juce::String ((int) onA.size()) + " onsets still, " + juce::String ((int) onC.size()) + " wild");
    check ("but only the pattern's own notes, or intervals of them", inSet && c.stepCount == 4 && onC.size() >= 5,
           stepsHeard (onC));
}

void ceiling()
{
    std::printf ("ceiling: Steps, Hold, and lowering Steps live\n");
    const double sr = 48000.0;
    const auto in = burstInput (sr, 4.6, kFour);
    {
        auto p = wetParams();
        p.maxSteps = 3;
        p.replaceOldest = false;
        const auto r = runBurst (in, p, sr, 128);
        const auto on = onsetsOf (r.out, sr, (int) (1.5 * sr));
        bool ok = r.stepCount == 3 && on.size() >= 6;
        for (size_t i = 1; i < on.size(); ++i)
            ok = ok && stepOf (on[i].freq) == (stepOf (on[i - 1].freq) + 1) % 3;
        check ("Hold: the fourth burst is turned away", ok, juce::String (r.stepCount) + " steps: " + stepsHeard (on, 0, 6));
    }
    {
        auto p = wetParams();
        const auto r = runBurst (in, p, sr, 128, [] (BurstEngine&, BurstEngine::Params& q, double t) {
            q.maxSteps = t >= 1.5 && t < 3.0 ? 2 : 8;
        });
        const auto two = onsetsOf (r.out, sr, (int) (1.7 * sr));
        bool onlyTwo = true;
        int seen = 0;
        for (const auto& o : two)
            if (o.sample < (int) (2.9 * sr)) { ++seen; onlyTwo = onlyTwo && stepOf (o.freq) <= 1; }
        const auto back = onsetsOf (r.out, sr, (int) (3.2 * sr));
        int high = 0;
        for (const auto& o : back)
            high += stepOf (o.freq) >= 2 ? 1 : 0;
        check ("Steps at 2 loops the first two", seen >= 5 && onlyTwo, stepsHeard (two, 0, 6));
        check ("Steps back at 8 brings the rest back", r.stepCount == 4 && high >= 2, stepsHeard (back, 0, 8));
    }
}

void exportPattern()
{
    std::printf ("export: the offline render is the live sequencer, cycle for cycle\n");
    const double sr = 48000.0;
    const auto in = burstInput (sr, 3.4, kFour);
    auto p = wetParams();

    BurstEngine engine;
    engine.prepare (sr, 128);
    juce::AudioBuffer<float> buf (2, 128);
    std::vector<float> live (in.size());
    for (size_t pos = 0; pos < in.size(); pos += 128)
    {
        const int n = (int) juce::jmin ((size_t) 128, in.size() - pos);
        for (int i = 0; i < n; ++i)
        {
            buf.setSample (0, i, in[pos + (size_t) i]);
            buf.setSample (1, i, in[pos + (size_t) i]);
        }
        juce::AudioBuffer<float> view (buf.getArrayOfWritePointers(), 2, n);
        engine.process (view, p);
        for (int i = 0; i < n; ++i)
            live[pos + (size_t) i] = buf.getSample (0, i);
    }

    BurstEngine::PatternCopy copy;
    const bool copied = engine.copyPattern (copy);
    check ("the pattern can be copied off the audio thread", copied && copy.steps.size() == 4,
           juce::String ((int) copy.steps.size()) + " steps, first " + juce::String (copy.steps.empty() ? 0 : (int) copy.steps[0].size()) + " samples");

    juce::AudioBuffer<float> rendered;
    BurstEngine::RenderSettings rs;
    rs.stepSeconds = p.stepSeconds;
    rs.length01 = p.length01;
    const int total = BurstEngine::renderPattern (copy, rs, rendered);
    check ("one cycle is steps times step", total == 4 * juce::roundToInt (0.2 * sr), juce::String (total) + " samples");

    std::vector<float> ren ((size_t) total);
    for (int i = 0; i < total; ++i)
        ren[(size_t) i] = rendered.getSample (0, i);
    const auto onR = onsetsOf (ren, sr);
    const auto onL = onsetsOf (live, sr, (int) (1.5 * sr));
    size_t k = 0;
    while (k < onL.size() && stepOf (onL[k].freq) != 0) ++k;
    double maxDiff = 1.0;
    if (! onR.empty() && k < onL.size())
    {
        const int d = onL[k].sample - onR[0].sample;
        maxDiff = 0.0;
        for (int i = 0; i < total && d + i < (int) live.size(); ++i)
            maxDiff = juce::jmax (maxDiff, (double) std::abs (live[(size_t) (d + i)] - ren[(size_t) i]));
    }
    check ("the render matches the live cycle sample for sample", onR.size() == 4 && maxDiff < 1.0e-5,
           juce::String ((int) onR.size()) + " onsets, largest difference " + juce::String (maxDiff, 7));
}

void deaf()
{
    std::printf ("deaf: bypassed, nothing is collected and the clock keeps its place\n");
    const double sr = 48000.0;
    const auto in = burstInput (sr, 3.5, { { 0.1, 0.1, 220.0, 0.5 }, { 1.0, 0.1, 440.0, 0.5 } });
    auto p = wetParams();
    int ticksAtBypass = 0, ticksAtEnd = 0;
    const auto r = runBurst (in, p, sr, 128, [&] (BurstEngine& e, BurstEngine::Params& q, double t) {
        q.bypass = t < 0.6 || t >= 2.0;
        if (t >= 2.0 && ticksAtBypass == 0) ticksAtBypass = e.uiTicks.load();
        ticksAtEnd = e.uiTicks.load();
    });
    const auto on = onsetsOf (r.out, sr, (int) (1.3 * sr));
    bool only440 = ! on.empty();
    for (const auto& o : on)
        only440 = only440 && near (o.freq, 440.0, 0.08);
    check ("a burst played while bypassed makes no step", r.stepCount == 1 && only440,
           juce::String (r.stepCount) + " step, " + stepsHeard (on, 0, 4));
    check ("the sequencer keeps ticking while bypassed", ticksAtEnd - ticksAtBypass >= 6,
           juce::String (ticksAtEnd - ticksAtBypass) + " ticks in the last 1.5 s");
}

void levels()
{
    std::printf ("levels: In feeds the gate, Blend is equal power, Out's floor is silence\n");
    const double sr = 48000.0;
    const auto quiet = burstInput (sr, 1.5, { { 0.1, 0.1, 220.0, 0.02 } });   // -34 dBFS, under a -30 dB gate
    auto p = wetParams();
    const auto a = runBurst (quiet, p, sr, 128);
    p.inputDb = 8.0f;
    const auto b = runBurst (quiet, p, sr, 128);
    check ("In lifts a quiet note over the threshold", a.stepCount == 0 && b.stepCount == 1,
           juce::String (a.stepCount) + " steps at 0 dB, " + juce::String (b.stepCount) + " at +8 dB");

    const auto in = burstInput (sr, 2.0, kFour);
    auto dry = wetParams();
    dry.blend01 = 0.0f;
    const auto d = runBurst (in, dry, sr, 128);
    double maxDiff = 0.0;
    for (size_t i = (size_t) (0.05 * sr); i < in.size(); ++i)
        maxDiff = juce::jmax (maxDiff, (double) std::abs (d.out[i] - in[i]));
    check ("Blend 0 passes the input untouched", maxDiff < 1.0e-6, "largest difference " + juce::String (maxDiff, 9));

    auto half = wetParams();
    half.blend01 = 0.5f;
    const auto h = runBurst (in, half, sr, 128);
    const double dryPart = peakOf (h.out, (int) (0.1 * sr), (int) (0.05 * sr));
    check ("Blend 50 % is -3 dB on the dry", std::abs (dbfs (dryPart / 0.5) + 3.0) < 0.2,
           juce::String (dbfs (dryPart / 0.5), 2) + " dB");

    auto floor = wetParams();
    floor.outDb = -60.0f;
    const auto f = runBurst (in, floor, sr, 128);
    check ("Out at the floor is silence", rmsOf (f.out, (int) (0.1 * sr), (int) (1.8 * sr)) == 0.0, "");
}

void cpu()
{
    std::printf ("cpu: cost per 128-sample block with a full pattern running\n");
    const double sr = 48000.0;
    std::vector<ToneBurst> sixteen;
    for (int i = 0; i < 16; ++i)
        sixteen.push_back ({ 0.1 + 0.3 * i, 0.15, 110.0 * (i + 1), 0.5 });
    const auto in = burstInput (sr, 15.0, sixteen);
    auto p = wetParams();
    p.maxSteps = 16;
    p.chaos01 = 0.5f;
    p.fade01 = 0.1f;
    const double t0 = juce::Time::getMillisecondCounterHiRes();
    const auto r = runBurst (in, p, sr, 128);
    const double us = (juce::Time::getMillisecondCounterHiRes() - t0) * 1000.0 / (in.size() / 128.0);
    check ("under 50 us per block", us < 50.0, juce::String (us, 2) + " us per block, " + juce::String (r.stepCount) + " steps");
}

void hostile()
{
    std::printf ("hostile: silence forever, and a signal far over full scale\n");
    const double sr = 48000.0;
    auto p = wetParams();
    const auto s = runBurst (std::vector<float> ((size_t) (3.0 * sr), 0.0f), p, sr, 128);
    check ("silence never makes a step", s.stepCount == 0 && rmsOf (s.out, 0, (int) s.out.size()) == 0.0, "");
    const auto loud = burstInput (sr, 2.0, { { 0.1, 0.1, 220.0, 1.0e6 } });
    const auto l = runBurst (loud, p, sr, 128);
    check ("a signal at +120 dB stays finite", allFinite (l.out) && l.stepCount == 1, juce::String (l.stepCount) + " step");
}


// Pitch resamples the material, not the clock: an octave up is twice the
// frequency in half the time, the ticks stay 200 ms apart, and the export
// agrees with the live sequencer.
void pitch()
{
    std::printf ("pitch: semitones on the material, the step clock untouched\n");
    const double sr = 48000.0;
    const auto in = burstInput (sr, 3.0, { { 0.1, 0.080, 220.0, 0.5 } });
    struct Case { float st; double freq; double heardMs; const char* name; };
    const Case cases[] = { { 12.0f, 440.0, 40.0, "+12 st is an octave up, twice as fast" },
                           { 7.0f, 329.63, 53.5, "+7 st is a fifth" },
                           { -12.0f, 110.0, 160.0, "-12 st is an octave down, cut by the step" } };
    for (const auto& c : cases)
    {
        auto p = wetParams();
        p.pitchSemitones = c.st;
        const auto r = runBurst (in, p, sr, 128);
        const auto on = onsetsOf (r.out, sr, (int) (1.0 * sr));
        bool ok = on.size() >= 4;
        double worstF = 0.0, heard = 0.0, worstGap = 0.0;
        juce::String durations;
        for (size_t k = 0; ok && k < on.size(); ++k)
        {
            worstF = juce::jmax (worstF, std::abs (on[k].freq - c.freq) / c.freq);
            // The first, not the last: the last may be cut by the end of the render.
            if (k == 0)
                heard = on[k].soundedFor / sr * 1000.0;
            durations += juce::String (on[k].soundedFor / sr * 1000.0, 0) + " ";
            if (k > 0)
                worstGap = juce::jmax (worstGap, std::abs ((double) (on[k].sample - on[k - 1].sample) - 0.2 * sr));
        }
        // -12 runs past the step, so the choke ends it: 200 ms less the 2 ms fade region.
        ok = ok && worstF < 0.03 && std::abs (heard - c.heardMs) < 8.0 && worstGap <= 2.0;
        check (c.name, ok, juce::String (on.empty() ? 0.0 : on[0].freq, 1) + " Hz for " + juce::String (heard, 1)
                                + " ms, ticks within " + juce::String (worstGap, 0) + " samples [" + durations.trim() + "]");
    }

    // Export at a pitch is the same voice.
    {
        auto p = wetParams();
        p.pitchSemitones = 5.0f;
        BurstEngine engine;
        engine.prepare (sr, 128);
        juce::AudioBuffer<float> buf (2, 128);
        for (size_t pos = 0; pos < in.size(); pos += 128)
        {
            const int n = (int) juce::jmin ((size_t) 128, in.size() - pos);
            for (int i = 0; i < n; ++i)
            {
                buf.setSample (0, i, in[pos + (size_t) i]);
                buf.setSample (1, i, in[pos + (size_t) i]);
            }
            juce::AudioBuffer<float> view (buf.getArrayOfWritePointers(), 2, n);
            engine.process (view, p);
        }
        BurstEngine::PatternCopy copy;
        juce::AudioBuffer<float> rendered;
        const bool copied = engine.copyPattern (copy);
        BurstEngine::RenderSettings rs;
        rs.stepSeconds = p.stepSeconds;
        rs.length01 = p.length01;
        rs.pitchSemitones = p.pitchSemitones;
        BurstEngine::renderPattern (copy, rs, rendered);
        std::vector<float> ren ((size_t) rendered.getNumSamples());
        for (int i = 0; i < rendered.getNumSamples(); ++i)
            ren[(size_t) i] = rendered.getSample (0, i);
        const auto on = onsetsOf (ren, sr);
        check ("the export is pitched the same way", copied && on.size() == 1 && near (on[0].freq, 293.66, 0.03),
               juce::String (on.empty() ? 0.0 : on[0].freq, 1) + " Hz");
    }
}


// Glue: off is bit-exact, on is harmonics without a level jump.
void glueTest()
{
    std::printf ("glue: the saturator at the end of the pattern's chain\n");
    const double sr = 48000.0;
    const auto in = burstInput (sr, 2.0, { { 0.1, 0.400, 330.0, 0.5 } });
    auto p = wetParams();
    const auto clean = runBurst (in, p, sr, 128);
    p.glue01 = 0.0f;
    const auto off = runBurst (in, p, sr, 128);
    check ("Glue at zero is bit-exact", fnvHash (clean.out) == fnvHash (off.out), juce::String (fnvHash (off.out)));

    auto g = wetParams();
    g.glue01 = 1.0f;
    const auto hot = runBurst (in, g, sr, 128);
    const int from = (int) (0.8 * sr), n = (int) (0.9 * sr);
    const double fund0 = goertzelAmp (clean.out, from, n, 330.0, sr), h3c = goertzelAmp (clean.out, from, n, 990.0, sr);
    const double fund1 = goertzelAmp (hot.out, from, n, 330.0, sr), h3 = goertzelAmp (hot.out, from, n, 990.0, sr);
    check ("Glue at full adds a third harmonic", h3 / juce::jmax (fund1, 1e-9) > 0.05 && h3 > h3c * 10.0,
           "3rd harmonic " + juce::String (dbfs (h3 / juce::jmax (fund1, 1e-9)), 1) + " dB under the fundamental (clean: "
               + juce::String (dbfs (h3c / juce::jmax (fund0, 1e-9)), 1) + ")");
    const double rmsC = rmsOf (clean.out, from, n), rmsH = rmsOf (hot.out, from, n);
    check ("and the level stays within 6 dB", std::abs (dbfs (rmsH) - dbfs (rmsC)) < 6.0,
           juce::String (dbfs (rmsC), 1) + " dBFS clean, " + juce::String (dbfs (rmsH), 1) + " glued");
    check ("and stays inside full scale", peakOf (hot.out, 0, (int) hot.out.size()) <= 1.0, juce::String (peakOf (hot.out, 0, (int) hot.out.size()), 3));
}

// Spread: alternate steps left and right; zero is exactly mono.
void spreadTest()
{
    std::printf ("spread: alternate steps sit left and right\n");
    const double sr = 48000.0;
    const auto in = burstInput (sr, 3.4, kFour);
    auto run = [&] (float spread) {
        BurstEngine engine;
        engine.prepare (sr, 128);
        auto p = wetParams();
        p.spread01 = spread;
        juce::AudioBuffer<float> buf (2, 128);
        std::vector<float> L (in.size()), R (in.size());
        for (size_t pos = 0; pos < in.size(); pos += 128)
        {
            const int n = (int) juce::jmin ((size_t) 128, in.size() - pos);
            for (int i = 0; i < n; ++i)
            {
                buf.setSample (0, i, in[pos + (size_t) i]);
                buf.setSample (1, i, in[pos + (size_t) i]);
            }
            juce::AudioBuffer<float> view (buf.getArrayOfWritePointers(), 2, n);
            engine.process (view, p);
            for (int i = 0; i < n; ++i)
            {
                L[pos + (size_t) i] = buf.getSample (0, i);
                R[pos + (size_t) i] = buf.getSample (1, i);
            }
        }
        return std::make_pair (L, R);
    };
    const auto mono = run (0.0f);
    double diff = 0.0;
    for (size_t i = 0; i < mono.first.size(); ++i)
        diff = juce::jmax (diff, (double) std::abs (mono.first[i] - mono.second[i]));
    check ("Spread at zero is mono", diff == 0.0, "largest L minus R " + juce::String (diff, 9));

    const auto wide = run (1.0f);
    const auto onL = onsetsOf (wide.first, sr, (int) (1.5 * sr));
    const auto onR = onsetsOf (wide.second, sr, (int) (1.5 * sr));
    bool leftEven = ! onL.empty(), rightOdd = ! onR.empty();
    for (const auto& o : onL) leftEven = leftEven && stepOf (o.freq) % 2 == 0;
    for (const auto& o : onR) rightOdd = rightOdd && stepOf (o.freq) % 2 == 1;
    check ("at full, steps 1 and 3 are left only", leftEven, stepsHeard (onL, 0, 6));
    check ("and steps 2 and 4 are right only", rightOdd, stepsHeard (onR, 0, 6));
}

struct Scenario { const char* name; void (*fn)(); };

const Scenario kScenarios[] = {
    { "burst", burst },       { "sync", sync },     { "direction", direction }, { "length", length },
    { "fade", fade },         { "fills", fills },   { "chaos", chaos },         { "ceiling", ceiling },
    { "export", exportPattern }, { "deaf", deaf },  { "levels", levels },       { "cpu", cpu },
    { "hostile", hostile },   { "pitch", pitch },     { "glue", glueTest },       { "spread", spreadTest },
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
