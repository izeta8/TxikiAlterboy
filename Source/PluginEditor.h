#pragma once

#include "PluginProcessor.h"

class AlterLookAndFeel : public juce::LookAndFeel_V4
{
public:
    AlterLookAndFeel();
    void drawRotarySlider (juce::Graphics&, int x, int y, int w, int h, float pos, float start, float end, juce::Slider&) override;
    void drawToggleButton (juce::Graphics&, juce::ToggleButton&, bool highlighted, bool down) override;
    void drawButtonBackground (juce::Graphics&, juce::Button&, const juce::Colour&, bool highlighted, bool down) override;
    void drawComboBox (juce::Graphics&, int w, int h, bool down, int bx, int by, int bw, int bh, juce::ComboBox&) override;
    juce::Font getComboBoxFont (juce::ComboBox&) override;
    void positionComboBoxText (juce::ComboBox&, juce::Label&) override;
};

// Red seven-segment style readout.
class LedReadout : public juce::Component
{
public:
    explicit LedReadout (bool showGhostDigits = true) : ghost (showGhostDigits) {}
    void setText (const juce::String& t)
    {
        if (t != text)
        {
            text = t;
            repaint();
        }
    }
    void paint (juce::Graphics&) override;

private:
    juce::String text;
    bool ghost = true;
};

// Pitch/Formant knobs: whole-semitone steps, Shift + drag for fine (0.1) control, like the original.
class SemitoneSlider : public juce::Slider
{
public:
    double snapValue (double attemptedValue, DragMode) override
    {
        if (juce::ModifierKeys::currentModifiers.isShiftDown())
            return std::round (attemptedValue * 10.0) / 10.0;
        return std::round (attemptedValue);
    }
};

class TxikiAlterboyEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit TxikiAlterboyEditor (TxikiAlterboyProcessor&);
    ~TxikiAlterboyEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;
    void refreshPresetBox();
    void setupKnob (juce::Slider& s);

    TxikiAlterboyProcessor& proc;
    AlterLookAndFeel lnf;

    SemitoneSlider pitchKnob, formantKnob;
    juce::Slider driveKnob, mixKnob;
    juce::ToggleButton linkButton, midiButton;
    juce::ToggleButton modeButtons[3];
    LedReadout pitchLed, formantLed, noteLed { false };

    juce::TextButton prevButton { "<" }, nextButton { ">" };
    juce::ComboBox presetBox;

    using SliderAtt = juce::AudioProcessorValueTreeState::SliderAttachment;
    using ButtonAtt = juce::AudioProcessorValueTreeState::ButtonAttachment;
    std::unique_ptr<SliderAtt> pitchAtt, formantAtt, driveAtt, mixAtt;
    std::unique_ptr<ButtonAtt> linkAtt, midiAtt;

    juce::Rectangle<int> goldPanel, bluePanel, redPanel, bodyArea, footer;
    juce::Image woodTexture;
    double lastDrive = -1.0, lastMix = -1.0;
    int lastMode = -1;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TxikiAlterboyEditor)
};
