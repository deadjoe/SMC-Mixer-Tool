cmake_minimum_required(VERSION 3.22)

set(CMAKE_OSX_DEPLOYMENT_TARGET "12.0" CACHE STRING "Minimum macOS version" FORCE)
project(SMCMixerToolGUI VERSION 1.0.0 LANGUAGES C CXX OBJCXX)

find_package(Git QUIET)
set(SMC_BUILD_INFO "${CMAKE_BINARY_DIR}/generated/BuildInfo.h")
add_custom_target(SMCBuildInfo
    COMMAND ${CMAKE_COMMAND}
        -DSOURCE_DIR=${CMAKE_SOURCE_DIR}
        -DOUTPUT=${SMC_BUILD_INFO}
        -DGIT_EXECUTABLE=${GIT_EXECUTABLE}
        -P ${CMAKE_SOURCE_DIR}/cmake/WriteBuildInfo.cmake
    BYPRODUCTS ${SMC_BUILD_INFO}
    VERBATIM
)
execute_process(
    COMMAND ${CMAKE_COMMAND}
        -DSOURCE_DIR=${CMAKE_SOURCE_DIR}
        -DOUTPUT=${SMC_BUILD_INFO}
        -DGIT_EXECUTABLE=${GIT_EXECUTABLE}
        -P ${CMAKE_SOURCE_DIR}/cmake/WriteBuildInfo.cmake
)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

include(FetchContent)
FetchContent_Declare(
    JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG 9.0.4
    GIT_SHALLOW TRUE
    GIT_PROGRESS TRUE
)
FetchContent_MakeAvailable(JUCE)

juce_add_gui_app(SMCMixerGUI
    PRODUCT_NAME "SMC Mixer Tool"
    BUNDLE_ID "local.smc-mixer.gui"
    BLUETOOTH_PERMISSION_ENABLED TRUE
    BLUETOOTH_PERMISSION_TEXT "Connect to SMC-Mixer to read firmware, battery, and MIDI device information."
    NEEDS_CURL FALSE
    NEEDS_WEB_BROWSER FALSE
)

juce_generate_juce_header(SMCMixerGUI)

target_sources(SMCMixerGUI PRIVATE
    Source/Main.cpp
    Source/MainComponent.h
    Source/MainComponent.cpp
    Source/DeviceModel.h
    Source/DeviceController.h
    Source/DeviceController.cpp
    Source/VendorProtocol.h
    Source/VendorProtocol.cpp
    Source/SettingsProtocol.h
    Source/SettingsProtocol.cpp
    Source/CoreMidiTransport.h
    Source/CoreMidiTransport.cpp
    Source/BleTransport.h
    Source/BleTransport.mm
    Source/ControlConfiguration.h
    Source/ControlConfiguration.cpp
    Source/SettingsDocument.h
    Source/SettingsDocument.cpp
    Source/UI/ControlInspectorComponent.h
    Source/UI/ControlInspectorComponent.cpp
    Source/UI/MixerLookAndFeel.h
    Source/UI/MixerLookAndFeel.cpp
    Source/UI/DeviceDiagramComponent.h
    Source/UI/DeviceDiagramComponent.cpp
    Source/UI/DeviceOverviewComponent.h
    Source/UI/DeviceOverviewComponent.cpp
    Source/UI/DiagnosticsComponent.h
    Source/UI/DiagnosticsComponent.cpp
)

set_source_files_properties(Source/BleTransport.mm PROPERTIES COMPILE_OPTIONS "-fobjc-arc")

target_compile_definitions(SMCMixerGUI PRIVATE
    DONT_SET_USING_JUCE_NAMESPACE=1
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_APPLICATION_NAME_STRING="$<TARGET_PROPERTY:SMCMixerGUI,JUCE_PRODUCT_NAME>"
    JUCE_APPLICATION_VERSION_STRING="$<TARGET_PROPERTY:SMCMixerGUI,JUCE_VERSION>"
)

find_library(CORE_MIDI_FRAMEWORK CoreMIDI REQUIRED)
find_library(CORE_BLUETOOTH_FRAMEWORK CoreBluetooth REQUIRED)
find_library(FOUNDATION_FRAMEWORK Foundation REQUIRED)

target_link_libraries(SMCMixerGUI PRIVATE
    juce::juce_gui_extra
    juce::juce_recommended_warning_flags
    ${CORE_MIDI_FRAMEWORK}
    ${CORE_BLUETOOTH_FRAMEWORK}
    ${FOUNDATION_FRAMEWORK}
)

target_include_directories(SMCMixerGUI PRIVATE "${CMAKE_BINARY_DIR}/generated")
add_dependencies(SMCMixerGUI SMCBuildInfo)

add_executable(SMCMixerCoreTests
    tests/core_tests.cpp
    Source/VendorProtocol.cpp
    Source/SettingsProtocol.cpp
    Source/ControlConfiguration.cpp
    Source/SettingsDocument.cpp
)
target_include_directories(SMCMixerCoreTests PRIVATE Source)
target_compile_features(SMCMixerCoreTests PRIVATE cxx_std_17)
target_compile_options(SMCMixerCoreTests PRIVATE -Wall -Wextra -Wpedantic)
