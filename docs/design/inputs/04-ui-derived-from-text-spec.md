# 04. UI implementation spec (plan 2.7)

Status: implementers' spec. Everything here is buildable as written; where the
design left a choice open it is decided below and marked "decision".

Inputs: `dybbuk-plan.md` 2.6 / 2.7, `CLAUDE.md`, the UI design (`ui.md`), the
parameter/state design (`params.md`) for parameter ids, ranges and readout
strings, the verification design (`tests.md`) for the UISnapshot gate list,
the judge verdicts (the mod winner, DESIGN 1, publishes per-destination
modulation views; both PT verdicts graft `uiLoopEnergy` and `uiDelaySeconds`
as atomics), and the house idiom in `../teder` (`PresetStation`,
`EngravedKnob`, `SlideSwitch`, `ThemeButton`) and `../infinite_sustainer`
(`VerticalFader`). JUCE 8.0.12 source in `build/_deps/juce-src/` was read for
every API named below.

Four corrections to the UI design, all applied in the text that follows:

1. `juce::Colour (uint32)` is **not** constexpr in JUCE 8.0.12
   (`juce_Colour.h:66`, `explicit Colour (uint32 argb) noexcept;`). The
   palettes are therefore constexpr tables of `juce::uint32` ARGB words,
   converted to `juce::Colour` inside `theme::palette()` through a
   function-local static (section 2). No static-init-order hazard remains.
2. The knob geometry put the circle flush with the component's top edge while
   the modulation arc and dot reach `D/2 + 8.5` px from the centre, so the arc
   was clipped by the knob's own bounds. A top inset `kKnobTopInset = 12`
   fixes it; every circle centre stays exactly where the design placed it and
   the label / value rows keep the design's y ranges (section 1).
3. The Time Sync note table is the 14-entry `timemap::kDivisions` from the
   parameter design (`1/32 .. 1/2D, 1 bar`), not the 13-entry list in the UI
   design; detent count is read from `timemap::kDivisionCount`, never typed.
4. Double-click ordering, verified in `juce_Component.cpp:2300-2335`: in
   8.0.12 `mouseDoubleClick` is delivered from `internalMouseUp`, after
   `mouseUp`. A knob's drag gesture is therefore always closed before the
   double-click handler runs, and `setValueAsCompleteGesture` never nests a
   gesture (nesting asserts in debug builds,
   `juce_AudioProcessorParameter.cpp:70`). The wheel handler must refuse
   events while a drag is open for the same reason.

Two housekeeping items the UI depends on, owned by the state section but
listed here so they are not lost: the preset root tag becomes
`"DybbukPreset"` (the template still writes `"SustainerPreset"`), and
`docs/PLAN.md`'s "every control has a tooltip" gate is replaced by "every
control shows name + value (+ hint) in the readout strip on hover"; the
editor drops `juce::TooltipWindow` entirely, per plan 2.7 ("no floating
tooltips").

---

## 1. Canvas and coordinate table

Canvas `canvasW = 720`, `canvasH = 576`, drawn at 1:1 in a `Canvas` child and
scaled to the window with the template's `setTransform (scale)` scheme,
aspect locked, resize limits half to double. Side margin `padX = 24`, usable
x 24..696 (672 px). All coordinates below are canvas coordinates; every
component is a child of `canvas`.

### 1.1 Knob component geometry

```
kKnobTopInset = 12            // ring headroom above the circle
component width  W = D + 46   // 23 px each side
component height H = D + 54
circle centre      (W / 2, kKnobTopInset + D / 2)
label row          y = D + 24, height 14   (caps, 10 px)
value row          y = D + 40, height 12   (11 px)
```

| Constant | D | W x H |
|---|---|---|
| `kKnobLarge` | 104 | 150 x 158 |
| `kKnobMediumLarge` | 84 | 130 x 138 |
| `kKnobMedium` | 68 | 114 x 122 |
| `kKnobSmall` | 52 | 98 x 106 (reserved: future Crust trim; not placed in v1) |

Outermost drawn radius (mod dot) is `D/2 + 8.5` for large and `D/2 + 5.9`
for medium, so the 12 px inset leaves at least 3.5 px of headroom and the 23
px side margins are never approached.

Note `kKnobMedium` width is 114 (D + 46), not the 110 in the design table;
the second-row x positions below are re-derived so the circle centres stay at
x 99 / 249 / 471 / 621.

### 1.2 Header (y 0..46)

| Component | x | y | w | h | Notes |
|---|---|---|---|---|---|
| Wordmark "Dybbuk" (canvas paint) | 24 | 11 | 96 | 26 | 22 px bold, `ink`, left |
| Hebrew (canvas paint) | 124 | 12 | 56 | 24 | 19 px regular, `faded`, centredLeft; text is `juce::String::fromUTF8 ("\xd7\x93\xd7\x99\xd7\x91\xd7\x95\xd7\xa7")` |
| `PresetStation` | 230 | 10 | 260 | 26 | prev arrow 18 / name 224 / next arrow 18 |
| Preset browser overlay (`presetStation.getBrowser()`) | 202 | 44 | 316 | 240 | hidden until opened; topmost of the ordinary children |
| `ThemeButton` | 676 | 13 | 20 | 20 | sun while brass, moon while parchment |
| Header rule (canvas paint) | 24 | 46 | 672 | 1 | `ink` at alpha 0.15 |

### 1.3 Hero row (large knobs, D 104, W 150, H 158)

| Component | x | y | w | h | Circle centre |
|---|---|---|---|---|---|
| Time knob | 33 | 48 | 150 | 158 | (108, 112) |
| Sync toggle (`PillToggle`, bool) | 156 | 62 | 44 | 16 | at Time's 2 o'clock; Time's outermost radius at y 70 reaches x 151.5, so 4.5 px clear |
| Decay knob | 201 | 48 | 150 | 158 | (276, 112) |
| Filter knob | 369 | 48 | 150 | 158 | (444, 112) |
| Blend knob | 537 | 48 | 150 | 158 | (612, 112) |

Cell pitch 168. Label row y 176..190, value row y 192..204, component bottom
206. The Sync pill overlaps Time's rectangle (x 156..183) and is added after
the knobs so it wins hit-testing; it never touches Decay's rectangle (x 201).

### 1.4 Second row (medium knobs, D 68, W 114, H 122)

| Component | x | y | w | h | Circle centre |
|---|---|---|---|---|---|
| Time Mod knob | 42 | 208 | 114 | 122 | (99, 254) |
| Strength knob | 192 | 208 | 114 | 122 | (249, 254) |
| Loop/Gate toggle (`PillToggle`, choice) | 316 | 240 | 88 | 28 | centred on x 360, y 254 |
| Resonance knob | 414 | 208 | 114 | 122 | (471, 254) |
| Absorb knob | 564 | 208 | 114 | 122 | (621, 254) |

Label row y 300..314, value row y 316..328, component bottom 330.

### 1.5 Bottom strip (y 334..476)

| Component | x | y | w | h | Notes |
|---|---|---|---|---|---|
| Agitate knob (D 84, W 130, H 138) | 135 | 334 | 130 | 138 | circle centre (200, 388); label 442..456; value 458..470 |
| `Ember` | 300 | 336 | 120 | 120 | centre (360, 396); largest glow radius 58 stays inside |
| Speed knob (D 68, W 114, H 122) | 463 | 342 | 114 | 122 | circle centre (520, 388), level with Agitate's; label 434..448; value 450..462 |
| `ClearButton` | 332 | 458 | 56 | 16 | small caps "clear", `faded` at rest |

### 1.6 Readout strip and OUT slider

| Component | x | y | w | h | Internal zones (component-local) |
|---|---|---|---|---|---|
| `ReadoutStrip` | 24 | 484 | 672 | 24 | name from x 12; value after name + 10; aside right-aligned at w - 12 |
| `OutSlider` | 24 | 518 | 672 | 40 | label "out" x 0..36; rail x 44..612 (`railW = 568`), rail centre y 20; value text x 616..672 |

Bottom margin 18 px (558..576). Nothing else is drawn on the canvas: no
section captions, no extra rules. The readout strip's panel fill is the only
separator above the fader.

### 1.7 Z-order

Add children in this order: knobs (ten), toggles (two), `ember`,
`clearButton`, `readout`, `outSlider`, `presetStation`, `themeButton`, then
`presetStation.getBrowser()` via `addChildComponent` (hidden), then the
optional `ThemeFade` overlay topmost (`setInterceptsMouseClicks (false,
false)`).

`Canvas::mouseDown` (a click on bare background) calls
`presetStation.closeBrowser()`.

expected: with the editor at 720 x 576 every component rectangle above lies
inside 0..720 x 0..576 and no two knob rectangles intersect (the only
intentional overlap is Sync pill over Time knob).

---

## 2. Theme tokens (`Source/ui/Theme.h`, `Theme.cpp`)

The template's mutable globals `background / ink / faded / accent` stay (the
Canvas and any existing call sites keep compiling) and become mirrors of the
active palette, refreshed by `setTheme`.

