#include "PluginEditor.h"

namespace
{
const juce::Colour kGold { 0xffc8962e };
const juce::Colour kGoldDark { 0xff9a6d1c };
const juce::Colour kBlue { 0xff1d5870 };
const juce::Colour kBlueDark { 0xff123c4e };
const juce::Colour kRed { 0xffb81d24 };
const juce::Colour kRedDark { 0xff85121a };
const juce::Colour kBody { 0xff1c1f24 };
const juce::Colour kLedRed { 0xffff2a1a };

constexpr int kWidth = 800;
constexpr int kHeight = 340;

juce::String noteName (float hz)
{
    if (hz <= 0.0f)
        return "---";
    const int midi = juce::roundToInt (69.0f + 12.0f * std::log2 (hz / 440.0f));
    return juce::MidiMessage::getMidiNoteName (midi, true, true, 3);
}

juce::Image makeWood (int w, int h)
{
    juce::Image img (juce::Image::RGB, w, h, false);
    juce::Graphics g (img);
    g.fillAll (juce::Colour (0xff6b3a1f));
    juce::Random rng (1234);
    for (int i = 0; i < 90; ++i)
    {
        const float x = rng.nextFloat() * (float) w;
        const float width = 0.5f + rng.nextFloat() * 2.5f;
        const float shade = rng.nextFloat();
        g.setColour ((shade > 0.5f ? juce::Colour (0xff4a2410) : juce::Colour (0xff8a5230)).withAlpha (0.25f + 0.35f * rng.nextFloat()));
        juce::Path p;
        p.startNewSubPath (x, 0.0f);
        float cx = x;
        for (int y = 0; y <= h; y += 20)
        {
            cx += (rng.nextFloat() - 0.5f) * 1.6f;
            p.lineTo (cx, (float) y);
        }
        g.strokePath (p, juce::PathStrokeType (width));
    }
    return img;
}
} // namespace

// ============================================================ LookAndFeel
AlterLookAndFeel::AlterLookAndFeel()
{
    setColour (juce::ComboBox::textColourId, kLedRed);
    setColour (juce::PopupMenu::backgroundColourId, juce::Colour (0xff15171a));
    setColour (juce::PopupMenu::textColourId, juce::Colours::white);
    setColour (juce::PopupMenu::highlightedBackgroundColourId, kRedDark);
    setColour (juce::TextButton::textColourOffId, juce::Colours::white);
    setColour (juce::TextButton::textColourOnId, juce::Colours::white);
}

void AlterLookAndFeel::drawRotarySlider (juce::Graphics& g, int x, int y, int w, int h, float pos, float start, float end, juce::Slider& s)
{
    const auto bounds = juce::Rectangle<int> (x, y, w, h).toFloat().reduced (4.0f);
    const float radius = juce::jmin (bounds.getWidth(), bounds.getHeight()) * 0.5f;
    const auto c = bounds.getCentre();
    const float angle = start + pos * (end - start);

    // Tick marks.
    g.setColour (juce::Colours::black.withAlpha (0.45f));
    for (int i = 0; i <= 10; ++i)
    {
        const float a = start + (float) i / 10.0f * (end - start);
        const auto p1 = c.getPointOnCircumference (radius * 0.98f, a);
        const auto p2 = c.getPointOnCircumference (radius * 0.86f, a);
        g.drawLine ({ p1, p2 }, i == 5 ? 2.0f : 1.2f);
    }

    // Drop shadow.
    const float kr = radius * 0.8f;
    g.setColour (juce::Colours::black.withAlpha (0.5f));
    g.fillEllipse (c.x - kr + 2.0f, c.y - kr + 4.0f, kr * 2.0f, kr * 2.0f);

    // Knurled black body.
    juce::ColourGradient body (juce::Colour (0xff4a4d52), c.x - kr * 0.6f, c.y - kr * 0.8f, juce::Colour (0xff050506), c.x + kr * 0.5f, c.y + kr, true);
    g.setGradientFill (body);
    g.fillEllipse (c.x - kr, c.y - kr, kr * 2.0f, kr * 2.0f);

    g.setColour (juce::Colours::black.withAlpha (0.6f));
    for (int i = 0; i < 36; ++i)
    {
        const float a = (float) i / 36.0f * juce::MathConstants<float>::twoPi + angle;
        g.drawLine ({ c.getPointOnCircumference (kr * 0.92f, a), c.getPointOnCircumference (kr, a) }, 1.0f);
    }

    const float cap = kr * 0.74f;
    juce::ColourGradient top (juce::Colour (0xff2e3136), c.x, c.y - cap, juce::Colour (0xff0b0c0e), c.x, c.y + cap, false);
    g.setGradientFill (top);
    g.fillEllipse (c.x - cap, c.y - cap, cap * 2.0f, cap * 2.0f);
    g.setColour (juce::Colours::white.withAlpha (0.08f));
    g.drawEllipse (c.x - cap, c.y - cap, cap * 2.0f, cap * 2.0f, 1.0f);

    // Pointer.
    g.setColour (s.isEnabled() ? juce::Colours::white : juce::Colours::grey);
    g.drawLine ({ c.getPointOnCircumference (kr * 0.25f, angle), c.getPointOnCircumference (kr * 0.95f, angle) }, 3.0f);
}

