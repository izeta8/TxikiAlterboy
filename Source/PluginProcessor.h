#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_dsp/juce_dsp.h>

#include "dsp/TubeDrive.h"
#include "dsp/VoiceShifter.h"

namespace ParamIDs
{
inline constexpr const char* pitch = "pitch";
inline constexpr const char* formant = "formant";
inline constexpr const char* link = "link";
inline constexpr const char* mode = "mode";
inline constexpr const char* drive = "drive";
inline constexpr const char* mix = "mix";
inline constexpr const char* midi = "midi";
inline constexpr const char* bypass = "bypass";
} // namespace ParamIDs

class TxikiAlterboyProcessor : public juce::AudioProcessor
{
public:
    TxikiAlterboyProcessor();
    ~TxikiAlterboyProcessor() override = default;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.1; }
    juce::AudioProcessorParameter* getBypassParameter() const override { return apvts.getParameter (ParamIDs::bypass); }

    int getNumPrograms() override;
    int getCurrentProgram() override { return currentProgram; }
    void setCurrentProgram (int index) override;
    const juce::String getProgramName (int index) override;
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    juce::AudioProcessorValueTreeState apvts;

    // UI feedback.
    std::atomic<float> detectedHz { 0.0f };
    std::atomic<float> midiPitch { 0.0f };
    std::atomic<bool> midiActive { false };
    std::atomic<float> outputLevel { 0.0f };

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
    void handleMidi (const juce::MidiMessage& m);
    void updateShifterParams();

    txiki::VoiceShifter shifter;
    txiki::TubeDrive tubes[2];
    juce::dsp::Oversampling<float> oversampling { 2, 1, juce::dsp::Oversampling<float>::filterHalfBandPolyphaseIIR, true };

    juce::AudioBuffer<float> wetBuffer;
    juce::AudioBuffer<float> dryDelay;
    int dryWritePos = 0;
    int latency = 0;
    int driveLatency = 0;

    juce::SmoothedValue<float> mixSmoothed, driveSmoothed, bypassSmoothed;

    std::vector<int> heldNotes;
    int currentProgram = 0;

    std::atomic<float>* pPitch = nullptr;
    std::atomic<float>* pFormant = nullptr;
    std::atomic<float>* pLink = nullptr;
    std::atomic<float>* pMode = nullptr;
    std::atomic<float>* pDrive = nullptr;
    std::atomic<float>* pMix = nullptr;
    std::atomic<float>* pMidi = nullptr;
    std::atomic<float>* pBypass = nullptr;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TxikiAlterboyProcessor)
};