```cpp
namespace theme
{
    enum class Kind : int { brass = 0, parchment = 1 };   // values ARE the stored property
    inline constexpr int  kThemeCount   = 2;
    inline constexpr Kind kDefaultTheme = Kind::brass;
    inline constexpr const char* kThemeProperty = "theme";  // apvts.state root property, int

    // Immutable ARGB words: constexpr-safe (juce::Colour is not).
    struct PaletteSpec
    {
        const char* name;
        juce::uint32 background, panel, ink, faded, accent,
                     emberCore, emberGlow,
                     knobBody, knobRing, knobPointer, modulated, runaway,
                     meterFill, meterPeak, readout, outline;
    };

    // The live colours, built from a PaletteSpec on first use.
    struct Palette
    {
        const char* name;
        juce::Colour background, panel, ink, faded, accent,
                    emberCore, emberGlow,
                    knobBody, knobRing, knobPointer, modulated, runaway,
                    meterFill, meterPeak, readout, outline;
        bool isLight() const { return background.getPerceivedBrightness() > 0.5f; }
    };

    const Palette& palette();                 // active set; components read it at paint time
    void setTheme (Kind kind);                // swaps the palette, refreshes the legacy globals
    void setTheme (int kindAsInt);            // jlimit (0, kThemeCount - 1)
    Kind currentTheme();
    Kind kindFromProperty (const juce::var& v);   // int 0/1, or legacy strings "brass"/"parchment"/bool darkMode; anything else -> brass
    const char* nameOf (Kind kind);           // "brass" / "parchment" (UI copy and snapshot file names)

    // Legacy shims: dark == brass.
    void setDarkMode (bool dark);             // setTheme (dark ? Kind::brass : Kind::parchment)
    bool isDarkMode();                        // currentTheme() == Kind::brass

    extern juce::Colour background, ink, faded, accent;   // mirrors of palette()
    inline juce::Colour inkA (float a) { return ink.withAlpha (a); }

    // Type. Empty = platform default sans (SF on macOS, which also carries the
    // Hebrew glyphs). Swapping to an embedded face later is this constant plus
    // a juce_add_binary_data target linked into BOTH Dybbuk and UISnapshot.
    inline constexpr const char* kTypeface = "";
    enum class Weight { regular, semibold, bold };
    juce::Font font (float px, Weight w = Weight::regular, float tracking = 0.0f);
    float textWidth (const juce::Font&, const juce::String&);

    // Spaced uppercase captions (every knob label, pill label, "out", "clear").
    void drawCaps (juce::Graphics&, const juce::String&, juce::Rectangle<float>,
                   juce::Justification, float px, Weight, float tracking, juce::Colour);

    // CLAUDE.md readout rule: never append a unit to text that already carries
    // a unit or a word. "-Inf", "1/8", "0.31 s", "Off", "Mono", "A2 +12" pass
    // through; "20" + "%" becomes "20 %"; "6.0" + "dB" becomes "6.0 dB".
    juce::String withUnit (const juce::String& valueText, const juce::String& label);
}
```

`Theme.cpp`:

```cpp
namespace
{
    constexpr theme::PaletteSpec kBrass {
        "brass",
        0xff15110c, // background
        0xff1f1912, // panel
        0xffefe5d0, // ink
        0xff9c8f77, // faded
        0xffc9a24e, // accent (brass)
        0xffffd98a, // emberCore
        0xffe26a2c, // emberGlow
        0xff2b241a, // knobBody
        0xff4c4131, // knobRing (track)
        0xffefe5d0, // knobPointer
        0xff7fb8ad, // modulated (verdigris: brass patina, reads as "not the knob")
        0xffe8502f, // runaway
        0xffc9a24e, // meterFill
        0xfff2c76a, // meterPeak
        0xffefe5d0, // readout
        0xff6b5c45  // outline
    };

    constexpr theme::PaletteSpec kParchment {
        "parchment",
        0xffefe8db, // background
        0xffe4dccc, // panel
        0xff1e1a14, // ink
        0xff66604f, // faded
        0xff7a5a10, // accent (dark brass)
        0xffffb640, // emberCore
        0xffc94a14, // emberGlow
        0xfff7f2e8, // knobBody
        0xffc6bba6, // knobRing
        0xff1e1a14, // knobPointer
        0xff25736a, // modulated
        0xffb32e1c, // runaway
        0xff7a5a10, // meterFill
        0xffc94a14, // meterPeak
        0xff1e1a14, // readout
        0xff9a8d75  // outline
    };

    theme::Palette build (const theme::PaletteSpec& s)
    {
        return { s.name, juce::Colour (s.background), juce::Colour (s.panel), /* ... all 16 in order */ };
    }

    const theme::Palette& paletteFor (theme::Kind k)
    {
        static const theme::Palette brass = build (kBrass);           // function-local: built on first use
        static const theme::Palette parchment = build (kParchment);
        return k == theme::Kind::parchment ? parchment : brass;
    }

    theme::Kind current = theme::kDefaultTheme;
}

const theme::Palette& theme::palette() { return paletteFor (current); }

void theme::setTheme (Kind kind)
{
    current = kind;
    const auto& p = palette();
    background = p.background; ink = p.ink; faded = p.faded; accent = p.accent;
}

juce::Font theme::font (float px, Weight w, float tracking)
{
    auto opts = juce::FontOptions (px);                       // height in px
    if (kTypeface[0] != 0) opts = opts.withName (kTypeface);
    opts = opts.withStyle (w == Weight::bold ? "Bold" : w == Weight::semibold ? "Semibold" : "Regular");
    return juce::Font (opts).withExtraKerningFactor (tracking);
}

void theme::drawCaps (juce::Graphics& g, const juce::String& text, juce::Rectangle<float> area,
                      juce::Justification just, float px, Weight w, float tracking, juce::Colour colour)
{
    const auto upper = text.toUpperCase();
    auto f = font (px * 1.15f, w, tracking);                  // system sans caps sit ~0.72 em; scale up a little
    const float wanted = textWidth (f, upper);
    if (wanted > area.getWidth() && wanted > 0.0f)            // shrink to fit, never clip mid-word
        f = font (juce::jmax (6.0f, px * 1.15f * area.getWidth() / wanted), w, tracking);
    g.setColour (colour); g.setFont (f); g.drawText (upper, area, just, false);
}

juce::String theme::withUnit (const juce::String& v, const juce::String& label)
{
    static const juce::String letters ("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ/");
    return (label.isEmpty() || v.containsAnyOf (letters)) ? v : v + " " + label;
}
```

If the platform has no "Semibold" face for the default sans, JUCE substitutes
the closest style; that is acceptable (the weight is a nicety, the caps
tracking carries the look).

**Contrast** (WCAG 2 relative luminance, text targets 4.5:1, state-carrying
graphics 3:1). Re-run whenever a token moves.

| Pair | brass | parchment |
|---|---|---|
| ink / background | 15.0 | 14.2 |
| faded / background | 5.9 | 5.1 |
| faded / panel (strip captions) | 5.5 | 4.6 |
| readout / panel | 13.9 | 12.7 |
| accent / background (value arcs) | 7.8 | 5.2 |
| accent / knobBody | 6.4 | 5.7 |
| runaway / panel (the word "runaway") | 4.7 | 4.6 |
| modulated / knobBody (mod arc) | 6.8 | 5.0 |
| emberGlow / background | 5.7 | 3.9 |
| knobPointer / knobBody | 12.3 | 15.5 |
| meterPeak / background | 11.8 | 3.4 (graphic only) |
| outline / background | 2.9 | 2.7 (decorative hairline, never carries state) |

**Persistence.** The property `theme` (int, 0 brass, 1 parchment) lives on
`apvts.state`; the state section lists it in `params::editorOnlyProperties()`
so `PresetManager::saveCurrent` strips it from preset files and `loadPreset`
preserves the live value across a load (section 10.3). Session save carries
it through `copyState()` unchanged. There is no `darkMode` property in
Dybbuk; `kindFromProperty` accepts the legacy forms only so a stray template
session does not throw the editor into the wrong theme.

---

## 3. The shared `Readout` record (`Source/ui/Readout.h`, header only)

One struct used by every control's value row and by the strip, so the
"runaway" / "unity" / "live" / clamp words are formatted in exactly one place
per control.

```cpp
struct Readout
{
    juce::String name;        // caps caption, e.g. "decay"
    juce::String value;       // musician's number with unit, e.g. "0.31 s", "1.15", "-Inf"
    juce::String aside;       // optional state word: "runaway", "unity", "live 0.92", "3.60 s max", "peak -1.0 dB", "clip"
    juce::Colour asideColour; // theme colour the aside is drawn in
    juce::String hint;        // one sentence, shown by the strip on hover when no aside is present
    bool operator== (const Readout& o) const;   // all string fields + asideColour (for repaint gating)
};
```

---

## 4. Knob component: `BrassKnob` (`Source/ui/BrassKnob.h/.cpp`)

Decision: a custom `juce::Component` on the `EngravedKnob` skeleton, not a
`LookAndFeel` over `juce::Slider`. Reasons: explicit `ParameterAttachment`
gestures, a second arc fed from processor atomics, a per-knob double-click
override that is not a parameter action (Decay -> Clear), a Shift-sensitive
incremental drag, sync detents, hover/drag callbacks into the readout strip,
no text box and no popup.

### 4.1 Public API

