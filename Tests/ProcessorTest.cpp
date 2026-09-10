// State discipline harness. CLAUDE.md names this as the most common bug class
// across these projects: something that affects sound or appearance is neither
// an APVTS parameter nor stamped through stampExtraState, so it silently fails
// to save, and the user finds it rather than the build.
//
// Everything here is a round trip with numbers: set, save, restore, compare.
// The audio scenarios feed tone BURSTS with silence between them, because the
// engine is a gated step recorder: nothing plays until a gated event has
// closed, so a continuous tone would prove nothing.
//
//   build/ProcessorTest_artefacts/RelWithDebInfo/ProcessorTest
#include "../Source/PluginProcessor.h"
#include "../Source/dsp/TimeMap.h"
#include "../Source/state/FactoryPresets.h"
#include "../Source/state/Randomiser.h"

#include <juce_audio_formats/juce_audio_formats.h>
#include "../Source/ui/Theme.h"

#include <cstdio>
#include <functional>
#include <vector>

namespace
{

int failures = 0;

void check (const char* what, bool ok, const juce::String& detail)
{
    if (! ok)
        ++failures;
    std::printf ("  %-46s %s   %s\n", what, ok ? "PASS" : "FAIL", detail.toRawUTF8());
}

juce::Array<juce::RangedAudioParameter*> rangedParams (juce::AudioProcessor& p)
{
    juce::Array<juce::RangedAudioParameter*> out;
    for (auto* parameter : p.getParameters())
        if (auto* ranged = dynamic_cast<juce::RangedAudioParameter*> (parameter))
            out.add (ranged);
    return out;
}

bool isDiscrete (const juce::RangedAudioParameter* p)
{
    return dynamic_cast<const juce::AudioParameterBool*> (p) != nullptr
           || dynamic_cast<const juce::AudioParameterChoice*> (p) != nullptr
           || dynamic_cast<const juce::AudioParameterInt*> (p) != nullptr;
}

void setRaw (DybbukProcessor& p, const char* paramId, float rawValue)
{
    auto* parameter = p.apvts.getParameter (paramId);
    parameter->setValueNotifyingHost (parameter->convertTo0to1 (rawValue));
}

// A value that is definitely not the default, for every parameter. Discrete
// parameters get clean 0 or 1: AudioParameterBool stores whatever raw float it
// is handed (0.63 stays 0.63 in memory), while the state tree snaps it to the
// step, so a fractional bool would look like a round-trip failure when the
// behaviour is identical.
void scrambleAllParameters (juce::AudioProcessor& p)
{
    int i = 0;
    for (auto* ranged : rangedParams (p))
    {
        const float def = ranged->getDefaultValue();
        if (isDiscrete (ranged))
            ranged->setValueNotifyingHost (def < 0.5f ? 1.0f : 0.0f);
        else
            ranged->setValueNotifyingHost (def < 0.5f ? 0.6f + 0.03f * (float) (i % 5)
                                                      : 0.2f + 0.03f * (float) (i % 5));
        ++i;
    }
}

// --- the test signal ---------------------------------------------------------

constexpr double kRate = 48000.0;
constexpr int kBlock = 128;

struct Burst
{
    double t0, dur, freq, amp;
};

// The sum of the bursts at time t, with a 2 ms edge on each so the gate sees
// a pick and not a step function.
float burstSample (const std::vector<Burst>& bursts, double t)
{
    float v = 0.0f;
    for (const auto& b : bursts)
    {
        const double rel = t - b.t0;
        if (rel < 0.0 || rel >= b.dur)
            continue;
        const double edge = 0.002;
        const double env = juce::jmin (1.0, rel / edge, (b.dur - rel) / edge);
        v += (float) (b.amp * env * std::sin (juce::MathConstants<double>::twoPi * b.freq * rel));
    }
    return v;
}

// Drives the processor over `seconds` of the bursts, block by block, handing
// each processed block to `tap` with the time at its start. `right` scales
// the right input channel (0 for a left-only source).
void run (DybbukProcessor& p, double seconds, const std::vector<Burst>& bursts,
          const std::function<void (double, const juce::AudioBuffer<float>&)>& tap,
          double startTime = 0.0, float right = 1.0f)
{
    juce::AudioBuffer<float> buffer (2, kBlock);
    juce::MidiBuffer midi;
    const int blocks = (int) std::ceil (seconds * kRate / kBlock);
    for (int b = 0; b < blocks; ++b)
    {
        const double t0 = startTime + b * kBlock / kRate;
        for (int i = 0; i < kBlock; ++i)
        {
            const float v = burstSample (bursts, t0 + i / kRate);
            buffer.setSample (0, i, v);
            buffer.setSample (1, i, v * right);
        }
        p.processBlock (buffer, midi);
        if (tap)
            tap (t0, buffer);
    }
}

// What came out: peak, RMS over a window, and whether it stayed finite.
struct Meter
{
    double windowStart = 0.0, windowEnd = 1.0e9;
    float peak = 0.0f;
    double sum = 0.0;
    int counted = 0;
    bool finite = true;

