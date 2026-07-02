cmake_minimum_required(VERSION 3.22)
project(RdhSynth VERSION 2.0.0)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ==============================================================================
# JUCE (requires /Applications/JUCE)
# ==============================================================================
add_subdirectory(/Applications/JUCE juce)

# ==============================================================================
# Plugin definition
# ==============================================================================
juce_add_plugin(RdhSynth
    COMPANY_NAME             "RdH"
    BUNDLE_ID                "com.rdh.synth"
    PLUGIN_MANUFACTURER_CODE RdHS
    PLUGIN_CODE              Hpq8
    FORMATS                  VST3 AU Standalone
    IS_SYNTH                 TRUE
    NEEDS_MIDI_INPUT         TRUE
    NEEDS_MIDI_OUTPUT        TRUE
    PRODUCT_NAME             "Rd-H Synth"
    VST3_CATEGORIES          "Instrument" "Synth"
)

juce_generate_juce_header(RdhSynth)

# ==============================================================================
# Source files
# ==============================================================================
target_sources(RdhSynth
    PRIVATE
        Source/PluginProcessor.cpp
        Source/PluginEditor.cpp
        Source/be/AudioAnalyzer.cpp
        Source/fe/CoolUiSkin.cpp
        Source/fe/SpectrumAnalyzerComponent.cpp
        Source/fe/AIMatchPanel.cpp
)

# ==============================================================================
# Compile definitions
# ==============================================================================
target_compile_definitions(RdhSynth
    PUBLIC
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_VST2=0
        JUCE_DISPLAY_SPLASH_SCREEN=0
)

# ==============================================================================
# JUCE modules
# juce_audio_utils includes: audio_basics, audio_formats, audio_processors,
#   gui_basics, gui_extra (includes MidiKeyboardComponent)
# juce_audio_basics provides juce::FFT used by AudioAnalyzer and
#   SpectrumAnalyzerComponent without requiring juce_dsp.
# ==============================================================================
target_link_libraries(RdhSynth
    PRIVATE
        juce::juce_audio_utils
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags
)

# ==============================================================================
# Offline render console app
# Headless preset(.rdhs) -> WAV renderer for objective DSP measurement
# (e.g. wavetable THD, noise spectral slope). Recompiles the same
# plugin sources so it exercises the real signal path. The JucePlugin_* macros
# (normally emitted by juce_add_plugin) are defined explicitly here, matching
# the juce_add_plugin(RdhSynth ...) settings above, since PluginProcessor.cpp
# references them and juce_add_console_app does not generate them.
# ==============================================================================
juce_add_console_app(RdhRender PRODUCT_NAME "RdhRender")
juce_generate_juce_header(RdhRender)

target_sources(RdhRender
    PRIVATE
        Tools/OfflineRenderer.cpp
        Source/PluginProcessor.cpp
        Source/PluginEditor.cpp
        Source/be/AudioAnalyzer.cpp
        Source/fe/CoolUiSkin.cpp
        Source/fe/SpectrumAnalyzerComponent.cpp
        Source/fe/AIMatchPanel.cpp
)

target_include_directories(RdhRender PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/Source)

target_compile_definitions(RdhRender
    PUBLIC
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_VST2=0
        JUCE_DISPLAY_SPLASH_SCREEN=0
        JUCE_STANDALONE_APPLICATION=1
        # --- mirror juce_add_plugin(RdhSynth) settings ---
        JucePlugin_Name="Rd-H Synth"
        JucePlugin_IsSynth=1
        JucePlugin_IsMidiEffect=0
        JucePlugin_WantsMidiInput=1
        JucePlugin_ProducesMidiOutput=1
)

target_link_libraries(RdhRender
    PRIVATE
        juce::juce_audio_utils
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags
)
