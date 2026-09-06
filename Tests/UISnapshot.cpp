// Renders the plugin editor to PNGs without a host, so UI changes can be
// reviewed visually from the command line.
//
// Run it from wherever you want the files, then LOOK AT THE OUTPUT. A UI
// change that has not been looked at is not finished.
//
//   cd /tmp && .../UISnapshot   ->  editor_snapshot*.png
#include "../Source/PluginEditor.h"
#include "../Source/PluginProcessor.h"

#include <cstdio>

namespace
{
    // Always rebuild through the REAL construction path. Flipping a global on
    // a live editor would miss colours that components cached when they were
    // built, which is exactly the class of bug this exists to catch.
    void rebuild (DybbukProcessor& processor, std::unique_ptr<juce::AudioProcessorEditor>& editor)
    {
        editor.reset();
        editor.reset (processor.createEditor());
    }

    double phase = 0.0;

    void pushAudio (DybbukProcessor& processor, int blocks, bool signalOn)
    {
        juce::AudioBuffer<float> buffer (2, 128);
        juce::MidiBuffer midi;
        for (int b = 0; b < blocks; ++b)
        {
            for (int i = 0; i < 128; ++i)
            {
                const float v = signalOn ? 0.5f * (float) std::sin (phase) : 0.0f;
                phase += 220.0 / 48000.0 * juce::MathConstants<double>::twoPi;
                buffer.setSample (0, i, v);
                buffer.setSample (1, i, v);
            }
            processor.processBlock (buffer, midi);
        }
    }

    void setParam (DybbukProcessor& processor, const char* id, float value)
    {
        if (auto* p = processor.apvts.getParameter (id))
            p->setValueNotifyingHost (p->convertTo0to1 (value));
    }
}

int main()
{
    juce::ScopedJuceInitialiser_GUI juceInit;

    DybbukProcessor processor;
    processor.prepareToPlay (48000.0, 128);

    std::unique_ptr<juce::AudioProcessorEditor> editor (processor.createEditor());

    auto snapAfter = [&editor] (const juce::String& name, int settleMs)
    {
        juce::MessageManager::getInstance()->runDispatchLoopUntil (settleMs);
        const auto image = editor->createComponentSnapshot (editor->getLocalBounds(), true, 1.0f);
        const auto file = juce::File::getCurrentWorkingDirectory().getChildFile (name);
        file.deleteFile();
        juce::FileOutputStream stream (file);
        juce::PNGImageFormat().writeImageToStream (image, stream);
        std::printf ("wrote %s (%d x %d)\n", file.getFileName().toRawUTF8(), image.getWidth(),
                     image.getHeight());
    };

    auto snap = [&snapAfter] (const juce::String& name) { snapAfter (name, 250); };

    // 1. At rest, defaults, brass.
    snap ("editor_snapshot.png");

    // 2. Playing: the ember lit, the meter moving, a value on the strip.
    setParam (processor, params::id::decay, 0.85f);
    setParam (processor, params::id::agitate, 55.0f);
    pushAudio (processor, 300, true);
    pushAudio (processor, 40, false);
    snap ("editor_snapshot_active.png");

    // 3. Runaway: Decay past unity, the red zone lit and the ember hot.
    setParam (processor, params::id::decay, 1.12f);
    setParam (processor, params::id::filter, 3000.0f);
    pushAudio (processor, 500, true);
    pushAudio (processor, 200, false);
    snap ("editor_snapshot_runaway.png");

    // 4. Synced Time: detents on the ring, a note value on the readout.
    setParam (processor, params::id::decay, 0.45f);
    setParam (processor, params::id::timesync, 1.0f);
    setParam (processor, params::id::time, 0.5f);
    pushAudio (processor, 60, true);
    snap ("editor_snapshot_synced.png");
    setParam (processor, params::id::timesync, 0.0f);

    // 5. The theme cross-fade, caught in the middle. The dispatch loop runs the
    // editor's timer, so a short wait after the toggle lands part way through
    // the 350 ms dissolve: this frame should show both sheets at once.
    if (auto* d = dynamic_cast<DybbukEditor*> (editor.get()))
    {
        d->toggleTheme();
        snapAfter ("editor_snapshot_theme_fade.png", 110);
    }

    // 6. The alternate sheet, settled, rebuilt through the real path.
    processor.apvts.state.setProperty (theme::kThemeProperty, (int) theme::Kind::light, nullptr);
    rebuild (processor, editor);
    pushAudio (processor, 120, true);
    snap ("editor_snapshot_light.png");

    // 7. Every factory preset, which also reviews every readout in the tables.
    processor.apvts.state.setProperty (theme::kThemeProperty, (int) theme::kDefaultTheme, nullptr);
    for (const auto& info : processor.presetManager.getPresets())
    {
        if (! info.factory)
            continue;

        processor.presetManager.loadPreset (info);
        rebuild (processor, editor);
        pushAudio (processor, 200, true);
        snap ("editor_snapshot_" + info.name.toLowerCase().replaceCharacter (' ', '_') + ".png");
    }

    theme::setTheme (theme::kDefaultTheme);
    editor.reset();
    return 0;
}