```cpp
class BrassKnob : public juce::Component
{
public:
    enum class Size { large, mediumLarge, medium, small };
    static constexpr int kKnobLarge = 104, kKnobMediumLarge = 84, kKnobMedium = 68, kKnobSmall = 52;
    static constexpr int kKnobTopInset = 12;
    static int diameterFor (Size s);
    static juce::Rectangle<int> boundsFor (Size s, juce::Point<int> circleCentre); // W = D+46, H = D+54, centre at (W/2, 12 + D/2)

    BrassKnob (juce::RangedAudioParameter& param, juce::String label, juce::String hint, Size size);

    // Readout strip plumbing (no tooltips anywhere).
    std::function<void (BrassKnob&)> onHover;            // mouseEnter, and mouseDown (so a click also claims the strip)
    std::function<Readout()> readoutProvider;            // default: generic (section 4.6); Time / Decay override
    Readout readout() const;                             // calls readoutProvider

    // Decay: the ring past this normalised position is the runaway zone (-1 = none).
    void setRunawayZone (float normStart);
    // Time in sync mode: n detents drawn on the ring and the drag / wheel snap to them (0 = continuous).
    void setDetents (int count);
    // Live modulation: base = the parameter; live = where the engine is; spread = audio-rate band half-width.
    void setModulation (bool active, float liveNorm, float spreadNorm);
    // Decay -> Clear. Return true = handled, the default reset is skipped.
    std::function<bool()> onDoubleClick;

    void tick();                                          // editor timer: glide + repaint gating
    int  diameter() const;
    float normalisedValue() const { return normValue; }
    juce::RangedAudioParameter& parameter() const { return param; }

    void paint (juce::Graphics&) override;
    void mouseEnter (const juce::MouseEvent&) override;
    void mouseExit  (const juce::MouseEvent&) override;
    void mouseDown  (const juce::MouseEvent&) override;
    void mouseDrag  (const juce::MouseEvent&) override;
    void mouseUp    (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;

private:
    juce::RangedAudioParameter& param;
    juce::ParameterAttachment attachment;                 // ctor lambda: normValue = convertTo0to1 (v); snap shownValue while dragging or on first sight; repaint
    juce::String labelText, hintText;
    Size size;
    float normValue = 0.0f, shownValue = -1.0f;          // shownValue glides toward normValue (preset morph); < 0 = adopt instantly
    float lastDragY = 0.0f;                               // incremental drag
    bool hovering = false, dragging = false;              // dragging == a gesture is open
    float runawayFrom = -1.0f;
    int detents = 0;
    bool modActive = false;
    float modLive = 0.0f, modSpread = 0.0f, modShownLive = 0.0f, modShownSpread = 0.0f;
    static constexpr bool kShowKnobValues = true;         // value row under the label
};
```

Named constants (in `BrassKnob.cpp`):

| Constant | Value | Meaning |
|---|---|---|
| `kDragPixelsPerRange` | 220.0f | vertical pixels for the full range (house value) |
| `kFineFactor` | 8.0f | Shift held: 1760 px for the full range |
| `kWheelPerNotch` | 0.5f | norm per wheel `deltaY` unit (house value); divided by `kFineFactor` with Shift |
| `kKnobGlide` | 0.35f | per-tick easing of `shownValue` toward `normValue` |
| `kKnobGlideEps` | 0.002f | below this the glide stops |
| `kModEase` | 0.5f | per-tick easing of the shown live / spread values |
| `kModRepaintEps` | 0.003f | repaint only when a shown mod value moved more than this |
| `kArcStartDeg` / `kArcEndDeg` | -135 / +135 | pointer sweep; `deg = -135 + 270 * norm` |

### 4.2 Geometry and layers

D = diameter, c = circle centre `(W/2, 12 + D/2)`, `R = D/2 - 6` (body
radius), `t = max (2, D * 0.045)` (track thickness: large 4.68, mediumLarge
3.78, medium 3.06), angles in degrees clockwise from 12 o'clock. Arcs are
`juce::Path::addCentredArc (c.x, c.y, r, r, 0, rad (a0), rad (a1), true)`
stroked with `PathStrokeType (thickness, curved, rounded)`.

| # | Layer | Radius / size | Colour |
|---|---|---|---|
| 1 | Body disc | radius R; radial gradient `knobBody` (centre) to `knobBody.darker (0.25)` (edge); 1 px `outline` stroke | |
| 2 | Track ring | arc -135..+135 at radius `R + 4.5`, thickness t | `knobRing` (hover: `knobRing.interpolatedWith (ink, 0.18)`) |
| 3 | Runaway zone (Decay only) | track segment from `angle (runawayFrom)` to +135 at alpha 0.35; a 1.5 px radial tick at `runawayFrom` from `R + 2` to `R + 4.5 + t + 2` at alpha 0.7 | `runaway` |
| 4 | Detents (Time synced) | `detents` radial ticks, 3 px long, from `R + 4.5 + t + 1`, 1 px wide, alpha 0.5, at `angle (i / (detents - 1))` | `faded` |
| 5 | Value arc | radius `R + 4.5`, thickness t, from -135 to `angle (shown)`; the part past `runawayFrom` (if any) drawn in `runaway` at full alpha instead of `accent` | `accent` (drag: `accent.brighter (0.12)`) |
| 6 | Mod band | radius `R + 4.5 + t + 2.5`, thickness `t * 0.55`, from `angle (live - spread)` to `angle (live + spread)`, alpha 0.35; drawn only when `modActive && modShownSpread > 0.002` | `modulated` |
| 7 | Mod arc | same radius / thickness as 6, from `angle (shown)` to `angle (modShownLive)`; plus a filled dot of radius `t * 0.6` at `angle (modShownLive)`; drawn only when `modActive` | `modulated` |
| 8 | Pointer | line from `0.50 R` to `0.90 R` along `angle (shown)`, width `max (2, D * 0.026)` (+0.6 while dragging), rounded caps | `knobPointer` |
| 9 | Hub dot | filled circle radius `D * 0.03` at c | `outline` |
| 10 | Label | `drawCaps` 10 px semibold tracking 0.12 in the row `y = D + 24, h 14` | `faded`; `ink` while hovering or dragging |
| 11 | Value row | `readout().value` in `font (11)` centred in the row `y = D + 40, h 12`; if `aside` is non-empty and the knob is Decay-style (aside is a state word), it follows the value after one space in `asideColour` | `faded` |

`angle (n) = degreesToRadians (-135 + 270 * jlimit (0, 1, n))`; `shown =
shownValue < 0 ? normValue : shownValue`.

expected: Decay's `runawayFrom = 1.0f / 1.15f = 0.86957`, so the red zone
starts at `-135 + 270 * 0.86957 = 99.8 deg` (about 3:20 on a clock face) and
ends at 135 deg; the zone is `35.2 deg` wide in both themes.

expected: on the large knob the track ring's outer edge is at 52.84 px and
the mod dot's outer edge at 60.5 px from the centre; with the centre at
`y = 112` the topmost drawn pixel is at `y = 51.5`, below the header rule
(46) and inside the component (top 48).

### 4.3 States

- Default: layers 1, 2, 5, 8, 9, 10, 11 (+3 on Decay, +4 on synced Time).
- Hover: track ring lightens, label `ink`, cursor `UpDownResizeCursor`,
  `onHover (*this)` fires once on `mouseEnter`.
- Drag: value arc `accent.brighter (0.12)`, pointer +0.6 px, label `ink`.
  `mouseExit` while dragging changes nothing (no flicker when the pointer
  leaves the rectangle during a long throw).
- Modulated: layers 6 and 7. The base pointer stays on the parameter (what
  you set); the dot shows where the engine is (what you hear). Hidden when
  `modActive == false` (route depth or Agitate at zero) so an unmodulated
  knob looks plain.
- Disabled (`setEnabled (false)`): whole paint inside
  `beginTransparencyLayer (0.35f)` / `endTransparencyLayer`, as
  `EngravedKnob`. Nothing is disabled in v1 (Speed still sets the fall time
  in gate mode); the path exists for later.

### 4.4 Interaction

Incremental drag (never distance-from-drag-start) so pressing or releasing
Shift mid-drag cannot jump:

```cpp
void BrassKnob::mouseDown (const juce::MouseEvent& e)
{
    lastDragY = e.position.y;
    dragging = true;
    attachment.beginGesture();
    if (onHover) onHover (*this);          // a click also claims the strip
    repaint();
}

void BrassKnob::mouseDrag (const juce::MouseEvent& e)
{
    const float pixels = kDragPixelsPerRange * (e.mods.isShiftDown() ? kFineFactor : 1.0f);
    float norm = juce::jlimit (0.0f, 1.0f, normValue + (lastDragY - e.position.y) / pixels);
    lastDragY = e.position.y;
    if (detents > 1)
        norm = std::round (norm * (float) (detents - 1)) / (float) (detents - 1);
    attachment.setValueAsPartOfGesture (param.convertFrom0to1 (norm));
}

void BrassKnob::mouseUp (const juce::MouseEvent&)
{
    if (dragging) { dragging = false; attachment.endGesture(); }
    repaint();
}

void BrassKnob::mouseDoubleClick (const juce::MouseEvent&)
{
    // 8.0.12 delivers this AFTER mouseUp of the second click, so no gesture is open here.
    if (onDoubleClick && onDoubleClick()) return;
    attachment.setValueAsCompleteGesture (param.convertFrom0to1 (param.getDefaultValue()));
}

void BrassKnob::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w)
{
    if (dragging) return;                  // never nest a gesture inside the drag's
    float norm;
    if (detents > 1)                       // one detent per notch, direction from the sign
        norm = normValue + (w.deltaY > 0 ? 1.0f : w.deltaY < 0 ? -1.0f : 0.0f) / (float) (detents - 1);
    else
        norm = normValue + w.deltaY * kWheelPerNotch / (e.mods.isShiftDown() ? kFineFactor : 1.0f);
    attachment.setValueAsCompleteGesture (param.convertFrom0to1 (juce::jlimit (0.0f, 1.0f, norm)));
}
```

Fine adjust is enabled on every knob, not only Time (the plan asks for Time;
a fine Decay near unity is genuinely useful and it costs nothing). With
detents, a plain drag snaps to the nearest division and Shift has no effect
(there is nothing finer than a note value).

Hit region is the whole component rectangle (house idiom). The label and
value rows are part of it, which is a larger target, not a bug.

### 4.5 Glide, modulation view, repaint gating

