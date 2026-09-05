#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "Theme.h"

// The centrepiece: the loop's own energy as a coal that breathes. It is the
// one part of the UI that shows what the instrument is doing rather than what
// you set, which matters most when the loop is playing itself.
//
// No timer of its own; the editor drives every animated component from one.
class Ember : public juce::Component
{
public:
    Ember();

    void setEnergy (float energy01) noexcept { target = juce::jlimit (0.0f, 1.0f, energy01); }
    void setRunaway (bool shouldBeRunaway) noexcept { runaway = shouldBeRunaway; }
    void flash() noexcept { flashDip = 1.0f; } // Clear: dip to the ash bed, then re-light
    void tick();
    float liveLevel() const noexcept { return live; }

    void paint (juce::Graphics& g) override;

private:
    float target = 0.0f, shown = 0.0f, live = 0.0f;
    bool runaway = false;
    float runawayMix = 0.0f;
    float n1 = 0.0f, n2 = 0.0f; // two-pole filtered noise: flicker that is not steppy
    float phase = 0.0f;
    float flashDip = 0.0f;
    float lastPainted = -1.0f;
    juce::Random rng;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Ember)
};
