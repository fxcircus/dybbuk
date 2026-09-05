// State discipline harness. CLAUDE.md names this as the most common bug class
// across these projects: something that affects sound or appearance is neither
// an APVTS parameter nor stamped through stampExtraState, so it silently fails
// to save, and the user finds it rather than the build.
//
// Everything here is a round trip with numbers: set, save, restore, compare.
//
//   build/ProcessorTest_artefacts/RelWithDebInfo/ProcessorTest
#include "../Source/PluginProcessor.h"
#include "../Source/state/FactoryPresets.h"
#include "../Source/ui/Theme.h"

#include <cstdio>

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
           || dynamic_cast<const juce::AudioParameterChoice*> (p) != nullptr;
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

void sessionRoundTrip()
{
    std::printf ("session round trip: every parameter and the editor properties\n");

    DybbukProcessor a;
    a.prepareToPlay (48000.0, 128);
    scrambleAllParameters (a);
    a.apvts.state.setProperty (theme::kThemeProperty, 1, nullptr);
    a.apvts.state.setProperty ("uiScale", 1.25f, nullptr);

    juce::MemoryBlock blob;
    a.getStateInformation (blob);

    DybbukProcessor b;
    b.prepareToPlay (48000.0, 128);
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
    check ("state version stamped", (int) b.apvts.state.getProperty ("stateVersion", 0) >= 1,
           "stateVersion = " + b.apvts.state.getProperty ("stateVersion", 0).toString());
}

