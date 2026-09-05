# Dybbuk UI implementation spec (plan section 2.7)

Sources read: `dybbuk-plan.md`, `CLAUDE.md`, all of `Source/`, both harnesses, `CMakeLists.txt`, Teder's editor / PresetStation / EngravedKnob / PatentTheme / SlideSwitch / UISnapshot, Infinite Sustainer's editor / VerticalFader / FreezeBell / UISnapshot. JUCE 8.0.12 source is present at `build/_deps/juce-src/` and the APIs referenced below were checked there (`ParameterAttachment` gestures, `FontOptions::withHeight/withStyle`, `MouseEvent::getDistanceFromDragStartY`, `ModifierKeys::isShiftDown`, `Slider::setVelocityModeParameters`). Two things to flag before the layout: (a) the template's `PresetManager` still writes the root tag `"SustainerPreset"`; rename it to `"DybbukPreset"` before the first preset is saved. (b) `docs/PLAN.md` Phase 5 gate says "every control has a tooltip", while `dybbuk-plan.md` 2.7 says no floating tooltips anywhere. This spec follows 2.7: every control has a hint string, but it is shown in the readout strip on hover, and the editor drops `juce::TooltipWindow` entirely. Record that in `docs/PROGRESS.md`.

---

## 1. Canvas and coordinate table

**Canvas: 720 x 576** (`canvasW = 720`, `canvasH = 576`), aspect locked, resize limits half to double, exactly the template's `Canvas` + `setTransform (scale)` scheme. Side margin `padX = 24`, usable width 24..696 (672 px).

Knob component geometry (all sizes): circle of diameter D at the top of the component, centred horizontally; label baseline row at `D + 12` (height 14); value row at `D + 28` (height 12); component height `D + 42`; component width `D + 46`. So the knob's circle centre is at `(x + w/2, y + D/2)`.

| Constant | Value |
|---|---|
| `kKnobLarge` | 104 (component 150 x 146) |
| `kKnobMediumLarge` | 84 (component 130 x 126) |
| `kKnobMedium` | 68 (component 110 x 110) |
| `kKnobSmall` | 52 (component 98 x 94, reserved: future Crust trim) |

### Header (y 0..46)

| Component | x | y | w | h | Notes |
|---|---|---|---|---|---|
| Wordmark "Dybbuk" (canvas paint) | 24 | 11 | 96 | 26 | 22 px bold, `ink`, left |
| Hebrew "דיבוק" (canvas paint) | 124 | 12 | 56 | 24 | 19 px regular, `faded`, centredLeft, `juce::String::fromUTF8 ("\xd7\x93\xd7\x99\xd7\x91\xd7\x95\xd7\xa7")` |
| PresetStation | 230 | 10 | 260 | 26 | prev arrow 18 / name 224 / next arrow 18 |
| Preset browser overlay | 202 | 44 | 316 | 240 | child of canvas, hidden until opened, z-order above rows |
| ThemeButton | 676 | 13 | 20 | 20 | sun/moon glyph |
| Header rule (canvas paint) | 24 | 46 | 672 | 1 | `ink` at 0.15 alpha |

### Hero row (y 60..206), large knobs D 104, w 150, h 146

| Component | x | y | w | h | Circle centre |
|---|---|---|---|---|---|
| Time knob | 33 | 60 | 150 | 146 | (108, 112) |
| Sync toggle (PillToggle) | 158 | 62 | 44 | 16 | sits at the knob's 2 o'clock, outside its ring (ring outer radius 60 reaches x 151 at that height) |
| Decay knob | 201 | 60 | 150 | 146 | (276, 112) |
| Filter knob | 369 | 60 | 150 | 146 | (444, 112) |
| Blend knob | 537 | 60 | 150 | 146 | (612, 112) |

Cell pitch 168; centres 108 / 276 / 444 / 612. Label row y 176..190, value row y 192..204.

### Second row (y 220..330), medium knobs D 68, w 110, h 110

| Component | x | y | w | h | Circle centre |
|---|---|---|---|---|---|
| Time Mod knob | 44 | 220 | 110 | 110 | (99, 254) |
| Strength knob | 194 | 220 | 110 | 110 | (249, 254) |
| Loop/Gate toggle (PillToggle, two labels) | 316 | 244 | 88 | 28 | centred on x 360, vertically on the knob centres |
| Resonance knob | 416 | 220 | 110 | 110 | (471, 254) |
| Absorb knob | 566 | 220 | 110 | 110 | (621, 254) |

Label row y 300..314, value row y 316..328.

### Bottom strip (y 336..476)

| Component | x | y | w | h | Notes |
|---|---|---|---|---|---|
| Agitate knob (D 84) | 135 | 346 | 130 | 126 | circle centre (200, 388) |
| Ember | 300 | 336 | 120 | 120 | centre (360, 396); glow radius up to 58 stays inside |
| Speed knob (D 68) | 465 | 354 | 110 | 110 | circle centre (520, 388), level with Agitate's |
| Clear button | 332 | 458 | 56 | 16 | small caps "clear", `faded` at rest |

### Readout strip

| Component | x | y | w | h |
|---|---|---|---|---|
| ReadoutStrip | 24 | 484 | 672 | 24 |

### OUT slider