```cpp
void BrassKnob::tick()
{
    bool dirty = false;
    if (shownValue >= 0.0f && std::abs (normValue - shownValue) > kKnobGlideEps)
    {
        shownValue += (normValue - shownValue) * kKnobGlide;   // the preset morph
        dirty = true;
    }
    if (modActive)
    {
        const float l = modShownLive + (modLive - modShownLive) * kModEase;
        const float s = modShownSpread + (modSpread - modShownSpread) * kModEase;
        if (std::abs (l - modShownLive) > kModRepaintEps || std::abs (s - modShownSpread) > kModRepaintEps)
        { modShownLive = l; modShownSpread = s; dirty = true; }
    }
    if (dirty) repaint();
}

void BrassKnob::setModulation (bool active, float liveNorm, float spreadNorm)
{
    if (active != modActive) { modActive = active; if (! active) { modShownLive = normValue; modShownSpread = 0; } repaint(); }
    modLive = juce::jlimit (0.0f, 1.0f, liveNorm);
    modSpread = juce::jlimit (0.0f, 0.5f, spreadNorm);
}
```

The attachment lambda snaps `shownValue = normValue` while
`isMouseButtonDown()` or on first sight (`shownValue < 0`), exactly as
`EngravedKnob`; otherwise `tick()` glides it.

### 4.6 Readouts per knob

Generic provider (installed by the constructor):

```
name  = labelText
value = theme::withUnit (param.getCurrentValueAsText(), param.getLabel())
aside = modActive ? "live " + withUnit (param.getText (modShownLive, 0), label) : ""   (asideColour = modulated)
hint  = hintText
```

Overrides installed by the editor:

- **Time** (needs the processor): `value` = `param.getCurrentValueAsText()`,
  which the parameter design makes the division name when Sync is on and
  `timemap::timeReadout (seconds)` when free. Aside:
  free and unmodulated: none; synced: `timemap::timeReadout
  (proc.getDelaySeconds())` in `faded`, or, when `proc.isSyncClamped()`,
  `timeReadout (kDelayMaxSec) + " max"` (or `+ " min"` at the floor) in
  `runaway`; modulated (either mode): `"live " + timeReadout
  (delaySecondsForTime01 (modShownLive))` in `modulated` takes precedence.
- **Decay**: `value = juce::String (param.convertFrom0to1 (normValue), 2)`
  built from the number, never from the host string (so a host string that
  already carries "runaway" cannot double it). Aside: value in
  `[0.995, 1.005]` -> `"unity"` in `faded`; value `> 1.005` -> `"runaway"`
  in `runaway`; else modulation aside as generic.

expected readouts (defaults from the parameter design): Time 0.45 -> `0.25 s`
(0.02752 * 130.81^0.45 = 0.2467 s); Time 0.5 -> `0.31 s`; Time 0 -> `28 ms`;
Time 1 -> `3.60 s`; Decay 0.45 -> `0.45`; Decay 1.0 -> `1.00` + `unity`;
Decay 1.15 -> `1.15` + `runaway`; Filter 8000 -> `8000 Hz`; Resonance 15 ->
`15 %`; Absorb 20 -> `20 %`; Blend 50 -> `50 %`; Agitate 0 -> `0 %`; Speed
0.35 -> `2.9 s`; Strength 0 -> `0.0 dB`; Time Mod 0 -> `0 %`.

### 4.7 Constructor sites (editor initialiser list)

| Member | Parameter id | Label | Hint | Size |
|---|---|---|---|---|
| `timeKnob` | `params::id::time` | `time` | `delay length. shift-drag for fine control. sync snaps to note values` | large |
| `decayKnob` | `params::id::decay` | `decay` | `feedback. past 1.00 the loop regenerates. double-click clears the loop` | large |
| `filterKnob` | `params::id::filter` | `filter` | `in-loop lowpass cutoff. full right lets everything through` | large |
| `blendKnob` | `params::id::blend` | `blend` | `dry to wet, equal power in the middle` | large |
| `timeModKnob` | `params::id::timemod` | `time mod` | `audio-rate wobble on the delay clock, for clangs and ring mod colours` | medium |
| `strengthKnob` | `params::id::strength` | `strength` | `input gain and drive into the loop` | medium |
| `resonanceKnob` | `params::id::resonance` | `resonance` | `filter resonance, singing on its own past 90 percent` | medium |
| `absorbKnob` | `params::id::absorb` | `absorb` | `how much of the wet signal is lost and darkened` | medium |
| `agitateKnob` | `params::id::agitate` | `agitate` | `depth of all internal modulation` | mediumLarge |
| `speedKnob` | `params::id::agitspeed` | `speed` | `agitation rate, from a minute per breath up to audio rate` | medium |

After construction the editor sets `decayKnob.setRunawayZone (1.0f / 1.15f)`
(computed as `decayParam.convertTo0to1 (1.0f)` so a range change follows),
`decayKnob.onDoubleClick = [this] { proc.requestClear(); clearButton.flash(); ember.flash(); return true; }`,
and `timeKnob.setDetents (synced ? timemap::kDivisionCount : 0)`.

No hint string contains an em dash; none may be added.

---

## 5. Toggles: `PillToggle` (`Source/ui/PillToggle.h/.cpp`)

One class, two shapes, chosen by the parameter type.

```cpp
class PillToggle : public juce::Component
{
public:
    // Bool parameter: one caption + lit dot (Sync). Choice parameter with two
    // options: two captions + sliding thumb (Loop / Gate).
    PillToggle (juce::RangedAudioParameter& param, juce::StringArray captions, juce::String name, juce::String hint);

    std::function<void (PillToggle&)> onHover;
    Readout readout() const;              // name, current caption ("on"/"off" for the bool form), hint
    void tick();                          // thumb glide, 0.35 per tick like SlideSwitch

    void paint (juce::Graphics&) override;
    void mouseEnter / mouseExit / mouseDown / mouseDrag / mouseUp (const juce::MouseEvent&) override;

private:
    juce::RangedAudioParameter& param;
    juce::ParameterAttachment attachment; // lambda: index = roundToInt (convertTo0to1 (v) * (n - 1)); repaint
    juce::StringArray captions;           // bool form: { "sync" }; choice form: { "loop", "gate" }
    juce::String nameText, hintText;
    int index = 0;                        // 0 / 1
    float thumbPos = 0.0f;                // choice form only, glides 0..1
    bool hovering = false, dragging = false, gestureChangedIndex = false;
};
```

**Sync form (44 x 16).** Pill: rounded rect radius 8, fill `panel`, 1 px
`outline` (`ink` on hover). Dot: 6 px circle at x 10, `accent` when on,
`knobRing` when off. Caption "sync": `drawCaps` 8 px semibold tracking 0.10
in x 18..40, `ink` when on, `faded` when off. Click anywhere toggles
(`setValueAsCompleteGesture (convertFrom0to1 (index ? 0 : 1))`). Readout:
`sync` / `on` or `off`.

**Loop/Gate form (88 x 28).** Track: rounded rect 88 x 20 at y 4, radius 10,
fill `panel`, 1 px `outline` (`ink` on hover). Thumb: 42 x 16 at y 6, x =
`2 + thumbPos * 42`, radius 8, fill `knobBody.brighter (0.15)`, 1 px
`outline`. Captions "loop" in x 0..44 and "gate" in x 44..88, `drawCaps` 8 px
semibold tracking 0.10, current `ink`, other `faded`. Interaction as
`SlideSwitch`: `mouseDown` picks the nearest half and sets it, drag moves the
thumb continuously and snaps the parameter when the nearest half changes,
`mouseUp` lets `tick()` glide the thumb home; a click on the current half
flips to the other (a two-state control should never need aiming). Readout:
`agitation` / `loop` or `gate`.

Editor members: `syncToggle (param (id::timesync), { "sync" }, "sync", "lock the delay to note values at the host tempo; long divisions cap at 3.60 s")`
and `modeToggle (param (id::agitmode), { "loop", "gate" }, "agitation", "loop cycles on its own; gate fires one cycle per note you play")`.

---

## 6. The ember: `Ember` (`Source/ui/Ember.h/.cpp`)

A 120 x 120 component with no mouse interaction
(`setInterceptsMouseClicks (false, false)`), animated by the editor timer.
Decision: one timer for the whole editor at `kUiHz = 30`; the flicker is
two-pole filtered noise, so it does not read as steppy. If it does in
practice, raise `kUiHz` to 60 (every `tick()` in the UI is gated on visible
change). The ember never owns a timer.

```cpp
class Ember : public juce::Component
{
public:
    Ember();
    void setEnergy (float energy01);   // proc.getLoopEnergy(), each tick
    void setRunaway (bool);            // editor computes: decayValue > 1.005 && energy > kRunawayEnergy
    void flash();                      // Clear: dip to the ash bed, then re-light from whatever energy remains
    void tick();
    void paint (juce::Graphics&) override;
    float liveLevel() const { return live; }   // for the snapshot log
private:
    float target = 0, shown = 0, live = 0;
    bool  runaway = false; float runawayMix = 0;      // 0 brass ember .. 1 red ember
    float n1 = 0, n2 = 0;                             // two-pole filtered noise
    float phase = 0;                                  // seconds
    float flashDip = 0;                               // 1 -> 0 after Clear
    float lastPainted = -1; bool lastRunawayLit = false;
    juce::Random rng;
    struct Spark { float angle = 0, radius = 0, life = 0; } sparks[3];
};
```

Named constants:

| Constant | Value |
|---|---|
| `kUiHz` (editor) | 30 |
| `kEmberAttack` / `kEmberRelease` | 0.35 / 0.08 per tick (about 100 ms up, 400 ms down) |
| `kFlickerBase` / `kFlickerHot` / `kFlickerSmooth` | 0.06 / 0.10 / 0.30 |
| `kBreathHz` / `kBreathDepth` | 0.25 / 0.03 |
| `kRunawayPulseHz` / `kRunawayPulseDepth` / `kRunawayMixRate` | 1.2 / 0.08 / 0.15 |
| `kFlashDecay` | 0.12 per tick |
| `kRunawayEnergy` (editor) | 0.5 |
| `kEmberRepaintEps` | 0.004 |
| `kSparkDriftPx` / `kSparkLifeTicks` | 1.2 / 8 |