void AlterLookAndFeel::drawToggleButton (juce::Graphics& g, juce::ToggleButton& b, bool highlighted, bool)
{
    const auto r = b.getLocalBounds().toFloat();
    const float size = juce::jmin (r.getHeight(), 22.0f);
    const auto box = juce::Rectangle<float> (r.getX() + 1.0f, r.getCentreY() - size * 0.5f, size, size);

    g.setColour (juce::Colours::black.withAlpha (0.55f));
    g.fillRoundedRectangle (box.translated (1.0f, 2.0f), 2.0f);
    g.setColour (juce::Colour (0xff2a2c30));
    g.fillRoundedRectangle (box, 2.0f);

    const auto led = box.reduced (3.0f);
    if (b.getToggleState())
    {
        g.setColour (kLedRed.withAlpha (0.35f));
        g.fillRoundedRectangle (led.expanded (3.0f), 3.0f);
        juce::ColourGradient lit (juce::Colour (0xffff7a5a), led.getCentreX(), led.getY(), juce::Colour (0xffd01010), led.getCentreX(), led.getBottom(), false);
        g.setGradientFill (lit);
    }
    else
    {
        juce::ColourGradient off (juce::Colour (0xff6d7075), led.getCentreX(), led.getY(), juce::Colour (0xff3a3c40), led.getCentreX(), led.getBottom(), false);
        g.setGradientFill (off);
    }
    g.fillRoundedRectangle (led, 1.5f);
    if (highlighted)
    {
        g.setColour (juce::Colours::white.withAlpha (0.15f));
        g.fillRoundedRectangle (led, 1.5f);
    }

    if (b.getButtonText().isNotEmpty())
    {
        g.setColour (juce::Colours::white.withAlpha (0.9f));
        g.setFont (juce::FontOptions (11.5f, juce::Font::bold));
        g.drawText (b.getButtonText(), juce::Rectangle<float> (box.getRight() + 8.0f, r.getY(), r.getWidth() - size - 8.0f, r.getHeight()), juce::Justification::centredLeft);
    }
}

void AlterLookAndFeel::drawButtonBackground (juce::Graphics& g, juce::Button& b, const juce::Colour&, bool highlighted, bool down)
{
    auto r = b.getLocalBounds().toFloat().reduced (1.0f);
    g.setColour (juce::Colour (down ? 0xff111214 : 0xff3a3d42).brighter (highlighted ? 0.2f : 0.0f));
    g.fillRoundedRectangle (r, 2.0f);
    g.setColour (juce::Colours::black);
    g.drawRoundedRectangle (r, 2.0f, 1.0f);
}

void AlterLookAndFeel::drawComboBox (juce::Graphics& g, int w, int h, bool, int, int, int, int, juce::ComboBox&)
{
    const auto r = juce::Rectangle<int> (0, 0, w, h).toFloat();
    g.setColour (juce::Colour (0xff120606));
    g.fillRoundedRectangle (r, 2.0f);
    g.setColour (juce::Colours::black);
    g.drawRoundedRectangle (r.reduced (0.5f), 2.0f, 1.0f);
    juce::Path arrow;
    arrow.addTriangle ((float) w - 16.0f, h * 0.4f, (float) w - 8.0f, h * 0.4f, (float) w - 12.0f, h * 0.62f);
    g.setColour (kLedRed.withAlpha (0.8f));
    g.fillPath (arrow);
}