| Component | x | y | w | h | Internal zones (component-local) |
|---|---|---|---|---|---|
| OutSlider | 24 | 518 | 672 | 40 | label "out" 0..36; rail 44..612 (568 px), rail centre y 20; value text 616..672 |

Bottom margin 18 px (558..576). Nothing else is drawn on the canvas: no section captions, no extra rules. The strip's panel fill is the only separator above the fader.

Z-order (add in this order): canvas, all knobs, toggles, ember, clear, readout, out slider, preset station, theme button, then `presetStation.getBrowser()` as a child component (hidden), then an optional `ThemeFade` overlay topmost.

---

## 2. Theme tokens

Extend `Source/ui/Theme.h`. Keep the mutable globals `background / ink / faded / accent` so nothing breaks, but make them mirrors of a `Palette`:

```cpp
namespace theme
{
    enum class Kind { brass, parchment };

    struct Palette
    {
        const char* name;
        juce::Colour background, panel, ink, faded, accent,
                    emberCore, emberGlow,
                    knobBody, knobRing, knobPointer, modulated, runaway,
                    meterFill, meterPeak, readout, outline;
    };

    const Palette& palette();            // the active set; components read it at paint time
    void setTheme (Kind kind);           // swaps the palette and refreshes the legacy globals
    Kind currentTheme();
    Kind kindFromName (const juce::String& name);  // "parchment" -> parchment, anything else -> brass
    const char* nameOf (Kind kind);      // "brass" / "parchment"

    // Legacy shims so existing call sites and UISnapshot keep compiling.
    // dark == brass.
    void setDarkMode (bool dark);        // setTheme (dark ? brass : parchment)
    bool isDarkMode();

    extern juce::Colour background, ink, faded, accent;   // mirrors of palette()
    inline juce::Colour inkA (float a) { return ink.withAlpha (a); }

    // Type. kTypeface empty = system font (SF on macOS, which also carries the
    // Hebrew glyphs). Swapping to an embedded face later is this constant plus
    // a juce_add_binary_data target linked into BOTH Dybbuk and UISnapshot.
    inline constexpr const char* kTypeface = "";
    enum class Weight { regular, semibold, bold };
    juce::Font font (float px, Weight w = Weight::regular, float tracking = 0.0f);
    float textWidth (const juce::Font&, const juce::String&);
    // Spaced uppercase labels (10 px, tracking 0.12) used under every control.
    void drawCaps (juce::Graphics&, const juce::String&, juce::Rectangle<float>,
                   juce::Justification, float px, Weight, float tracking, juce::Colour);
}
```

`Theme.cpp`:

```cpp
namespace
{
    constexpr theme::Palette brassPalette {
        "brass",
        juce::Colour (0xff15110c), // background
        juce::Colour (0xff1f1912), // panel
        juce::Colour (0xffefe5d0), // ink
        juce::Colour (0xff9c8f77), // faded
        juce::Colour (0xffc9a24e), // accent (brass)
        juce::Colour (0xffffd98a), // emberCore
        juce::Colour (0xffe26a2c), // emberGlow
        juce::Colour (0xff2b241a), // knobBody
        juce::Colour (0xff4c4131), // knobRing (track)
        juce::Colour (0xffefe5d0), // knobPointer
        juce::Colour (0xff7fb8ad), // modulated (verdigris: brass patina, reads as "not the knob")
        juce::Colour (0xffe8502f), // runaway
        juce::Colour (0xffc9a24e), // meterFill
        juce::Colour (0xfff2c76a), // meterPeak
        juce::Colour (0xffefe5d0), // readout
        juce::Colour (0xff6b5c45)  // outline
    };

    constexpr theme::Palette parchmentPalette {
        "parchment",
        juce::Colour (0xffefe8db), // background
        juce::Colour (0xffe4dccc), // panel
        juce::Colour (0xff1e1a14), // ink
        juce::Colour (0xff66604f), // faded
        juce::Colour (0xff7a5a10), // accent (dark brass)
        juce::Colour (0xffffb640), // emberCore
        juce::Colour (0xffc94a14), // emberGlow
        juce::Colour (0xfff7f2e8), // knobBody
        juce::Colour (0xffc6bba6), // knobRing
        juce::Colour (0xff1e1a14), // knobPointer
        juce::Colour (0xff25736a), // modulated
        juce::Colour (0xffb32e1c), // runaway
        juce::Colour (0xff7a5a10), // meterFill
        juce::Colour (0xffc94a14), // meterPeak
        juce::Colour (0xff1e1a14), // readout
        juce::Colour (0xff9a8d75)  // outline
    };
}
```

`juce::Colour` has a constexpr `uint32` constructor, so the two palettes are constant-initialised (no static-init-order issue for components constructed before the editor body runs).

**Contrast check** (WCAG 2 relative-luminance ratios, computed numerically, not eyeballed). Text targets 4.5:1, graphics 3:1:

| Pair | brass | parchment |
|---|---|---|
| ink / background | 15.0 | 14.2 |
| faded / background | 5.9 | 5.1 |
| faded / panel (readout strip captions) | 5.5 | 4.6 |
| readout / panel | 13.9 | 12.7 |
| accent / background (value arcs) | 7.8 | 5.2 |
| accent / knobBody | 6.4 | 5.7 |
| runaway / panel (the word "runaway") | 4.7 | 4.6 |
| modulated / knobBody (mod arc) | 6.8 | 5.0 |
| emberGlow / background | 5.7 | 3.9 |
| knobPointer / knobBody | 12.3 | 15.5 |
| meterPeak / background | 11.8 | 3.4 (graphic only) |
| outline / background | 2.9 | 2.7 (decorative hairline; state is carried by arc + pointer, not the outline) |