```cpp
void Ember::tick()
{
    shown += (target - shown) * (target > shown ? kEmberAttack : kEmberRelease);
    n1 += (rng.nextFloat() * 2.0f - 1.0f - n1) * kFlickerSmooth;
    n2 += (n1 - n2) * kFlickerSmooth;
    phase += 1.0f / (float) kUiHz;
    runawayMix += ((runaway ? 1.0f : 0.0f) - runawayMix) * kRunawayMixRate;
    flashDip = juce::jmax (0.0f, flashDip - kFlashDecay);

    const float flicker = n2 * (kFlickerBase + kFlickerHot * shown);
    const float breath  = kBreathDepth * shown * std::sin (twoPi * kBreathHz * phase);
    const float pulse   = kRunawayPulseDepth * runawayMix * std::sin (twoPi * kRunawayPulseHz * phase);
    live = juce::jlimit (0.0f, 1.0f, shown * (1.0f + flicker) + breath + pulse) * (1.0f - flashDip);

    // sparks: for each slot, if life > 0: radius += kSparkDriftPx, life -= 1/kSparkLifeTicks;
    //         else if live > 0.6 and rng.nextFloat() < 0.3: respawn at random angle, radius = body radius + 4, life = 1

    const bool runawayLit = runawayMix > 0.5f;
    if (shown < 0.01f && runawayMix < 0.01f && flashDip <= 0.0f && lastPainted <= 0.01f && lastPainted >= 0.0f)
        return;                                                        // cold ember: an idle editor repaints nothing
    if (std::abs (live - lastPainted) > kEmberRepaintEps || runawayLit != lastRunawayLit)
    { lastPainted = live; lastRunawayLit = runawayLit; repaint(); }
}
```

**Drawing** (c = centre (60, 60), E = `live`,
`glow = emberGlow.interpolatedWith (runaway, runawayMix)`,
`core = emberCore.interpolatedWith (runaway.brighter (0.4f), runawayMix * 0.6f)`,
`lightGround = palette().isLight()`):

| # | Layer | Radius | Fill | Alpha |
|---|---|---|---|---|
| 1 | Ash bed (always) | 26 | `panel.darker (0.35)` disc; 1 px ring `outline`, or `runaway` when `runawayMix > 0.5` | ring 0.6 |
| 2 | Outer glow | `30 + 28 E` | radial `ColourGradient (glow.withAlpha (a), c.x, c.y, glow.withAlpha (0), c.x + r, c.y, true)` | `a = 0.55 E` (x 0.8 on a light ground) |
| 3 | Body | `14 + 10 E` | radial gradient `core` (centre) to `glow` (edge) | `0.25 + 0.75 E` |
| 4 | Core | `4 + 6 E` | `core` | `0.6 + 0.4 E` |
| 5 | Sparks (E > 0.6) | 1.5 px dots at `c + (sin a, -cos a) * radius` | `core` | `(E - 0.6) * 2.5 * life` |

All alphas use `live`, so flicker modulates every layer together. No
allocation beyond the gradient objects per paint; the spark array is fixed.

expected: at E = 0 only the ash bed and a body of radius 14 at alpha 0.25 are
visible (present but unlit); at E = 1 the glow radius is 58 (inside the 60 px
half-size), body 24, core 10. After `flash()` the ember reaches the bed
colour within one tick and is back to 90 % of its level in 8 ticks
(`1 - 0.12 * 8 = 0.04` residual dip).

**What the ember reads.** `proc.getLoopEnergy()` is a 0..1 value the engine
publishes once per block, already log-mapped (0 at or below the engine's
`kEmberFloorDb`, 1 at or above its `kEmberCeilDb`; the engine section owns
those numbers and the attack/release before publication). The UI adds only
the smoothing above. Runaway is not an engine flag in v1: the editor computes
`runaway = decayValue > 1.005f && energy > kRunawayEnergy`, where
`decayValue` is the Decay parameter's plain value (the knob, not the
Follower-modulated live value).

---

## 7. Readout strip: `ReadoutStrip` (`Source/ui/ReadoutStrip.h/.cpp`)

672 x 24, `panel` fill, rounded 3. It holds a source, not text.

```cpp
class ReadoutStrip : public juce::Component
{
public:
    void setProvider (std::function<Readout()> provider);   // null = rest state (preset name)
    void setBypassed (bool);                                 // editor: bypass parameter >= 0.5
    void setDragging (bool);                                 // editor: any control currently dragging (hides the hint)
    void tick();                                             // re-query the provider; repaint only if the Readout changed
    void paint (juce::Graphics&) override;
    Readout current() const;                                 // for the snapshot log
private:
    std::function<Readout()> provider;
    Readout shown; bool bypassed = false, dragging = false;
};
```

Layout (component-local): name `drawCaps` 10 px semibold tracking 0.12 in
`faded` from x 12, width = `textWidth + 2`; value in `font (13)` `readout`
starting at `12 + nameWidth + 10`; right slot ends at `w - 12`, `font (12)`:

1. if `bypassed`: right slot = `bypassed` in `runaway` (overrides everything);
2. else if `aside` non-empty: right slot = `aside` in `asideColour`;
3. else if `hint` non-empty and not `dragging`: right slot = `hint` in `faded`,
   truncated with an ellipsis if it would collide with the value (leave 16 px);
4. else empty.

**Provider changes (all wired in the editor):**

| Event | Effect |
|---|---|
| Knob / toggle / out slider `onHover` (mouseEnter or mouseDown) | provider = that control's `readout`; it stays after the pointer leaves (sticky) |
| A control's value changes (drag or host automation) | if it is the current provider, `tick()` picks the new text up; otherwise nothing (automation of an unrelated parameter never hijacks the strip) |
| `PresetStation` hover | provider = `{ "presets", currentName (+ " *" if dirty), "", {}, "click the name to browse, arrows to step" }` |
| `ThemeButton` hover | `{ "theme", nameOf (current), "", {}, "switch to " + nameOf (other) }` |
| `ClearButton` hover | `{ "clear", "", "", {}, "flush the loop now. double-click decay does the same" }` |
| Fresh editor | provider = `timeKnob`, so the strip is never empty and the delay readout (note values when synced) is always visible somewhere |

Rest state (provider null, defensive only): `{ "preset", currentName }`.

`tick()` calls the provider every editor tick (a few string formats) and
repaints only when the resulting `Readout` differs from `shown`. This is what
keeps modulated values and synced seconds live without the controls knowing
about the strip.

expected strip lines (name, value, right slot):
`time  0.25 s  delay length. shift-drag for fine control. sync snaps to note values` (fresh editor, brass);
`time  1/8  0.25 s` (Sync on, Time at 5/13, 120 BPM);
`time  1 bar  3.60 s max` (Sync on, Time at 13/13, 40 BPM; the aside is red);
`decay  1.00  unity`; `decay  1.15  runaway` (red);
`out  -3.2 dB  peak -1.0 dB`; `out  0.0 dB  clip` (red, latched 1 s);
`sync  on`; `agitation  gate`; any state while bypassed: right slot `bypassed` (red).

---

## 8. OUT slider with integrated meter: `OutSlider` (`Source/ui/OutSlider.h/.cpp`)

Horizontal, 672 x 40, bound to `params::id::out` (the parameter design:
`skewedRange (-60, 6, centre -12)`, `-60` printed `-Inf`, label `dB`). The
point of "integrated" is one scale: every dB value, meter or handle, maps to
rail x through the parameter's own `convertTo0to1`, so the handle at 0 dB sits
exactly over the meter's 0 dBFS point and headroom is the visible gap between
fill and handle.

```cpp
class OutSlider : public juce::Component
{
public:
    OutSlider (juce::RangedAudioParameter& outParam, juce::String hint);
    std::function<void (OutSlider&)> onHover;
    void setPeak (float linearPeak);       // proc.getOutputLevel(), each tick, BEFORE tick()
    void tick();                           // ballistics + repaint gating
    Readout readout() const;               // "out", value, aside "peak -1.0 dB" (faded) or "clip" (runaway)
    void paint (juce::Graphics&) override;
    void mouseEnter / mouseExit / mouseDown / mouseDrag / mouseUp / mouseDoubleClick / mouseWheelMove overrides;
private:
    float xForDb (float db) const { return railX + railW * param.convertTo0to1 (juce::jlimit (kMeterFloorDb, 6.0f, db)); }
    float dbForX (float x) const  { return param.convertFrom0to1 (juce::jlimit (0.0f, 1.0f, (x - railX) / railW)); }
    juce::RangedAudioParameter& param; juce::ParameterAttachment attachment;
    float normValue = 0; float inputPeak = 0, meterDb = -60, holdDb = -60; int holdTicks = 0, clipTicks = 0;
    bool hovering = false, dragging = false; float lastMeterPainted = -60;
    static constexpr float railX = 44, railW = 568, railY = 15, railH = 10;
};
```

Constants:

| Constant | Value |
|---|---|
| `kMeterFloorDb` | -60 |
| `kMeterFallDbPerSec` | 24 |
| `kPeakHoldSec` | 1.5 |
| `kPeakFallDbPerSec` | 12 |
| `kClipHoldSec` | 1.0 |
| `kMeterRepaintDb` | 0.2 |
| `kWheelDb` | 0.5 |