    void operator() (double t0, const juce::AudioBuffer<float>& buffer)
    {
        for (int i = 0; i < buffer.getNumSamples(); ++i)
        {
            const float y = buffer.getSample (0, i);
            if (! std::isfinite (y))
                finite = false;
            peak = juce::jmax (peak, std::abs (y));
            const double t = t0 + i / kRate;
            if (t >= windowStart && t < windowEnd)
            {
                sum += (double) y * y;
                ++counted;
            }
        }
    }

    double rmsDb() const
    {
        return 20.0 * std::log10 (juce::jmax (std::sqrt (sum / juce::jmax (1, counted)), 1.0e-12));
    }
};

// Four picks, one every 400 ms, each 100 ms long: the phrase every audio
// scenario plays. Well over the -30 dB default threshold, well separated so
// the gate closes between them.
const std::vector<Burst> kPhrase = { { 0.10, 0.100, 220.0, 0.5 }, { 0.50, 0.100, 330.0, 0.5 },
                                     { 0.90, 0.100, 277.2, 0.5 }, { 1.30, 0.100, 440.0, 0.5 } };

// --- scenarios ----------------------------------------------------------------

void sessionRoundTrip()
{
    std::printf ("session round trip: every parameter and the editor properties\n");

    DybbukProcessor a;
    a.prepareToPlay (kRate, kBlock);
    scrambleAllParameters (a);
    a.apvts.state.setProperty (theme::kThemeProperty, 1, nullptr);
    a.apvts.state.setProperty ("uiScale", 1.25f, nullptr);

    juce::MemoryBlock blob;
    a.getStateInformation (blob);

    DybbukProcessor b;
    b.prepareToPlay (kRate, kBlock);
    b.setStateInformation (blob.getData(), (int) blob.getSize());

    int mismatches = 0;
    juce::String worst;
    const auto pa = rangedParams (a);
    const auto pb = rangedParams (b);
    for (int i = 0; i < pa.size(); ++i)
    {
        const float va = pa[i]->getValue(), vb = pb[i]->getValue();
        const bool same = isDiscrete (pa[i])
                              ? pa[i]->getText (va, 32) == pb[i]->getText (vb, 32)
                              : std::abs (va - vb) <= 1.0e-5f;
        if (! same)
        {
            ++mismatches;
            worst = pa[i]->paramID + " " + juce::String (va, 4) + " vs " + juce::String (vb, 4);
        }
    }

    check ("all parameters restored", mismatches == 0,
           juce::String (pa.size()) + " parameters, " + juce::String (mismatches) + " wrong "
               + worst);
    check ("theme restored from the session", (int) b.apvts.state.getProperty (theme::kThemeProperty, -1) == 1,
           "theme = " + b.apvts.state.getProperty (theme::kThemeProperty, -1).toString());
    check ("window scale restored", std::abs ((float) b.apvts.state.getProperty ("uiScale", 0.0f) - 1.25f) < 1.0e-5f,
           "uiScale = " + b.apvts.state.getProperty ("uiScale", 0.0f).toString());
    check ("state version stamped", (int) b.apvts.state.getProperty ("stateVersion", 0) >= 3,
           "stateVersion = " + b.apvts.state.getProperty ("stateVersion", 0).toString());
}

void presetRoundTrip()
{
    std::printf ("user preset round trip: saved values return, appearance does not travel\n");

    DybbukProcessor p;
    p.prepareToPlay (kRate, kBlock);

    const juce::String name ("zz probe temp");
    auto file = p.presetManager.userFolder().getChildFile (juce::File::createLegalFileName (name) + ".preset");
    file.deleteFile();

    scrambleAllParameters (p);
    p.apvts.state.setProperty (theme::kThemeProperty, 1, nullptr);

    juce::Array<float> saved;
    for (auto* ranged : rangedParams (p))
        saved.add (ranged->getValue());

    check ("preset saved", p.presetManager.saveCurrent (name), file.getFullPathName());

    const auto text = file.loadFileAsString();
    check ("preset file does not carry the theme", ! text.contains (theme::kThemeProperty),
           juce::String (text.length()) + " bytes");

    // Move everything, and flip both the theme and bypass, then load it back.
    for (auto* ranged : rangedParams (p))
        ranged->setValueNotifyingHost (ranged->getDefaultValue());
    p.apvts.state.setProperty (theme::kThemeProperty, 0, nullptr);
    auto* bypass = p.apvts.getParameter (params::id::bypass);
    bypass->setValueNotifyingHost (1.0f);

    bool loaded = false;
    for (const auto& info : p.presetManager.getPresets())
        if (info.name == name)
            loaded = p.presetManager.loadPreset (info);
    check ("preset loaded", loaded, name);

    int mismatches = 0;
    const auto after = rangedParams (p);
    for (int i = 0; i < after.size(); ++i)
    {
        if (params::performanceParams().contains (after[i]->paramID))
            continue;
        const bool same = isDiscrete (after[i])
                              ? after[i]->getText (after[i]->getValue(), 32) == after[i]->getText (saved[i], 32)
                              : std::abs (after[i]->getValue() - saved[i]) <= 1.0e-5f;
        if (! same)
            ++mismatches;
    }
    check ("preset restored every sound parameter", mismatches == 0,
           juce::String (mismatches) + " wrong");

    check ("preset did not change the theme", (int) p.apvts.state.getProperty (theme::kThemeProperty, -1) == 0,
           "theme = " + p.apvts.state.getProperty (theme::kThemeProperty, -1).toString());
    check ("preset did not take the plugin back into circuit", bypass->getValue() > 0.5f,
           "bypass = " + juce::String (bypass->getValue(), 2));
    check ("preset is not dirty right after loading", ! p.presetManager.isDirty(), "");

    file.deleteFile();
}

void factoryPresetsLoad()
{
    std::printf ("factory presets: every table applies and reads back\n");

    DybbukProcessor p;
    p.prepareToPlay (kRate, kBlock);

    for (int i = 0; i < numFactoryPresets(); ++i)
    {
        const auto& fp = factoryPreset (i);
        bool found = false;
        for (const auto& info : p.presetManager.getPresets())
            if (info.factory && info.name == fp.name)
                found = p.presetManager.loadPreset (info);

        int wrong = 0;
        juce::String detail;
        for (int v = 0; v < fp.numValues; ++v)
        {
            auto* parameter = p.apvts.getParameter (fp.values[v].id);
            if (parameter == nullptr)
            {
                ++wrong;
                detail = juce::String (fp.values[v].id) + " missing";
                continue;
            }
            const float actual = parameter->convertFrom0to1 (parameter->getValue());
            const float wanted = fp.values[v].value;
            if (std::abs (actual - wanted) > juce::jmax (0.01f, std::abs (wanted) * 0.01f))
            {
                ++wrong;
                detail = juce::String (fp.values[v].id) + " " + juce::String (actual, 3)
                         + " wanted " + juce::String (wanted, 3);
            }
        }

        check ("factory preset applies", found && wrong == 0,
               juce::String (fp.name) + ": " + juce::String (fp.numValues) + " values, "
                   + juce::String (wrong) + " wrong " + detail);
    }
}

// A preset table is just numbers until you hear it. Each preset is played the
// phrase, then left alone: the pattern it made must still be sounding, and
// bounded, a second and a half after the last pick.
void presetsMakeSound()
{
    std::printf ("factory presets: each one captures the phrase and plays it back\n");

    for (int i = 0; i < numFactoryPresets(); ++i)
    {
        DybbukProcessor p;
        p.prepareToPlay (kRate, kBlock);

        const auto& fp = factoryPreset (i);
        for (const auto& info : p.presetManager.getPresets())
            if (info.factory && info.name == fp.name)
                p.presetManager.loadPreset (info);

        Meter m;
        m.windowStart = 1.8;
        m.windowEnd = 3.0;
        run (p, 3.0, kPhrase, std::ref (m));

        check ("preset captures and plays",
               m.finite && m.peak < 1.0f && p.getStepCount() >= 2 && m.rmsDb() > -50.0,
               juce::String (fp.name) + ": " + juce::String (p.getStepCount()) + " steps, pattern "
                   + juce::String (m.rmsDb(), 1) + " dBFS after the phrase, peak " + juce::String (m.peak, 3));
    }
}

// A mono track feeding a stereo effect is the common Ableton case, and it must
// not leave one side silent or stale.
void monoToStereo()
{
    std::printf ("mono to stereo: a mono input fills both outputs\n");

    DybbukProcessor p;

    juce::AudioProcessor::BusesLayout layout;
    layout.inputBuses.add (juce::AudioChannelSet::mono());
    layout.outputBuses.add (juce::AudioChannelSet::stereo());

    check ("mono in, stereo out is offered", p.checkBusesLayoutSupported (layout), "");
    check ("and can be applied", p.setBusesLayout (layout), "");

    p.prepareToPlay (kRate, kBlock);
    setRaw (p, params::id::blend, 70.0f);

    float peakL = 0.0f, peakR = 0.0f, biggestDifference = 0.0f;
    // Only the mono input channel is written (right = 0); the processor must
    // copy it across before the engine sees it.
    run (p, 2.5, kPhrase, [&] (double t0, const juce::AudioBuffer<float>& buffer)
    {
        for (int i = 0; i < kBlock; ++i)
        {
            const double t = t0 + i / kRate;
            if (t > 1.6) // the pattern alone
            {
                peakL = juce::jmax (peakL, std::abs (buffer.getSample (0, i)));
                peakR = juce::jmax (peakR, std::abs (buffer.getSample (1, i)));
            }
            biggestDifference = juce::jmax (biggestDifference,
                                            std::abs (buffer.getSample (0, i) - buffer.getSample (1, i)));
        }
    }, 0.0, 0.0f);

    check ("both outputs carry the pattern", peakL > 0.05f && peakR > 0.05f,
           "L peak " + juce::String (peakL, 3) + ", R peak " + juce::String (peakR, 3));
    check ("and they match", biggestDifference < 1.0e-6f,
           "largest L minus R is " + juce::String (biggestDifference, 9));
}

// A stereo source must come out stereo. The pattern is mono (the gate hears a
// sum, the steps are one channel), but there is no reason for the dry signal
// to lose its image just by passing through the plugin.
void stereoDry()
{
    std::printf ("stereo: a stereo source keeps its image through the plate\n");

    DybbukProcessor p;
    p.prepareToPlay (kRate, kBlock);
    setRaw (p, params::id::blend, 0.0f); // dry only

    float peakL = 0.0f, peakR = 0.0f;
    run (p, 0.6, { { 0.10, 0.300, 220.0, 0.4 } }, [&] (double, const juce::AudioBuffer<float>& buffer)
    {
        peakL = juce::jmax (peakL, buffer.getMagnitude (0, 0, kBlock));
        peakR = juce::jmax (peakR, buffer.getMagnitude (1, 0, kBlock));
    }, 0.0, 0.0f);

    check ("the left channel carries the left input", peakL > 0.3f,
           "left peak " + juce::String (peakL, 3));
    check ("and it does not leak into the right", peakR < 0.02f,
           "right peak " + juce::String (peakR, 4) + " (summing to mono would give "
               + juce::String (peakL * 0.5f, 3) + ")");

    // Wet only: the pattern itself is mono by design, so both sides carry it.
    setRaw (p, params::id::blend, 100.0f);
    float wetL = 0.0f, wetR = 0.0f;
    run (p, 1.5, { { 0.10, 0.100, 220.0, 0.4 } }, [&] (double t0, const juce::AudioBuffer<float>& buffer)
    {
        if (t0 > 0.4)
        {
            wetL = juce::jmax (wetL, buffer.getMagnitude (0, 0, kBlock));
            wetR = juce::jmax (wetR, buffer.getMagnitude (1, 0, kBlock));
        }
    }, 0.0, 0.0f);
    check ("the pattern itself is on both sides",
           wetL > 0.02f && std::abs (wetL - wetR) < 0.02f,
           "wet peaks " + juce::String (wetL, 3) + " / " + juce::String (wetR, 3));
}

// Bypass: the engine goes deaf rather than off. A pick played while out of
// circuit must not become a step; the pattern must survive the trip; and the
// host hears a crossfade, never a cut.
void bypassAndAudio()
{
    std::printf ("bypass: deaf while out of circuit, crossfades in and out, pattern survives\n");

    DybbukProcessor p;
    p.prepareToPlay (kRate, kBlock);
    setRaw (p, params::id::blend, 100.0f); // full wet, so what comes through is the engine

    // Out of circuit from the start.
    setRaw (p, params::id::bypass, 1.0f);
    run (p, 0.1, {}, nullptr);
    Meter bypassed;
    run (p, 0.8, { { 0.10, 0.100, 220.0, 0.5 } }, std::ref (bypassed), 0.0);
    check ("a pick played while bypassed makes no step", p.getStepCount() == 0,
           juce::String (p.getStepCount()) + " steps");
    check ("bypassed output is the dry input", bypassed.peak > 0.45f && bypassed.peak < 0.55f,
           "peak " + juce::String (bypassed.peak, 3) + " for a 0.5 pick");

    // Back in: the phrase makes a pattern.
    setRaw (p, params::id::bypass, 0.0f);
    run (p, 0.1, {}, nullptr);
    run (p, 1.8, kPhrase, nullptr);
    const int stepsBefore = p.getStepCount();
    Meter playing;
    run (p, 0.5, {}, std::ref (playing));
    check ("in circuit, the phrase becomes a pattern", stepsBefore >= 2 && playing.peak > 0.05f,
           juce::String (stepsBefore) + " steps, playing at peak " + juce::String (playing.peak, 3));

    // Out again while it plays: the largest sample-to-sample jump across the
    // fade has to be what the material itself would do (a 220 Hz sine at 0.5
    // moves 0.014 per sample), not the 0.5 a hard switch would produce.
    float biggestJump = 0.0f, last = 0.0f;
    bool primed = false;
    auto jumps = [&] (double, const juce::AudioBuffer<float>& buffer)
    {
        for (int i = 0; i < kBlock; ++i)
        {
            const float y = buffer.getSample (0, i);
            if (primed)
                biggestJump = juce::jmax (biggestJump, std::abs (y - last));
            last = y;
            primed = true;
        }
    };
    run (p, 0.05, {}, jumps);
    setRaw (p, params::id::bypass, 1.0f);
    run (p, 0.1, {}, jumps);
    Meter settled;
    run (p, 0.5, {}, std::ref (settled));
    check ("bypass fades rather than cutting", biggestJump < 0.06f,
           "largest jump " + juce::String (biggestJump, 4));
    check ("bypassed output follows the (silent) dry input", settled.peak < 0.002f,
           "peak " + juce::String (settled.peak, 6) + " after the fade");

    // And back in: the pattern is still there.
    biggestJump = 0.0f;
    setRaw (p, params::id::bypass, 0.0f);
    run (p, 0.1, {}, jumps);
    Meter after;
    run (p, 1.5, {}, std::ref (after));
    check ("pattern survived the trip out of circuit",
           p.getStepCount() == stepsBefore && after.peak > 0.05f,
           juce::String (p.getStepCount()) + " steps, peak " + juce::String (after.peak, 3));
    check ("and the way back in does not click either", biggestJump < 0.06f,
           "largest jump " + juce::String (biggestJump, 4));
    check ("output finite throughout", bypassed.finite && playing.finite && settled.finite && after.finite, "");
}

void readouts()
{
    std::printf ("readouts: what the host and Push will show\n");

    DybbukProcessor p;
    struct Expect { const char* id; float value; const char* text; };
    const Expect cases[] = {
        { params::id::step, 0.0f, "20 ms" },
        { params::id::step, 0.5f, "0.20 s" },
        { params::id::step, params::knob01ForStepSeconds (0.250), "0.25 s" },
        { params::id::step, 1.0f, "2.00 s" },
        { params::id::steps, 8.0f, "8" },
        { params::id::steps, 16.0f, "16" },
        { params::id::threshold, -30.0f, "-30.0" },
        { params::id::blend, 50.0f, "50" },
        { params::id::freeze, 1.0f, "Frozen" },
        { params::id::freeze, 0.0f, "Off" },
        { params::id::fills, 0.0f, "Off" },
        { params::id::fills, 30.0f, "30 %" },
        { params::id::chaos, 0.0f, "Still" },
        { params::id::chaos, 40.0f, "40 %" },
        { params::id::direction, 2.0f, "Pendulum" },
        { params::id::direction, 4.0f, "Drunk" },
        { params::id::length, 100.0f, "100" },
        { params::id::length, 5.0f, "5" },
        { params::id::fade, 0.0f, "Never" },
        { params::id::fade, 35.0f, "35 %" },
        { params::id::full, 0.0f, "Replace" },
        { params::id::full, 1.0f, "Hold" },
        { params::id::stepsync, 0.0f, "Free" },
        { params::id::stepsync, 1.0f, "Sync" },
        { params::id::input, 0.0f, "0.0" },
        { params::id::input, -60.0f, "-Inf" },
        { params::id::out, -60.0f, "-Inf" },
        { params::id::out, -6.0f, "-6.0" },
    };

    for (const auto& c : cases)
    {
        auto* parameter = dynamic_cast<juce::RangedAudioParameter*> (p.apvts.getParameter (c.id));
        const auto text = parameter->getText (parameter->convertTo0to1 (c.value), 32);
        check ("readout", text == c.text,
               juce::String (c.id) + " at " + juce::String (c.value, 2) + " reads \"" + text
                   + "\" (want \"" + c.text + "\")");
    }

    // The default Step must read as the quarter second it was chosen to be.
    {
        auto* step = dynamic_cast<juce::RangedAudioParameter*> (p.apvts.getParameter (params::id::step));
        const auto text = step->getText (step->getDefaultValue(), 32);
        check ("default Step reads 0.25 s", text == "0.25 s", "\"" + text + "\"");
    }

    // Synced, the same knob reads as a division: Push shows the host's string,
    // so a synced Step must never read "0.31 s" there.
    {
        setRaw (p, params::id::stepsync, 1.0f);
        auto* step = dynamic_cast<juce::RangedAudioParameter*> (p.apvts.getParameter (params::id::step));
        struct Div { float knob; const char* name; };
        const Div divs[] = { { 0.0f, "1/32" }, { 2.0f / 13.0f, "1/16" }, { 8.0f / 13.0f, "1/4" }, { 1.0f, "1 bar" } };
        for (const auto& d : divs)
        {
            const auto text = step->getText (d.knob, 32);
            check ("synced Step reads a division", text == d.name,
                   "knob " + juce::String (d.knob, 3) + " reads \"" + text + "\" (want \"" + d.name + "\")");
        }
        setRaw (p, params::id::stepsync, 0.0f);
    }

    // The shared helper the editor's readout strip will use.
    check ("stepReadout below 100 ms is integer ms", params::stepReadout (0.0453) == "45 ms",
           params::stepReadout (0.0453));
    check ("stepReadout from 100 ms up is seconds", params::stepReadout (0.6584) == "0.66 s",
           params::stepReadout (0.6584));

    // No raw float may ever leak into a readout: this is the juce::String(v, 0)
    // trap that CLAUDE.md warns about.
    for (auto* ranged : rangedParams (p))
    {
        bool longNumber = false;
        juce::String offender;
        for (float norm : { 0.0f, 0.137f, 0.5f, 0.831f, 1.0f })
        {
            const auto text = ranged->getText (norm, 32);
            const int dot = text.indexOfChar ('.');
            if (dot < 0)
                continue;

            int digits = 0; // only the digits, so " s" does not count
            for (int i = dot + 1; i < text.length() && juce::CharacterFunctions::isDigit (text[i]); ++i)
                ++digits;
            if (digits > 3)
            {
                longNumber = true;
                offender = text;
            }
        }
        check ("no raw floats in the readout", ! longNumber, ranged->paramID + " " + offender);
    }

    // The knob map must be its own inverse, or a preset written in seconds
    // lands on a different time than it names.
    {
        double worst = 0.0;
        for (double s : { 0.02, 0.06, 0.18, 0.25, 0.5, 1.0, 2.0 })
            worst = juce::jmax (worst, std::abs (params::stepSecondsForKnob01 (params::knob01ForStepSeconds (s)) - s));
        check ("step knob map round-trips", worst < 1.0e-4, "worst error " + juce::String (worst, 6) + " s");
    }
}

// The dice. A random button is where "keep it musical" is hardest to keep: a
// uniform roll over the parameters produces something unusable almost every
// time. So this is not a range check. It rolls every character many times,
// plays each patch the phrase, and requires that the gate heard it, that the
// result is audible and bounded, and that the rolls differ.
void diceIsMusical()
{
    std::printf ("dice: every character hears the phrase, makes sound, stays bounded, and touches nothing it must not\n");

    for (int character = 0; character < Randomiser::numCharacters(); ++character)
    {
        int silent = 0, tooLoud = 0, nonFinite = 0, deaf = 0, rolls = 12;
        double quietest = 1.0e9, loudest = -1.0e9;
        juce::String name;

        for (int roll = 0; roll < rolls; ++roll)
        {
            DybbukProcessor p;
            p.prepareToPlay (kRate, kBlock);

            juce::Random rng ((juce::int64) (character * 1000 + roll + 1));
            Randomiser::randomiseCharacter (p.apvts, rng, character);
            name = Randomiser::lastCharacterName();

            // The phrase twice over, then a second and a half of the pattern
            // alone. The level is measured from the second pick on, while
            // playing, because that is what the player hears.
            std::vector<Burst> phrase = kPhrase;
            for (const auto& b : kPhrase)
                phrase.push_back ({ b.t0 + 1.6, b.dur, b.freq * 1.5, b.amp });

            // The step count is watched as it goes rather than read at the
            // end: a rolled Fade can legitimately empty a short pattern
            // within the run, and that is erosion, not deafness.
            Meter m;
            m.windowStart = 0.5;
            m.windowEnd = 4.8;
            int mostSteps = 0;
            run (p, 4.8, phrase, [&] (double t0, const juce::AudioBuffer<float>& buffer)
            {
                m (t0, buffer);
                mostSteps = juce::jmax (mostSteps, p.getStepCount());
            });

            if (mostSteps == 0)
                ++deaf;
            if (! m.finite)
                ++nonFinite;
            if (m.peak > 1.0f)
                ++tooLoud;
            const double rms = m.rmsDb();
            quietest = juce::jmin (quietest, rms);
            loudest = juce::jmax (loudest, rms);
            if (rms < -50.0)
                ++silent;
        }

        check ("every roll hears the phrase", deaf == 0,
               name + ": " + juce::String (deaf) + " of " + juce::String (rolls) + " rolls captured nothing");
        check ("every roll makes sound", silent == 0,
               name + ": " + juce::String (silent) + " of " + juce::String (rolls)
                   + " rolls were inaudible (quietest " + juce::String (quietest, 1) + " dBFS)");
        check ("every roll stays inside full scale", tooLoud == 0,
               name + ": " + juce::String (tooLoud) + " of " + juce::String (rolls) + " over");
        check ("every roll is finite", nonFinite == 0, name);
        check ("and they are not all the same level", loudest - quietest > 1.0,
               name + ": " + juce::String (quietest, 1) + " to " + juce::String (loudest, 1) + " dBFS");
    }

    // What the dice must never touch. Bypass is performance state, In and
    // Out are the two controls that can hurt someone wearing headphones, Sync
    // is a workflow choice, and Freeze is the player's hand on the pattern.
    DybbukProcessor p;
    auto* bypass = p.apvts.getParameter (params::id::bypass);
    auto* in = p.apvts.getParameter (params::id::input);
    auto* out = p.apvts.getParameter (params::id::out);
    auto* sync = p.apvts.getParameter (params::id::stepsync);
    auto* record = p.apvts.getParameter (params::id::freeze);

    bypass->setValueNotifyingHost (1.0f);
    in->setValueNotifyingHost (0.2f);
    out->setValueNotifyingHost (0.8f);
    sync->setValueNotifyingHost (1.0f);
    record->setValueNotifyingHost (1.0f);

    const float bypassWas = bypass->getValue(), inWas = in->getValue();
    const float outWas = out->getValue(), syncWas = sync->getValue(), recordWas = record->getValue();

    for (int i = 0; i < 40; ++i)
        p.randomiseParameters();

    // Exactly equal, not nearly: "the dice did not touch this" is a claim about
    // the bits, and juce::exactlyEqual is how you say so without the compiler
    // assuming you meant a tolerance.
    check ("the dice never takes the plugin out of circuit",
           juce::exactlyEqual (bypass->getValue(), bypassWas),
           "Bypass " + juce::String (bypassWas) + " -> " + juce::String (bypass->getValue()));
    check ("and never touches the level controls",
           juce::exactlyEqual (in->getValue(), inWas)
               && juce::exactlyEqual (out->getValue(), outWas),
           "In " + juce::String (inWas, 3) + " -> " + juce::String (in->getValue(), 3) + ", Out "
               + juce::String (outWas, 3) + " -> " + juce::String (out->getValue(), 3));
    check ("and leaves Sync alone", juce::exactlyEqual (sync->getValue(), syncWas),
           "Sync " + juce::String (syncWas));
    check ("and leaves Freeze alone", juce::exactlyEqual (record->getValue(), recordWas),
           "Freeze " + juce::String (recordWas));

    // And it has to actually roll something different each time, or it is a
    // preset button with a dice on it.
    DybbukProcessor q;
    q.randomiseParameters();
    juce::MemoryBlock first;
    q.getStateInformation (first);
    int identical = 0;
    for (int i = 0; i < 20; ++i)
    {
        q.randomiseParameters();
        juce::MemoryBlock next;
        q.getStateInformation (next);
        if (next == first)
            ++identical;
    }
    check ("consecutive rolls differ", identical == 0,
           juce::String (identical) + " of 20 rolls repeated the first");
}

// Not a pass/fail: renders one roll of each character, so the dice can be
// listened to rather than only measured. `ProcessorTest render`.
void renderDice()
{
    std::printf ("render: one roll of each dice character, to the working directory\n");

    auto pluck = [] (double t, double f0)
    {
        const double env = std::exp (-9.0 * t);
        double v = 0.0;
        for (int h = 1; h <= 6; ++h)
            v += std::sin (juce::MathConstants<double>::twoPi * f0 * h * t + 0.3 * h) / (h * h);
        return 0.45 * env * v;
    };

    for (int c = 0; c < Randomiser::numCharacters(); ++c)
    {
        DybbukProcessor p;
        p.prepareToPlay (kRate, kBlock);
        juce::Random rng ((juce::int64) (c * 7919 + 11));
        Randomiser::randomiseCharacter (p.apvts, rng, c);
        const juce::String name (Randomiser::lastCharacterName());

        const int total = (int) (14.0 * kRate);
        juce::AudioBuffer<float> file (2, total), buffer (2, kBlock);
        juce::MidiBuffer midi;
        const double notes[] = { 110.0, 146.83, 196.0, 164.81, 220.0, 130.81 };

        // Six muted plucks, one every 0.7 s, then eight seconds of the pattern
        // playing itself with nothing else played into it.
        int pos = 0;
        while (pos < total)
        {
            const int len = juce::jmin (kBlock, total - pos);
            for (int i = 0; i < len; ++i)
            {
                const double t = (double) (pos + i) / kRate;
                float v = 0.0f;
                if (t < 4.2)
                {
                    const int n = (int) (t / 0.7);
                    const double rel = t - n * 0.7;
                    if (rel < 0.25)
                        v = (float) pluck (rel, notes[juce::jlimit (0, 5, n)]);
                }
                buffer.setSample (0, i, v);
                buffer.setSample (1, i, v);
            }
            p.processBlock (buffer, midi);
            for (int ch = 0; ch < 2; ++ch)
                file.copyFrom (ch, pos, buffer, ch, 0, len);
            pos += len;
        }

        const auto out = juce::File::getCurrentWorkingDirectory()
                             .getChildFile ("dice_" + name.toLowerCase() + ".wav");
        out.deleteFile();
        juce::WavAudioFormat wav;
        std::unique_ptr<juce::OutputStream> stream = out.createOutputStream();
        if (stream != nullptr)
        {
            const auto options = juce::AudioFormatWriterOptions().withSampleRate (kRate)
                                     .withNumChannels (2).withBitsPerSample (24);
            if (auto writer = wav.createWriterFor (stream, options))
                writer->writeFromAudioSampleBuffer (file, 0, total);
        }
        std::printf ("  wrote %s (%d steps, peak %.3f)\n", out.getFileName().toRawUTF8(),
                     p.getStepCount(), file.getMagnitude (0, total));
    }
}

// Export: a three-step pattern rendered to a WAV must be exactly three steps
// long, audible, and named for what it is.
void exportPattern()
{
    std::printf ("export: the pattern goes to disk as one pass of itself\n");

    DybbukProcessor p;
    p.prepareToPlay (kRate, kBlock);
    setRaw (p, params::id::step, params::knob01ForStepSeconds (0.200));
    check ("nothing to export before a pattern exists", ! p.canExportPattern()
                                                            && p.renderPatternToFile() == juce::File(),
           "");

    run (p, 1.6, { { 0.10, 0.100, 220.0, 0.5 }, { 0.50, 0.100, 330.0, 0.5 }, { 0.90, 0.100, 440.0, 0.5 } },
         nullptr);
    check ("three picks made three steps", p.getStepCount() == 3 && p.canExportPattern(),
           juce::String (p.getStepCount()) + " steps");

    const auto dest = juce::File::getSpecialLocation (juce::File::tempDirectory)
                          .getChildFile ("dybbuk_export_test.wav");
    dest.deleteFile();
    check ("writePatternWav succeeds", p.writePatternWav (dest), dest.getFullPathName());

    juce::WavAudioFormat wav;
    std::unique_ptr<juce::AudioFormatReader> reader (wav.createReaderFor (dest.createInputStream().release(), true));
    const int stepSamples = juce::roundToInt (0.200 * kRate);
    if (reader != nullptr)
    {
        const auto length = (int) reader->lengthInSamples;
        juce::AudioBuffer<float> back (2, juce::jmax (1, length));
        reader->read (&back, 0, length, 0, true, true);
        check ("the file is exactly three steps long", std::abs (length - 3 * stepSamples) <= 2,
               juce::String (length) + " samples (want " + juce::String (3 * stepSamples) + ")");
        check ("stereo 32-bit float at the engine's rate",
               reader->numChannels == 2 && reader->bitsPerSample == 32 && reader->usesFloatingPointData
                   && std::abs (reader->sampleRate - kRate) < 1.0,
               juce::String ((int) reader->numChannels) + " ch, " + juce::String ((int) reader->bitsPerSample)
                   + " bit, " + juce::String (juce::roundToInt (reader->sampleRate)) + " Hz");
        check ("and it is not silent", back.getMagnitude (0, length) > 0.05f,
               "peak " + juce::String (back.getMagnitude (0, length), 3));
    }
    else
    {
        check ("the file reads back", false, dest.getFullPathName());
    }
    dest.deleteFile();

    const auto name = p.exportFileName();
    check ("the name says what it is", name.contains ("3 steps") && name.contains ("200 ms") && name.endsWith (".wav"),
           name);

    setRaw (p, params::id::stepsync, 1.0f);
    const auto syncedName = p.exportFileName();
    check ("synced, the name says the division and the tempo",
           syncedName.contains ("3 steps") && syncedName.contains ("bpm") && ! syncedName.contains ("/"),
           syncedName);
    setRaw (p, params::id::stepsync, 0.0f);

    const auto written = p.renderPatternToFile();
    check ("renderPatternToFile lands in the export folder",
           written.existsAsFile() && written.getFileName().contains ("3 steps")
               && written.getParentDirectory() == DybbukProcessor::exportFolder(),
           written.getFullPathName());
    written.deleteFile();
}

void ordering()
{
    std::printf ("parameter order: Push bank 1 is the eight that matter\n");

    DybbukProcessor p;
    const char* wanted[] = { params::id::step, params::id::steps, params::id::threshold,
                             params::id::blend, params::id::freeze, params::id::fills,
                             params::id::chaos, params::id::direction };

    const auto all = rangedParams (p);
    bool ok = all.size() >= 8;
    juce::String order;
    for (int i = 0; i < 8 && i < all.size(); ++i)
    {
        order += all[i]->paramID + " ";
        if (all[i]->paramID != wanted[i])
            ok = false;
    }
    check ("first eight parameters", ok, order.trim());
    check ("fifteen parameters in all", all.size() == 15, juce::String (all.size()));
    check ("bypass is declared last", all.size() > 0 && all[all.size() - 1]->paramID == params::id::bypass,
           all.size() > 0 ? all[all.size() - 1]->paramID : juce::String ("none"));
    check ("bypass is the host bypass parameter", p.getBypassParameter() != nullptr
                                                      && p.getBypassParameter()->getName (16) == "Bypass",
           p.getBypassParameter() != nullptr ? p.getBypassParameter()->getName (16) : juce::String ("none"));
}

} // namespace

int main (int argc, char* argv[])
{
    if (argc > 1 && juce::String (argv[1]) == "render")
    {
        juce::ScopedJuceInitialiser_GUI init;
        renderDice();
        return 0;
    }

    juce::ScopedJuceInitialiser_GUI juceInit;

    ordering();
    readouts();
    sessionRoundTrip();
    presetRoundTrip();
    factoryPresetsLoad();
    presetsMakeSound();
    diceIsMusical();
    monoToStereo();
    stereoDry();
    bypassAndAudio();
    exportPattern();

    std::printf ("\n%d failures\n", failures);
    return failures == 0 ? 0 : 1;
}