void presetRoundTrip()
{
    std::printf ("user preset round trip: saved values return, appearance does not travel\n");

    DybbukProcessor p;
    p.prepareToPlay (48000.0, 128);

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
    p.prepareToPlay (48000.0, 128);

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

// A preset table is just numbers until you hear it. This catches the edit that
// silences a preset or pushes it into a runaway nobody asked for.
void presetsMakeSound()
{
    std::printf ("factory presets: each one passes audio and stays bounded\n");

    for (int i = 0; i < numFactoryPresets(); ++i)
    {
        DybbukProcessor p;
        p.prepareToPlay (48000.0, 128);

        const auto& fp = factoryPreset (i);
        for (const auto& info : p.presetManager.getPresets())
            if (info.factory && info.name == fp.name)
                p.presetManager.loadPreset (info);

        juce::AudioBuffer<float> buffer (2, 128);
        juce::MidiBuffer midi;
        double phase = 0.0;
        double sumEarly = 0.0, sumLate = 0.0;
        int countEarly = 0, countLate = 0;
        float peak = 0.0f;
        bool finite = true;

        const int blocks = (int) (5.0 * 48000.0 / 128.0);
        for (int b = 0; b < blocks; ++b)
        {
            const double t = b * 128.0 / 48000.0;
            for (int n = 0; n < 128; ++n)
            {
                const float v = t < 1.0 ? 0.35f * (float) std::sin (phase) : 0.0f;
                phase += 220.0 / 48000.0 * juce::MathConstants<double>::twoPi;
                buffer.setSample (0, n, v);
                buffer.setSample (1, n, v);
            }
            p.processBlock (buffer, midi);

            for (int n = 0; n < 128; ++n)
            {
                const float y = buffer.getSample (0, n);
                if (! std::isfinite (y))
                    finite = false;
                peak = juce::jmax (peak, std::abs (y));
                if (t >= 1.2 && t < 2.2) { sumEarly += (double) y * y; ++countEarly; }
                if (t >= 3.5 && t < 4.5) { sumLate += (double) y * y; ++countLate; }
            }
        }

        const double early = std::sqrt (sumEarly / juce::jmax (1, countEarly));
        const double late = std::sqrt (sumLate / juce::jmax (1, countLate));
        const double earlyDb = 20.0 * std::log10 (juce::jmax (early, 1.0e-12));
        const double lateDb = 20.0 * std::log10 (juce::jmax (late, 1.0e-12));

        check ("preset sounds and stays bounded",
               finite && peak < 1.0f && earlyDb > -50.0,
               juce::String (fp.name) + ": tail " + juce::String (earlyDb, 1) + " dBFS at 1 s, "
                   + juce::String (lateDb, 1) + " dBFS at 4 s, peak " + juce::String (peak, 3));
    }
}

void bypassAndAudio()
{
    std::printf ("bypass: crossfades to dry, and the loop keeps its state\n");

    DybbukProcessor p;
    p.prepareToPlay (48000.0, 128);
    p.apvts.getParameter (params::id::blend)->setValueNotifyingHost (1.0f); // full wet
    p.apvts.getParameter (params::id::decay)->setValueNotifyingHost (0.8f / 1.15f);

    juce::AudioBuffer<float> buffer (2, 128);
    juce::MidiBuffer midi;
    double phase = 0.0;

    auto runBlocks = [&] (int blocks, bool feedSignal)
    {
        float peak = 0.0f;
        for (int b = 0; b < blocks; ++b)
        {
            for (int i = 0; i < 128; ++i)
            {
                const float v = feedSignal ? 0.4f * (float) std::sin (phase) : 0.0f;
                phase += 330.0 / 48000.0 * juce::MathConstants<double>::twoPi;
                buffer.setSample (0, i, v);
                buffer.setSample (1, i, v);
            }
            p.processBlock (buffer, midi);
            peak = juce::jmax (peak, buffer.getMagnitude (0, 128));
        }
        return peak;
    };

    runBlocks (200, true);                       // fill the loop
    const float wetPeak = runBlocks (40, false); // it should still be ringing

    p.apvts.getParameter (params::id::bypass)->setValueNotifyingHost (1.0f);
    const float crossfade = runBlocks (12, false);    // 32 ms: the 20 ms fade to dry lives here
    const float duringBypass = runBlocks (50, false); // settled: bypassed output is the dry input, silence

    p.apvts.getParameter (params::id::bypass)->setValueNotifyingHost (0.0f);
    const float afterBypass = runBlocks (20, false);

    check ("loop rings on after the input stops", wetPeak > 0.01f,
           "peak " + juce::String (wetPeak, 4));
    check ("bypassed output follows the dry input", duringBypass < 0.002f,
           "peak " + juce::String (duringBypass, 6) + " after the fade, "
               + juce::String (crossfade, 4) + " during it");
    check ("bypass fades rather than cutting", crossfade < wetPeak * 1.1f,
           "crossfade peak " + juce::String (crossfade, 4) + " versus wet " + juce::String (wetPeak, 4));
    check ("loop survived the trip out of circuit", afterBypass > 0.005f,
           "peak " + juce::String (afterBypass, 4));

    bool finite = true;
    for (int ch = 0; ch < 2; ++ch)
        for (int i = 0; i < 128; ++i)
            if (! std::isfinite (buffer.getSample (ch, i)))
                finite = false;
    check ("output finite throughout", finite, "");
}

void readouts()
{
    std::printf ("readouts: what the host and Push will show\n");

    DybbukProcessor p;
    struct Expect { const char* id; float value; const char* text; };
    const Expect cases[] = {
        { params::id::time, 0.0f, "28 ms" },
        { params::id::decay, 1.08f, "1.08 runaway" },
        { params::id::decay, 1.0f, "1.00" },
        { params::id::out, -60.0f, "-Inf" },
        { params::id::out, 0.0f, "0.0" },
        { params::id::filter, 1262.0f, "1262" },
        { params::id::agitspeed, 0.08f, "12.5 s" },
        { params::id::agitspeed, 262.0f, "262 Hz" },
        { params::id::toneslevel, 0.0f, "Off" },
        { params::id::spread, 0.0f, "Mono" },
        { params::id::blend, 50.0f, "50" },
        { params::id::tonespitch, 110.0f, "A2" },
    };

    for (const auto& c : cases)
    {
        auto* parameter = dynamic_cast<juce::RangedAudioParameter*> (p.apvts.getParameter (c.id));
        const auto text = parameter->getText (parameter->convertTo0to1 (c.value), 32);
        check ("readout", text == c.text,
               juce::String (c.id) + " at " + juce::String (c.value, 2) + " reads \"" + text
                   + "\" (want \"" + c.text + "\")");
    }

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

            int digits = 0; // only the digits, so " s" and " runaway" do not count
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
}

void ordering()
{
    std::printf ("parameter order: Push bank 1 is the eight that matter\n");

    DybbukProcessor p;
    const char* wanted[] = { params::id::time, params::id::decay, params::id::filter,
                             params::id::resonance, params::id::absorb, params::id::blend,
                             params::id::agitate, params::id::agitspeed };

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
    check ("bypass is declared last", all.size() > 0 && all[all.size() - 1]->paramID == params::id::bypass,
           all.size() > 0 ? all[all.size() - 1]->paramID : juce::String ("none"));
    check ("bypass is the host bypass parameter", p.getBypassParameter() != nullptr
                                                      && p.getBypassParameter()->getName (16) == "Bypass",
           p.getBypassParameter() != nullptr ? p.getBypassParameter()->getName (16) : juce::String ("none"));
}

} // namespace

int main()
{
    juce::ScopedJuceInitialiser_GUI juceInit;

    ordering();
    readouts();
    sessionRoundTrip();
    presetRoundTrip();
    factoryPresetsLoad();
    presetsMakeSound();
    bypassAndAudio();

    std::printf ("\n%d failures\n", failures);
    return failures == 0 ? 0 : 1;
}