Ballistics, per tick (the processor publishes only the per-block linear peak
in `uiOutputLevel`):

```cpp
const float peakDb = juce::Decibels::gainToDecibels (inputPeak, kMeterFloorDb);
meterDb = juce::jmax (peakDb, meterDb - kMeterFallDbPerSec / kUiHz);
if (peakDb >= holdDb) { holdDb = peakDb; holdTicks = (int) (kPeakHoldSec * kUiHz); }
else if (--holdTicks <= 0) holdDb = juce::jmax (kMeterFloorDb, holdDb - kPeakFallDbPerSec / kUiHz);
clipTicks = inputPeak >= 1.0f ? (int) (kClipHoldSec * kUiHz) : juce::jmax (0, clipTicks - 1);
// repaint when |meterDb - lastMeterPainted| > kMeterRepaintDb, or hold / clip state changed
```

Drawing order (component-local, rail rect `(44, 15, 568, 10)`, radius 5):

1. Label `out`: `drawCaps` 10 px semibold tracking 0.12 `faded`, x 0..36, centredLeft.
2. Rail: fill `knobRing`, 1 px `outline`.
3. Meter fill from `railX` to `xForDb (meterDb)`: `meterFill` at alpha 0.85, with a horizontal gradient to `meterPeak` over the last 10 % of the rail (x > 555).
4. Peak-hold tick: 2 x 14 px line at `xForDb (holdDb)`, `meterPeak` (hidden while `holdDb <= kMeterFloorDb`).
5. Clip cap: while `clipTicks > 0` the rail's last 6 px fill `runaway`.
6. Scale ticks under the rail (y 27..30) at -24, -12, -6, 0, +6 dB: 1 x 3 px `outline`; numerals "-12" and "0" only, `font (8)` `faded`, centred under their ticks (y 30..38).
7. Handle: 14 x 24 rounded rect (r 3) centred on `xForDb (value)`, y 8; fill `knobBody`, 1 px `outline`, a 2 px vertical `accent` centre line. Hover: outline `ink`. Drag: fill `accent`, centre line `knobBody`.
8. Value text: `withUnit (param.getCurrentValueAsText(), param.getLabel())` in `font (12)` `ink`, right-aligned in x 616..672.

expected rail x for the scale ticks with the parameter design's skew (skew
factor `log 0.5 / log (48/66) = 2.176`): -24 dB -> 195.9, -12 dB -> 328.0,
-6 dB -> 411.0, 0 dB -> 505.6, +6 dB -> 612.0 (all +-1 px on the
screenshot). The handle at the default 0 dB therefore sits at x 505.6, and a
meter reading 0 dBFS fills exactly up to the handle's centre line.

Interaction mirrors `VerticalFader`: `mouseDown` begins the gesture and jumps
to the click (absolute, `dbForX`), drag is absolute along the rail,
`mouseUp` ends the gesture, double-click resets to the default (0 dB), wheel
nudges `kWheelDb` (refused while dragging). Cursor `LeftRightResizeCursor`.
`onHover` on enter and on mouseDown.

Readout: `{ "out", value, aside, colour, hint }` with aside `"clip"` in
`runaway` while `clipTicks > 0`, else `"peak " + String (holdDb, 1) + " dB"`
in `faded` when `holdDb > kMeterFloorDb`, else empty. Hint: `output level
after the blend; the meter behind it shows the same scale`.

---

## 9. Clear: `ClearButton` (`Source/ui/ClearButton.h/.cpp`)

56 x 16. `drawCaps` "clear" 9 px semibold tracking 0.14, centred. Rest
`faded`; hover `ink` (cursor `PointingHandCursor`); flash: text and a 1 px
rounded (r 8) outline in `accent` for `kClearFlashTicks = 8`, then a linear
fade back to the rest colour over `kClearFadeTicks = 8`.

```cpp
class ClearButton : public juce::Component
{
public:
    std::function<void()> onClick;                 // fired on mouseDown (momentary: a performer wants it on press)
    std::function<void (ClearButton&)> onHover;
    void flash();                                  // flashTicks = kClearFlashTicks + kClearFadeTicks
    void tick();                                   // counts down, repaints while > 0
    Readout readout() const;                       // { "clear", "", "", {}, hint }
    void paint (juce::Graphics&) override;
    void mouseEnter / mouseExit / mouseDown (const juce::MouseEvent&) override;
private:
    int flashTicks = 0; bool hovering = false;
};
```

Editor wiring: `clearButton.onClick = [this] { proc.requestClear(); clearButton.flash(); ember.flash(); }`.
Clear is not a parameter: not automated, not in presets, nothing to stamp.
The processor's `requestClear()` is one atomic increment; the engine consumes
it at the next block boundary (engine section). The editor additionally
watches `proc.getClearsServed()` each tick and, when it increments without a
matching local click (a Clear that arrived by another path, e.g. a future
MIDI mapping), calls `ember.flash()` so the ember always dips when the loop
actually empties.

---

## 10. Header

### 10.1 Preset selector: `PresetStation` (`Source/ui/PresetStation.h/.cpp`), ported from Teder

Port Teder's class nearly verbatim; the template's `PresetManager` already has
every method it calls (`getPresets`, `loadPreset`, `saveCurrent`,
`deletePreset`, `renamePreset`, `renameCurrentFile`, `presetNameExists`,
`stepPreset`, `toggleStar`, `isStarred`, `getCurrentName`, `setCurrentName`,
`isDirty`, `userFolder`). Changes when porting:

- `patent::` -> `theme::` (`paper` -> `background`, `red` -> `runaway`,
  `serif (px, w)` -> `font (px, w)`, `drawSmallCaps` -> `drawCaps`,
  `inkA` stays). Browser card: fill `background`, 2 px `ink` border, list
  selection `inkA (0.08)`, search box background `inkA (0.05)`.
- The name label uses `font (15, semibold)` (no script face); the dirty
  asterisk in `runaway` rides after the centred name exactly as in Teder.
- Row provenance tag: drop `PresetManager::modeName (info.mode)`; keep
  `factory` / `user`.
- Cursor: `PointingHandCursor` everywhere (no glyph cursors in Dybbuk).
- Add `void openBrowserForTest()` (calls the private `browser->open()`), used
  by UISnapshot only.
- `applyThemeColours()` re-caches the Label / TextEditor colours; the editor
  calls it after every `theme::setTheme`. Constructor trap from the template
  note: `theme::setTheme (...)` must run before `PresetStation` is
  constructed, i.e. first thing in the editor constructor body, and the
  station is declared after `proc`.
- Hover feeds the strip (section 7); the station gets
  `std::function<void()> onHover` fired from `mouseEnter`.
- Layout inside 260 x 26: prev arrow 18 px, name 224 px, next arrow 18 px.
  Arrows are filled triangles in `ink`, `reduced (5, 8)` of their box.
  Browser overlay `setSize (316, 240)`, positioned by the editor at
  (202, 44); closes on a blank canvas click, on choosing a preset, and on a
  second click on the name.

### 10.2 Theme toggle: `ThemeButton` (`Source/ui/ThemeButton.h/.cpp`)

Teder's inner class, extracted into its own file pair (one class per pair),
`patent` -> `theme`, tooltip client removed, plus `onHover`. 20 x 20. Sun
while brass (eight 3 px rays from radius 6.5 to 9.5, 4.5 px disc), moon while
parchment (14 px disc with a 13 px bite, non-zero winding off), `ink` at
alpha 0.75 (1.0 on hover). It shows the mode you would switch to.

```cpp
themeButton.onClick = [this]
{
    const auto next = theme::currentTheme() == theme::Kind::brass ? theme::Kind::parchment : theme::Kind::brass;
    proc.apvts.state.setProperty (theme::kThemeProperty, (int) next, nullptr);
    applyTheme (next);
};
```

### 10.3 Theme persistence and survival across preset loads

The property is `theme` on `apvts.state` (int). Two things make it survive:

1. The state section's `PresetManager` keeps `params::editorOnlyProperties()`
   (`{ "theme", "uiScale" }`) out of preset files and restores the live values
   after `replaceState` in `loadPreset`. Setting a root property never touches
   a parameter listener, so a theme flip never dirties the preset.
2. The editor timer re-reads `apvts.state[theme::kThemeProperty]` every tick
   (one `var` compare) and calls `applyTheme` if `kindFromProperty (v) !=
   theme::currentTheme()`. That covers a session reload while the editor is
   open (`setStateInformation` replaces the tree) and any host that restores
   state after the editor exists.

The editor constructor starts with
`theme::setTheme (theme::kindFromProperty (proc.apvts.state.getProperty (theme::kThemeProperty, (int) theme::kDefaultTheme)));`
before any Label caches colours.

`applyTheme (Kind k)`: `theme::setTheme (k); presetStation.applyThemeColours(); canvas.repaint();`
(a full-canvas repaint reaches every child). Optional flourish, port last or
skip: Teder's `ThemeFade` (snapshot of the canvas at scale 2 before the swap,
alpha `-= 1/8` per tick, 8 ticks).

### 10.4 Copy

Header strings: `Dybbuk`, the Hebrew word, the preset name. Nothing else.
No em dashes anywhere in `Source/`; the definition of done for every UI
commit includes `grep -rn $'\xe2\x80\x94' Source/` returning nothing.

---

## 11. Editor (`Source/PluginEditor.h/.cpp`)

### 11.1 Members, in declaration order (construction order matters: `proc` first, station after the theme is set)

