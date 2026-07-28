add_subdirectory(shaders)
set(ORION_SHADER_DIR ${ORION_SHADER_DIR} PARENT_SCOPE)

add_library(orion_engine STATIC
    src/CApi.cpp
    src/Engine.cpp
    src/gpu/MetalDevice.mm
    src/raw/RawImage.cpp
    src/gpu/Resources.mm
    src/pipe/Pipeline.cpp
    src/pipe/DevelopPipeline.cpp
    src/pipe/ToneCurve.cpp
    src/pipe/WhiteBalance.cpp
    src/util/ImageWriter.mm
)

add_library(Orion::Engine ALIAS orion_engine)

target_include_directories(orion_engine
    PUBLIC  ${CMAKE_CURRENT_SOURCE_DIR}/include
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_compile_definitions(orion_engine
    PRIVATE ORION_VERSION_STRING="${PROJECT_VERSION}"
            ORION_SHADER_DIR="${ORION_SHADER_DIR}"
)

add_dependencies(orion_engine orion_shaders)

# ARC for the Objective-C++ translation units. The C++ ones never see ObjC.
set_source_files_properties(
    src/gpu/MetalDevice.mm
    src/gpu/Resources.mm
    src/util/ImageWriter.mm
    PROPERTIES COMPILE_OPTIONS "-fobjc-arc"
)

find_library(METAL_FRAMEWORK      Metal      REQUIRED)
find_library(FOUNDATION_FRAMEWORK Foundation REQUIRED)
find_library(COREGRAPHICS_FRAMEWORK CoreGraphics REQUIRED)
find_library(IMAGEIO_FRAMEWORK    ImageIO    REQUIRED)

# LibRaw. Homebrew ships no pkg-config binary by default, so locate it directly.
# libraw_r is the thread-safe build — we decode off the UI thread.
find_path(LIBRAW_INCLUDE_DIR
    NAMES libraw/libraw.h
    HINTS /opt/homebrew/opt/libraw/include /usr/local/opt/libraw/include
    REQUIRED
)
find_library(LIBRAW_LIBRARY
    NAMES raw_r raw
    HINTS /opt/homebrew/opt/libraw/lib /usr/local/opt/libraw/lib
    REQUIRED
)
message(STATUS "LibRaw: ${LIBRAW_LIBRARY}")
set(LIBRAW_LIBRARY ${LIBRAW_LIBRARY} PARENT_SCOPE)

target_include_directories(orion_engine PRIVATE ${LIBRAW_INCLUDE_DIR})

target_link_libraries(orion_engine
    PRIVATE ${METAL_FRAMEWORK} ${FOUNDATION_FRAMEWORK}
            ${COREGRAPHICS_FRAMEWORK} ${IMAGEIO_FRAMEWORK}
            ${LIBRAW_LIBRARY}
)
