cmake_minimum_required(VERSION 3.24)

project(Orion
        VERSION 0.1.0
        DESCRIPTION "Fast, subscription-free RAW photo editor"
        LANGUAGES CXX OBJCXX)

if(NOT APPLE)
    message(FATAL_ERROR
        "Orion is macOS-only for now. The engine is portable by design, but the "
        "Metal backend is not — see planning/ARCHITECTURE.md.")
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_OBJCXX_STANDARD 20)
set(CMAKE_OBJCXX_STANDARD_REQUIRED ON)

# macOS 14 floor — @Observable in the SwiftUI shell requires it (decision #26).
set(CMAKE_OSX_DEPLOYMENT_TARGET "14.0" CACHE STRING "Minimum macOS version" FORCE)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE RelWithDebInfo CACHE STRING "" FORCE)
endif()

add_compile_options(-Wall -Wextra -Wpedantic)

add_subdirectory(engine)
add_subdirectory(apps/probe)
add_subdirectory(apps/bench)
add_subdirectory(apps/rawstat)
add_subdirectory(apps/pixstat)
add_subdirectory(apps/tests)
add_subdirectory(app)
