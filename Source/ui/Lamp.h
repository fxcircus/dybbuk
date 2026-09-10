#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <array>

#include "Theme.h"

// The centrepiece: the red ember behind hatched glass is now the dybbuk
// itself, and the pattern lives around it as tentacles. One per step, laid
// clockwise from twelve o'clock in the slots of the Steps ceiling (four steps
// at a ceiling of eight fill the top half of the ring, and the empty slots
// are the room to add); each limb's reach follows the peak of its material
// and its ink follows its fade, the sounding one is bright and a touch
// larger, and the ember pulses on every tick, so the dot is the clock. A
// step that has just joined grows out of the housing at its own slot, so
// nothing else moves when it arrives.
//
// While the engine is listening the ember breathes slowly and the ring is an
// empty guide circle. While the gate is open the ember flares and the pip
// about to be written appears outlined at the next position. A fill warms
// the ring and makes the pips shiver for as long as the scrambled order runs.
// A clear drops the ember to its bed and the ring collapses inward.
//
// It is the one part of the window that shows what the instrument is doing
// rather than what you set. No timer of its own; the editor drives every
// animated component from one. Nothing here allocates in paint.
class Lamp : public juce::Component
{
public:
    static constexpr int kMaxPips = 16;

    Lamp();

    // The pattern as the engine publishes it, polled once per editor tick.
    void setPattern (int stepCount, int currentStep, int ticks) noexcept;
    void setStep (int index, float level01, float gain01) noexcept;
    // The ceiling and what happens at it: the limbs are spaced over it, and
    // the pip being written is shown in the slot it will actually take.
    void setCeiling (int maxSteps, bool holdWhenFull) noexcept;
    // The engine's count of steps that have joined, polled after setPattern:
    // a change is a new limb, which starts at nothing and grows.
    void setCommits (int commits) noexcept;
    void setGateOpen (bool open) noexcept { gate = open; }
    void setFillRunning (bool running) noexcept { fill = running; }
    void setBypassed (bool shouldBeBypassed) noexcept { bypassed = shouldBeBypassed; }
    void setFrozen (bool shouldBeFrozen) noexcept { frozen = shouldBeFrozen; }
    void flash() noexcept; // Clear: drop to the ember bed, collapse the ring, re-light

    // Which player has the pattern (the Mode parameter's index). The creature
    // changes its bearing to match, easing between bearings rather than
    // snapping: Trance stretches the limbs, Legion splits every tip into a
    // fan of bulbs, Wraith leaves a ghost of each limb that sounded, Tremor
    // gives the ember a tremor and the sounding limb a twitch.
    enum Mode { golem = 0, wraith, trance, legion, tremor, kModeCount };   // mirrors BurstEngine::Mode
    void setMode (int mode) noexcept { modeWanted = juce::jlimit (0, kModeCount - 1, mode); }

    void tick();
    float liveLevel() const noexcept { return live; }

    void paint (juce::Graphics& g) override;

private:
    juce::Colour emberColour (const theme::Palette& p) const noexcept;   // red, or blue as the frost sets
    juce::Colour pipColour (const theme::Palette& p) const noexcept;

    // Published state.
    int count = 0, current = -1, lastTicks = 0, lastCommits = 0;
    int ceiling = 8;
    bool hold = false, ceilingSeen = false;
    bool gate = false, fill = false, bypassed = false, frozen = false;
    std::array<float, kMaxPips> level {}, gain {};

    // The ember.
    float envelope = 0.0f, flicker = 0.0f, live = 0.0f, lastPainted = -1.0f;
    float pulse = 0.0f, flare = 0.0f, dip = 0.0f, breath = 0.0f, warmth = 0.0f;
    float frost = 0.0f;                         // 1 once Freeze has set in: blue, and still

    // The ring. The ceiling eases rather than jumps, so turning the Steps
    // knob re-spaces the limbs instead of snapping them; that and the shift
    // when a full ring replaces its oldest are the only times a limb moves
    // off its slot. Each limb has a growth of its own, zero as it joins.
    float shownCeiling = 8.0f;
    float shift = 0.0f;                         // 1 as the oldest is replaced: the rest sit a slot on, and slide back
    std::array<float, kMaxPips> grow {};
    bool joinedThisFrame = false;               // setPattern saw the count rise, so setCommits need not
    float collapse = 0.0f;                      // 1 at the clear, 0 when the ring is gone
    int ghostCount = 0;                         // the ring as it was at the clear
    std::array<float, kMaxPips> ghostLevel {}, ghostGain {}, jitter {};
    float writhe = 0.0f;                        // the tentacles' slow motion, in radians
    bool ringDirty = true;
    bool ticked = false;                        // a sequencer tick arrived since the last frame
    juce::Random rng;

    // The bearing. One eased weight per mode so a change crossfades: the
    // outgoing bearing lets go as the incoming one takes hold.
    int modeWanted = 0;
    std::array<float, kModeCount> modeMix {};

    // Wraith: the sounding limb as it stood on each of the last few ticks,
    // its wobble frozen at that moment, fading and drifting back as it ages.
    struct Trail
    {
        int slot = -1;
        float slots = 1.0f, level = 0.0f, gain = 0.0f, writhe = 0.0f, age = 0.0f;
    };
    static constexpr int kTrails = 4;
    std::array<Trail, kTrails> trails {};
    int nextTrail = 0;

    // Tremor: where the ember has shaken to this frame, and how hard the
    // sounding limb is twitching.
    float tremorX = 0.0f, tremorY = 0.0f, twitch = 0.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Lamp)
};