juce::Font AlterLookAndFeel::getComboBoxFont (juce::ComboBox&) { return juce::FontOptions (15.0f, juce::Font::bold); }

void AlterLookAndFeel::positionComboBoxText (juce::ComboBox& box, juce::Label& label)
{
    label.setBounds (6, 1, box.getWidth() - 24, box.getHeight() - 2);
    label.setFont (getComboBoxFont (box));
}

// ============================================================ LED readout
void LedReadout::paint (juce::Graphics& g)
{
    const auto r = getLocalBounds().toFloat();
    g.setColour (juce::Colour (0xff0e0404));
    g.fillRoundedRectangle (r, 2.0f);
    g.setColour (juce::Colours::black);
    g.drawRoundedRectangle (r.reduced (0.5f), 2.0f, 1.0f);

    juce::Font font (juce::FontOptions (juce::Font::getDefaultMonospacedFontName(), r.getHeight() * 0.78f, juce::Font::bold));
    g.setFont (font);
    if (ghost)
    {
        g.setColour (kLedRed.withAlpha (0.1f));
        g.drawText ("888.8", r.reduced (6.0f, 0.0f), juce::Justification::centredRight);
    }
    g.setColour (kLedRed.withAlpha (0.3f));
    g.drawText (text, r.reduced (5.0f, 0.0f).translated (0.0f, 0.5f), juce::Justification::centredRight);
    g.setColour (kLedRed);
    g.drawText (text, r.reduced (6.0f, 0.0f), juce::Justification::centredRight);
}

// ============================================================ Editor
TxikiAlterboyEditor::TxikiAlterboyEditor (TxikiAlterboyProcessor& p)
    : AudioProcessorEditor (&p), proc (p)
{
    setLookAndFeel (&lnf);

    for (juce::Slider* k : { static_cast<juce::Slider*> (&pitchKnob), static_cast<juce::Slider*> (&formantKnob), &driveKnob, &mixKnob })
        setupKnob (*k);

    pitchKnob.setTooltip ("Semitone steps. Hold Shift for fine adjustment.");
    pitchAtt = std::make_unique<SliderAtt> (proc.apvts, ParamIDs::pitch, pitchKnob);
    formantKnob.setTooltip ("Semitone steps. Hold Shift for fine adjustment.");
    formantAtt = std::make_unique<SliderAtt> (proc.apvts, ParamIDs::formant, formantKnob);
    driveAtt = std::make_unique<SliderAtt> (proc.apvts, ParamIDs::drive, driveKnob);
    mixAtt = std::make_unique<SliderAtt> (proc.apvts, ParamIDs::mix, mixKnob);

    linkButton.setTooltip ("Link: formant follows pitch (tape/DJ-style)");
    addAndMakeVisible (linkButton);
    linkAtt = std::make_unique<ButtonAtt> (proc.apvts, ParamIDs::link, linkButton);

    midiButton.setButtonText ("MIDI");
    midiButton.setTooltip ("Play the pitch from a MIDI keyboard (C3 = 0). In Robot mode notes set the robot pitch.");
    addAndMakeVisible (midiButton);
    midiAtt = std::make_unique<ButtonAtt> (proc.apvts, ParamIDs::midi, midiButton);

    const char* modeNames[] = { "TRANSPOSE", "QUANTIZE", "ROBOT" };
    for (int i = 0; i < 3; ++i)
    {
        auto& b = modeButtons[i];
        b.setButtonText (modeNames[i]);
        b.setClickingTogglesState (false);
        b.onClick = [this, i]
        {
            if (auto* param = proc.apvts.getParameter (ParamIDs::mode))
            {
                param->beginChangeGesture();
                param->setValueNotifyingHost (param->convertTo0to1 ((float) i));
                param->endChangeGesture();
            }
        };
        addAndMakeVisible (b);
    }

    for (auto* led : { &pitchLed, &formantLed, &noteLed })
        addAndMakeVisible (led);

    for (int i = 0; i < proc.getNumPrograms(); ++i)
        presetBox.addItem (proc.getProgramName (i), i + 1);
    presetBox.onChange = [this]
    {
        const int idx = presetBox.getSelectedId() - 1;
        if (idx >= 0 && idx != proc.getCurrentProgram())
            proc.setCurrentProgram (idx);
    };
    addAndMakeVisible (presetBox);

    prevButton.onClick = [this]
    {
        const int n = proc.getNumPrograms();
        proc.setCurrentProgram ((proc.getCurrentProgram() + n - 1) % n);
        refreshPresetBox();
    };
    nextButton.onClick = [this]
    {
        proc.setCurrentProgram ((proc.getCurrentProgram() + 1) % proc.getNumPrograms());
        refreshPresetBox();
    };
    addAndMakeVisible (prevButton);
    addAndMakeVisible (nextButton);

    woodTexture = makeWood (26, kHeight);
    refreshPresetBox();
    setSize (kWidth, kHeight);
    timerCallback();
    startTimerHz (30);
}

