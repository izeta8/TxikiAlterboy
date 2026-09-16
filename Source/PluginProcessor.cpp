#include "PluginProcessor.h"
#include "PluginEditor.h"

namespace
{
struct FactoryPreset
{
    const char* name;
    float pitch, formant;
    bool link;
    int mode;
    float drive, mix;
};

const FactoryPreset factoryPresets[] = {
    { "Init", 0.0f, 0.0f, false, 0, 0.0f, 1.0f },
    { "Deeper Voice", 0.0f, -2.5f, false, 0, 0.0f, 1.0f },
    { "Rich Background", 0.0f, -1.5f, false, 0, 0.1f, 0.45f },
    { "Octave Down Double", -12.0f, 0.0f, false, 0, 0.0f, 0.5f },
    { "Octave Up Double", 12.0f, 0.0f, false, 0, 0.0f, 0.4f },
    { "Fifth Harmony", 7.0f, 1.0f, false, 0, 0.0f, 0.5f },
    { "Third Harmony", 4.0f, 0.5f, false, 0, 0.0f, 0.5f },
    { "Natural Up", 3.0f, 1.0f, false, 0, 0.0f, 1.0f },
    { "Natural Down", -4.0f, -1.5f, false, 0, 0.0f, 1.0f },
    { "Chipmunk", 7.0f, 7.0f, true, 0, 0.0f, 1.0f },
    { "Tape Slow", -5.0f, -5.0f, true, 0, 0.15f, 1.0f },
    { "Monster", -7.0f, -6.0f, false, 0, 0.45f, 1.0f },
    { "Gender Bend Up", 5.0f, 3.5f, false, 0, 0.0f, 1.0f },
    { "Gender Bend Down", -5.0f, -3.0f, false, 0, 0.0f, 1.0f },
    { "Hard Tune", 0.0f, 0.0f, false, 1, 0.0f, 1.0f },
    { "Hard Tune Grit", 0.0f, 0.0f, false, 1, 0.45f, 1.0f },
    { "Tuned Low Double", -12.0f, -1.0f, false, 1, 0.1f, 0.5f },
    { "Robot C", 0.0f, 0.0f, false, 2, 0.0f, 1.0f },
    { "Robot Low Drone", -12.0f, -2.0f, false, 2, 0.3f, 1.0f },
    { "Vocoder Lead (MIDI)", 0.0f, 0.0f, false, 2, 0.2f, 1.0f },
    { "Distorted Robot", -5.0f, 0.0f, false, 2, 0.8f, 1.0f },
};

const int numFactoryPresets = (int) (sizeof (factoryPresets) / sizeof (factoryPresets[0]));
} // namespace

TxikiAlterboyProcessor::TxikiAlterboyProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput ("Input", juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "PARAMS", createLayout())
{
    pPitch = apvts.getRawParameterValue (ParamIDs::pitch);
    pFormant = apvts.getRawParameterValue (ParamIDs::formant);
    pLink = apvts.getRawParameterValue (ParamIDs::link);
    pMode = apvts.getRawParameterValue (ParamIDs::mode);
    pDrive = apvts.getRawParameterValue (ParamIDs::drive);
    pMix = apvts.getRawParameterValue (ParamIDs::mix);
    pMidi = apvts.getRawParameterValue (ParamIDs::midi);
    pBypass = apvts.getRawParameterValue (ParamIDs::bypass);
    heldNotes.reserve (128);
}

