cmake_minimum_required(VERSION 3.22)

project(Spectrumming VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(SPECTRUMMING_BUILD_PLUGIN "Build the JUCE plug-in targets" ON)
option(SPECTRUMMING_BUILD_TESTS "Build tests" ON)
set(SPECTRUMMING_ARTIFACT_DIR "${CMAKE_CURRENT_SOURCE_DIR}/artifacts" CACHE PATH
    "Directory for final VST3, AU, and Standalone products")
set(EHL_JUCE_SOURCE_DIR "" CACHE PATH "Existing JUCE source checkout; avoids network when set")
set(EHL_JUCE_DESIGN_MODULE_SOURCE_DIR "" CACHE PATH
    "Existing juce-ehl-design-module checkout; uses modules/juce-ehl-design-module when unset")

add_library(spectrumming_core STATIC Source/core/SpectrummingCore.cpp)
target_include_directories(spectrumming_core PUBLIC Source)
target_compile_features(spectrumming_core PUBLIC cxx_std_17)

if(MSVC)
    target_compile_options(spectrumming_core PRIVATE /W4)
else()
    target_compile_options(spectrumming_core PRIVATE -Wall -Wextra -Wpedantic)
endif()

if(SPECTRUMMING_BUILD_TESTS)
    enable_testing()

    find_package(Threads REQUIRED)
    add_executable(SpectrummingCoreTests Tests/CoreTests.cpp)
    target_link_libraries(SpectrummingCoreTests PRIVATE spectrumming_core Threads::Threads)
    add_test(NAME SpectrummingCoreTests COMMAND SpectrummingCoreTests)
endif()

if(SPECTRUMMING_BUILD_PLUGIN)
    include(FetchContent)

    if(EHL_JUCE_SOURCE_DIR)
        if(NOT EXISTS "${EHL_JUCE_SOURCE_DIR}/CMakeLists.txt")
            message(FATAL_ERROR "EHL_JUCE_SOURCE_DIR does not contain JUCE: ${EHL_JUCE_SOURCE_DIR}")
        endif()
        add_subdirectory("${EHL_JUCE_SOURCE_DIR}" "${CMAKE_CURRENT_BINARY_DIR}/juce")
    elseif(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/JUCE/CMakeLists.txt")
        add_subdirectory(JUCE)
    else()
        FetchContent_Declare(
            JUCE
            GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
            GIT_TAG 8.0.15
            GIT_SHALLOW TRUE)
        FetchContent_MakeAvailable(JUCE)
    endif()

    set(EHL_JUCE_DESIGN_BUILD_TESTS OFF CACHE BOOL "Build EHL JUCE design module tests" FORCE)
    if(EHL_JUCE_DESIGN_MODULE_SOURCE_DIR)
        if(NOT EXISTS "${EHL_JUCE_DESIGN_MODULE_SOURCE_DIR}/CMakeLists.txt")
            message(FATAL_ERROR
                "EHL_JUCE_DESIGN_MODULE_SOURCE_DIR does not contain juce-ehl-design-module: ${EHL_JUCE_DESIGN_MODULE_SOURCE_DIR}")
        endif()
        add_subdirectory("${EHL_JUCE_DESIGN_MODULE_SOURCE_DIR}" "${CMAKE_CURRENT_BINARY_DIR}/juce-ehl-design-module")
    elseif(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/modules/juce-ehl-design-module/CMakeLists.txt")
        add_subdirectory(modules/juce-ehl-design-module)
    elseif(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../juce-ehl-design-module/CMakeLists.txt")
        add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/../juce-ehl-design-module" "${CMAKE_CURRENT_BINARY_DIR}/juce-ehl-design-module")
    else()
        message(FATAL_ERROR
            "juce-ehl-design-module not found. Set EHL_JUCE_DESIGN_MODULE_SOURCE_DIR or add modules/juce-ehl-design-module.")
    endif()

    set(SPECTRUMMING_FORMATS VST3 Standalone)
    if(APPLE)
        list(APPEND SPECTRUMMING_FORMATS AU)
    endif()

    set(EHL_COPY_PLUGIN_AFTER_BUILD_DEFAULT OFF)
    if(APPLE AND NOT DEFINED ENV{CI})
        set(EHL_COPY_PLUGIN_AFTER_BUILD_DEFAULT ON)
    endif()
    option(EHL_COPY_PLUGIN_AFTER_BUILD
        "Copy built plug-in formats to the current user's standard plug-in folders"
        ${EHL_COPY_PLUGIN_AFTER_BUILD_DEFAULT})

    set(EHL_PLUGIN_COPY_ARGS COPY_PLUGIN_AFTER_BUILD ${EHL_COPY_PLUGIN_AFTER_BUILD})
    if(APPLE)
        list(APPEND EHL_PLUGIN_COPY_ARGS
            VST3_COPY_DIR "$ENV{HOME}/Library/Audio/Plug-Ins/VST3"
            AU_COPY_DIR "$ENV{HOME}/Library/Audio/Plug-Ins/Components")
    endif()

    file(GLOB_RECURSE spectrumming_plugin_module_sources CONFIGURE_DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/Source/plugin/*.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/Source/plugin/*.h")

    set(spectrumming_required_plugin_sources
        Source/ParameterIDs.h
        Source/PluginEditor.cpp
        Source/PluginEditor.h
        Source/PluginProcessor.cpp
        Source/PluginProcessor.h)
    set(spectrumming_plugin_sources
        ${spectrumming_required_plugin_sources}
        ${spectrumming_plugin_module_sources})

    juce_add_plugin(Spectrumming
            VERSION ${PROJECT_VERSION}
            COMPANY_NAME "EsionHsrahLatigid"
            BUNDLE_ID "jp.ehl.spectrumming"
            PRODUCT_NAME "Spectrumming"
            DESCRIPTION "MIDI-driven image-to-spectrum instrument"
            PLUGIN_MANUFACTURER_CODE EHL_
            PLUGIN_CODE Spct
            FORMATS ${SPECTRUMMING_FORMATS}
            IS_SYNTH TRUE
            NEEDS_MIDI_INPUT TRUE
            NEEDS_MIDI_OUTPUT FALSE
            IS_MIDI_EFFECT FALSE
            AU_SANDBOX_SAFE FALSE
            SUPPRESS_AU_PLIST_RESOURCE_USAGE TRUE
            EDITOR_WANTS_KEYBOARD_FOCUS TRUE
            MICROPHONE_PERMISSION_ENABLED FALSE
            ${EHL_PLUGIN_COPY_ARGS})

        target_sources(Spectrumming PRIVATE
            ${spectrumming_plugin_sources}
            Source/bridge/BridgeDefaults.h
            Source/bridge/BridgeFrame.h
            Source/bridge/BridgeInterfaces.h
            Source/bridge/SharedFrameFile.cpp
            Source/bridge/SharedFrameFile.h)

        target_compile_definitions(Spectrumming PUBLIC
            JUCE_REPORT_APP_USAGE=0
            JUCE_USE_CURL=0
            JUCE_VST3_CAN_REPLACE_VST2=0
            JUCE_WEB_BROWSER=0)

        target_link_libraries(Spectrumming
            PRIVATE
                spectrumming_core
                EHL::JuceDesign
                juce::juce_audio_utils
            PUBLIC
                juce::juce_recommended_config_flags
                juce::juce_recommended_lto_flags
                juce::juce_recommended_warning_flags)

        set(spectrumming_stage_platform "linux-x64")
        if(APPLE)
            set(spectrumming_stage_platform "macos-arm64")
        elseif(WIN32)
            set(spectrumming_stage_platform "windows-x64")
        endif()
        set(spectrumming_juce_products "${CMAKE_CURRENT_BINARY_DIR}/Spectrumming_artefacts/$<CONFIG>")
        set(spectrumming_staged_products "${SPECTRUMMING_ARTIFACT_DIR}/plugin-release/${spectrumming_stage_platform}")
        set(spectrumming_artifact_dependencies Spectrumming_VST3 Spectrumming_Standalone)
        set(spectrumming_artifact_commands
            COMMAND ${CMAKE_COMMAND} -E make_directory "${spectrumming_staged_products}/vst3"
            COMMAND ${CMAKE_COMMAND} -E make_directory "${spectrumming_staged_products}/standalone"
            COMMAND ${CMAKE_COMMAND} -E make_directory "${spectrumming_staged_products}/au")

        if(APPLE)
            list(APPEND spectrumming_artifact_commands
                COMMAND ${CMAKE_COMMAND} -E rm -rf "${spectrumming_staged_products}/vst3/spectrumming_vst3_plugin.vst3"
                COMMAND ${CMAKE_COMMAND} -E rm -rf "${spectrumming_staged_products}/standalone/spectrumming_standalone_plugin.app"
                COMMAND ${CMAKE_COMMAND} -E rm -rf "${spectrumming_staged_products}/au/spectrumming_au_plugin.component"
                COMMAND ${CMAKE_COMMAND} -E copy_directory
                        "${spectrumming_juce_products}/VST3/Spectrumming.vst3" "${spectrumming_staged_products}/vst3/spectrumming_vst3_plugin.vst3"
                COMMAND ${CMAKE_COMMAND} -E copy_directory
                        "${spectrumming_juce_products}/Standalone/Spectrumming.app" "${spectrumming_staged_products}/standalone/spectrumming_standalone_plugin.app")
        elseif(WIN32)
            list(APPEND spectrumming_artifact_commands
                COMMAND ${CMAKE_COMMAND} -E rm -rf "${spectrumming_staged_products}/vst3/spectrumming_vst3_plugin.vst3"
                COMMAND ${CMAKE_COMMAND} -E copy_directory
                        "${spectrumming_juce_products}/VST3/Spectrumming.vst3" "${spectrumming_staged_products}/vst3/spectrumming_vst3_plugin.vst3"
                COMMAND ${CMAKE_COMMAND} -E copy
                        "${spectrumming_juce_products}/Standalone/Spectrumming.exe" "${spectrumming_staged_products}/standalone/spectrumming_standalone_plugin.exe")
        else()
            list(APPEND spectrumming_artifact_commands
                COMMAND ${CMAKE_COMMAND} -E rm -rf "${spectrumming_staged_products}/vst3/spectrumming_vst3_plugin.vst3"
                COMMAND ${CMAKE_COMMAND} -E copy_directory
                        "${spectrumming_juce_products}/VST3/Spectrumming.vst3" "${spectrumming_staged_products}/vst3/spectrumming_vst3_plugin.vst3"
                COMMAND ${CMAKE_COMMAND} -E copy
                        "${spectrumming_juce_products}/Standalone/Spectrumming" "${spectrumming_staged_products}/standalone/spectrumming_standalone_plugin")
        endif()

        if(TARGET Spectrumming_AU)
            list(APPEND spectrumming_artifact_dependencies Spectrumming_AU)
            list(APPEND spectrumming_artifact_commands
                COMMAND ${CMAKE_COMMAND} -E copy_directory
                        "${spectrumming_juce_products}/AU/Spectrumming.component" "${spectrumming_staged_products}/au/spectrumming_au_plugin.component")
        endif()

        if(APPLE)
            list(APPEND spectrumming_artifact_commands
                COMMAND codesign --force --deep --sign -
                        "${spectrumming_staged_products}/vst3/spectrumming_vst3_plugin.vst3"
                COMMAND codesign --force --deep --sign -
                        "${spectrumming_staged_products}/standalone/spectrumming_standalone_plugin.app"
                COMMAND codesign --force --deep --sign -
                        "${spectrumming_staged_products}/au/spectrumming_au_plugin.component")
        endif()

        list(APPEND spectrumming_artifact_commands
            COMMAND ${CMAKE_COMMAND}
                -DPRODUCT_NAME=Spectrumming
                -DVERSION=${PROJECT_VERSION}
                -DSLUG=spectrumming
                -DBUNDLE_ID=jp.ehl.spectrumming
                -DPLUGIN_CODE=Spct
                -DSTAGE_DIR=${spectrumming_staged_products}
                -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/WriteArtifactsManifest.cmake")

        add_custom_target(ehl_stage_products
            ${spectrumming_artifact_commands}
            DEPENDS ${spectrumming_artifact_dependencies}
            COMMENT "Staging Spectrumming products under ${SPECTRUMMING_ARTIFACT_DIR}/plugin-release")

        add_custom_target(Spectrumming_Artifacts DEPENDS ehl_stage_products)

        if(SPECTRUMMING_BUILD_TESTS)
            add_executable(SpectrummingIntegrationTests Tests/PluginIntegrationTest.cpp)
            target_include_directories(SpectrummingIntegrationTests PRIVATE Source)
            target_link_libraries(SpectrummingIntegrationTests PRIVATE Spectrumming juce::juce_audio_utils)
            add_test(NAME SpectrummingPluginIntegrationTests COMMAND SpectrummingIntegrationTests)

            juce_add_console_app(SpectrummingPluginLoadTest
                PRODUCT_NAME "Spectrumming Plugin Load Test")
            target_sources(SpectrummingPluginLoadTest PRIVATE Tests/PluginLoadTest.cpp)
            target_include_directories(SpectrummingPluginLoadTest PRIVATE Source)
            target_link_libraries(SpectrummingPluginLoadTest PRIVATE
                juce::juce_audio_processors_headless
                juce::juce_recommended_config_flags
                juce::juce_recommended_warning_flags)
            target_compile_definitions(SpectrummingPluginLoadTest PRIVATE
                JUCE_PLUGINHOST_VST3=1
                JUCE_USE_CURL=0
                JUCE_WEB_BROWSER=0)
            add_dependencies(SpectrummingPluginLoadTest Spectrumming_VST3)
            add_test(NAME SpectrummingHostVST3
                COMMAND SpectrummingPluginLoadTest
                        "${spectrumming_juce_products}/VST3/Spectrumming.vst3"
                        "Spectrumming")

            add_executable(SpectrummingBridgeProtocolTests Tests/BridgeProtocolTests.cpp)
            target_include_directories(SpectrummingBridgeProtocolTests PRIVATE Source)
            target_compile_features(SpectrummingBridgeProtocolTests PRIVATE cxx_std_17)
            add_test(NAME SpectrummingBridgeProtocolTests COMMAND SpectrummingBridgeProtocolTests)

            add_executable(SpectrummingSharedFrameFileTests
                Tests/SharedFrameFileTests.cpp
                Source/bridge/SharedFrameFile.cpp)
            target_include_directories(SpectrummingSharedFrameFileTests PRIVATE Source)
            target_link_libraries(SpectrummingSharedFrameFileTests PRIVATE
                juce::juce_core
                juce::juce_recommended_config_flags
                juce::juce_recommended_warning_flags)
            add_test(NAME SpectrummingSharedFrameFileTests COMMAND SpectrummingSharedFrameFileTests)

            add_test(
                NAME SpectrummingArtifactChecks
                COMMAND ${CMAKE_COMMAND}
                        -DSTAGE_DIR=${spectrumming_staged_products}
                        -DSLUG=spectrumming
                        -DEXPECT_PRODUCT=Spectrumming
                        -DEXPECT_VERSION=${PROJECT_VERSION}
                        -DEXPECT_BUNDLE_ID=jp.ehl.spectrumming
                        -DEXPECT_PLUGIN_CODE=Spct
                        -DEXPECT_AU=$<BOOL:${APPLE}>
                        -DEXPECT_AU_SANDBOX_SAFE=FALSE
                        -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/VerifyArtifacts.cmake)
        endif()
    if(APPLE OR WIN32)
        juce_add_gui_app(SpectrummingBridge
            COMPANY_NAME "EsionHsrahLatigid"
            BUNDLE_ID "jp.ehl.spectrumming.bridge"
            VERSION ${PROJECT_VERSION}
            CAMERA_PERMISSION_ENABLED TRUE
            CAMERA_PERMISSION_TEXT "Spectrumming Bridge uses the selected camera as a visual spectrum source."
            PRODUCT_NAME "Spectrumming Bridge")
        target_sources(SpectrummingBridge PRIVATE
            Source/bridge/BridgeDefaults.h
            Source/bridge/BridgeFrame.h
            Source/bridge/BridgeInterfaces.h
            Source/bridge/BridgeMain.cpp
            Source/bridge/JuceCameraFrameSource.h
            Source/bridge/SharedFrameFile.cpp
            Source/bridge/SharedFrameFile.h)
        target_include_directories(SpectrummingBridge PRIVATE Source)
        target_compile_definitions(SpectrummingBridge PRIVATE
            JUCE_REPORT_APP_USAGE=0
            JUCE_USE_CURL=0
            JUCE_WEB_BROWSER=0
            JUCE_USE_CAMERA=1
            SPECTRUMMING_BRIDGE_ENABLE_JUCE_CAMERA=1)
        target_link_libraries(SpectrummingBridge PRIVATE
            EHL::JuceDesign
            juce::juce_audio_utils
            juce::juce_video
            juce::juce_recommended_config_flags
            juce::juce_recommended_warning_flags)
        target_link_libraries(SpectrummingBridge PRIVATE spectrumming_core)

        if(DEFINED spectrumming_staged_products)
            if(APPLE)
                add_custom_command(TARGET SpectrummingBridge POST_BUILD
                    COMMAND ${CMAKE_COMMAND} -E make_directory "${spectrumming_staged_products}/bridge"
                    COMMAND ${CMAKE_COMMAND} -E rm -rf "${spectrumming_staged_products}/bridge/Spectrumming Bridge.app"
                    COMMAND ${CMAKE_COMMAND} -E copy_directory
                        "$<TARGET_BUNDLE_DIR:SpectrummingBridge>"
                        "${spectrumming_staged_products}/bridge/Spectrumming Bridge.app")
            elseif(WIN32)
                add_custom_command(TARGET SpectrummingBridge POST_BUILD
                    COMMAND ${CMAKE_COMMAND} -E make_directory "${spectrumming_staged_products}/bridge"
                    COMMAND ${CMAKE_COMMAND} -E copy_if_different "$<TARGET_FILE:SpectrummingBridge>"
                        "${spectrumming_staged_products}/bridge/Spectrumming Bridge.exe")
            endif()
        endif()
        if(TARGET ehl_stage_products)
            add_dependencies(ehl_stage_products SpectrummingBridge)
        endif()
    else()
        add_custom_target(SpectrummingBridge
            COMMAND ${CMAKE_COMMAND} -E echo "SpectrummingBridge is supported on macOS and Windows")
        message(STATUS "SpectrummingBridge is not built on this platform")
    endif()
endif()