TxikiAlterboyEditor::~TxikiAlterboyEditor()
{
    stopTimer();
    setLookAndFeel (nullptr);
}

void TxikiAlterboyEditor::setupKnob (juce::Slider& s)
{
    s.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    s.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    s.setRotaryParameters (juce::degreesToRadians (225.0f), juce::degreesToRadians (495.0f), true);
    s.setVelocityBasedMode (false);
    s.setMouseDragSensitivity (220);
    s.setDoubleClickReturnValue (true, 0.0);
    addAndMakeVisible (s);
}

void TxikiAlterboyEditor::refreshPresetBox()
{
    presetBox.setSelectedId (proc.getCurrentProgram() + 1, juce::dontSendNotification);
}

void TxikiAlterboyEditor::timerCallback()
{
    const int mode = (int) proc.apvts.getRawParameterValue (ParamIDs::mode)->load();
    for (int i = 0; i < 3; ++i)
        modeButtons[i].setToggleState (i == mode, juce::dontSendNotification);

    const bool link = proc.apvts.getRawParameterValue (ParamIDs::link)->load() > 0.5f;
    const bool midi = proc.midiActive.load();
    const float pitch = midi ? proc.midiPitch.load() : (float) pitchKnob.getValue();

    pitchLed.setText (juce::String (pitch, 1));
    formantLed.setText (juce::String (link ? pitch : (float) formantKnob.getValue(), 1));
    formantKnob.setEnabled (!link);
    formantKnob.setAlpha (link ? 0.5f : 1.0f);

    if (mode == 2)
        noteLed.setText (noteName (440.0f * std::pow (2.0f, (72.0f + pitch - 69.0f) / 12.0f)));
    else
        noteLed.setText (noteName (proc.detectedHz.load()));

    refreshPresetBox();

    if (driveKnob.getValue() != lastDrive || mixKnob.getValue() != lastMix || mode != lastMode)
    {
        lastDrive = driveKnob.getValue();
        lastMix = mixKnob.getValue();
        lastMode = mode;
        repaint();
    }
}