```cpp
class DybbukEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit DybbukEditor (DybbukProcessor&);
    ~DybbukEditor() override;
    void resized() override;

    // Test-only hooks for Tests/UISnapshot.cpp (a hover cannot be synthesised from a harness).
    void showInReadout (const char* paramIdOrControl);   // "time", "decay", ..., "out", "sync", "agitmode", "clear"
    void openPresetBrowserForTest();
    juce::String readoutLineForLog() const;              // "name | value | aside" of the strip, for the snapshot log

private:
    static constexpr int canvasW = 720, canvasH = 576;
    static constexpr int kUiHz = 30;
    static constexpr float kRunawayEnergy = 0.5f;

    struct Canvas : juce::Component { void paint (juce::Graphics&) override; void mouseDown (const juce::MouseEvent&) override;
                                      std::function<void()> onBlankClick; };

    void timerCallback() override;
    void applyTheme (theme::Kind);
    void wireReadout (BrassKnob&);                       // onHover -> readout.setProvider ([&k] { return k.readout(); })
    juce::RangedAudioParameter& param (const char* id) const;

    DybbukProcessor& proc;
    Canvas canvas;

    BrassKnob timeKnob, decayKnob, filterKnob, blendKnob,
              timeModKnob, strengthKnob, resonanceKnob, absorbKnob,
              agitateKnob, speedKnob;
    PillToggle syncToggle, modeToggle;
    Ember ember;
    ClearButton clearButton;
    ReadoutStrip readout;
    OutSlider outSlider;
    PresetStation presetStation;                         // after theme::setTheme in the ctor body? No: members construct
                                                         // before the body, so the theme is set in the initialiser list via
                                                         // a helper member declared FIRST (see 11.2)
    ThemeButton themeButton;
    // optional: ThemeFade themeFade;

    std::atomic<float>* pTimeSync = nullptr;             // cached raw values the timer reads
    std::atomic<float>* pDecay = nullptr;
    std::atomic<float>* pBypass = nullptr;
    bool lastSynced = false;
    int  clearsSeen = 0, clearsClicked = 0;
    bool constructing = true;                            // resized() must not persist uiScale during setSize in the ctor
};
```

Because members are constructed before the constructor body, the theme must
be applied before `presetStation` is built. Decision: declare a tiny first
member `struct ThemeInit { explicit ThemeInit (DybbukProcessor& p) { theme::setTheme (theme::kindFromProperty (p.apvts.state.getProperty (theme::kThemeProperty, (int) theme::kDefaultTheme))); } } themeInit;`
immediately after `proc`. `PresetStation` and every other member then see the
right palette in their constructors, and the body still calls
`presetStation.applyThemeColours()` once for good measure.

`Canvas::paint`: `fillAll (theme::background)`; wordmark; Hebrew; header rule
(section 1.2). Nothing else.

### 11.2 Constructor body, in order

1. `addAndMakeVisible (canvas); canvas.setBounds (0, 0, canvasW, canvasH);`
   `canvas.onBlankClick = [this] { presetStation.closeBrowser(); };`
2. `setBounds` of every child from the table in section 1 and add them in the
   z-order of 1.7.
3. Decay: `setRunawayZone (param (id::decay).convertTo0to1 (1.0f))`,
   `onDoubleClick` (section 4.7). Time: readout override (4.6), detents from
   the current sync state. Decay: readout override (4.6).
4. `wireReadout` for the ten knobs; hovers for toggles, out slider, clear,
   station, theme button (section 7 table).
5. `clearButton.onClick`, `themeButton.onClick`.
6. `readout.setProvider ([this] { return timeKnob.readout(); })` (fresh
   editor).
7. Cache `pTimeSync`, `pDecay`, `pBypass` via `apvts.getRawParameterValue`.
8. `presetStation.applyThemeColours()`.
9. `startTimerHz (kUiHz)`.
10. `setResizable (true, true); setResizeLimits (canvasW / 2, canvasH / 2, canvasW * 2, canvasH * 2);`
    `getConstrainer()->setFixedAspectRatio (720.0 / 576.0);`
    `const float s = jlimit (0.5f, 2.0f, (float) apvts.state.getProperty ("uiScale", 1.0f));`
    `setSize (roundToInt (canvasW * s), roundToInt (canvasH * s)); constructing = false;`

No `juce::TooltipWindow` member. No `setTooltip` calls anywhere.

### 11.3 `resized()`

```cpp
const float scale = juce::jmin ((float) getWidth() / canvasW, (float) getHeight() / canvasH);
canvas.setTransform (juce::AffineTransform::scale (scale));
if (! constructing)
    proc.apvts.state.setProperty ("uiScale", scale, nullptr);   // editor-only property, kept out of presets by the state section
```

### 11.4 Timer duties (`kUiHz = 30`), in order

1. **Theme watch**: `kindFromProperty (apvts.state[theme::kThemeProperty])`
   vs `theme::currentTheme()`; `applyTheme` on mismatch.
2. **Poll the engine** (relaxed atomics through processor accessors, section
   12): `outSlider.setPeak (proc.getOutputLevel())`; `const float energy =
   proc.getLoopEnergy(); ember.setEnergy (energy); ember.setRunaway
   (pDecay->load() > 1.005f && energy > kRunawayEnergy)`; for each of
   `ModDest::time / filter / decay`: `const auto m = proc.getModView (d);
   knob.setModulation (m.active, m.liveNorm, m.spreadNorm)` on `timeKnob /
   filterKnob / decayKnob`.
3. **Sync watch**: `const bool synced = pTimeSync->load() >= 0.5f; if
   (synced != lastSynced) { lastSynced = synced; timeKnob.setDetents (synced
   ? timemap::kDivisionCount : 0); }`.
4. **Clear watch**: `const int served = proc.getClearsServed(); if (served !=
   clearsSeen) { if (served > clearsClicked) ember.flash(); clearsSeen =
   served; }` (a locally clicked Clear already flashed on press;
   `clearsClicked` is bumped in the click lambdas).
5. **Bypass indicator**: `readout.setBypassed (pBypass->load() >= 0.5f)`.
6. **Dragging flag**: `readout.setDragging (juce::Component::isMouseButtonDownAnywhere())`.
7. `tick()` on: the ten knobs, both toggles, `ember`, `clearButton`,
   `outSlider`, `presetStation` (dirty asterisk / name), `readout` (re-query),
   `themeFade` if ported.

Every `tick()` repaints only on visible change, so an idle editor with a cold
ember repaints nothing.

### 11.5 Test hooks

`showInReadout (id)`: sets the strip provider to the named control's
`readout()` (ids as in the parameter table plus `"out"`, `"sync"`,
`"agitmode"`, `"clear"`, `"presets"`, `"theme"`). `openPresetBrowserForTest()`
forwards to the station. `readoutLineForLog()` returns
`name + " | " + value + " | " + aside`. Nothing else in production code exists
for tests; UISnapshot fakes host tempo through `AudioProcessor::setPlayHead`
(section 14).

---

## 12. What the UI needs from the processor (contract for sections 02 / 03)

All message-thread accessors over relaxed atomics that the engine writes once
per block; tearing is acceptable. Names are the ones the editor calls.

```cpp
// DybbukProcessor (public)
float getOutputLevel() const;            // exists: linear per-block peak, post Out
float getLoopEnergy() const;             // engine uiLoopEnergy, 0..1 log-mapped
float getDelaySeconds() const;           // engine uiDelaySeconds: the smoothed, pre-modulation delay actually in force (free or synced)
bool  isSyncClamped() const;             // the processor resolved sync this block and hit kDelayMinSec or kDelayMaxSec
int   getClearsServed() const;           // engine uiClearsServed
void  requestClear();                    // forwards to the engine's atomic counter mailbox; never blocks

enum class ModDest { time = 0, filter = 1, decay = 2, count = 3 };
struct ModView { float liveNorm = 0.0f; float spreadNorm = 0.0f; bool active = false; };
ModView getModView (ModDest) const;      // normalised parameter units; conversion below
```

`getModView` conversion (owned by the processor, so the UI knows nothing
about octaves). Inputs are whatever the modulation section publishes; the
mod winner exposes per-destination offsets in the destination's modulation
unit (`uiDestMod[d]` / `uiTimeModOct`), plus, required by this section, a
per-block spread for Time:

| Dest | live | spread | active |
|---|---|---|---|
| time | `timeNorm - uiTimeModOct / kTimeKnobOctaves` (positive octaves raise fs_chip = shorter delay = lower knob position) | `uiTimeModSpreadOct / kTimeKnobOctaves` | `agitate01 > 0.005 || timeMod01 > 0.005` |
| filter | `filterNorm + uiFilterModOct / kFilterKnobOctaves` (the Filter range is a true log range, so octaves are linear in norm) | 0 | `agitate01 > 0.005` |
| decay | `decayNorm + uiDecayMod / 1.15` | 0 | `agitate01 > 0.005` |

with `kTimeKnobOctaves = log2 (timemap::kDelayMaxSec / timemap::kDelayMinSec) = log2 (130.81) = 7.031`
and `kFilterKnobOctaves = log2 (18000 / 20) = 9.814`, both `inline constexpr`
next to the conversion; `xNorm` is the parameter's current normalised value.
`uiTimeModSpreadOct` = per-block peak `|timeOct[i] - blockMean|` run through
a 50 ms one-pole release (`kModSpreadReleaseSec = 0.05`) on the audio side,
so audio-rate FM shows as a steady band rather than a strobing one. If the
engine ships without it, `spreadNorm` is 0 and the band simply does not
appear; nothing else changes.

