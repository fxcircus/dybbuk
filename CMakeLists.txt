cmake_minimum_required(VERSION 3.22)
project(Dybbuk VERSION 0.1.0)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_OSX_DEPLOYMENT_TARGET "14.0" CACHE STRING "" FORCE)
# Dev builds: arm64 only (much faster). Flip to "arm64;x86_64" for a universal release.
set(CMAKE_OSX_ARCHITECTURES "arm64" CACHE STRING "" FORCE)

include(FetchContent)
FetchContent_Declare(JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG        8.0.12
    GIT_SHALLOW    TRUE)
FetchContent_MakeAvailable(JUCE)

# --- Fonts ------------------------------------------------------------------
# The design's typography is half its character: EB Garamond for everything,
# Frank Ruhl Libre for the Hebrew wordmark, Pinyon Script for the preset name.
# Embedded rather than looked up by name, so the plugin looks the same on a
# machine that has never seen these faces. All three are SIL Open Font License.
juce_add_binary_data(DybbukFonts
    HEADER_NAME DybbukFonts.h
    NAMESPACE fonts
    SOURCES
        Resources/fonts/EBGaramond-Regular.ttf
        Resources/fonts/EBGaramond-SemiBold.ttf
        Resources/fonts/FrankRuhlLibre-Bold.ttf
        Resources/fonts/PinyonScript-Regular.ttf)

# Binary data is linked into a shared library on some platforms; position
# independent code keeps that valid.
set_target_properties(DybbukFonts PROPERTIES POSITION_INDEPENDENT_CODE TRUE)

# --- The plugin -------------------------------------------------------------
# PLUGIN_CODE must be unique per plugin and start with an uppercase letter.
# Standalone is here deliberately: it is the fast test rig (no DAW rescan).
juce_add_plugin(Dybbuk
    COMPANY_NAME              "fxcircus"
    BUNDLE_ID                 "com.fxcircus.dybbuk"
    IS_SYNTH                  FALSE
    NEEDS_MIDI_INPUT          TRUE
    NEEDS_MIDI_OUTPUT         FALSE
    IS_MIDI_EFFECT            FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD   TRUE
    PLUGIN_MANUFACTURER_CODE  Fxci
    PLUGIN_CODE               Dybk
    FORMATS                   AU VST3 Standalone
    PRODUCT_NAME              "Dybbuk")

# Sources shared by the plugin and the headless UI renderer. Keep DSP-only
# files in ENGINE_SOURCES so EngineTest never drags in the editor.
set(ENGINE_SOURCES
    Source/dsp/BurstEngine.cpp)

set(PLUGIN_SOURCES
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp
    Source/Parameters.cpp
    Source/state/PresetManager.cpp
    Source/state/Randomiser.cpp
    Source/state/FactoryPresets.cpp
    Source/state/WavExport.cpp
    Source/ui/Theme.cpp
    Source/ui/EngravedKnob.cpp
    Source/ui/VerticalFader.cpp
    Source/ui/Lamp.cpp
    Source/ui/PlateControls.cpp
    Source/ui/PresetHeader.cpp
    ${ENGINE_SOURCES})

target_sources(Dybbuk PRIVATE ${PLUGIN_SOURCES})

target_compile_definitions(Dybbuk PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0)

target_link_libraries(Dybbuk
    PRIVATE
        DybbukFonts
        juce::juce_audio_utils
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)

# --- Headless UI renderer ---------------------------------------------------
# Writes editor_snapshot*.png to the CWD so UI changes can be reviewed without
# opening a DAW. This is the single most useful tool in the repo: run it after
# every UI change and actually look at the output.
juce_add_console_app(UISnapshot PRODUCT_NAME "UISnapshot")
target_sources(UISnapshot PRIVATE Tests/UISnapshot.cpp ${PLUGIN_SOURCES})
target_compile_definitions(UISnapshot PRIVATE
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_MODAL_LOOPS_PERMITTED=1
    "JucePlugin_Name=\"Dybbuk\""
    "JucePlugin_VersionString=\"0.1.0\"")
target_link_libraries(UISnapshot
    PRIVATE
        DybbukFonts
        juce::juce_audio_utils
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags)

# --- State harness ----------------------------------------------------------
# Round trips every parameter and every readout through save/restore. The bug
# class this exists for is state that silently fails to save: CLAUDE.md calls
# it the most common one across these projects, and it is always the user who
# finds it.
juce_add_console_app(ProcessorTest PRODUCT_NAME "ProcessorTest")
target_sources(ProcessorTest PRIVATE Tests/ProcessorTest.cpp ${PLUGIN_SOURCES})
target_compile_definitions(ProcessorTest PRIVATE
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_MODAL_LOOPS_PERMITTED=1
    "JucePlugin_Name=\"Dybbuk\""
    "JucePlugin_VersionString=\"0.1.0\"")
target_link_libraries(ProcessorTest
    PRIVATE
        DybbukFonts
        juce::juce_audio_utils
        juce::juce_audio_formats
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags)

# --- Offline DSP harness ----------------------------------------------------
# Drives the engine like a host and prints measurements. Add a named scenario
# for every behaviour worth trusting; assert with numbers, not by ear.
juce_add_console_app(EngineTest PRODUCT_NAME "EngineTest")
target_sources(EngineTest PRIVATE Tests/EngineTest.cpp ${ENGINE_SOURCES})
target_compile_definitions(EngineTest PRIVATE
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0)
target_link_libraries(EngineTest
    PRIVATE
        juce::juce_dsp
        juce::juce_audio_formats
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags)