Every text token clears 4.5:1 on the surface it is drawn on; every state-carrying graphic clears 3:1. The one deliberately sub-3:1 token is `outline`, which never carries state. Re-run the check whenever a token moves; the formula is eight lines of Python.

---

## 3. Knob component: `BrassKnob`

**Recommendation: a custom `juce::Component` (EngravedKnob pattern), not a LookAndFeel.** Reasons: the knob needs a `ParameterAttachment` with explicit gestures, a second arc fed from processor atomics, a per-knob double-click override that is *not* a parameter action (Decay -> Clear), a Shift-sensitive drag, sync detents on Time, hover/drag callbacks into the readout strip, and no text box or popup. Bending `juce::Slider` + LookAndFeel to that means fighting `RotaryHorizontalVerticalDrag`, velocity mode toggling per-modifier, and `Slider`'s own double-click and popup behaviour. EngravedKnob already proves the custom-component route in ~250 lines in two shipped plugins; copy its skeleton (attachment lambda, `shownValue` glide, `tick()`, wheel), replace the drawing.

```cpp
class BrassKnob : public juce::Component
{
public:
    enum class Size { large, mediumLarge, medium, small };

    BrassKnob (juce::RangedAudioParameter& param, juce::String label,
               juce::String hint, Size size);

    // Readout strip plumbing (no tooltips anywhere).
    std::function<void (BrassKnob&, bool entering)> onHover;
    std::function<void (BrassKnob&)> onLiveValue;   // fired on every drag step and attachment change
    juce::String labelText, hintText;
    std::function<juce::String()> valueText;         // default: parameter.getCurrentValueAsText(); Time/Decay override
    std::function<bool()> onDoubleClick;             // return true = handled (Decay -> Clear)

    // Decay: the ring past this normalised position is the runaway zone.
    void setRunawayZone (float normStart);           // -1 = none (default)
    // Time in sync mode: n detents drawn on the ring and the drag snaps to them.
    void setDetents (int count);                     // 0 = continuous
    // Live modulation from the processor: base value is the parameter; live is where
    // the engine actually is; spread is the fast component (audio-rate FM) as a band.
    void setModulation (bool active, float liveNorm, float spreadNorm);

    void tick();                                     // editor timer: glide + repaint gating
    float diameter() const;

    void paint (juce::Graphics&) override;
    void mouseEnter / mouseExit / mouseDown / mouseDrag / mouseUp / mouseDoubleClick / mouseWheelMove;

private:
    juce::RangedAudioParameter& parameter;
    juce::ParameterAttachment attachment;
    Size size;
    float normValue = 0, shownValue = -1;            // shownValue glides toward normValue (preset morph)
    float lastDragY = 0;                             // incremental drag, see below
    bool hovering = false, dragging = false;
    float runawayFrom = -1; int detents = 0;
    bool modActive = false; float modLive = 0, modSpread = 0, modShownLive = 0, modShownSpread = 0;
};
```

**Geometry** (D = diameter, c = circle centre, R = D/2 - 6 = body radius):

| Layer | Radius / size | Colour |
|---|---|---|
| Body disc | R, radial gradient `knobBody` (centre) to `knobBody.darker (0.25)` (edge); 1 px `outline` stroke | |
| Track ring | arc -135..+135 deg at `R + 4.5`, thickness `t = max (2, D * 0.045)` (large 4.7, medium 3.1) | `knobRing` |
| Value arc | same radius/thickness, from -135 deg to the value angle, round caps | `accent` |
| Pointer | line from `0.50R` to `0.90R`, width `max (2, D * 0.026)` (large 2.7, medium 2), round caps | `knobPointer` |
| Hub dot | radius `D * 0.03` at centre | `outline` |
| Mod arc | radius `R + 4.5 + t + 2.5`, thickness `t * 0.55`, from value angle to live angle; plus a dot of radius `t * 0.6` at the live angle | `modulated` |
| Mod band | same radius as mod arc, thickness `t * 0.55`, spanning `live +- spread`, alpha 0.35 | `modulated` |
| Runaway zone (Decay) | track ring segment from `runawayFrom` to 1.0 drawn at 0.35 alpha at rest; the value arc portion past `runawayFrom` drawn at full alpha; a 1.5 px radial tick at `runawayFrom` from `R + 2` to `R + 4.5 + t + 2` at 0.7 alpha | `runaway` |
| Detents (Time synced) | 13 radial ticks 3 px long at `R + 4.5 + t + 1`, 0.5 alpha | `faded` |
| Label | `drawCaps` 10 px semibold tracking 0.12 in the row at `D + 12` | `faded` at rest, `ink` when hovering or dragging |
| Value | 11 px regular in the row at `D + 28` (`kShowKnobValues = true`; flip to false if the design had none) | `faded` |

Angle for a normalised value: `deg = -135 + 270 * norm`. Decay's runaway zone: `runawayFrom = 1.0f / 1.15f = 0.8696` (normalised in a linear 0..1.15 range), i.e. the zone starts at 99.8 deg.

