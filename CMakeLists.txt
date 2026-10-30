cmake_minimum_required(VERSION 3.24)

project(Orion
        VERSION 0.5.0
        DESCRIPTION "Fast, subscription-free RAW photo editor"
        LANGUAGES CXX)

# Windows and Linux build the engine on Dawn alone, its tests, and the
# Avalonia shell's native library (decision #376). Clang everywhere: the GPU
# tests and three engine files use __fp16, which MSVC's cl lacks; on Windows
# that is clang-cl, which keeps MSVC's runtime and libraries.
if(APPLE)
    enable_language(OBJCXX)
elseif(NOT CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    message(FATAL_ERROR "Windows and Linux build with Clang (clang-cl on Windows): "
                        "__fp16 is a Clang extension. See decision #376.")
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(APPLE)
    set(CMAKE_OBJCXX_STANDARD 20)
    set(CMAKE_OBJCXX_STANDARD_REQUIRED ON)

    # macOS 14 floor (decision #26; the SwiftUI app that needed it is retired, #380,
    # and the Avalonia app keeps the same floor so an update reaches the same Macs).
    set(CMAKE_OSX_DEPLOYMENT_TARGET "14.0" CACHE STRING "Minimum macOS version" FORCE)
endif()

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Linux links the static engine into the shell's shared library (#376).
if(NOT APPLE)
    set(CMAKE_POSITION_INDEPENDENT_CODE ON)
endif()

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE RelWithDebInfo CACHE STRING "" FORCE)
endif()

if(MSVC)   # clang-cl, whose -Wall is -Weverything
    # F16C: __fp16 conversions inline, rather than calls into compiler-rt that
    # MSVC's link has no library for. ponytail: needs an x64 CPU from 2012 on
    # (Ivy Bridge, Piledriver); link clang_rt.builtins instead if older matters.
    add_compile_options(/W3 -mf16c)
    add_compile_definitions(_USE_MATH_DEFINES NOMINMAX _CRT_SECURE_NO_WARNINGS)
else()
    add_compile_options(-Wall -Wextra -Wpedantic)
endif()

add_subdirectory(engine)
if(APPLE)
    add_subdirectory(apps/probe)
    add_subdirectory(apps/bench)
    add_subdirectory(apps/rawstat)
endif()
add_subdirectory(apps/tests)

# The Avalonia shell's native library on Windows and Linux (macOS builds it
# with its native/build.sh). Named by the caller, so moving the shell is a
# change to the workflow, not here.
set(ORION_SHELL_DIR "" CACHE PATH "The Avalonia shell, relative to the source tree")
if(ORION_SHELL_DIR AND NOT APPLE)
    add_subdirectory(${ORION_SHELL_DIR}/native shell-native)
endif()