Also assumed from the parameter section: ids `params::id::time, decay,
filter, resonance, absorb, blend, agitate, agitspeed, strength, out, timemod,
timesync, agitmode, toneslevel, tonespitch, spread, bypass`;
`timemap::kDivisions[kDivisionCount = 14]`, `timemap::divisionIndexForTime01`,
`timemap::timeReadout (double seconds)`, `timemap::delaySecondsForTime01`,
`timemap::kDelayMinSec = 0.02752`, `timemap::kDelayMaxSec = 3.6`; the Time
parameter's host string switches to the division name while Sync is on; the
Decay host string is the plain two-decimal number or that number plus
" runaway" (the UI builds its own either way); `params::editorOnlyProperties()
= { "theme", "uiScale" }`; preset root tag `"DybbukPreset"`.

---

## 13. Files under `Source/ui/` and CMake

| File pair | Class | Role |
|---|---|---|
| `Theme.h/.cpp` | `theme::` | palettes, `setTheme`, fonts, `drawCaps`, `withUnit` (extends the template) |
| `Readout.h` | `Readout` | header only, section 3 |
| `BrassKnob.h/.cpp` | `BrassKnob` | all ten rotaries |
| `PillToggle.h/.cpp` | `PillToggle` | Sync (bool) and Loop/Gate (choice) |
| `Ember.h/.cpp` | `Ember` | section 6 |
| `ClearButton.h/.cpp` | `ClearButton` | section 9 |
| `ReadoutStrip.h/.cpp` | `ReadoutStrip` | section 7 |
| `OutSlider.h/.cpp` | `OutSlider` | section 8 |
| `PresetStation.h/.cpp` | `PresetStation` | ported from Teder |
| `ThemeButton.h/.cpp` | `ThemeButton` | extracted from Teder's inner class |

`PluginEditor.h/.cpp` keeps the template's `Canvas` and owns everything
above. `CMakeLists.txt` `PLUGIN_SOURCES` grows by the eight new `.cpp`
files (`BrassKnob`, `PillToggle`, `Ember`, `ClearButton`, `ReadoutStrip`,
`OutSlider`, `PresetStation`, `ThemeButton`); `ENGINE_SOURCES` is untouched,
so `EngineTest` never sees the editor. `UISnapshot` already compiles
`PLUGIN_SOURCES`, so it picks the new files up automatically.

---

## 14. UISnapshot states (`Tests/UISnapshot.cpp`)

Keep the template's `snap` lambda. Add helpers:

```cpp
void set (const char* id, float plainValue);        // p->setValueNotifyingHost (p->convertTo0to1 (v))
void runSine (double seconds, float amp = 0.5f);    // 220 Hz at 48 k / 128, both channels
void runSilence (double seconds);
void settle (int ms);                               // MessageManager::runDispatchLoopUntil (ms)
struct FixedTempo : juce::AudioPlayHead              // fakes a host: bpm, 4/4, playing
{
    double bpm; juce::Optional<PositionInfo> getPosition() const override
    { PositionInfo p; p.setBpm (bpm); p.setTimeSignature (TimeSignature { 4, 4 }); p.setIsPlaying (true); p.setPpqPosition (0.0); return p; }
};
```

Static states settle for 300 ms; any "lit" state settles for 1200 ms (36
ticks) so the ember's 0.08/tick release and the meter hold have landed. After
every snapshot print `editor->readoutLineForLog()` and, for each knob, `label
| value | aside` (via a small `dumpReadouts` that calls each knob's
`readout()`), so the console log doubles as a readout audit. Reset to brass
(`theme::setTheme (theme::Kind::brass)`) and clear the play head
(`processor.setPlayHead (nullptr)`) at the end.

| File | State | How to reach it | Look for |
|---|---|---|---|
| `editor_snapshot.png` | idle, brass, ember cold, strip shows time | construct, settle 300 | every value row is a musician's number; strip `time / 0.25 s / hint`; ember = ash bed + faint body |
| `editor_snapshot_active.png` | ember lit, meter + peak hold, value arcs | `set (decay, 0.85); set (time, 0.5); set (blend, 0.5)`; `runSine (1.0); runSilence (0.3)`; settle 1200 | glow visible, hold tick right of the fill, handle at x 505.6 +-1 |
| `editor_snapshot_modulated.png` | mod arc on Filter, band on Time | `set (agitate, 100); set (agitspeed, 0.3); set (timemod, 60)`; `runSine (2.0)`; settle 1200 | verdigris arc outside Filter's track ring, band around Time's dot, neither crosses a label row |
| `editor_snapshot_runaway.png` | Decay past unity, ember red, strip `decay / 1.15 / runaway` | `set (decay, 1.15)`; `runSine (0.5); runSilence (2.0)`; `showInReadout ("decay")`; settle 1200 | red arc segment from 99.8 to 135 deg, red ring on the ash bed, red aside |
| `editor_snapshot_hover.png` | hovered readout with hint | `showInReadout ("filter")`; settle 300 | `filter / 8000 Hz / in-loop lowpass cutoff ...` |
| `editor_snapshot_sync.png` | Time detents drawn, strip `time / 1/8 / 0.25 s` | `set (timesync, 1); set (time, 5.0f / 13.0f)`; `processor.setPlayHead (&tempo120)`; `runSilence (0.2)`; `showInReadout ("time")`; settle 300 | 14 ticks outside the track ring, Sync pill lit, aside in `faded` |
| `editor_snapshot_sync_clamped.png` | strip aside `3.60 s max` in red | as above with `tempo40` and `set (time, 1.0f)` (1 bar at 40 BPM = 6.0 s); `runSilence (0.2)` | value `1 bar`, red aside |
| `editor_snapshot_gate.png` | Agit Mode gate | `set (agitmode, 1)`; `showInReadout ("agitmode")`; settle 300 | thumb on the right, `gate` in ink, strip `agitation / gate` |
| `editor_snapshot_parchment.png` | alternate theme, idle | `editor.reset(); state.setProperty ("theme", 1); editor.reset (createEditor())`; settle 300 | light ground, dark brass accents, same geometry |
| `editor_snapshot_parchment_active.png` | alternate theme, ember lit | same drive as active, after the rebuild; settle 1200 | the glow must still read on the light ground (alpha x 0.8 rule) |
| `editor_snapshot_parchment_preset.png` | theme survives a preset load | still parchment: `presetManager.loadPreset (factory "Echo-Verb")`; settle 300 | parchment intact, header shows `Echo-Verb`; print `state["theme"]` (expected 1) |
| `editor_snapshot_browser.png` | preset browser open over the rows | `openPresetBrowserForTest()`; settle 300 | card at (202, 44), rows `Init` + four factory names + any user presets |
| `editor_snapshot_preset_<name>.png` x 4 | each factory preset, brass | back to brass, `loadPreset` each of Echo-Verb, Wow and Flutter, Bat Cave, Breathing; settle 300 | value rows match the tables in the parameter design |

expected (the harness prints these lines; a human checks the images):
`time | 0.25 s |` for the fresh editor; `decay | 1.15 | runaway` for
runaway; `time | 1/8 | 0.25 s` for sync; `time | 1 bar | 3.60 s max` for
sync_clamped; `out | 0.0 dB | peak -x.x dB` for active with a hold within
[-12, 0] dB after 0.5 amplitude through the default Blend; `agitation | gate |`
for gate; `theme=1` after the preset load in the parchment pass.

Reviewing checklist per image (CLAUDE.md): every value readout is a
musician's number (no `658.289`); the Decay red zone starts at the same angle
in both themes; the mod arc sits outside the track ring and never crosses the
label row; the Sync pill does not touch Time's ring; nothing in the header,
hints or captions contains an em dash; the bottom margin under the OUT
slider is 18 px.

---

## 15. Definition of done additions for UI work

- `grep -rn $'\xe2\x80\x94' Source/` is empty.
- All UISnapshot images regenerated and opened, both themes.
- `docs/PROGRESS.md` records: tooltips replaced by the readout strip; theme
  stored as int property `theme` and kept out of presets; `uiScale`
  persisted; fine adjust on every knob.

---

## Open questions (resolved here with the easiest-to-change assumption)

1. **Theme property type.** The parameter design stores `theme` as an int
   (0 brass, 1 parchment); the UI design used strings. Assumption: int, with
   `theme::kindFromProperty` accepting int, the two strings and a legacy bool,
   so either producer round-trips. Changing to strings is one function.
2. **Fine adjust on every knob, not only Time.** Assumption: all knobs
   (`kFineFactor = 8`). Restricting it to Time is one boolean in the
   constructor.
3. **Bypass has no control on the canvas** (plan 2.7 is layout-final and
   lists none; the host's device switch drives `getBypassParameter()`).
   Assumption: the strip shows a red `bypassed` aside whenever the parameter
   is on, nothing else. A click target can be added to the wordmark later
   without moving anything.
4. **Runaway detection for the ember** uses the Decay knob value, not the
   Follower-modulated live value. Assumption: knob only (`> 1.005 && energy >
   0.5`); switching to `max (knob, live)` is one line in the timer.
5. **Knob value rows** (`kShowKnobValues = true`). If the visual pass finds
   them noisy under the strip, the constant hides them and the component
   heights shrink by 16 px (rows re-derived from `boundsFor`).
6. **Window scale persistence** (`uiScale`). Assumption: persisted as an
   editor-only property; if a host misbehaves with a non-default initial
   size, ignore the property at construction (one line) and keep writing it.
7. **Clear via MIDI** (Phase 4 gate mentions mapping Clear to a MIDI button
   in Live). Clear is not a parameter, so Live cannot map it; that needs a
   processor-side MIDI note handler (plan Phase 5 "touch" gestures) and is
   out of the UI's scope. The `getClearsServed` watch already flashes the
   ember for any such external Clear.
8. **`ThemeFade` flourish.** Optional; port last or skip.
9. **Typeface.** System sans (`kTypeface = ""`); an embedded face is the
   constant plus a `juce_add_binary_data` target linked into both `Dybbuk`
   and `UISnapshot`.