**States**

- Default: as above.
- Hover: track ring lightens (`knobRing.interpolatedWith (ink, 0.18)`), label turns `ink`, cursor `UpDownResizeCursor`, `onHover (*this, true)`.
- Drag: value arc `accent.brighter (0.12)`, pointer width + 0.6, label `ink`; `onLiveValue` every step so the strip stays live; `onHover (*this, false)` is NOT sent on exit while dragging.
- Modulated: mod arc + dot visible; the base pointer stays on the parameter value (what you set) and the dot shows where the engine is (what you hear). Hidden when `modActive == false` (route depth or Agitate at zero) so an unmodulated knob looks plain.
- Disabled (`setEnabled (false)`): whole paint inside `beginTransparencyLayer (0.35f)`, as EngravedKnob does. Used for Speed when the loop/gate toggle is on gate? No: Speed still matters in gate mode (fall time). Nothing is disabled in v1; keep the path for later.

**Modulation source (processor -> UI).** The engine publishes, per hero destination, two relaxed atomics:

```cpp
enum class ModDest { time, filter, decay, count };
struct ModView { std::atomic<float> liveNorm { 0 }, spreadNorm { 0 }, active { 0 }; };
ModView uiMod[(int) ModDest::count];
```

Audio side, once per block: `liveNorm` = the block-average of the modulated destination expressed in the parameter's normalised range (so the UI needs no knowledge of skew; the engine has the range via a callback or a precomputed table), `spreadNorm` = per-block peak |deviation from the block mean|, run through a 50 ms one-pole release (`kModSpreadReleaseSec = 0.05`) so audio-rate FM shows as a steady band rather than a strobing one; `active` = 1 when the route's effective depth (route depth x Agitate) exceeds 0.005. The editor timer calls `knob.setModulation (active, live, spread)` at 30 Hz; the knob eases `modShownLive += (modLive - modShownLive) * 0.5f` per tick (kills 30 Hz aliasing on slow sweeps without lagging visibly) and repaints only when either shown value moved by > 0.003.

**Drag and fine adjust.** Incremental, not distance-from-drag-start, so holding or releasing Shift mid-drag never jumps:

```cpp
constexpr float kDragPixelsPerRange = 220.0f;   // house value (EngravedKnob)
constexpr float kFineFactor = 8.0f;             // Shift: 1760 px for the full range

void BrassKnob::mouseDown (const juce::MouseEvent& e)
{
    lastDragY = e.position.y; dragging = true;
    attachment.beginGesture(); repaint();
}
void BrassKnob::mouseDrag (const juce::MouseEvent& e)
{
    const float pixels = kDragPixelsPerRange * (e.mods.isShiftDown() ? kFineFactor : 1.0f);
    float norm = juce::jlimit (0.0f, 1.0f, normValue + (lastDragY - e.position.y) / pixels);
    lastDragY = e.position.y;
    if (detents > 1)
        norm = std::round (norm * (float) (detents - 1)) / (float) (detents - 1);
    attachment.setValueAsPartOfGesture (parameter.convertFrom0to1 (norm));
    if (onLiveValue) onLiveValue (*this);
}
```

This is the "sensitivity tweak" route rather than `Slider::setVelocityModeParameters`; it is two lines and applies uniformly. The plan asks for it on Time; enable it on every knob (nothing to lose, and a fine Decay near unity is genuinely useful). Wheel: `norm + wheel.deltaY * 0.5f` as in the house knob, or `* 0.5f / kFineFactor` with Shift.

**Double-click.** Default resets to `parameter.getDefaultValue()` through `setValueAsCompleteGesture`. Decay's constructor site sets `decayKnob.onDoubleClick = [this] { proc.requestClear(); clearButton.flash(); return true; };` and the knob skips the reset when the hook returns true. Note the JUCE ordering: a double-click still delivers `mouseDown` first, which begins a gesture; `mouseUp` ends it, so the sequence is clean.

**Glide.** `tick()`: `if (|normValue - shownValue| > 0.002) shownValue += (normValue - shownValue) * kKnobGlide (0.35)`, snapping to the parameter during a drag (attachment lambda), exactly as EngravedKnob.

Constructor sites (editor initialiser list), for the record:

```
timeKnob      (param (id::time),      "time",       "delay length. shift-drag for fine. sync snaps to note values", Size::large)
decayKnob     (param (id::decay),     "decay",      "feedback. past 1.00 the loop regenerates. double-click clears the loop", Size::large)
filterKnob    (param (id::filter),    "filter",     "in-loop lowpass cutoff", Size::large)
blendKnob     (param (id::blend),     "blend",      "dry to wet, equal power", Size::large)
timeModKnob   (param (id::timemod),   "time mod",   "audio-rate modulation of the delay clock", Size::medium)
strengthKnob  (param (id::strength),  "strength",   "input gain and drive into the loop", Size::medium)
resonanceKnob (param (id::resonance), "resonance",  "filter resonance, up to self-oscillation", Size::medium)
absorbKnob    (param (id::absorb),    "absorb",     "how much of the wet signal is lost and darkened", Size::medium)
agitateKnob   (param (id::agitate),   "agitate",    "depth of all internal modulation", Size::mediumLarge)
speedKnob     (param (id::agitspeed), "speed",      "agitation rate", Size::medium)
```

