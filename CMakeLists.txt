cmake_minimum_required(VERSION 3.22)
set(CMAKE_OSX_DEPLOYMENT_TARGET "12.0" CACHE STRING "")
set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "")
project(NEON_RIOT VERSION 1.1.0)
set(CMAKE_CXX_STANDARD 20)

set(JUCE_DIR "${CMAKE_SOURCE_DIR}/JUCE" CACHE PATH "Path to a JUCE 8 checkout")
add_subdirectory("${JUCE_DIR}" JUCE)

juce_add_plugin(NeonRiot
    COMPANY_NAME "Rob Cawley"
    COMPANY_WEBSITE "https://robcawley.com"
    BUNDLE_ID "com.robcawley.neonriot"
    PLUGIN_MANUFACTURER_CODE Rcwl
    PLUGIN_CODE NRio
    PRODUCT_NAME "NEON RIOT"
    DESCRIPTION "Heavy bass synthesizer"
    IS_SYNTH TRUE
    NEEDS_MIDI_INPUT TRUE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    AU_MAIN_TYPE kAudioUnitType_MusicDevice
    FORMATS AU
    COPY_PLUGIN_AFTER_BUILD TRUE
    VERSION 1.1.0)

juce_add_binary_data(NeonRiotAssets SOURCES
    Resources/Fonts/Rajdhani-SemiBold.ttf
    Resources/Fonts/Rajdhani-Bold.ttf
    Resources/Fonts/Rajdhani-Medium.ttf
    Resources/Fonts/ShareTechMono-Regular.ttf)

set(NEON_UI_SOURCES
    Source/UI/Theme.cpp
    Source/UI/Chassis.cpp
    Source/UI/Controls.cpp
    Source/UI/Tooltips.cpp
    Source/UI/Displays.cpp)

target_sources(NeonRiot PRIVATE
    ${NEON_UI_SOURCES}
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp
    Source/Parameters.cpp
    Source/Presets.cpp
    Source/DSP/Engine.cpp
    Source/DSP/Effects.cpp)

target_include_directories(NeonRiot PRIVATE Source)

target_compile_definitions(NeonRiot PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    )

target_link_libraries(NeonRiot PRIVATE
    NeonRiotAssets
    juce::juce_audio_utils
    juce::juce_dsp
    PUBLIC
    juce::juce_recommended_config_flags
    juce::juce_recommended_lto_flags
    juce::juce_recommended_warning_flags)

# Offline render/test harness: renders every factory preset to WAV and
# reports peak, RMS, DC and non-finite samples.
juce_add_console_app(NeonRiotRender PRODUCT_NAME "NeonRiotRender")
target_sources(NeonRiotRender PRIVATE
    Tools/Render.cpp
    Tools/Evolve.cpp
    ${NEON_UI_SOURCES}
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp
    Source/Parameters.cpp
    Source/Presets.cpp
    Source/DSP/Engine.cpp
    Source/DSP/Effects.cpp)
target_include_directories(NeonRiotRender PRIVATE Source)
target_compile_definitions(NeonRiotRender PRIVATE
    JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JucePlugin_Name="NEON RIOT" JucePlugin_IsSynth=1
    JucePlugin_WantsMidiInput=1 JucePlugin_ProducesMidiOutput=0 JucePlugin_IsMidiEffect=0)
target_link_libraries(NeonRiotRender PRIVATE
    NeonRiotAssets
    juce::juce_audio_utils juce::juce_dsp juce::juce_audio_formats
    juce::juce_recommended_config_flags)

# Host the installed AU through JUCE's AudioUnitPluginFormat; no processor implementation linked.
juce_add_console_app(NeonRiotHostTest PRODUCT_NAME "NeonRiotHostTest")
target_sources(NeonRiotHostTest PRIVATE Tools/HostTest.cpp)
target_compile_definitions(NeonRiotHostTest PRIVATE
    JUCE_PLUGINHOST_AU=1 JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_MODAL_LOOPS_PERMITTED=1)
target_link_libraries(NeonRiotHostTest PRIVATE
    juce::juce_audio_utils juce::juce_recommended_config_flags)

# Loop pack renderer: MIDI loops through NEON RIOT presets (seamless, tempo-tagged WAVs).
juce_add_console_app(NeonRiotLoops PRODUCT_NAME "NeonRiotLoops")
target_sources(NeonRiotLoops PRIVATE
    Tools/LoopRender.cpp
    ${NEON_UI_SOURCES}
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp
    Source/Parameters.cpp
    Source/Presets.cpp
    Source/DSP/Engine.cpp
    Source/DSP/Effects.cpp)
target_include_directories(NeonRiotLoops PRIVATE Source)
target_compile_definitions(NeonRiotLoops PRIVATE
    JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JucePlugin_Name="NEON RIOT" JucePlugin_IsSynth=1
    JucePlugin_WantsMidiInput=1 JucePlugin_ProducesMidiOutput=0 JucePlugin_IsMidiEffect=0)
target_link_libraries(NeonRiotLoops PRIVATE
    NeonRiotAssets
    juce::juce_audio_utils juce::juce_dsp juce::juce_audio_formats
    juce::juce_recommended_config_flags)
