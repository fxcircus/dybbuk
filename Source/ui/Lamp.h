#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "Theme.h"

// The centrepiece: the loop's own energy as a lamp behind hatched glass. It is
// the one part of the window that shows what the instrument is doing rather
// than what you set, which matters most when the loop is playing itself.
//
// The canvas drives its flicker from the knob positions. This one is driven by
// the engine's real loop energy and keeps the canvas's flicker character on
// top, so it looks the same and tells the truth.
//
// No timer of its own; the editor drives every animated component from one.
class Lamp : public juce::Component
{
public:
    Lamp();

    void setEnergy (float energy01) noexcept { target = juce::jlimit (0.0f, 1.0f, energy01); }
    void setRunaway (bool shouldBeRunaway) noexcept { runaway = shouldBeRunaway; }
    void setBypassed (bool shouldBeBypassed) noexcept { bypassed = shouldBeBypassed; }
    void setAgitation (float agitate01, float speed01) noexcept;
    // The chaos source's own energy, which the engine already publishes and
    // nothing consumed. It is the one thing on the plate that moves without
    // anybody touching a knob, and a self-playing instrument you cannot watch
    // is one a player concludes is doing nothing.
    void setChaos (float energy01) noexcept
    {
        chaos = energy01 < 0.0f ? 0.0f : (energy01 > 1.0f ? 1.0f : energy01);
    }
    void flash() noexcept { dip = 1.0f; } // Clear: drop to the ember bed, then re-light
    void tick();
    float liveLevel() const noexcept { return live; }

    void paint (juce::Graphics& g) override;

private:
    float target = 0.0f, envelope = 0.0f, flicker = 0.0f, live = 0.0f;
    float agitation = 0.0f, speed = 0.35f, chaos = 0.0f;
    bool runaway = false, bypassed = false;
    float runawayMix = 0.0f, dip = 0.0f;
    float lastPainted = -1.0f;
    juce::Random rng;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Lamp)
};
