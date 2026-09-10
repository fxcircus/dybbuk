# Dybbuk — working rules

## Read first
- `docs/PLAN.md` is the spec: phases, each with a verification gate.
- `docs/PROGRESS.md` is the living truth. **When the plan and the tracker
  disagree, the tracker wins** — plans go stale within days, so record what
  actually shipped there rather than pretending the plan stayed current.
- `docs/IDEAS.md` is the backlog. Park good ideas there instead of derailing
  the current phase.
- Do not start a phase until the previous phase's gate fully passes.

## Toolchain on this machine
These facts override any plan where they conflict.

- macOS 26, Xcode 26.6, Apple clang 21, CMake 4.4.2, Ninja 1.13.2, Apple Silicon.
- JUCE 8.0.12 via CMake FetchContent. **Never use the Projucer.**
- `CMAKE_OSX_DEPLOYMENT_TARGET` is **"14.0"**; `CMAKE_OSX_ARCHITECTURES` is **"arm64"**.
- If CMake 4 throws a policy version error, configure with
  `-DCMAKE_POLICY_VERSION_MINIMUM=3.5`. Never edit JUCE's own CMake files.
- clang 21 is newer than JUCE 8.0.12, so JUCE modules emit warnings. Ignore
  those — the zero-warning requirement applies only to code in `Source/`.
- pluginval CLI is at `/Applications/pluginval.app/Contents/MacOS/pluginval`.
- If a JUCE API has changed since your training data, read the JUCE 8.0.12
  source in `build/_deps/juce-src/` rather than guessing.

## Verification — the part that actually matters
The two harnesses exist so work can be checked without a DAW. Use them; they
are the difference between finding a bug and shipping one.

- **`Tests/EngineTest`** — drives the engine like a host and prints numbers.
  Add a named scenario for every behaviour worth trusting, and make it print
  something that would change if the behaviour broke (RMS, a frequency
  estimate, a channel ratio). Run: `build/EngineTest_artefacts/RelWithDebInfo/EngineTest [scenario]`.
- **`Tests/UISnapshot`** — renders the editor to PNGs.
  `cmake --build build --target UISnapshot`, run it from a scratch directory,
  then **actually look at the images**. A UI change that hasn't been looked at
  isn't finished.
- **Standalone build** is the fast test rig. Launch it, play, quit — no plugin
  rescan. Use it for the daily loop; the DAW is a validation gate, not the
  inner loop.
- **Ableton Live only rescans plugins at startup**, so a DAW test means quit
  and reopen Live. Say so when handing work over.

## Definition of done
Before claiming a feature is complete:
1. `./build.sh` passes — clean build, zero warnings from `Source/`.
2. pluginval strictness 10 passes; `auval` passes.
3. A new or updated `EngineTest` scenario covers the behaviour.
4. If the UI changed: snapshot rendered **and reviewed**, both themes.
5. State round-trip tested: save, reload, values restored.
6. `docs/PROGRESS.md` updated.
7. Committed and pushed.

## Audio-thread rules (hard constraints)
- No allocation, locks, file I/O, or logging in `processBlock`.
- `ScopedNoDenormals` at the top of `processBlock`.
- All continuous parameters go through `SmoothedValue`.
- Read parameters via cached `std::atomic<float>*` from the APVTS; snapshot
  them into a plain `Params` struct once per block.
- Allocate for the worst case in `prepareToPlay`; handle repeat calls and
  sample-rate changes.
- Make no assumptions about block size or channel count.
- UI→engine commands go through lock-free atomic mailboxes, never locks.
- Engine→UI state is published through atomics the editor polls.

## State discipline
**Anything that affects sound or appearance is either an APVTS parameter, or
it is explicitly stamped into the state tree via `stampExtraState` /
`applyExtraState`.** There is no third category. Non-parameter engine state
that skips those hooks will silently fail to save in sessions and presets —
this has been the most common bug class across projects, and it is always
found by the user rather than by the build.

## Style
- One class per file pair under `Source/dsp/` and `Source/ui/`.
- Comment the *why* of DSP choices, not the *what*.
- Match the surrounding code's idiom, naming, and comment density.
- If a DSP problem needs an ear rather than a compiler, stop and say what to
  listen for instead of guessing at fixes.
- **Defaults belong to the user.** Never retune default parameter values as a
  side effect of another change.

## Project-specific
- Repo: github.com/fxcircus/dybbuk (origin, SSH). Commit to main and push
  after every commit.
- Push directly to main unless told otherwise.
- Breaking previously saved sessions/presets: allowed until the first tagged
  release. Nothing has been saved with this plugin yet, so parameter IDs and
  state layout may still change without migration code.
- Source spec: `dybbuk-plan.md` (what we are emulating and why). `docs/DESIGN.md`
  is the technical design derived from it; `docs/PLAN.md` holds the phase gates.

## Value readouts (apply to every new parameter)

Readouts are for musicians, not calculators. All float parameters go through
the `floatParam` helper in Source/Parameters.cpp, whose string function
enforces:

- **ms parameters**: seconds with two decimals from 100 ms up ("0.66 s"),
  integer milliseconds below ("45 ms"). The unit is baked into the string and
  the parameter label left empty, so hosts don't append a stale "ms" after a
  seconds readout.
- **Hz**: integers from 100 Hz up ("1262 Hz"); two decimals below (LFO rates).
- **%**: integers only.
- **dB / unitless**: one or two decimals.
- Word readouts ("Inf", "Off", "-Inf") stay as-is; any UI code that appends
  the label to the value text must skip strings already containing a unit or
  word.

Trap that caused this rule: `juce::String (value, 0)` means *shortest
full-precision representation*, NOT zero decimals — a "rounding" helper that
passes 0 never rounds, and raw floats like 658.289 leak onto the UI and into
host automation lanes. Never pass 0 to that constructor expecting an integer;
use `juce::roundToInt` instead. Verify readouts visually (screenshot every
section), not just by compiling.

## Bypass (apply to every new plugin)

The template ships host-integrated bypass; keep all four pieces when building
on it:

1. A `Bypass` bool parameter declared LAST (1 = bypassed — the polarity VST3's
   kIsBypass and the AU equivalent expect; a parameter named for the opposite
   state reads backwards in every host).
2. `getBypassParameter()` override returning it, so the DAW's own bypass
   button drives this parameter (and your UI switch follows it).
3. A 20 ms crossfade between wet and dry in processBlock (`bypassDry` +
   `bypassMix`), never a hard switch: a host automating bypass otherwise
   clicks at whatever level the wet path happened to be.
4. Keep the engine RUNNING while bypassed so its state survives the trip.
   Decide per-plugin whether it should see the real input or silence while
   out of circuit (silence = it collects nothing you play while bypassed —
   usually what a player expects from a looper/freezer).

Bypass is performance state: exclude it from preset dirty-tracking and
preserve its value across preset loads (a preset must never take the plugin
in or out of circuit).
