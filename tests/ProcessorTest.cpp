// Processor-level tests: user presets (save/load/delete) and state round-trip.

#include "../Source/PluginProcessor.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <cstdio>

namespace
{
int failures = 0;

void check (bool ok, const char* what)
{
    std::printf ("%-60s %s\n", what, ok ? "OK" : "FAIL");
    if (!ok)
        ++failures;
}

float plain (TxikiAlterboyProcessor& p, const char* id)
{
    auto* param = p.apvts.getParameter (id);
    return param->convertFrom0to1 (param->getValue());
}

void set (TxikiAlterboyProcessor& p, const char* id, float v)
{
    auto* param = p.apvts.getParameter (id);
    param->setValueNotifyingHost (param->convertTo0to1 (v));
}
} // namespace

int main()
{
    juce::ScopedJuceInitialiser_GUI gui;
    const juce::String name ("__txiki_test_preset");

    {
        TxikiAlterboyProcessor p;
        set (p, ParamIDs::pitch, 5.0f);
        set (p, ParamIDs::formant, -3.0f);
        set (p, ParamIDs::mode, 1.0f);
        set (p, ParamIDs::drive, 0.4f);
        set (p, ParamIDs::mix, 0.6f);
        set (p, ParamIDs::link, 1.0f);

        check (p.saveUserPreset (name), "save user preset");
        check (p.getUserPresetNames().contains (name), "preset listed");
        check (p.getCurrentUserPreset() == name, "current user preset after save");

        p.setCurrentProgram (0);
        check (p.getCurrentUserPreset().isEmpty(), "factory program clears user preset");
        check (std::abs (plain (p, ParamIDs::pitch)) < 0.01f, "factory Init resets pitch");

        set (p, ParamIDs::bypass, 1.0f);
        check (p.loadUserPreset (name), "load user preset");
        check (std::abs (plain (p, ParamIDs::pitch) - 5.0f) < 0.01f, "pitch restored");
        check (std::abs (plain (p, ParamIDs::formant) + 3.0f) < 0.01f, "formant restored");
        check (std::abs (plain (p, ParamIDs::mode) - 1.0f) < 0.01f, "mode restored");
        check (std::abs (plain (p, ParamIDs::drive) - 0.4f) < 0.01f, "drive restored");
        check (std::abs (plain (p, ParamIDs::mix) - 0.6f) < 0.01f, "mix restored");
        check (plain (p, ParamIDs::link) > 0.5f, "link restored");
        check (plain (p, ParamIDs::bypass) > 0.5f, "bypass not touched by presets");
        set (p, ParamIDs::bypass, 0.0f);

        // State round-trip keeps user preset name and UI scale.
        p.setUiScale (1.5f);
        juce::MemoryBlock state;
        p.getStateInformation (state);
        TxikiAlterboyProcessor q;
        q.setStateInformation (state.getData(), (int) state.getSize());
        check (q.getCurrentUserPreset() == name, "state keeps user preset name");
        check (std::abs (q.getUiScale() - 1.5f) < 0.001f, "state keeps UI scale");
        check (std::abs (plain (q, ParamIDs::pitch) - 5.0f) < 0.01f, "state keeps pitch");

        check (!p.loadUserPreset ("__does_not_exist__"), "missing preset fails cleanly");
        check (!p.saveUserPreset ("   "), "empty name rejected");

        check (p.deleteUserPreset (name), "delete user preset");
        check (!p.getUserPresetNames().contains (name), "preset no longer listed");
        check (p.getCurrentUserPreset().isEmpty(), "current user preset cleared on delete");
    }

    std::printf ("%s (%d failures)\n", failures == 0 ? "PROCESSOR TESTS PASSED" : "PROCESSOR TESTS FAILED", failures);
    return failures == 0 ? 0 : 1;
}
