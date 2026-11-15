cmake_minimum_required(VERSION 3.22)

# --- macOS : binaire universel (Apple Silicon + Intel), macOS 11 minimum ----------
set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "Version minimale de macOS")
if(APPLE AND NOT CMAKE_OSX_ARCHITECTURES)
  set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "Architectures macOS" FORCE)
endif()

project(LMenTS VERSION 0.3.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()

option(LMENTS_BUILD_PLUGIN "Compiler le plugin (VST3, Standalone) avec JUCE" ON)
option(LMENTS_BUILD_TOOLS "Compiler les outils de test (tools/)" ON)

# --- La puce virtuelle et la carte du module (Source/emu) ----------------------------------
add_library(lments_emu STATIC
  Source/emu/CortexM4.cpp
  Source/emu/ElementsMachine.cpp
  Source/emu/FirmwareFile.cpp)
target_include_directories(lments_emu PUBLIC Source/emu Source/hw)
set_target_properties(lments_emu PROPERTIES POSITION_INDEPENDENT_CODE ON)
# Le calcul flottant doit rester exactement celui qui est écrit (pas de fusion
# multiplication-addition décidée par le compilateur).
target_compile_options(lments_emu PRIVATE -O3 -ffp-contract=off)

# --- Outils de test sans JUCE (voir tools/) --------------------------------------------------
if(LMENTS_BUILD_TOOLS)
  add_executable(lments_emu_run tools/emu_run.cpp)
  target_link_libraries(lments_emu_run PRIVATE lments_emu)
  add_executable(lments_machine_test tools/machine_test.cpp)
  target_link_libraries(lments_machine_test PRIVATE lments_emu)
endif()

# --- Plugin ---------------------------------------------------------------------------------
if(LMENTS_BUILD_PLUGIN)
  # Si un dossier JUCE/ existe à côté de ce fichier, il est utilisé ; sinon JUCE est
  # téléchargé automatiquement au premier « cmake » (connexion internet nécessaire).
  if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/JUCE/CMakeLists.txt")
    add_subdirectory(JUCE)
  else()
    include(FetchContent)
    FetchContent_Declare(JUCE
      GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
      GIT_TAG 8.0.15
      GIT_SHALLOW TRUE)
    FetchContent_MakeAvailable(JUCE)
  endif()

  juce_add_plugin(LMenTS
    PRODUCT_NAME "LMenTS"
    COMPANY_NAME "Labtechno"
    BUNDLE_ID "com.labtechno.lments"
    PLUGIN_MANUFACTURER_CODE Labt
    PLUGIN_CODE Lmnt
    FORMATS VST3 Standalone
    IS_SYNTH TRUE
    NEEDS_MIDI_INPUT TRUE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD FALSE
    VST3_CATEGORIES Instrument Synth
    MICROPHONE_PERMISSION_ENABLED TRUE
    MICROPHONE_PERMISSION_TEXT "LMenTS peut faire entrer du son dans les prises EXT IN du module.")

  target_sources(LMenTS PRIVATE
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp)
  target_include_directories(LMenTS PRIVATE Source Source/hw Source/engine)
  target_compile_definitions(LMenTS PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0)
  target_link_libraries(LMenTS
    PRIVATE
      lments_emu
      juce::juce_audio_utils
    PUBLIC
      juce::juce_recommended_config_flags
      juce::juce_recommended_warning_flags)

  # Essai du plugin sans logiciel hôte : charge un firmware, joue, sauve et relit l'état.
  if(LMENTS_BUILD_TOOLS)
    juce_add_console_app(lments_plugin_test)
    target_sources(lments_plugin_test PRIVATE tools/plugin_test.cpp)
    target_include_directories(lments_plugin_test PRIVATE Source Source/hw Source/engine)
    target_compile_definitions(lments_plugin_test PRIVATE JucePlugin_Name="LMenTS" JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)
    target_link_libraries(lments_plugin_test PRIVATE LMenTS lments_emu juce::juce_audio_utils)
  endif()
endif()