Hint strings are shown in the readout strip, never as tooltips. No em dashes anywhere in them.

---

## 4. The ember: `Ember`

A 120 x 120 `juce::Component` with no mouse interaction (`setInterceptsMouseClicks (false, false)`), fed by the editor timer, not its own. One timer for the whole editor (`kUiHz = 30`); the flicker below is low-pass filtered noise, so it does not read as steppy at 30 Hz. If it does in practice, bump the editor timer to 60 Hz (every `tick()` in the UI is already gated on visible change, so the cost is small). Do not give the ember a private timer.

```cpp
class Ember : public juce::Component
{
public:
    void setEnergy (float energy01);   // from proc.getLoopEnergy(), each tick
    void setRunaway (bool);            // editor computes: decay >= 1.0 && energy > kRunawayEnergy
    void flash();                      // Clear: a dip to black then back
    void tick();
    void paint (juce::Graphics&) override;
private:
    float target = 0, shown = 0;       // smoothed energy
    float runawayMix = 0;              // 0 brass ember .. 1 red ember
    float n1 = 0, n2 = 0;              // two-pole filtered noise
    float phase = 0;                   // breathing clock (seconds)
    float flashDip = 0;                // 1 -> 0 after Clear
    float lastPainted = -1;
    juce::Random rng;
    struct Spark { float angle, radius, life; };
    Spark sparks[3];
};
```

**Processor side.** `uiLoopEnergy` is an `std::atomic<float>` written once per block: take the RMS of the feedback node (post-saturator) over the block, envelope it with 30 ms attack / 150 ms release (`kEnergyAttackSec`, `kEnergyReleaseSec`), map `20 log10 (env + 1e-6)` from `kEnergyFloorDb = -60` .. `kEnergyCeilDb = -6` to 0..1, clamp. Runaway is not an engine flag in v1: the editor computes `runaway = decayParam >= 1.0f && energy > kRunawayEnergy (0.5f)`. That covers "the loop is hot and set to regenerate" without asking the engine to decide what self-sustaining means.

**Smoothing (per tick at 30 Hz):**

```cpp
constexpr float kEmberAttack = 0.35f, kEmberRelease = 0.08f;  // ~100 ms up, ~400 ms down
constexpr float kFlickerBase = 0.06f, kFlickerHot = 0.10f, kFlickerSmooth = 0.30f;
constexpr float kBreathHz = 0.25f, kBreathDepth = 0.03f;
constexpr float kRunawayPulseHz = 1.2f, kRunawayPulseDepth = 0.08f, kRunawayMixRate = 0.15f;
constexpr float kFlashDecay = 0.12f;

void Ember::tick()
{
    shown += (target - shown) * (target > shown ? kEmberAttack : kEmberRelease);
    n1 += (rng.nextFloat() * 2 - 1 - n1) * kFlickerSmooth;
    n2 += (n1 - n2) * kFlickerSmooth;                       // two poles: no per-frame jitter
    phase += 1.0f / kUiHz;
    runawayMix += ((runaway ? 1.0f : 0.0f) - runawayMix) * kRunawayMixRate;
    flashDip = juce::jmax (0.0f, flashDip - kFlashDecay);

    const float flicker = n2 * (kFlickerBase + kFlickerHot * shown);
    const float breath  = kBreathDepth * shown * std::sin (juce::MathConstants<float>::twoPi * kBreathHz * phase);
    const float pulse   = kRunawayPulseDepth * runawayMix * std::sin (juce::MathConstants<float>::twoPi * kRunawayPulseHz * phase);
    live = juce::jlimit (0.0f, 1.0f, shown * (1 + flicker) + breath + pulse) * (1 - flashDip);

    // A cold ember is static: skip repaints below the floor so an idle editor costs nothing.
    if (shown < 0.01f && runawayMix < 0.01f && flashDip <= 0 && lastPainted <= 0.01f) return;
    if (std::abs (live - lastPainted) > 0.004f || runawayChanged) { lastPainted = live; repaint(); }
}
```

**Drawing** (c = component centre, E = live 0..1, glow colour `g = emberGlow.interpolatedWith (runaway, runawayMix)`, core colour `k = emberCore.interpolatedWith (runaway.brighter (0.4), runawayMix * 0.6)`):

| Layer | Radius | Fill | Alpha |
|---|---|---|---|
| 1 Ash bed (always) | 26 | `panel.darker (0.35)` disc, 1 px `outline` ring (ring goes `runaway` when `runawayMix > 0.5`) | ring 0.6 |
| 2 Outer glow | `30 + 28 E` | radial `ColourGradient` from `g` at centre to `g.withAlpha (0)` at the edge | centre alpha `0.55 E` (x 0.8 on parchment: `palette().background.getPerceivedBrightness() > 0.5`) |
| 3 Body | `14 + 10 E` | radial gradient `k` (centre) to `g` (edge) | `0.25 + 0.75 E` |
| 4 Core | `4 + 6 E` | `k` | `0.6 + 0.4 E` |
| 5 Sparks (E > 0.6) | 3 fixed slots; each spawns at random angle, radius `body + 4`, drifts outward 1.2 px/tick and dies over 8 ticks | 1.5 px dots in `k` | `(E - 0.6) * 2.5 * life` |

