# Skin runtime (RmlUi + Lua 5.4 + the custom panel elements), without JUCE so it builds and tests on its own.
# Dependencies are fetched at pinned commits by scripts/fetch_deps.sh into <repo>/work/deps.
# Standalone (tests):  cmake -S plugin/skin -B build/skin && cmake --build build/skin && ctest --test-dir build/skin
cmake_minimum_required(VERSION 3.22)
project(ND2Skin LANGUAGES C CXX)
if(PROJECT_IS_TOP_LEVEL)
  set(CMAKE_CXX_STANDARD 17)
  set(CMAKE_CXX_STANDARD_REQUIRED ON)
  if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
  endif()
endif()
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

set(ND2_DEPS_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../../work/deps" CACHE PATH "Fetched dependencies")
foreach(dep RmlUi Lua FreeType)
  if(NOT EXISTS "${ND2_DEPS_DIR}/${dep}")
    message(FATAL_ERROR "${dep} not found in ${ND2_DEPS_DIR}. Run scripts/fetch_deps.sh rmlui lua freetype")
  endif()
endforeach()

# ---- Lua 5.4, compiled as C++ (errors become C++ exceptions, so they unwind cleanly through RmlUi's C++ code)
set(LUA_DIR "${ND2_DEPS_DIR}/Lua")
set(LUA_CORE lapi lcode lctype ldebug ldo ldump lfunc lgc llex lmem lobject lopcodes lparser lstate lstring
             ltable ltm lundump lvm lzio lauxlib lbaselib lcorolib lmathlib lstrlib ltablib lutf8lib)
set(LUA_SOURCES "")
foreach(f ${LUA_CORE})
  list(APPEND LUA_SOURCES "${LUA_DIR}/${f}.c")
endforeach()
set_source_files_properties(${LUA_SOURCES} PROPERTIES LANGUAGE CXX)
add_library(nd2_lua STATIC ${LUA_SOURCES})
target_include_directories(nd2_lua SYSTEM PUBLIC "${LUA_DIR}")
if(UNIX)
  target_compile_definitions(nd2_lua PRIVATE LUA_USE_POSIX)
endif()
if(NOT MSVC)
  target_compile_options(nd2_lua PRIVATE -w)
endif()
add_library(Lua::Lua INTERFACE IMPORTED)
set_target_properties(Lua::Lua PROPERTIES INTERFACE_LINK_LIBRARIES nd2_lua)
set(CMAKE_DISABLE_FIND_PACKAGE_Lua TRUE)

# ---- FreeType (font rendering for RmlUi), no optional dependencies
set(FT_DISABLE_ZLIB ON CACHE BOOL "" FORCE)
set(FT_DISABLE_BZIP2 ON CACHE BOOL "" FORCE)
set(FT_DISABLE_PNG ON CACHE BOOL "" FORCE)
set(FT_DISABLE_HARFBUZZ ON CACHE BOOL "" FORCE)
set(FT_DISABLE_BROTLI ON CACHE BOOL "" FORCE)
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
set(SKIP_INSTALL_ALL ON CACHE BOOL "" FORCE)
add_subdirectory("${ND2_DEPS_DIR}/FreeType" "${CMAKE_BINARY_DIR}/deps/freetype" EXCLUDE_FROM_ALL)
target_compile_options(freetype PRIVATE -w)
add_library(Freetype::Freetype ALIAS freetype)
set(CMAKE_DISABLE_FIND_PACKAGE_Freetype TRUE)

# ---- RmlUi with its Lua plugin and debugger
set(RMLUI_LUA_BINDINGS ON CACHE BOOL "" FORCE)
set(RMLUI_LUA_BINDINGS_LIBRARY "lua_as_cxx" CACHE STRING "" FORCE)
set(RMLUI_SAMPLES OFF CACHE BOOL "" FORCE)
set(RMLUI_PRECOMPILED_HEADERS OFF CACHE BOOL "" FORCE)
set(RMLUI_INSTALL_RUNTIME_DEPENDENCIES OFF CACHE BOOL "" FORCE)
set(BUILD_TESTING OFF CACHE BOOL "" FORCE)
add_subdirectory("${ND2_DEPS_DIR}/RmlUi" "${CMAKE_BINARY_DIR}/deps/rmlui" EXCLUDE_FROM_ALL)

# ---- our skin runtime
add_library(nd2_skin STATIC
  src/skin_discovery.cpp
  src/skin_elements.cpp
  src/skin_lua.cpp
  src/skin_system.cpp
  src/skin_view.cpp)
target_include_directories(nd2_skin PUBLIC src)
target_link_libraries(nd2_skin PUBLIC rmlui_core rmlui_debugger rmlui_lua nd2_lua)

if(PROJECT_IS_TOP_LEVEL)
  enable_testing()
  add_executable(skin_test tests/skin_test.cpp)
  target_link_libraries(skin_test PRIVATE nd2_skin)
  target_compile_definitions(skin_test PRIVATE ND2_TEST_SKINS="${CMAKE_CURRENT_SOURCE_DIR}/tests/skins"
                                                ND2_DEFAULT_SKIN="${CMAKE_CURRENT_SOURCE_DIR}/../../skins/default/nd2-default")
  add_test(NAME skin COMMAND skin_test)
endif()
