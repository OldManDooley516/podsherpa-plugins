cmake_minimum_required(VERSION 3.22)
set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "Minimum macOS version")
project(PodSherpaPlugins VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")   # no VC++ runtime install needed on Windows

include(FetchContent)
FetchContent_Declare(JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG 9.0.5
    GIT_SHALLOW TRUE)
FetchContent_MakeAvailable(JUCE)

if(APPLE)
    set(PS_FORMATS VST3 AU)
else()
    set(PS_FORMATS VST3)
endif()

set(PS_DEFS JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_VST3_CAN_REPLACE_VST2=0 JUCE_DISPLAY_SPLASH_SCREEN=0)

# ps_plugin(<target> <product name> <4-char code> <VST3 category> <sources...>)
function(ps_plugin target name code category)
    juce_add_plugin(${target}
        COMPANY_NAME "Southern Digital"
        BUNDLE_ID "com.southerndigital.podsherpa.${target}"
        PLUGIN_MANUFACTURER_CODE SoDg
        PLUGIN_CODE ${code}
        FORMATS ${PS_FORMATS}
        PRODUCT_NAME "${name}"
        IS_SYNTH FALSE
        NEEDS_MIDI_INPUT FALSE
        NEEDS_MIDI_OUTPUT FALSE
        VST3_CATEGORIES Fx ${category}
        AU_MAIN_TYPE kAudioUnitType_Effect
        COPY_PLUGIN_AFTER_BUILD FALSE)
    target_sources(${target} PRIVATE ${ARGN})
    target_compile_definitions(${target} PUBLIC ${PS_DEFS})
    target_link_libraries(${target}
        PRIVATE juce::juce_audio_processors juce::juce_gui_basics
        PUBLIC juce::juce_recommended_config_flags juce::juce_recommended_lto_flags)
endfunction()

ps_plugin(Altitude  "PodSherpa Altitude"  PsAl Dynamics plugins/Altitude/Plugin.cpp)
ps_plugin(Ridgeline "PodSherpa Ridgeline" PsRl Dynamics plugins/Ridgeline/Plugin.cpp)
ps_plugin(Scree     "PodSherpa Scree"     PsSc Restoration plugins/Scree/Plugin.cpp)
ps_plugin(Serac     "PodSherpa Serac"     PsSe Restoration plugins/Serac/Plugin.cpp)
ps_plugin(Summit   "PodSherpa Summit"   PsSm Dynamics plugins/Summit/Plugin.cpp)
ps_plugin(Traverse  "PodSherpa Traverse"  PsTv Dynamics plugins/Traverse/Plugin.cpp)
ps_plugin(Clearing  "PodSherpa Clearing"  PsCl Restoration plugins/Clearing/Plugin.cpp)
ps_plugin(Contour   "PodSherpa Contour"   PsCt EQ plugins/Contour/Plugin.cpp)
ps_plugin(ContourDynamic "PodSherpa Contour Dynamic" PsCd EQ plugins/ContourDynamic/Plugin.cpp)
ps_plugin(Dropoff  "PodSherpa Dropoff"  PsDo Filter   plugins/Dropoff/Plugin.cpp)
ps_plugin(Basecamp "PodSherpa Basecamp" PsBc Mastering
    plugins/Basecamp/BasecampProcessor.cpp plugins/Basecamp/BasecampEditor.cpp)

# Headless checks of the DSP and the rack logic
juce_add_console_app(ps_tests PRODUCT_NAME "ps_tests")
target_sources(ps_tests PRIVATE tests/tests.cpp
    plugins/Basecamp/BasecampProcessor.cpp plugins/Basecamp/BasecampEditor.cpp)
target_compile_definitions(ps_tests PRIVATE ${PS_DEFS})
target_link_libraries(ps_tests PRIVATE juce::juce_audio_processors juce::juce_gui_basics juce::juce_recommended_config_flags)
# Runs one module over a raw audio file, for checking a port against the original JSFX
juce_add_console_app(ps_ref PRODUCT_NAME "ps_ref")
target_sources(ps_ref PRIVATE tests/ref.cpp)
target_compile_definitions(ps_ref PRIVATE ${PS_DEFS})
target_link_libraries(ps_ref PRIVATE juce::juce_audio_basics juce::juce_data_structures juce::juce_recommended_config_flags)
enable_testing()
add_test(NAME ps_tests COMMAND ps_tests)
