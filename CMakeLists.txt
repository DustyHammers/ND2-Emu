cmake_minimum_required(VERSION 3.22)

# macOS 13+, universal (Apple Silicon + Intel). Must be set before project(). The Intel half is also what a host
# running under Rosetta loads in its own process (Logic Pro: otherwise the AU runs out of process, without its panel).
set(CMAKE_OSX_DEPLOYMENT_TARGET "13.0" CACHE STRING "Minimum macOS version")
if(APPLE AND NOT CMAKE_OSX_ARCHITECTURES)
  set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "Target architectures")
endif()

project(ND2Emu VERSION 0.3.1 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()

# Windows: built with clang-cl (the Visual Studio ClangCL toolset), which has the same CPU dispatch as macOS
# (computed goto); MSVC itself would use the slower switch.
if(WIN32)
  add_compile_definitions(_USE_MATH_DEFINES _CRT_SECURE_NO_WARNINGS)
endif()

option(ND2_BUILD_CLI     "Build the nd2emu command-line renderer" ON)
option(ND2_BUILD_TESTS   "Build unit tests" ON)
option(ND2_BUILD_PLUGIN  "Build VST3/AU/Standalone (needs JUCE)" OFF)

# ---- emulation core (no OS image or presets are part of this repository)
add_library(nd2core STATIC
  source/core/dspic/decode.cpp
  source/core/dspic/cpu.cpp
  source/core/nd2/board.cpp
  source/core/nd2/bends.cpp
  source/core/nd2/rom.cpp
  source/core/nd2/device.cpp
  source/core/nd2/engine.cpp
  source/core/nd2/banks.cpp
  source/core/nd2/bankio.cpp
  source/core/util/rate_converter.cpp
  source/core/util/midi_stream.cpp)
target_include_directories(nd2core PUBLIC source/core source/core/dspic source/core/nd2)
find_package(Threads REQUIRED)
target_link_libraries(nd2core PUBLIC Threads::Threads)
if(MSVC)
  target_compile_options(nd2core PRIVATE /W3)
  if(CMAKE_CXX_COMPILER_ID STREQUAL "Clang")   # clang-cl: the same optimisation as on macOS
    target_compile_options(nd2core PRIVATE $<$<CONFIG:Release>:/clang:-O3>)
  endif()
else()
  target_compile_options(nd2core PRIVATE -Wall -Wextra -Wno-unused-parameter -Wno-sign-compare
                         $<$<CONFIG:Release>:-O3>)
endif()

if(ND2_BUILD_CLI)
  add_executable(nd2emu source/cli/main.cpp)
  target_link_libraries(nd2emu PRIVATE nd2core)
  add_executable(nd2render source/cli/render.cpp)
  target_link_libraries(nd2render PRIVATE nd2core)
endif()

if(ND2_BUILD_TESTS)
  enable_testing()
  add_executable(nd2tests tests/test_core.cpp)
  target_link_libraries(nd2tests PRIVATE nd2core)
  add_test(NAME core COMMAND nd2tests)
  add_executable(nd2utiltests tests/test_util.cpp tests/reference/rate_converter_v1.cpp)
  target_include_directories(nd2utiltests PRIVATE tests/reference)
  target_link_libraries(nd2utiltests PRIVATE nd2core)
  if(NOT MSVC)   # the reference converter is compiled as the library is (same floating-point code generation)
    target_compile_options(nd2utiltests PRIVATE $<$<CONFIG:Release>:-O3>)
  endif()
  add_test(NAME util COMMAND nd2utiltests)
  add_executable(nd2enginetest tests/test_engine.cpp)
  target_link_libraries(nd2enginetest PRIVATE nd2core)
  add_test(NAME engine COMMAND nd2enginetest)   # needs ND2_OS, otherwise skipped
  add_executable(nd2bankstest tests/test_banks.cpp)
  target_link_libraries(nd2bankstest PRIVATE nd2core)
  add_test(NAME banks COMMAND nd2bankstest)     # format tests always; the build needs ND2_OS and ND2_BANKS
  add_executable(nd2devicetest tests/test_device.cpp)
  target_link_libraries(nd2devicetest PRIVATE nd2core)
  add_test(NAME device COMMAND nd2devicetest)   # needs ND2_OS in the environment, otherwise skipped
  add_executable(nd2bendstest tests/test_bends.cpp)
  target_link_libraries(nd2bendstest PRIVATE nd2core)
  add_test(NAME bends COMMAND nd2bendstest)     # setting checks always; the sound checks need ND2_OS (ND2_FLASH optional)
endif()

if(ND2_BUILD_PLUGIN)
  add_subdirectory(plugin)
endif()