void TxikiAlterboyEditor::resized()
{
    auto area = getLocalBounds();
    auto topBar = area.removeFromTop (40).reduced (30, 7);
    prevButton.setBounds (topBar.removeFromLeft (26));
    topBar.removeFromLeft (3);
    nextButton.setBounds (topBar.removeFromLeft (26));
    topBar.removeFromLeft (8);
    presetBox.setBounds (topBar.removeFromLeft (260));

    bodyArea = area.reduced (26, 0);
    auto inner = bodyArea.reduced (16, 14);
    footer = inner.removeFromBottom (44);
    inner.removeFromBottom (6);

    goldPanel = inner.removeFromLeft (286);
    inner.removeFromLeft (12);
    bluePanel = inner.removeFromLeft (142);
    inner.removeFromLeft (12);
    redPanel = inner;

    const int knob = 84;
    auto gp = goldPanel.reduced (18, 0);
    auto pitchCol = gp.removeFromLeft (100);
    auto formantCol = gp.removeFromRight (100);
    pitchKnob.setBounds (pitchCol.getCentreX() - knob / 2, goldPanel.getY() + 38, knob, knob);
    formantKnob.setBounds (formantCol.getCentreX() - knob / 2, goldPanel.getY() + 38, knob, knob);
    pitchLed.setBounds (pitchCol.getCentreX() - 38, goldPanel.getY() + 134, 76, 26);
    formantLed.setBounds (formantCol.getCentreX() - 38, goldPanel.getY() + 134, 76, 26);
    linkButton.setBounds (goldPanel.getCentreX() - 11, goldPanel.getY() + 68, 22, 22);

    auto bp = bluePanel.reduced (12, 0);
    for (int i = 0; i < 3; ++i)
        modeButtons[i].setBounds (bp.getX(), bluePanel.getY() + 44 + i * 34, bp.getWidth(), 24);

    auto rp = redPanel;
    auto driveCol = rp.removeFromLeft (rp.getWidth() / 2);
    auto mixCol = rp;
    driveKnob.setBounds (driveCol.getCentreX() - knob / 2, redPanel.getY() + 38, knob, knob);
    mixKnob.setBounds (mixCol.getCentreX() - knob / 2, redPanel.getY() + 38, knob, knob);

    noteLed.setBounds (footer.getRight() - 236, footer.getCentreY() - 12, 74, 24);
    midiButton.setBounds (footer.getRight() - 140, footer.getCentreY() - 11, 70, 22);
}

