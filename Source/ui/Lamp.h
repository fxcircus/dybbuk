#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <array>

#include "Theme.h"

// The centrepiece: the red ember behind hatched glass is now the dybbuk
// itself, and the pattern lives around it. One pip per step on a ring, laid
// clockwise from twelve o'clock over the whole circle whatever the count (four
// steps make a square, sixteen a full ring); each pip's size follows the peak
// of its material and its ink follows its fade, the sounding one is bright
// and a touch larger, and the ember pulses on every tick, so the dot is the
// clock.
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
    // The ceiling and what happens at it, so the pip being written can be
    // shown in the slot it will actually take.
    void setCeiling (int maxSteps, bool holdWhenFull) noexcept;
    void setGateOpen (bool open) noexcept { gate = open; }
    void setFillRunning (bool running) noexcept { fill = running; }
    void setBypassed (bool shouldBeBypassed) noexcept { bypassed = shouldBeBypassed; }
    void flash() noexcept; // Clear: drop to the ember bed, collapse the ring, re-light
    void tick();
    float liveLevel() const noexcept { return live; }

    void paint (juce::Graphics& g) override;

private:
    juce::Colour pipColour (const theme::Palette& p) const noexcept;

    // Published state.
    int count = 0, current = -1, lastTicks = 0;
    int ceiling = 8;
    bool hold = false;
    bool gate = false, fill = false, bypassed = false;
    std::array<float, kMaxPips> level {}, gain {};

    // The ember.
    float envelope = 0.0f, flicker = 0.0f, live = 0.0f, lastPainted = -1.0f;
    float pulse = 0.0f, flare = 0.0f, dip = 0.0f, breath = 0.0f, warmth = 0.0f;

    // The ring. Slots ease rather than jump, so a step arriving turns the
    // others into their new places instead of snapping them.
    float shownSlots = 1.0f;
    float collapse = 0.0f;                      // 1 at the clear, 0 when the ring is gone
    int ghostCount = 0;                         // the ring as it was at the clear
    std::array<float, kMaxPips> ghostLevel {}, ghostGain {}, jitter {};
    bool ringDirty = true;
    juce::Random rng;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Lamp)
};
