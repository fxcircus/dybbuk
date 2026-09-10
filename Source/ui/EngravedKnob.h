#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include "Theme.h"

// The knob from the v3 canvas: line art, not a filled arc. An outer ring of
// eleven index ticks, a knurled inner rim, two dashed marks at the travel
// limits, and a needle with a filled arrowhead. Nothing is shaded, so the
// whole face reads as an engraving on the plate.
//
// A custom Component rather than a LookAndFeel over juce::Slider, because
// several of its behaviours are not slider behaviours: a live modulation arc
// fed from the engine's atomics, a double-click that fires Clear on Decay
// instead of resetting it, and sync detents that appear with the Sync mark.
class EngravedKnob : public juce::Component
{
public:
    struct Spec
    {
        int size = 104;          // the face diameter the design specifies
        float labelPx = 12.0f;   // caption under the knob
        float valuePx = 13.0f;   // value under the caption
        float legendPx = 9.0f;   // the min and max labels beside the face
        bool legends = true;     // Speed draws no legends and no limit marks
        bool redNeedle = false;  // Agitate's arrowhead is red
    };

    static Spec heroSpec();
    static Spec midSpec();
    static Spec macroSpec();
    static Spec speedSpec();

    EngravedKnob (juce::RangedAudioParameter& parameterToUse, juce::String label, Spec spec);

    // Total component size for a given spec, so the editor can place by centre.
    static juce::Rectangle<int> boundsFor (const Spec& spec, juce::Point<int> faceCentre);

    void setLegends (juce::String minLabel, juce::String maxLabel);
    void setDangerFrom (float normStart) noexcept; // Decay: the red zone and red max legend
    void setDetents (int count) noexcept;          // Time in sync: quantise and mark the ring
    void setAccent (bool on) noexcept;             // the caption in red: this mode gives the knob another meaning
    void setModulation (bool active, float liveNorm) noexcept;
    void setValueTextProvider (std::function<juce::String()> provider);

    std::function<bool()> onDoubleClick; // return true to skip the reset to default
    std::function<void()> onValueChange;

    void tick();
    float normalisedValue() const noexcept { return normValue; }
    juce::RangedAudioParameter& parameter() const noexcept { return param; }
    bool isDragging() const noexcept { return dragging; }
    bool isHot() const noexcept;

    void paint (juce::Graphics& g) override;
    void mouseEnter (const juce::MouseEvent& e) override;
    void mouseExit (const juce::MouseEvent& e) override;
    void mouseDown (const juce::MouseEvent& e) override;
    void mouseDrag (const juce::MouseEvent& e) override;
    void mouseUp (const juce::MouseEvent& e) override;
    void mouseDoubleClick (const juce::MouseEvent& e) override;
    void mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel) override;

private:
    juce::Point<float> faceCentre() const noexcept;
    juce::String valueText() const;

    juce::RangedAudioParameter& param;
    juce::ParameterAttachment attachment;
    juce::String labelText, minLegend, maxLegend;
    std::function<juce::String()> valueProvider;
    Spec spec;

    float normValue = 0.0f;
    float shownValue = -1.0f; // glides toward normValue, so a preset load sweeps
    float lastDragY = 0.0f;
    float dragNorm = 0.0f;    // the drag's own, unquantised position: detents snap the value, not the hand
    bool hovering = false, dragging = false, accented = false;
    float dangerFrom = -1.0f;
    int detents = 0;
    bool modActive = false;
    float modLive = 0.0f, modShown = -1.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (EngravedKnob)
};