juce::AudioProcessorValueTreeState::ParameterLayout TxikiAlterboyProcessor::createLayout()
{
    using namespace juce;
    std::vector<std::unique_ptr<RangedAudioParameter>> params;

    auto semis = AudioParameterFloatAttributes()
                     .withStringFromValueFunction ([] (float v, int) { return String (v, 1); })
                     .withLabel ("st");

    params.push_back (std::make_unique<AudioParameterFloat> (ParameterID { ParamIDs::pitch, 1 }, "Pitch",
                                                             NormalisableRange<float> (-12.0f, 12.0f, 0.1f), 0.0f, semis));
    params.push_back (std::make_unique<AudioParameterFloat> (ParameterID { ParamIDs::formant, 1 }, "Formant",
                                                             NormalisableRange<float> (-12.0f, 12.0f, 0.1f), 0.0f, semis));
    params.push_back (std::make_unique<AudioParameterBool> (ParameterID { ParamIDs::link, 1 }, "Link", false));
    params.push_back (std::make_unique<AudioParameterChoice> (ParameterID { ParamIDs::mode, 1 }, "Mode",
                                                              StringArray { "Transpose", "Quantize", "Robot" }, 0));

    auto percent = AudioParameterFloatAttributes()
                       .withStringFromValueFunction ([] (float v, int) { return String (roundToInt (v * 100.0f)) + " %"; });
    params.push_back (std::make_unique<AudioParameterFloat> (ParameterID { ParamIDs::drive, 1 }, "Drive",
                                                             NormalisableRange<float> (0.0f, 1.0f), 0.0f, percent));
    params.push_back (std::make_unique<AudioParameterFloat> (ParameterID { ParamIDs::mix, 1 }, "Mix",
                                                             NormalisableRange<float> (0.0f, 1.0f), 1.0f, percent));
    params.push_back (std::make_unique<AudioParameterBool> (ParameterID { ParamIDs::midi, 1 }, "MIDI Control", true));
    params.push_back (std::make_unique<AudioParameterBool> (ParameterID { ParamIDs::bypass, 1 }, "Bypass", false));

    return { params.begin(), params.end() };
}

bool TxikiAlterboyProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto out = layouts.getMainOutputChannelSet();
    const auto in = layouts.getMainInputChannelSet();
    if (out != juce::AudioChannelSet::mono() && out != juce::AudioChannelSet::stereo())
        return false;
    if (in != juce::AudioChannelSet::mono() && in != juce::AudioChannelSet::stereo())
        return false;
    // mono->mono, mono->stereo, stereo->stereo
    return !(in == juce::AudioChannelSet::stereo() && out == juce::AudioChannelSet::mono());
}

void TxikiAlterboyProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    shifter.prepare (sampleRate, 2);
    for (auto& t : tubes)
        t.prepare (sampleRate * 2.0);
    oversampling.reset();
    oversampling.initProcessing ((size_t) samplesPerBlock);
    driveLatency = juce::roundToInt (oversampling.getLatencyInSamples());

    latency = shifter.getLatencySamples() + driveLatency;
    setLatencySamples (latency);

    wetBuffer.setSize (2, samplesPerBlock);
    dryDelay.setSize (2, latency + 1);
    dryDelay.clear();
    dryWritePos = 0;

    mixSmoothed.reset (sampleRate, 0.03);
    mixSmoothed.setCurrentAndTargetValue (pMix->load());
    driveSmoothed.reset (sampleRate, 0.05);
    driveSmoothed.setCurrentAndTargetValue (pDrive->load());
    bypassSmoothed.reset (sampleRate, 0.02);
    bypassSmoothed.setCurrentAndTargetValue (pBypass->load());

    heldNotes.clear();
    midiActive = false;
}

void TxikiAlterboyProcessor::handleMidi (const juce::MidiMessage& m)
{
    if (m.isNoteOn())
    {
        heldNotes.erase (std::remove (heldNotes.begin(), heldNotes.end(), m.getNoteNumber()), heldNotes.end());
        heldNotes.push_back (m.getNoteNumber());
    }
    else if (m.isNoteOff())
    {
        heldNotes.erase (std::remove (heldNotes.begin(), heldNotes.end(), m.getNoteNumber()), heldNotes.end());
    }
    else if (m.isAllNotesOff() || m.isAllSoundOff())
    {
        heldNotes.clear();
    }
}

void TxikiAlterboyProcessor::updateShifterParams()
{
    const auto mode = (txiki::Mode) juce::jlimit (0, 2, (int) pMode->load());
    float pitch = pPitch->load();

    const bool useMidi = pMidi->load() > 0.5f && !heldNotes.empty();
    if (useMidi)
    {
        // C3 (MIDI 60) = pitch 0. Robot mode may follow the full keyboard.
        const float fromNote = (float) (heldNotes.back() - 60);
        pitch = mode == txiki::Mode::Robot ? juce::jlimit (-36.0f, 36.0f, fromNote) : juce::jlimit (-12.0f, 12.0f, fromNote);
    }
    midiActive = useMidi;
    midiPitch = pitch;

    const bool link = pLink->load() > 0.5f;
    shifter.setParameters (mode, pitch, link ? pitch : pFormant->load(), link);
}

void TxikiAlterboyProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages)
{
    juce::ScopedNoDenormals noDenormals;

    const int numSamples = buffer.getNumSamples();
    const int numIn = getTotalNumInputChannels();
    const int numOut = getTotalNumOutputChannels();

    if (wetBuffer.getNumSamples() < numSamples)
        wetBuffer.setSize (2, numSamples, false, false, true);

    for (int ch = numIn; ch < numOut; ++ch)
        buffer.clear (ch, 0, numSamples);

    // Stereo pitch engine (shared analysis, per-channel synthesis), MIDI applied sample-accurately.
    auto* wetL = wetBuffer.getWritePointer (0);
    auto* wetR = wetBuffer.getWritePointer (1);
    auto midiIt = midiMessages.cbegin();
    updateShifterParams();

    for (int i = 0; i < numSamples; ++i)
    {
        bool changed = false;
        while (midiIt != midiMessages.cend() && (*midiIt).samplePosition <= i)
        {
            handleMidi ((*midiIt).getMessage());
            ++midiIt;
            changed = true;
        }
        if (changed)
            updateShifterParams();

        float frameIn[2] = { 0.0f, 0.0f }, frameOut[2];
        if (numIn > 0)
        {
            frameIn[0] = buffer.getReadPointer (0)[i];
            frameIn[1] = buffer.getReadPointer (juce::jmin (1, numIn - 1))[i];
        }
        shifter.processFrame (frameIn, frameOut);
        wetL[i] = frameOut[0];
        wetR[i] = frameOut[1];
    }
    while (midiIt != midiMessages.cend())
    {
        handleMidi ((*midiIt).getMessage());
        ++midiIt;
    }
    updateShifterParams();
    detectedHz = shifter.getDetectedHz();

    // Tube drive at 2x.
    driveSmoothed.setTargetValue (pDrive->load());
    {
        juce::dsp::AudioBlock<float> block (wetBuffer.getArrayOfWritePointers(), 2, (size_t) numSamples);
        auto up = oversampling.processSamplesUp (block);
        auto* upL = up.getChannelPointer (0);
        auto* upR = up.getChannelPointer (1);
        const int upLen = (int) up.getNumSamples();
        for (int i = 0; i < upLen; ++i)
        {
            if (i % 2 == 0)
            {
                const float amount = driveSmoothed.getNextValue();
                tubes[0].setAmount (amount);
                tubes[1].setAmount (amount);
            }
            upL[i] = tubes[0].process (upL[i]);
            upR[i] = tubes[1].process (upR[i]);
        }
        oversampling.processSamplesDown (block);
    }

    // Latency-compensated dry/wet.
    mixSmoothed.setTargetValue (pMix->load());
    bypassSmoothed.setTargetValue (pBypass->load() > 0.5f ? 1.0f : 0.0f);
    const int delayLen = dryDelay.getNumSamples();
    float peak = 0.0f;
    for (int i = 0; i < numSamples; ++i)
    {
        // Host bypass keeps the engine running and outputs the latency-aligned dry signal.
        const float m = mixSmoothed.getNextValue() * (1.0f - bypassSmoothed.getNextValue());
        const int readPos = dryWritePos; // buffer length = latency + 1
        for (int ch = 0; ch < numOut; ++ch)
        {
            const int srcCh = juce::jmin (ch, numIn - 1);
            const float dryIn = numIn > 0 ? buffer.getReadPointer (srcCh)[i] : 0.0f;
            auto* delayData = dryDelay.getWritePointer (juce::jmin (ch, 1));
            const float dry = delayData[readPos];
            delayData[(readPos + delayLen - 1) % delayLen] = dryIn;
            const float wetSample = (numOut == 1 ? 0.5f * (wetL[i] + wetR[i]) : (ch == 0 ? wetL[i] : wetR[i]));
            const float y = dry * (1.0f - m) + wetSample * m;
            buffer.getWritePointer (ch)[i] = y;
            peak = juce::jmax (peak, std::abs (y));
        }
        dryWritePos = (dryWritePos + 1) % delayLen;
    }
    outputLevel = peak;
}

int TxikiAlterboyProcessor::getNumPrograms() { return numFactoryPresets; }

const juce::String TxikiAlterboyProcessor::getProgramName (int index)
{
    return juce::isPositiveAndBelow (index, numFactoryPresets) ? factoryPresets[index].name : "";
}