All alphas use `live`, so flicker modulates every layer coherently (the whole thing breathes) rather than radii only. Radial gradients: `juce::ColourGradient (g.withAlpha (a), c.x, c.y, g.withAlpha (0.0f), c.x + r, c.y, true)`. No allocation beyond the gradient object per paint; the spark array is fixed.

Cold state (E = 0): only the ash bed and a barely-there body at alpha 0.25, radius 14: the ember is visibly present but unlit. On Clear: `flash()` sets `flashDip = 1`, the ember goes black for ~3 ticks and re-lights from whatever energy remains.

---

## 5. Readout strip: `ReadoutStrip`

A 672 x 24 component with `panel` fill (rounded 3). Layout: name in small caps `faded` at x 12, value in `readout` 13 px at x `12 + nameWidth + 10`, optional aside right-aligned at `w - 12` (13 px). It holds a *source*, not text:

```cpp
class ReadoutStrip : public juce::Component
{
public:
    struct Source { juce::String name, value, aside; juce::Colour asideColour; };
    void setProvider (std::function<Source()> provider);  // null = preset name only
    void tick();                                          // re-query, repaint if changed
    void paint (juce::Graphics&) override;
};
```

**Events that change the provider**

| Event | Effect |
|---|---|
| Knob / toggle / slider `mouseEnter` | provider = that control's readout; it stays after `mouseExit` (sticky), so glancing away does not blank the strip |
| Any control's drag / attachment value change | if that control is the current provider, `tick()` picks the new text up; if not, nothing (host automation of an unrelated parameter must not hijack the strip) |
| Header controls (`PresetStation`, `ThemeButton`, `Clear`) `mouseEnter` | provider = their hint: `presets  click the name to browse, arrows to step`; `theme  switch to parchment`; `clear  flush the loop now` |
| Fresh editor | provider = Time knob, so the strip is never empty and the delay readout (note values when synced) is always visible somewhere |

`tick()` calls the provider every editor tick (cheap: a few string formats) and repaints only when the resulting `Source` differs from the last painted one. This is what keeps modulated values and synced seconds live without the controls knowing about the strip.

**Text per control**

- Generic knob: `name = labelText`, `value = valueText()` (parameter text, unit appended from `getLabel()` unless the text already contains a unit or a word, per the CLAUDE.md readout rule). Aside, when modulated: `"live " + textForNorm (modLive)` in `modulated`.
- Time, free: `"time"  "0.66 s"`. Time, synced: `"time"  "1/8"` with aside `"0.25 s"` in `faded`; when the division exceeds the 3.6 s clamp: aside `"3.60 s max"` in `runaway`. The effective seconds come from `proc.getTimeSeconds()` (an engine atomic `uiTimeSeconds` written per block from the smoothed target, pre-modulation). Note table, 13 entries indexed by `roundToInt (norm * 12)`: `1/32, 1/16T, 1/16, 1/8T, 1/16., 1/8, 1/4T, 1/8., 1/4, 1/2T, 1/4., 1/2, 1/1`. With no playhead (Standalone, UISnapshot) the engine assumes 120 BPM.
- Decay: `"decay"  "0.82"`; at 0.995..1.005 `"1.00"` with aside `"unity"` in `faded`; above 1.005 `"1.15"` with aside `"runaway"` in `runaway`. The per-knob value row under Decay shows the same word in `runaway`.
- Out slider: `"out"  "-3.2 dB"`, aside `"peak -1.0 dB"` (the held peak) in `faded`, or `"clip"` in `runaway` while the clip latch is lit.
- Toggles: `"sync"  "on"` / `"agitation"  "loop"`.

No tooltip window exists in the editor. Remove `juce::TooltipWindow tooltipWindow` from `PluginEditor.h`.

---

## 6. OUT slider with integrated meter: `OutSlider`

