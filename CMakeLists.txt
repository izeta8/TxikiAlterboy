cmake_minimum_required(VERSION 3.22)
project(TxikiAlterboy VERSION 1.0.0)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

set(JUCE_DIR "C:/SDKs/JUCE" CACHE PATH "JUCE checkout")
add_subdirectory(${JUCE_DIR} JUCE)

juce_add_plugin(TxikiAlterboy
    COMPANY_NAME "Txiki"
    COMPANY_WEBSITE ""
    PLUGIN_MANUFACTURER_CODE Txki
    PLUGIN_CODE TxAb
    PRODUCT_NAME "Txiki AlterBoy"
    FORMATS VST3 Standalone
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT TRUE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD FALSE
    VST3_CATEGORIES "Fx|Pitch Shift"
    DESCRIPTION "Monophonic voice manipulation")

target_sources(TxikiAlterboy PRIVATE
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp)

target_include_directories(TxikiAlterboy PRIVATE Source)

target_compile_definitions(TxikiAlterboy PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0)

target_link_libraries(TxikiAlterboy
    PRIVATE
        juce::juce_audio_utils
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)

add_executable(OfflineTest tests/OfflineTest.cpp)

# Processor-level tests (presets, state) built as a JUCE console app.
juce_add_console_app(ProcessorTest PRODUCT_NAME "ProcessorTest")
target_sources(ProcessorTest PRIVATE
    tests/ProcessorTest.cpp
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp)
target_include_directories(ProcessorTest PRIVATE Source)
target_compile_definitions(ProcessorTest PRIVATE
    JucePlugin_Name="Txiki AlterBoy"
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0)
target_link_libraries(ProcessorTest PRIVATE
    juce::juce_audio_utils
    juce::juce_dsp
    juce::juce_recommended_config_flags)