void TxikiAlterboyProcessor::setCurrentProgram (int index)
{
    if (!juce::isPositiveAndBelow (index, numFactoryPresets))
        return;
    currentProgram = index;
    currentUserPreset = {};
    const auto& p = factoryPresets[index];
    auto set = [this] (const char* id, float value)
    {
        if (auto* param = apvts.getParameter (id))
            param->setValueNotifyingHost (param->convertTo0to1 (value));
    };
    set (ParamIDs::pitch, p.pitch);
    set (ParamIDs::formant, p.formant);
    set (ParamIDs::link, p.link ? 1.0f : 0.0f);
    set (ParamIDs::mode, (float) p.mode);
    set (ParamIDs::drive, p.drive);
    set (ParamIDs::mix, p.mix);
}

void TxikiAlterboyProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    state.setProperty ("program", currentProgram, nullptr);
    state.setProperty ("userPreset", currentUserPreset, nullptr);
    if (auto xml = state.createXml())
        copyXmlToBinary (*xml, destData);
}

void TxikiAlterboyProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        if (xml->hasTagName (apvts.state.getType()))
        {
            auto tree = juce::ValueTree::fromXml (*xml);
            currentProgram = tree.getProperty ("program", 0);
            currentUserPreset = tree.getProperty ("userPreset", juce::String()).toString();
            apvts.replaceState (tree);

            // replaceState skips parameters whose denormalised value looks unchanged, which
            // leaves e.g. a bool holding a host-written 0.17. Push every value explicitly.
            for (auto* p : getParameters())
                if (auto* ranged = dynamic_cast<juce::RangedAudioParameter*> (p))
                    if (auto* stored = apvts.getRawParameterValue (ranged->getParameterID()))
                        ranged->setValueNotifyingHost (ranged->convertTo0to1 (stored->load()));
        }
}

juce::File TxikiAlterboyProcessor::getUserPresetFolder()
{
    return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory).getChildFile ("TxikiAlterboy").getChildFile ("Presets");
}

juce::StringArray TxikiAlterboyProcessor::getUserPresetNames() const
{
    juce::StringArray names;
    for (const auto& f : getUserPresetFolder().findChildFiles (juce::File::findFiles, false, "*.txpreset"))
        names.add (f.getFileNameWithoutExtension());
    names.sortNatural();
    return names;
}

bool TxikiAlterboyProcessor::saveUserPreset (const juce::String& name)
{
    const auto legal = juce::File::createLegalFileName (name.trim());
    if (legal.isEmpty())
        return false;
    auto folder = getUserPresetFolder();
    if (!folder.createDirectory())
        return false;

    juce::XmlElement xml ("TxikiPreset");
    for (auto* p : getParameters())
        if (auto* ranged = dynamic_cast<juce::RangedAudioParameter*> (p))
            if (ranged->getParameterID() != ParamIDs::bypass)
            {
                auto* e = xml.createNewChildElement ("PARAM");
                e->setAttribute ("id", ranged->getParameterID());
                e->setAttribute ("value", ranged->convertFrom0to1 (ranged->getValue()));
            }
    if (!xml.writeTo (folder.getChildFile (legal + ".txpreset")))
        return false;
    currentUserPreset = legal;
    return true;
}

bool TxikiAlterboyProcessor::loadUserPreset (const juce::String& name)
{
    auto xml = juce::XmlDocument::parse (getUserPresetFolder().getChildFile (name + ".txpreset"));
    if (xml == nullptr || !xml->hasTagName ("TxikiPreset"))
        return false;
    for (auto* e : xml->getChildWithTagNameIterator ("PARAM"))
        if (auto* param = apvts.getParameter (e->getStringAttribute ("id")))
        {
            param->beginChangeGesture();
            param->setValueNotifyingHost (param->convertTo0to1 ((float) e->getDoubleAttribute ("value")));
            param->endChangeGesture();
        }
    currentUserPreset = name;
    return true;
}

bool TxikiAlterboyProcessor::deleteUserPreset (const juce::String& name)
{
    const bool ok = getUserPresetFolder().getChildFile (name + ".txpreset").deleteFile();
    if (ok && currentUserPreset == name)
        currentUserPreset = {};
    return ok;
}

juce::AudioProcessorEditor* TxikiAlterboyProcessor::createEditor() { return new TxikiAlterboyEditor (*this); }

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new TxikiAlterboyProcessor(); }