Horizontal, 672 x 40, bound to the `out` parameter (range -60..+6 dB with `-60` printed as `"-Inf"`, skewed so 0 dB sits at ~0.75 of the rail: `NormalisableRange (-60, 6)` with `setSkewForCentre (-9)`; the template's `floatParam` needs a `value <= -59.9f -> "-Inf"` special case). The whole point of "integrated" is that the meter and the handle share one scale: a dB value maps to rail x through the *parameter's* `convertTo0to1`, so the handle at 0 dB sits exactly over the 0 dBFS point of the meter and the player sees headroom as the gap between fill and handle.

Layout (component-local): label `out` small caps `faded` at 0..36; rail 44..612 (`railW = 568`), height 10, rounded 5, fill `knobRing`, 1 px `outline`; value text 616..672 right-aligned 12 px `ink`.

Drawing order:
1. Rail.
2. Meter fill from rail left to `railX + railW * normOf (meterDb)`, `meterFill` at 0.85 alpha, with a horizontal gradient to `meterPeak` over the last 10 % of the rail (above about 0 dB).
3. Peak-hold tick: 2 x 14 px line at `normOf (holdDb)`, `meterPeak`.
4. Clip cap: while the clip latch is lit, the rail's right end cap (last 6 px) fills `runaway`.
5. Scale ticks under the rail at -24, -12, -6, 0, +6 dB: 1 x 3 px `outline`, numerals only at -12 and 0 (8 px `faded`).
6. Handle: 14 x 24 rounded rect (r 3) centred on the value x, `knobBody` fill, 1 px `outline`, a 2 px vertical `accent` centre line. Hover: outline -> `ink`. Drag: fill -> `accent`, centre line -> `knobBody`.

Ballistics (UI side, per tick; the processor only publishes the per-block linear peak in the existing `uiOutputLevel`):

```cpp
constexpr float kMeterFloorDb = -60, kMeterFallDbPerSec = 24, kPeakHoldSec = 1.5f,
                kPeakFallDbPerSec = 12, kClipHoldSec = 1.0f;
peakDb  = Decibels::gainToDecibels (proc.getOutputLevel(), kMeterFloorDb);
meterDb = jmax (peakDb, meterDb - kMeterFallDbPerSec / kUiHz);
if (peakDb >= holdDb) { holdDb = peakDb; holdTicks = kPeakHoldSec * kUiHz; }
else if (--holdTicks <= 0) holdDb = jmax (kMeterFloorDb, holdDb - kPeakFallDbPerSec / kUiHz);
if (proc.getOutputLevel() >= 1.0f) clipTicks = kClipHoldSec * kUiHz; else clipTicks = jmax (0, clipTicks - 1);
```

Repaint when `meterDb` moved by > 0.2 dB, the hold or clip state changed, or the value changed. Interaction mirrors `VerticalFader`: `mouseDown` begins the gesture and jumps to the click (absolute), drag is absolute along the rail, `mouseUp` ends the gesture, double-click resets to default (0 dB), wheel nudges 0.5 dB. Hover/drag feed the readout strip as in section 5.

---

## 7. Header

**Preset selector: port Teder's `PresetStation` nearly verbatim.** The template's `PresetManager` already exposes the same API the station needs (`getPresets`, `loadPreset`, `saveCurrent`, `deletePreset`, `renamePreset`, `renameCurrentFile`, `presetNameExists`, `stepPreset`, `toggleStar`, `isStarred`, `getCurrentName`, `setCurrentName`, `isDirty`, `userFolder`). Changes when porting: replace `patent::` with `theme::` (`paper` -> `background`, `red` -> `runaway`, `serif` -> `font`, `drawSmallCaps` -> `drawCaps`), drop the `script` font for the name label (use `font (15, semibold)`), delete the row provenance tag's `PresetManager::modeName (info.mode)` part (Teder-specific; keep `factory` / `user`), keep the custom `saveCursor` only if `Theme` grows the glyph-cursor helper, otherwise `PointingHandCursor`. The browser overlay stays parented to the canvas at (202, 44, 316, 240) and closes on blank-canvas clicks (`Canvas::mouseDown -> presetStation.closeBrowser()`). A smaller hand-rolled selector would save perhaps 150 lines and lose search, stars, rename, save-as and two-click delete; not worth it. Remember the house trap: `PresetStation` caches Label/TextEditor colours in its constructor, so the editor calls `theme::setTheme (...)` first thing in its body and then `presetStation.applyThemeColours()`.

**Theme toggle: `ThemeButton`**, extracted from Teder's inner class into its own file pair (one class per pair). Sun while brass (you would switch to the light sheet), moon while parchment. Click:

```cpp
themeButton.onClick = [this]
{
    const auto next = theme::currentTheme() == theme::Kind::brass ? theme::Kind::parchment
                                                                  : theme::Kind::brass;
    proc.apvts.state.setProperty ("theme", theme::nameOf (next), nullptr);
    applyTheme (next);   // setTheme + presetStation.applyThemeColours() + canvas.repaint()
};
```

**Persistence and survival across preset loads.** The property lives on `apvts.state` under the key `"theme"` (values `"brass"` / `"parchment"`, default brass), so `getStateInformation`'s `copyState()` saves it with the session. Two things make it survive preset traffic:

1. `PresetManager` gains `static const juce::StringArray& editorProperties()` returning `{ "theme" }`. `loadPreset` snapshots those properties before `apvts.replaceState (...)` and re-applies them after; `saveCurrent` removes them from the copied tree before writing, so a preset never carries a theme. (Today Teder's presets *do* carry `darkMode`, which is exactly the leak the plan rules out.)
2. The editor timer re-reads `apvts.state["theme"]` every tick (a string compare) and calls `applyTheme` if it differs from `theme::currentTheme()`. That covers a session reload while the editor is open (`setStateInformation` replaces the tree) and any host that restores state after the editor exists.

The editor constructor reads it the same way the template does today: `theme::setTheme (theme::kindFromName (proc.apvts.state.getProperty ("theme", "brass")))` before anything caches colours. `UISnapshot` sets `"theme"` to `"parchment"` and rebuilds the editor for the alternate render (real construction path, as the harness comment insists).

Optional flourish, port as-is if wanted: Teder's `ThemeFade` snapshot crossfade (8 ticks). It is cosmetic; leave it for last.

**Copy.** Header strings: `Dybbuk`, `דיבוק`, preset name, nothing else. Hints and captions listed in sections 3 and 5 contain no em dashes; the check is a grep for `\xe2\x80\x94` over `Source/` added to the definition-of-done for UI commits.

---

## 8. Files under `Source/ui/` and editor timer duties

| File pair | Class | Role |
|---|---|---|
| `Theme.h/.cpp` | `theme::` | palettes, `setTheme`, fonts, `drawCaps` (extends the template) |
| `BrassKnob.h/.cpp` | `BrassKnob` | all ten rotaries (section 3) |
| `PillToggle.h/.cpp` | `PillToggle` | Sync (bool param, one label + lit dot) and Loop/Gate (choice param, two labels, sliding thumb glides at 0.35/tick like `SlideSwitch`); hover feeds the strip |
| `Ember.h/.cpp` | `Ember` | section 4 |
| `ClearButton.h/.cpp` | `ClearButton` | small caps text, `onClick`, `flash()` lights it `accent` for `kClearFlashTicks = 8` then fades; hover -> `ink` |
| `ReadoutStrip.h/.cpp` | `ReadoutStrip` | section 5 |
| `OutSlider.h/.cpp` | `OutSlider` | section 6 |
| `PresetStation.h/.cpp` | `PresetStation` | ported from Teder |
| `ThemeButton.h/.cpp` | `ThemeButton` | extracted from Teder's inner class |

`PluginEditor.h/.cpp` keeps the template's `Canvas` (background, wordmark, Hebrew, header rule) and owns everything above. `CMakeLists.txt` `PLUGIN_SOURCES` grows by the eight new `.cpp` files; `ENGINE_SOURCES` is untouched, so `EngineTest` never sees the editor.

Processor additions the UI depends on (message-thread accessors over relaxed atomics, all written once per block in the engine): `getOutputLevel()` (exists), `getLoopEnergy()`, `getTimeSeconds()`, `getModulation (ModDest, float& live, float& spread, bool& active)`, and `requestClear()` (sets an `std::atomic<bool>`; the engine consumes it at the next block boundary and flushes PT cores, SVF state and the DC blocker, no allocation). Clear is not a parameter, so it is not automated and not in presets; nothing to stamp.

**Editor timer (`startTimerHz (kUiHz = 30)`), in order:**

1. Theme watch: `apvts.state["theme"]` vs `theme::currentTheme()`; `applyTheme` on mismatch.
2. Poll engine: output peak -> `outSlider.setPeak`; loop energy -> `ember.setEnergy`; `ember.setRunaway (decay >= 1.0f && energy > kRunawayEnergy)`; the three `ModView`s -> `timeKnob / filterKnob / decayKnob .setModulation`.
3. Sync watch: if the `timesync` parameter flipped, `timeKnob.setDetents (synced ? 13 : 0)` (the knob's `valueText` lambda already branches on the same atomic).
4. `tick()` on: every `BrassKnob` (glide), both `PillToggle`s (thumb glide), `ember`, `clearButton` (flash fade), `outSlider` (ballistics), `presetStation` (dirty asterisk / name), `readout` (re-query provider), `themeFade` if ported.

Everything in step 4 repaints only on visible change, so an idle editor with a cold ember repaints nothing.

---

## 9. UISnapshot additions

Keep the template's `snap` lambda. Add a helper that pushes N blocks of a 220 Hz sine at 0.5 (already there) and one that pushes N blocks of silence. Before any "lit" snapshot run the dispatch loop for 1200 ms (36 ticks) so the ember's 0.08/tick release and the meter hold have settled; the existing 300 ms is enough for static states. Expose one deliberate test hook on the editor, `void DybbukEditor::showInReadout (const char* paramId)`, because a hover cannot be synthesised from the harness; it is documented as test-only.

| File | State | How to reach it |
|---|---|---|
| `editor_snapshot.png` | idle, brass, ember cold, strip shows time | construct, 300 ms |
| `editor_snapshot_active.png` | ember lit, meter + peak hold, value arcs | Decay 0.85, Time 0.5, Blend 0.5; 1 s of sine then 0.3 s silence; 1200 ms loop |
| `editor_snapshot_modulated.png` | mod arcs on Filter (slow sweep) and band on Time (audio-rate) | Agitate 1.0, Speed 0.3 Hz, Time Mod 0.6; 2 s of sine; 1200 ms loop |
| `editor_snapshot_runaway.png` | Decay arc past unity in red, ember red, strip "1.15 runaway" | Decay 1.15, 0.5 s sine then 2 s silence; `showInReadout ("decay")`; 1200 ms loop |
| `editor_snapshot_sync.png` | Time detents drawn, strip "1/8  0.25 s" | Sync on, Time at 5/12; `showInReadout ("time")` |
| `editor_snapshot_sync_clamped.png` | strip aside "3.60 s max" in red | Sync on, Time at 12/12 (1/1) at 120 BPM is 2 s, so also set the engine's fallback BPM to 40 via a test setter, or skip this one until sync lands |
| `editor_snapshot_parchment.png` | alternate theme, idle | set `"theme"` = `"parchment"`, rebuild the editor (real path) |
| `editor_snapshot_parchment_active.png` | alternate theme with the ember lit | same drive as active, after the rebuild; this is the one that catches a glow that vanishes on a light ground |
| `editor_snapshot_browser.png` | preset browser open over the rows | `presetStation.openBrowserForTest()` or set the browser visible through `getBrowser()` |

Reset the theme to brass at the end (`theme::setTheme (theme::Kind::brass)`) so the harness leaves the globals as it found them. Reviewing checklist for each image, per CLAUDE.md: every value readout is a musician's number (no `658.289`), the Decay red zone starts at the same angle in both themes, the mod arc sits outside the track ring and never crosses the label row, the Sync pill does not touch Time's ring, and nothing in the header, hints or captions contains an em dash.