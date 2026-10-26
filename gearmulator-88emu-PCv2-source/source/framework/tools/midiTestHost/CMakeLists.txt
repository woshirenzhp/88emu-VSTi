cmake_minimum_required(VERSION 3.22)
project(MidiTestHost VERSION 1.0.0 LANGUAGES C CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
enable_testing()
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
# Embed helper debug symbols to avoid depending on a running MSPDB server.
if(MSVC)
    set(extra_compiler_flag_arguments "-DCMAKE_POLICY_DEFAULT_CMP0141=NEW" "-DCMAKE_MSVC_DEBUG_INFORMATION_FORMAT=Embedded" "-DCMAKE_CXX_FLAGS_DEBUG=/Z7 /Od")
endif()
add_subdirectory("${CMAKE_CURRENT_LIST_DIR}/../../../3rdparty/JUCE" "${CMAKE_BINARY_DIR}/JUCE")
juce_add_gui_app(MidiTestHost PRODUCT_NAME "MIDI Test Host" COMPANY_NAME "Gearmulator" BUNDLE_ID "org.gearmulator.miditesthost")
target_sources(MidiTestHost PRIVATE Main.cpp Routing.h BridgeTests.cpp)
target_include_directories(MidiTestHost PRIVATE "${CMAKE_CURRENT_LIST_DIR}/../../../3rdparty/JUCE/modules/juce_audio_processors/format_types/VST3_SDK")
if(MSVC)
    target_compile_options(MidiTestHost PRIVATE /utf-8)
endif()
# GDI avoids glyph corruption on affected Windows setups after native file dialogs.
target_compile_definitions(MidiTestHost PRIVATE
    JUCE_PLUGINHOST_VST3=1 JUCE_PLUGINHOST_VST=0 JUCE_PLUGINHOST_AU=0
    JUCE_PLUGINHOST_MULTI_BUS_MIDI=1 JUCE_ASIO=0 JUCE_WASAPI=1
    JUCE_USE_DIRECTWRITE=0
    JUCE_USE_CURL=0 JUCE_WEB_BROWSER=0 JUCE_MODAL_LOOPS_PERMITTED=1)
target_link_libraries(MidiTestHost PRIVATE juce::juce_audio_utils juce::juce_recommended_config_flags)
add_test(NAME midi_host_routing COMMAND MidiTestHost --self-test)