void TxikiAlterboyEditor::paint (juce::Graphics& g)
{
    // Top bar.
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xff3a3c40), 0, 0, juce::Colour (0xff1c1d20), 0, 40, false));
    g.fillRect (0, 0, getWidth(), 40);

    // Wooden side cheeks.
    for (int side = 0; side < 2; ++side)
    {
        const int x = side == 0 ? 0 : getWidth() - 26;
        g.drawImageAt (woodTexture, x, 40);
        g.setGradientFill (juce::ColourGradient (juce::Colours::white.withAlpha (0.12f), (float) x, 40.0f, juce::Colours::black.withAlpha (0.35f), (float) x + 26.0f, 40.0f, false));
        g.fillRect (x, 40, 26, getHeight() - 40);
    }
    g.setColour (juce::Colour (0xff4a2410));
    g.fillRect (0, getHeight() - 6, getWidth(), 6);

    // Body.
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xff2b2f35), 0, (float) bodyArea.getY(), kBody, 0, (float) bodyArea.getBottom(), false));
    g.fillRect (bodyArea);
    juce::Random rng (77);
    g.setColour (juce::Colours::white.withAlpha (0.025f));
    for (int i = 0; i < 400; ++i)
        g.fillRect (bodyArea.getX() + rng.nextInt (bodyArea.getWidth()), bodyArea.getY() + rng.nextInt (bodyArea.getHeight()), 1, 1);

    // Screws.
    for (auto pt : { bodyArea.getTopLeft().translated (9, 9), bodyArea.getTopRight().translated (-9, 9),
                     bodyArea.getBottomLeft().translated (9, -9), bodyArea.getBottomRight().translated (-9, -9) })
    {
        g.setColour (juce::Colour (0xff8a8d92));
        g.fillEllipse ((float) pt.x - 4.5f, (float) pt.y - 4.5f, 9.0f, 9.0f);
        g.setColour (juce::Colour (0xff303236));
        g.drawLine ((float) pt.x - 3.0f, (float) pt.y + 1.5f, (float) pt.x + 3.0f, (float) pt.y - 1.5f, 1.5f);
    }

    auto panel = [&g] (juce::Rectangle<int> r, juce::Colour c, juce::Colour dark)
    {
        const auto rf = r.toFloat();
        g.setColour (juce::Colours::black.withAlpha (0.5f));
        g.fillRoundedRectangle (rf.translated (0.0f, 3.0f), 3.0f);
        g.setGradientFill (juce::ColourGradient (c.brighter (0.08f), rf.getX(), rf.getY(), dark, rf.getX(), rf.getBottom(), false));
        g.fillRoundedRectangle (rf, 3.0f);
        g.setColour (juce::Colours::white.withAlpha (0.12f));
        g.drawHorizontalLine (r.getY() + 1, rf.getX() + 3.0f, rf.getRight() - 3.0f);
    };
    panel (goldPanel, kGold, kGoldDark);
    panel (bluePanel, kBlue, kBlueDark);
    panel (redPanel, kRed, kRedDark);

    auto label = [&g] (const juce::String& text, juce::Rectangle<int> r, float size, juce::Justification j = juce::Justification::centred)
    {
        g.setFont (juce::FontOptions (size, juce::Font::bold));
        g.setColour (juce::Colours::black.withAlpha (0.35f));
        g.drawText (text, r.translated (0, 1), j);
        g.setColour (juce::Colours::white.withAlpha (0.92f));
        g.drawText (text, r, j);
    };

    label ("PITCH", { pitchKnob.getX() - 20, goldPanel.getY() + 10, pitchKnob.getWidth() + 40, 22 }, 16.0f);
    label ("FORMANT", { formantKnob.getX() - 20, goldPanel.getY() + 10, formantKnob.getWidth() + 40, 22 }, 16.0f);
    label ("LINK", { linkButton.getX() - 20, linkButton.getBottom() + 3, 62, 14 }, 10.0f);
    g.setColour (juce::Colours::white.withAlpha (0.8f));
    g.drawLine ((float) pitchKnob.getRight() + 2.0f, (float) linkButton.getBounds().getCentreY(), (float) linkButton.getX() - 4.0f, (float) linkButton.getBounds().getCentreY(), 1.5f);
    g.drawLine ((float) linkButton.getRight() + 4.0f, (float) linkButton.getBounds().getCentreY(), (float) formantKnob.getX() - 2.0f, (float) linkButton.getBounds().getCentreY(), 1.5f);

    label ("MODE", { bluePanel.getX(), bluePanel.getY() + 10, bluePanel.getWidth(), 22 }, 16.0f);
    label ("DRIVE", { driveKnob.getX() - 20, redPanel.getY() + 10, driveKnob.getWidth() + 40, 22 }, 16.0f);
    label ("MIX", { mixKnob.getX() - 20, redPanel.getY() + 10, mixKnob.getWidth() + 40, 22 }, 16.0f);
    label ("MIN", { driveKnob.getX() - 12, driveKnob.getBottom() + 2, 40, 14 }, 10.0f);
    label ("MAX", { driveKnob.getRight() - 28, driveKnob.getBottom() + 2, 40, 14 }, 10.0f);
    label ("DRY", { mixKnob.getX() - 12, mixKnob.getBottom() + 2, 40, 14 }, 10.0f);
    label ("WET", { mixKnob.getRight() - 28, mixKnob.getBottom() + 2, 40, 14 }, 10.0f);

    // Drive / mix value hints.
    g.setFont (juce::FontOptions (11.0f));
    g.setColour (juce::Colours::white.withAlpha (0.6f));
    g.drawText (juce::String (juce::roundToInt (driveKnob.getValue() * 100.0)) + "%", driveKnob.getX(), driveKnob.getBottom() + 22, driveKnob.getWidth(), 14, juce::Justification::centred);
    g.drawText (juce::String (juce::roundToInt (mixKnob.getValue() * 100.0)) + "%", mixKnob.getX(), mixKnob.getBottom() + 22, mixKnob.getWidth(), 14, juce::Justification::centred);

    // Footer branding.
    auto f = footer;
    g.setFont (juce::FontOptions (26.0f, juce::Font::bold));
    g.setColour (kLedRed);
    g.drawText ("txiki", f.getX() + 4, f.getY(), 62, f.getHeight(), juce::Justification::centredLeft);
    g.setColour (juce::Colours::white);
    g.drawText ("alterboy", f.getX() + 64, f.getY(), 120, f.getHeight(), juce::Justification::centredLeft);
    g.setFont (juce::FontOptions (13.0f));
    g.setColour (juce::Colours::white.withAlpha (0.7f));
    g.drawText ("monophonic voice manipulation", f.getX() + 186, f.getY() + 3, 220, f.getHeight(), juce::Justification::centredLeft);
    g.setFont (juce::FontOptions (10.0f, juce::Font::bold));
    g.drawText (juce::String (modeButtons[2].getToggleState() ? "ROBOT NOTE" : "INPUT NOTE"), noteLed.getX() - 80, noteLed.getY(), 76, noteLed.getHeight(), juce::Justification::centredRight);
}
