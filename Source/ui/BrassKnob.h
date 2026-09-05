#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include "Readout.h"
#include "Theme.h"

// The knob. A custom Component rather than a LookAndFeel over juce::Slider,
// because four of its behaviours are not slider behaviours: a second arc fed
// from the engine's atomics showing where modulation has actually taken the
// value, a per-knob double-click that is not a parameter action (Decay fires
// Clear), sync detents that appear and disappear, and hover callbacks that
// drive the readout strip instead of a tooltip.
class BrassKnob : public juce::Component
{
public:
    enum class Size { large, mediumLarge, medium };

    static constexpr int kKnobLarge = 104, kKnobMediumLarge = 84, kKnobMedium = 68;
    static constexpr int kKnobTopInset = 12;

    static int diameterFor (Size s) noexcept
    {
        return s == Size::large ? kKnobLarge : (s == Size::mediumLarge ? kKnobMediumLarge : kKnobMedium);
    }

    // W = D + 46, H = D + 54, circle centre at (W/2, 12 + D/2).
    static juce::Rectangle<int> boundsFor (Size s, juce::Point<int> circleCentre) noexcept
    {
        const int d = diameterFor (s);
        const int w = d + 46, h = d + 54;
        return { circleCentre.x - w / 2, circleCentre.y - (kKnobTopInset + d / 2), w, h };
    }

    BrassKnob (juce::RangedAudioParameter& parameterToUse, juce::String label, juce::String hint,
               Size size);

    // The readout strip is where controls explain themselves; there are no
    // tooltips anywhere in this UI.
    std::function<void (BrassKnob&)> onHover;
    std::function<Readout()> readoutProvider;
    Readout readout() const;

    void setRunawayZone (float normStart) noexcept; // Decay: the red part of the ring
    void setDetents (int count) noexcept;           // Time in sync: quantise to note values
    void setModulation (bool active, float liveNorm, float spreadNorm) noexcept;
    std::function<bool()> onDoubleClick; // return true to skip the reset-to-default

    void tick();
    int diameter() const noexcept { return diameterFor (knobSize); }
    float normalisedValue() const noexcept { return normValue; }
    juce::RangedAudioParameter& parameter() const noexcept { return param; }
    bool isDragging() const noexcept { return dragging; }

    void paint (juce::Graphics& g) override;
    void mouseEnter (const juce::MouseEvent& e) override;
    void mouseExit (const juce::MouseEvent& e) override;
    void mouseDown (const juce::MouseEvent& e) override;
    void mouseDrag (const juce::MouseEvent& e) override;
    void mouseUp (const juce::MouseEvent& e) override;
    void mouseDoubleClick (const juce::MouseEvent& e) override;
    void mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel) override;

private:
    juce::RangedAudioParameter& param;
    juce::ParameterAttachment attachment;
    juce::String labelText, hintText;
    Size knobSize;

    float normValue = 0.0f;
    float shownValue = -1.0f; // glides toward normValue so a preset load sweeps; < 0 adopts instantly
    float lastDragY = 0.0f;
    bool hovering = false, dragging = false;
    float runawayFrom = -1.0f;
    int detents = 0;
    bool modActive = false;
    float modLive = 0.0f, modSpread = 0.0f, modShownLive = -1.0f, modShownSpread = 0.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (BrassKnob)
};
