// Renders the plugin editor to PNGs without a host, so UI changes can be
// reviewed visually from the command line.
//
// Run it from wherever you want the files, then LOOK AT THE OUTPUT. A UI
// change that hasn't been looked at isn't finished.
//
//   cd /tmp && .../UISnapshot   ->  editor_snapshot*.png (idle, active, alt theme, each preset)
#include "../Source/PluginEditor.h"
#include "../Source/PluginProcessor.h"

#include <cstdio>

int main()
{
    juce::ScopedJuceInitialiser_GUI juceInit;

    DybbukProcessor processor;
    processor.prepareToPlay (48000.0, 128);

    std::unique_ptr<juce::AudioProcessorEditor> editor (processor.createEditor());

    auto snap = [&editor] (const char* name)
    {
        juce::MessageManager::getInstance()->runDispatchLoopUntil (300);
        const auto image = editor->createComponentSnapshot (editor->getLocalBounds(), true, 1.0f);
        const auto file = juce::File::getCurrentWorkingDirectory().getChildFile (name);
        file.deleteFile();
        juce::FileOutputStream stream (file);
        juce::PNGImageFormat().writeImageToStream (image, stream);
        std::printf ("wrote %s (%d x %d)\n", file.getFullPathName().toRawUTF8(),
                     image.getWidth(), image.getHeight());
    };

    snap ("editor_snapshot.png");

    // Push audio through so any meters/scopes show real state, then snapshot
    // again. Add snapshots here for every visually distinct state the UI has.
    {
        juce::AudioBuffer<float> buffer (2, 128);
        juce::MidiBuffer midi;
        double phase = 0.0;
        for (int block = 0; block < 200; ++block)
        {
            for (int i = 0; i < 128; ++i)
            {
                const float v = 0.5f * (float) std::sin (phase);
                phase += 220.0 / 48000.0 * juce::MathConstants<double>::twoPi;
                buffer.setSample (0, i, v);
                buffer.setSample (1, i, v);
            }
            processor.processBlock (buffer, midi);
        }
        snap ("editor_snapshot_active.png");
    }

    // The alternate theme through the REAL construction path: set the
    // property and rebuild the editor. Flipping the global on a live editor
    // would miss colours that components cached at construction, which is
    // exactly the class of bug this catches.
    editor.reset();
    processor.apvts.state.setProperty (theme::kThemeProperty, 1, nullptr);
    editor.reset (processor.createEditor());
    snap ("editor_snapshot_alt.png");

    // Every factory preset, which also reviews every readout in the tables.
    processor.apvts.state.setProperty (theme::kThemeProperty, theme::kDefaultTheme, nullptr);
    for (const auto& info : processor.presetManager.getPresets())
    {
        if (! info.factory)
            continue;
        processor.presetManager.loadPreset (info);
        editor.reset();
        editor.reset (processor.createEditor());
        const auto file = "editor_snapshot_" + info.name.toLowerCase().replaceCharacter (' ', '_') + ".png";
        snap (file.toRawUTF8());
    }
    theme::setTheme (theme::kDefaultTheme);

    editor.reset();
    return 0;
}
