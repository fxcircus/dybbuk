#include "PluginEditor.h"

void DybbukEditor::Canvas::paint (juce::Graphics& g)
{
    g.fillAll (theme::background);
    if (onPaint)
        onPaint (g);
}

DybbukEditor::DybbukEditor (DybbukProcessor& p)
    : juce::AudioProcessorEditor (p), proc (p)
{
    // The theme must be set before any Label/TextEditor caches its colours.
    theme::setDarkMode ((bool) proc.apvts.state.getProperty ("darkMode", false));

    addAndMakeVisible (canvas);
    canvas.setBounds (0, 0, canvasW, canvasH);
    canvas.onPaint = [this] (juce::Graphics& g)
    {
        g.setColour (theme::ink);
        g.setFont (juce::FontOptions (22.0f).withStyle ("Bold"));
        g.drawText (JucePlugin_Name, 0, 24, canvasW, 26, juce::Justification::centred);

        g.setColour (theme::faded);
        g.setFont (juce::FontOptions (12.0f));
        g.drawText ("replace this canvas with the real design",
                    0, 52, canvasW, 16, juce::Justification::centred);

        // Output meter — proof that the engine -> UI atomic path works.
        const auto meter = juce::Rectangle<float> (60.0f, 300.0f, canvasW - 120.0f, 10.0f);
        g.setColour (theme::inkA (0.25f));
        g.drawRect (meter, 1.0f);
        g.setColour (theme::accent);
        g.fillRect (meter.reduced (1.0f).withWidth ((meter.getWidth() - 2.0f)
                                                    * juce::jlimit (0.0f, 1.0f, meterLevel)));
    };

    auto addKnob = [this] (juce::Slider& s, juce::Label& l, const char* text,
                           const char* tooltip, int x)
    {
        s.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
        s.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 78, 18);
        s.setTooltip (tooltip);
        canvas.addAndMakeVisible (s);
        s.setBounds (x, 110, 120, 120);

        l.setText (text, juce::dontSendNotification);
        l.setJustificationType (juce::Justification::centred);
        l.setColour (juce::Label::textColourId, theme::ink);
        canvas.addAndMakeVisible (l);
        l.setBounds (x, 236, 120, 18);
    };

    addKnob (driveSlider, driveLabel, "Drive", "Input drive into the saturator.", 60);
    addKnob (toneSlider, toneLabel, "Tone", "Low-pass cutoff on the processed signal.", 250);
    addKnob (mixSlider, mixLabel, "Mix", "Dry/wet blend.", 440);

    bypassButton.setColour (juce::ToggleButton::textColourId, theme::ink);
    bypassButton.setColour (juce::ToggleButton::tickColourId, theme::accent);
    canvas.addAndMakeVisible (bypassButton);
    bypassButton.setBounds (canvasW / 2 - 45, 264, 90, 24);

    driveAttach  = std::make_unique<SliderAttachment> (proc.apvts, params::id::drive, driveSlider);
    toneAttach   = std::make_unique<SliderAttachment> (proc.apvts, params::id::tone, toneSlider);
    mixAttach    = std::make_unique<SliderAttachment> (proc.apvts, params::id::mix, mixSlider);
    bypassAttach = std::make_unique<ButtonAttachment> (proc.apvts, params::id::bypass, bypassButton);

    startTimerHz (30);

    setResizable (true, true);
    setResizeLimits (canvasW / 2, canvasH / 2, canvasW * 2, canvasH * 2);
    getConstrainer()->setFixedAspectRatio ((double) canvasW / (double) canvasH);
    setSize (canvasW, canvasH);
}

DybbukEditor::~DybbukEditor() = default;

juce::RangedAudioParameter& DybbukEditor::param (const char* id) const
{
    return *proc.apvts.getParameter (id);
}

void DybbukEditor::timerCallback()
{
    // Poll engine state; never reach into the engine directly.
    const float level = proc.getOutputLevel();
    if (std::abs (level - meterLevel) > 0.005f)
    {
        meterLevel = level;
        canvas.repaint();
    }
}

void DybbukEditor::resized()
{
    const float scale = juce::jmin ((float) getWidth() / (float) canvasW,
                                    (float) getHeight() / (float) canvasH);
    canvas.setTransform (juce::AffineTransform::scale (scale));
}
