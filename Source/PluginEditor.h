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
    void mouseDoubleClick (const juce::MouseEvent&) override
    {
        if (onDoubleClick)
            onDoubleClick();
    }

    std::function<void()> onDoubleClick;

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

// Fixed 800x340 logical layout; the editor scales it as a whole.
class AlterPanel : public juce::Component, private juce::Timer
{
public:
    static constexpr int kWidth = 800;
    static constexpr int kHeight = 340;

    explicit AlterPanel (TxikiAlterboyProcessor&);
    ~AlterPanel() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;
    void setupKnob (juce::Slider& s);
    void setupValueLabel (juce::Label& l, const char* paramId);
    void rebuildPresetBox();
    void refreshPresetBox();
    void savePresetDialog();
    void deletePresetDialog();
    void editValue (juce::Component& over, const char* paramId);
    void setParam (const char* paramId, float plainValue);
    void stepPreset (int delta);

    TxikiAlterboyProcessor& proc;
    AlterLookAndFeel lnf;

    SemitoneSlider pitchKnob, formantKnob;
    juce::Slider driveKnob, mixKnob;
    juce::ToggleButton linkButton, midiButton;
    juce::ToggleButton modeButtons[3];
    LedReadout pitchLed, formantLed, noteLed { false };
    juce::Label driveValue, mixValue;

    juce::TextButton prevButton { "<" }, nextButton { ">" }, saveButton { "SAVE" }, deleteButton { "DEL" };
    juce::ComboBox presetBox;
    std::unique_ptr<juce::TextEditor> valueEditor;
    juce::StringArray userPresetNames;

    using SliderAtt = juce::AudioProcessorValueTreeState::SliderAttachment;
    using ButtonAtt = juce::AudioProcessorValueTreeState::ButtonAttachment;
    std::unique_ptr<SliderAtt> pitchAtt, formantAtt, driveAtt, mixAtt;
    std::unique_ptr<ButtonAtt> linkAtt, midiAtt;

    juce::Rectangle<int> goldPanel, bluePanel, redPanel, bodyArea, footer;
    juce::Image woodTexture;
    int lastMode = -1;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AlterPanel)
};

class TxikiAlterboyEditor : public juce::AudioProcessorEditor
{
public:
    explicit TxikiAlterboyEditor (TxikiAlterboyProcessor&);
    void resized() override;

private:
    TxikiAlterboyProcessor& proc;
    AlterPanel panel;
    bool sizeInitialised = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TxikiAlterboyEditor)
};
