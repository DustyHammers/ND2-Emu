# ND2 plugin (VST3 + AU + Standalone). Included from the top-level CMakeLists.txt with -DND2_BUILD_PLUGIN=ON.
# JUCE, RmlUi, Lua and FreeType are fetched at pinned commits by scripts/fetch_deps.sh into <repo>/work/deps.
set(ND2_DEPS_DIR "${CMAKE_SOURCE_DIR}/work/deps" CACHE PATH "Fetched dependencies")
if(NOT EXISTS "${ND2_DEPS_DIR}/JUCE/CMakeLists.txt")
  message(FATAL_ERROR "JUCE not found in ${ND2_DEPS_DIR}. Run scripts/fetch_deps.sh")
endif()
if(APPLE)
  enable_language(OBJCXX)
endif()
add_subdirectory("${ND2_DEPS_DIR}/JUCE" "${CMAKE_BINARY_DIR}/JUCE" EXCLUDE_FROM_ALL)

# The skin runtime (no JUCE) and its OpenGL renderer: RmlUi's own GL3 backend, kept apart from JUCE's GL headers.
add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/skin" "${CMAKE_BINARY_DIR}/skin" EXCLUDE_FROM_ALL)
add_library(nd2_skin_gl STATIC
  Source/gl/RmlGLRenderer.cpp
  "${ND2_DEPS_DIR}/RmlUi/Backends/RmlUi_Renderer_GL3.cpp")
target_include_directories(nd2_skin_gl PRIVATE "${ND2_DEPS_DIR}/RmlUi/Backends")
target_compile_definitions(nd2_skin_gl PRIVATE GL_SILENCE_DEPRECATION)
set_target_properties(nd2_skin_gl PROPERTIES POSITION_INDEPENDENT_CODE ON)
target_link_libraries(nd2_skin_gl PUBLIC nd2_skin ${CMAKE_DL_LIBS})

# The default skin, compiled in (unpacked to the cache folder at run time; the skin system reads files).
file(GLOB ND2_DEFAULT_SKIN_FILES CONFIGURE_DEPENDS "${CMAKE_SOURCE_DIR}/skins/default/nd2-default/*")
juce_add_binary_data(nd2_default_skin NAMESPACE ND2DefaultSkin HEADER_NAME ND2DefaultSkin.h SOURCES ${ND2_DEFAULT_SKIN_FILES})
set_target_properties(nd2_default_skin PROPERTIES POSITION_INDEPENDENT_CODE ON)

if(APPLE)
  set(ND2_FORMATS VST3 AU Standalone)
else()
  set(ND2_FORMATS VST3 Standalone)     # other systems: compile checks and looking at skins
endif()

# Plugin identity. NEVER change any of these after the first build the owner tested: hosts use them (and the
# VST3 class ID derived from them) to find the plugin in saved projects. See CLAUDE.md.
juce_add_plugin(ND2
  COMPANY_NAME "DHammers"
  PLUGIN_MANUFACTURER_CODE DHmr
  PLUGIN_CODE Nd2E
  PRODUCT_NAME "ND2"
  PLUGIN_NAME "ND2"
  BUNDLE_ID com.dhammers.nd2
  DESCRIPTION "ND2 drum synthesizer"
  FORMATS ${ND2_FORMATS}
  IS_SYNTH TRUE
  NEEDS_MIDI_INPUT TRUE
  NEEDS_MIDI_OUTPUT TRUE
  IS_MIDI_EFFECT FALSE
  VST3_CATEGORIES Instrument Drum
  AU_MAIN_TYPE kAudioUnitType_MusicDevice
  MICROPHONE_PERMISSION_ENABLED TRUE
  MICROPHONE_PERMISSION_TEXT "ND2 uses audio inputs as trigger inputs (piezo pads)."
  COPY_PLUGIN_AFTER_BUILD FALSE)

set(ND2_PLUGIN_SOURCES
  Source/PluginProcessor.cpp
  Source/PluginEditor.cpp
  Source/RmlSkinComponent.cpp
  Source/OsFiles.cpp
  Source/Resources.cpp)
target_sources(ND2 PRIVATE ${ND2_PLUGIN_SOURCES})
target_include_directories(ND2 PRIVATE Source)
target_compile_definitions(ND2 PUBLIC
  JUCE_VST3_CAN_REPLACE_VST2=0      # part of the identity: there was never a VST2 version (fixes the VST3 class ID)
  JUCE_WEB_BROWSER=0
  JUCE_USE_CURL=0
  JUCE_DISPLAY_SPLASH_SCREEN=0)
if(APPLE)
  target_sources(ND2 PRIVATE Source/MacWindow.mm)
endif()
target_link_libraries(ND2
  PRIVATE
    nd2core
    nd2_skin_gl
    nd2_default_skin
    juce::juce_audio_processors
    juce::juce_audio_utils
    juce::juce_opengl
  PUBLIC
    juce::juce_recommended_config_flags
    juce::juce_recommended_lto_flags
    juce::juce_recommended_warning_flags)

# The plugin end to end without a plugin wrapper (processor + editor sources with JUCE): see tests/plugin_test.cpp.
add_executable(plugin_test tests/plugin_test.cpp ${ND2_PLUGIN_SOURCES})
target_include_directories(plugin_test PRIVATE Source)
target_compile_definitions(plugin_test PRIVATE
  JucePlugin_Name="ND2" JucePlugin_VersionString="0.0-test" JUCE_MODAL_LOOPS_PERMITTED=1
  JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_STANDALONE_APPLICATION=0 JUCE_DISPLAY_SPLASH_SCREEN=0)
target_link_libraries(plugin_test PRIVATE nd2core nd2_skin_gl nd2_default_skin
  juce::juce_audio_processors juce::juce_audio_utils juce::juce_opengl juce::juce_recommended_config_flags)
if(APPLE)
  target_sources(plugin_test PRIVATE Source/MacWindow.mm)
endif()
