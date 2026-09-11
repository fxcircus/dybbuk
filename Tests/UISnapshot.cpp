// Renders the plugin editor to PNGs without a host, so UI changes can be
// reviewed visually from the command line.
//
// Run it from wherever you want the files, then LOOK AT THE OUTPUT. A UI
// change that has not been looked at is not finished.
//
//   cd /tmp && .../UISnapshot   ->  editor_snapshot_*.png
#include "../Source/PluginEditor.h"
#include "../Source/PluginProcessor.h"
#include "../Source/state/Randomiser.h"

#include <cstdio>
#include <iterator>

namespace
{
    constexpr double kRate = 48000.0;
    constexpr int kBlock = 128;

    // Always rebuild through the REAL construction path. Flipping a global on
    // a live editor would miss colours that components cached when they were
    // built, which is exactly the class of bug this exists to catch.
    void rebuild (DybbukProcessor& processor, std::unique_ptr<juce::AudioProcessorEditor>& editor)
    {
        editor.reset();
        editor.reset (processor.createEditor());
    }

    double phase = 0.0;

    // Feeds the processor `seconds` of a tone at `amp` (with a 2 ms edge so
    // the gate sees a pick and not a step), or of silence when amp is 0.
    void push (DybbukProcessor& processor, double seconds, float amp, double freq = 220.0)
    {
        juce::AudioBuffer<float> buffer (2, kBlock);
        juce::MidiBuffer midi;
        // Whole blocks, like a host: the tail of the last one is silence.
        const int blocks = (int) std::ceil (seconds * kRate / kBlock);
        for (int b = 0; b < blocks; ++b)
        {
            for (int i = 0; i < kBlock; ++i)
            {
                const double rel = (b * kBlock + i) / kRate;
                const double edge = 0.002;
                const double env = amp > 0.0f ? juce::jmax (0.0, juce::jmin (1.0, rel / edge, (seconds - rel) / edge)) : 0.0;
                const float v = (float) (amp * env * std::sin (phase));
                phase += freq / kRate * juce::MathConstants<double>::twoPi;
                buffer.setSample (0, i, v);
                buffer.setSample (1, i, v);
            }
            processor.processBlock (buffer, midi);
        }
    }

