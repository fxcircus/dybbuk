# Dybbuk — progress tracker

**This file is the living truth.** `docs/PLAN.md` is the historical spec; when
they disagree, this wins. Record what actually shipped, especially anything
that departs from the plan — that section is what a future session reads first.

## Scope changes since the plan

<As features land that the plan didn't anticipate, describe them here in a
sentence or two each. Include the *why* where it isn't obvious, and note any
behaviour that would surprise someone reading only the plan.>

## Current state

- Parameter count:
- Formats: VST3 / AU / Standalone
- Test scenarios in `EngineTest`:
- Known issues:

## Phase gates

### Phase 0 — Skeleton
- [ ] `./build.sh` completes with zero warnings from `Source/`
- [ ] pluginval strictness 5 passes
- [ ] `auval` passes
- [ ] Appears in the DAW and passes audio unchanged (null test)

### Phase 1 — DSP core
- [ ] `EngineTest` scenarios cover the core behaviours
- [ ] No clicks on any parameter change
- [ ] CPU measured:

### Phase 2 — Parameters and state
- [ ] All parameters automatable in the host
- [ ] Session save/reload restores everything, parameters and extra state
- [ ] pluginval strictness 10 passes

### Phase 3 — Playability
- [ ] Played through the standalone build

### Phase 4 — Presets and control
- [ ] Preset save / load / rename / delete
- [ ] MIDI mapping works

### Phase 5 — UI
- [ ] Both themes rendered and reviewed
- [ ] Tooltips on every control
- [ ] Window scales, aspect locked

### Phase 6 — Validation matrix
- [ ] Sample rates 44.1 / 48 / 96 / 192 kHz
- [ ] Buffer sizes 32 / 64 / 128 / 512 / 2048
- [ ] Mono → stereo
- [ ] Offline render matches realtime
- [ ] Automation across a bounce
- [ ] State persistence; device copy-paste
- [ ] Host bypass mid-sound
- [ ] 30-minute soak
