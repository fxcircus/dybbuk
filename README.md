# JUCE plugin template

A working starting point for a new audio plugin on this machine, extracted from
Infinite Sustainer. It builds, validates, and ships the two tools that make
plugin work reviewable without a DAW.

## Start a new plugin

```bash
cd ~/Desktop/code/plugins/juce-plugin-template
# from anywhere - dest may be a new folder, an existing EMPTY folder, or "."
mkdir ~/Desktop/code/plugins/tape_delay && cd ~/Desktop/code/plugins/tape_delay
~/Desktop/code/plugins/juce-plugin-template/new-plugin.sh . "Tape Delay" Tpdl
cd ../tape_delay
git init && git add -A && git commit -m "Initial commit from template"
./build.sh
```

`new-plugin.sh` copies the template into the destination (folders holding
only session droppings like .claude/.git/.DS_Store count as empty) and renames the
target, product name, bundle ID, and plugin code throughout. The plugin code
must be four characters, first one uppercase, and unique across your plugins —
hosts key their caches off it.

Then, before writing any DSP:

1. Fill in `docs/PLAN.md`, **parameters table first**. The parameter list is
   the plugin's public API; adding is cheap, renaming is not.
2. Fill in the project-specific block at the bottom of `CLAUDE.md` (repo,
   whether breaking saved sessions is allowed).
3. Replace `Source/dsp/ExampleEngine.*` with the real engine, keeping its
   shape: a `Params` struct snapshotted once per block, `prepare()` allocating
   worst case, `process()` allocating nothing.

## What's in here

| Path | What it's for |
|---|---|
| `CMakeLists.txt` | VST3 + AU + **Standalone**, JUCE 8.0.12 via FetchContent, plus the two test targets |
| `build.sh` | build → install → pluginval (strictness 10) → auval |
| `new-plugin.sh` | clone-and-rename into a new project |
| `Source/Parameters.*` | the parameter layout, with skewed-range and display helpers |
| `Source/PluginProcessor.*` | cached atomic parameter reads, per-block `Params` snapshot, state hooks |
| `Source/PluginEditor.*` | fixed canvas scaled to the window, aspect locked |
| `Source/dsp/ExampleEngine.*` | a small engine demonstrating the audio-thread patterns |
| `Source/state/PresetManager.*` | file-backed presets: save/load/rename/delete, stars, dirty tracking, revert-to-loaded |
| `Source/ui/Theme.*` | mutable colour globals + dark mode |
| `Tests/EngineTest.cpp` | offline DSP harness — **the most important file here** |
| `Tests/UISnapshot.cpp` | headless editor → PNG renderer |
| `docs/` | PLAN / PROGRESS / IDEAS skeletons |

## The two habits that matter

**Verify with numbers, not assertions.** `EngineTest` drives the engine like a
host and prints measurements. Add a named scenario for every behaviour worth
trusting, printing something that would change if it broke. Every bug caught
before the user saw it was caught this way; every bug the user found was in
something unmeasured.

**Look at the UI you changed.** `UISnapshot` renders the editor to PNGs in
both themes. Build the target, run it from a scratch directory, and open the
images. A UI change that hasn't been looked at isn't finished.

Third, related: **use the Standalone build as the daily test rig.** Launch,
play, quit — no plugin rescan. Ableton Live only rescans at startup, so DAW
testing means quitting and reopening Live; keep that as a validation gate
rather than the inner loop.

## The bug class to design against

Anything affecting sound or appearance is either an APVTS parameter, or it is
explicitly stamped into the state tree through `stampExtraState` /
`applyExtraState`. There is no third category. Engine state that skips those
hooks silently fails to save in sessions and presets — that has been the most
common bug across projects, and it is always the user who finds it.