    // A gated phrase: `notes` bursts of 100 ms at 0.5 with 150 ms of rest
    // between them, so each becomes a step of the pattern and the pattern
    // runs at the default quarter second.
    void pushPhrase (DybbukProcessor& processor, int notes)
    {
        const double pitches[] = { 220.0, 330.0, 277.0, 165.0, 247.0, 196.0, 294.0, 370.0 };
        for (int i = 0; i < notes; ++i)
        {
            push (processor, 0.100, 0.5f, pitches[i % 8]);
            push (processor, 0.150, 0.0f);
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
    processor.prepareToPlay (kRate, kBlock);

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

    auto report = [&processor] (const char* what)
    {
        std::printf ("  %s: steps %d, current %d, gate %s, fill %s, commits %d\n", what, processor.getStepCount(),
                     processor.getCurrentStep(), processor.isGateOpen() ? "open" : "shut",
                     processor.isFillRunning() ? "on" : "off", processor.getCommits());
    };

    // 1. Listening: nothing played yet, the ember breathing, the ring empty.
    push (processor, 0.5, 0.0f);
    report ("listening");
    snap ("editor_snapshot_listening.png");

    // 2. A four-note phrase becomes four steps. The fourth is caught about
    // 80 ms after it commits, still growing out of the housing at its own
    // slot (the editor's timer only runs in the dispatch loop, so the growth
    // starts at the first frame after the feed stops); the three before it
    // must not have moved.
    pushPhrase (processor, 3);
    push (processor, 0.100, 0.5f, 165.0);
    push (processor, 0.120, 0.0f);
    report ("grow");
    snapAfter ("editor_snapshot_grow.png", 80);
    push (processor, 0.030, 0.0f);

    // ... and the sequencer is on one of them. Stopping mid-way between
    // ticks leaves the sounding step lit.
    push (processor, 0.55, 0.0f);
    report ("pattern");
    snap ("editor_snapshot_pattern.png");

    // 2b. Each mode, with the four-step pattern playing. The dybbuk eases
    // into each bearing over the editor's timer and Wraith's ghosts are laid
    // down one per tick, so the feed and the dispatch loop are interleaved:
    // the pattern has to be seen advancing, not just to have advanced.
    {
        auto playFor = [&processor] (double seconds)
        {
            for (double t = 0.0; t < seconds; t += 0.1)
            {
                push (processor, 0.1, 0.0f);
                juce::MessageManager::getInstance()->runDispatchLoopUntil (100);
            }
        };
        const char* modeNames[] = { "golem", "wraith", "trance", "legion", "tremor", "rattle", "mirror", "miasma" };
        static_assert (std::size (modeNames) == (size_t) Lamp::kModeCount, "one frame per player");
        for (int m = 0; m < Lamp::kModeCount; ++m)
        {
            setParam (processor, params::id::mode, (float) m);
            playFor (1.5);
            report (modeNames[m]);
            snapAfter ("editor_snapshot_mode_" + juce::String (modeNames[m]) + ".png", 30);

            // The hint line, pinned on the plate without a mouse: Decay's
            // sentence in Miasma, where it means something else than in
            // Golem, printed in the strip under the mode bar. This frame
            // reviews that it fits the strip and touches no readout.
            if (m == Lamp::miasma)
                if (auto* d = dynamic_cast<DybbukEditor*> (editor.get()))
                {
                    d->showHintForTests ("DECAY");
                    snapAfter ("editor_snapshot_hint.png", 30);
                    d->showHintForTests ({});
                }
        }
        setParam (processor, params::id::mode, 0.0f);
        playFor (1.0);
    }

    // 3. Mid-burst: the gate is open and a fifth pip is being written. The
    // engine only updates its atomics inside processBlock, so stopping the
    // feed half way through a note holds the gate open for the picture.
    push (processor, 0.050, 0.5f, 330.0);
    report ("armed_gate");
    snap ("editor_snapshot_armed_gate.png");
    push (processor, 0.050, 0.5f, 330.0);
    push (processor, 0.400, 0.0f);

    // 3b. A fill: disarmed, a gated onset scrambles the order for one cycle,
    // and the ring should warm and shiver for as long as it runs. Freeze is
    // on for this frame too, so it reviews the lit button and the frost.
    setParam (processor, params::id::freeze, 1.0f);
    push (processor, 0.100, 0.5f, 330.0);
    push (processor, 0.020, 0.0f);
    report ("fill");
    snap ("editor_snapshot_fill.png");
    setParam (processor, params::id::freeze, 0.0f);
    push (processor, 1.5, 0.0f);

    // 3c. Bypassed: everything in the middle dims, the pattern keeps its place.
    setParam (processor, params::id::bypass, 1.0f);
    push (processor, 0.3, 0.0f);
    report ("bypassed");
    snap ("editor_snapshot_bypassed.png");
    setParam (processor, params::id::bypass, 0.0f);
    push (processor, 0.3, 0.0f);

    // 3d. A clear, caught while the ring is still falling into the ember.
    processor.requestClear();
    push (processor, 0.05, 0.0f);
    report ("cleared");
    snapAfter ("editor_snapshot_cleared.png", 100);
    pushPhrase (processor, 5);
    push (processor, 0.3, 0.0f);

    // 4. Synced Step: detents on the ring, a note value on the readout, and
    // the Bar diamond at full strength and lit (every other frame has it
    // dimmed, since it means nothing off the grid).
    setParam (processor, params::id::stepsync, 1.0f);
    setParam (processor, params::id::barreset, 1.0f);
    setParam (processor, params::id::step, 0.5f);
    push (processor, 0.3, 0.0f);
    report ("synced");
    snap ("editor_snapshot_synced.png");
    setParam (processor, params::id::stepsync, 0.0f);
    setParam (processor, params::id::barreset, 0.0f);

    // 4b. RANDOM's ticklist, with the settings mark's hint pinned on the
    // plate (before the roll, whose name would otherwise share the strip). The real list is a popup window of its own, and a headless run
    // cannot keep one open: the window dismisses itself within 10 ms when no
    // JUCE component has keyboard focus (MenuWindow::handleMousePosition,
    // "menuWasHiddenBecauseOfAppChange"), and nothing here can have focus.
    // So the list is drawn through the same LookAndFeel hooks the window
    // paints with (background, section header, one row per item, at the
    // sizes the hooks ask for), from the same names and mask the editor
    // uses, and composed under the mark where the window opens in a host.
    // Two fields are unticked so both states of the tick are on the frame,
    // and one row is drawn highlighted to review the inverted row.
    if (auto* d = dynamic_cast<DybbukEditor*> (editor.get()))
    {
        processor.setRandomField (Randomiser::fieldTime, false);
        processor.setRandomField (Randomiser::fieldMode, false);
        d->showHintForTests ("SETTINGS");
        juce::MessageManager::getInstance()->runDispatchLoopUntil (30);

        auto& lf = editor->getLookAndFeel();
        const juce::PopupMenu::Options options;
        const int border = lf.getPopupMenuBorderSizeWithOptions (options);
        juce::Array<juce::PopupMenu::Item> items;
        {
            juce::PopupMenu::Item header ("RANDOM rolls");
            header.isSectionHeader = true;
            items.add (header);
        }
        for (int i = 0; i < Randomiser::kFieldCount; ++i)
        {
            juce::PopupMenu::Item item (Randomiser::fieldName (i));
            item.itemID = i + 1;
            item.isTicked = (processor.randomFields() & (1u << i)) != 0;
            items.add (item);
        }
        int listW = 0, listH = 0;
        juce::Array<int> rowH;
        for (const auto& item : items)
        {
            int w = 0, h = 0;
            if (item.isSectionHeader)
                lf.getIdealPopupMenuSectionHeaderSizeWithOptions (item.text, -1, w, h, options);
            else
                lf.getIdealPopupMenuItemSizeWithOptions (item.text, false, options.getStandardItemHeight(), w, h, options);
            listW = juce::jmax (listW, w);
            rowH.add (h);
            listH += h;
        }
        juce::Image list (juce::Image::ARGB, listW + 2 * border, listH + 2 * border, true);
        {
            juce::Graphics g (list);
            lf.drawPopupMenuBackgroundWithOptions (g, list.getWidth(), list.getHeight(), options);
            int y = border;
            for (int i = 0; i < items.size(); ++i)
            {
                const juce::Rectangle<int> row (border, y, listW, rowH[i]);
                juce::Graphics::ScopedSaveState state (g);
                g.setOrigin (row.getPosition());
                if (items[i].isSectionHeader)
                    lf.drawPopupMenuSectionHeaderWithOptions (g, row.withZeroOrigin(), items[i].text, options);
                else
                    lf.drawPopupMenuItemWithOptions (g, row.withZeroOrigin(), i == 4, items[i], options);
                y += rowH[i];
            }
        }

        juce::Image frame = editor->createComponentSnapshot (editor->getLocalBounds(), true, 1.0f);
        juce::Rectangle<int> at (642, 2, 28, 44);
        for (auto* c : editor->getChildComponent (0)->getChildren())
            if (c->getName() == "SETTINGS")
                at = c->getBounds();
        {
            juce::Graphics g (frame);
            g.drawImageAt (list, at.getX() - 8, at.getBottom() + 4);
        }
        const auto file = juce::File::getCurrentWorkingDirectory().getChildFile ("editor_snapshot_randommenu.png");
        file.deleteFile();
        juce::FileOutputStream stream (file);
        juce::PNGImageFormat().writeImageToStream (frame, stream);
        std::printf ("wrote %s (%d x %d, list %d x %d)\n", file.getFileName().toRawUTF8(), frame.getWidth(),
                     frame.getHeight(), list.getWidth(), list.getHeight());

        d->showHintForTests ({});
        processor.setRandomField (Randomiser::fieldTime, true);
        processor.setRandomField (Randomiser::fieldMode, true);
    }

    // 5. A roll of the dice: the patch changes and the character's name is
    // printed over the dybbuk for a few seconds. This frame reviews that the
    // caption lands in its clearing and collides with nothing.
    if (auto* d = dynamic_cast<DybbukEditor*> (editor.get()))
    {
        d->rollDice();
        push (processor, 0.3, 0.0f);
        report ("rolled");
        snap ("editor_snapshot_rolled.png");
    }

    // 6. The theme cross-fade, caught in the middle. The dispatch loop runs the
    // editor's timer, so a short wait after the toggle lands part way through
    // the 350 ms dissolve: this frame should show both sheets at once.
    if (auto* d = dynamic_cast<DybbukEditor*> (editor.get()))
    {
        d->toggleTheme();
        snapAfter ("editor_snapshot_theme_fade.png", 110);
    }

    // 7. The alternate sheet, settled, rebuilt through the real path, with
    // the pattern still running.
    processor.apvts.state.setProperty (theme::kThemeProperty, (int) theme::Kind::dark, nullptr);
    rebuild (processor, editor);
    push (processor, 0.3, 0.0f);
    report ("dark");
    snap ("editor_snapshot_dark.png");

    // 8. Every factory preset, which also reviews every readout in the tables.
    // Each is played a phrase of its own so the ring shows its step count.
    processor.apvts.state.setProperty (theme::kThemeProperty, (int) theme::kDefaultTheme, nullptr);
    for (const auto& info : processor.presetManager.getPresets())
    {
        if (! info.factory)
            continue;

        processor.presetManager.loadPreset (info);
        rebuild (processor, editor);
        processor.requestClear();
        push (processor, 0.1, 0.0f);
        pushPhrase (processor, 8);
        push (processor, 0.3, 0.0f);
        report (info.name.toRawUTF8());
        snap ("editor_snapshot_" + info.name.toLowerCase().replaceCharacter (' ', '_') + ".png");
    }

    theme::setTheme (theme::kDefaultTheme);
    editor.reset();
    return 0;
}
