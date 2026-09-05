# Dybbuk — plan

## What it is
<One paragraph: what the plugin does, what it's for, what makes it worth
building. If it ports an existing thing, say where that lives.>

## Parameters
The parameter list is the plugin's public API — adding is cheap, renaming and
reordering is not. Decide it here before writing DSP.

| ID | Name | Range | Default | Notes |
|---|---|---|---|---|
| | | | | |

## Phases

Each phase ends with a gate. Do not start the next phase until it passes.

### Phase 0 — Skeleton
CMake project builds VST3 + AU + Standalone, empty processor passes audio
through, `build.sh` runs clean.

**Gate:** builds with zero `Source/` warnings; pluginval strictness 5; appears
in the DAW; null test (invert against a bypassed copy) is silent.

### Phase 1 — DSP core
<The engine, headless. No UI beyond what already exists.>

**Gate:** `EngineTest` scenarios cover the core behaviours and print numbers
that prove them; no clicks on any parameter change; CPU measured and sane.

### Phase 2 — Parameters and state
All parameters wired, automatable, and saved. Non-parameter state stamped
through `stampExtraState` / `applyExtraState`.

**Gate:** every parameter appears in the host's automation list; change values,
save the session, reopen, values restored; pluginval strictness 10.

### Phase 3 — Playability
<Standalone playtest. What has to feel right.>

**Gate:** played for real through the standalone build; the specific feel
issues listed here are resolved.

### Phase 4 — Presets and control
Preset browser, MIDI mapping / footswitch control, keyboard shortcuts.

**Gate:** a full session is drivable without touching the mouse.

### Phase 5 — UI
Deliberately last. Design first (a Claude Design project works well for this),
then build against `UISnapshot`.

**Gate:** both themes rendered and reviewed; every control has a tooltip;
window scales and stays aspect-locked.

### Phase 6 — Validation matrix
- Sample rates: 44.1 / 48 / 96 / 192 kHz
- Buffer sizes: 32, 64, 128, 512, 2048
- Mono → stereo
- Offline render matches realtime
- Automation across a bounce
- State persistence; copy-paste the device between tracks
- Host bypass mid-sound — no clicks or stuck audio
- 30-minute stability soak

## Open questions
<Things only the user can decide. Ask them early, not at the end.>
