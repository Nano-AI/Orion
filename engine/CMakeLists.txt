# Dawn (#317, #375): the second GPU backend, on whenever tools/build-dawn.sh's
# output is present. Metal stays the default at runtime on macOS (ORION_GPU=dawn
# picks Dawn), and an engine built without it is the Metal engine exactly. On
# Windows and Linux Dawn is the only backend, so it is required (#376).
set(ORION_DAWN_DIR ${CMAKE_SOURCE_DIR}/third_party/dawn-build)
if(WIN32)
    set(ORION_DAWN_LIB ${ORION_DAWN_DIR}/lib/webgpu_dawn.lib)
else()
    set(ORION_DAWN_LIB ${ORION_DAWN_DIR}/lib/libwebgpu_dawn.a)
endif()
if(EXISTS ${ORION_DAWN_LIB})
    option(ORION_DAWN "Build the Dawn GPU backend" ON)
else()
    option(ORION_DAWN "Build the Dawn GPU backend" OFF)
endif()
message(STATUS "Dawn backend: ${ORION_DAWN}")
if(NOT APPLE AND NOT ORION_DAWN)
    message(FATAL_ERROR "Off macOS the engine renders only through Dawn: run tools/build-dawn.sh first.")
endif()

add_subdirectory(shaders)
set(ORION_SHADER_DIR ${ORION_SHADER_DIR} PARENT_SCOPE)

add_library(orion_engine STATIC
    src/CApi.cpp
    src/CApiExport.cpp
    src/CApiDisplay.cpp
    src/Engine.cpp
    src/ResourcePaths.cpp
    src/gpu/Gpu.cpp
    src/gpu/Readback.cpp
    src/raw/RawImage.cpp
    src/raw/NoiseProfile.cpp
    src/gpu/TexturePool.cpp
    src/pipe/Pipeline.cpp
    src/pipe/DevelopPipeline.cpp
    src/pipe/DevelopCapture.cpp
    src/pipe/DevelopLocal.cpp
    src/pipe/DevelopDenoiseLocal.cpp
    src/pipe/DevelopMask.cpp
    src/pipe/DevelopOutput.cpp
    src/pipe/DisplayCopy.cpp
    src/pipe/CubeLut.cpp
    src/pipe/LensDatabase.cpp
    src/pipe/LensOpcodes.cpp
    src/pipe/ToneCurve.cpp
    src/pipe/WhiteBalance.cpp
    src/util/ExportOps.cpp
    src/util/DngWriter.cpp
    src/util/JxlWriter.cpp
    src/merge/Merge.cpp
    src/merge/MergeRender.cpp
    src/merge/Align.cpp
    src/merge/HdrMerge.cpp
)

add_library(Orion::Engine ALIAS orion_engine)

# The macOS-only files and what replaces each elsewhere (decision #376's table):
# Metal is the default backend there; export is ImageIO there and OpenCV's
# imgcodecs plus libjxl elsewhere (ImageWriterPortable.cpp).
if(APPLE)
    target_sources(orion_engine PRIVATE
        src/gpu/MetalDevice.mm
        src/gpu/MetalResources.mm
        src/util/ImageWriter.mm
        src/util/JxlMetadata.mm
    )
else()
    target_sources(orion_engine PRIVATE src/util/ImageWriterPortable.cpp)
endif()

target_include_directories(orion_engine
    PUBLIC  ${CMAKE_CURRENT_SOURCE_DIR}/include
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src
)

# Relative to the build directory, never absolute: ResourcePaths finds the build
# directory at runtime. An absolute path here is the builder's home directory,
# compiled into every binary that links the engine, the shipped app included (#266).
file(RELATIVE_PATH ORION_SHADER_DIR_REL ${CMAKE_BINARY_DIR} ${ORION_SHADER_DIR})
file(RELATIVE_PATH ORION_DATA_DIR_REL ${CMAKE_BINARY_DIR} ${CMAKE_SOURCE_DIR}/data)
target_compile_definitions(orion_engine
    PRIVATE ORION_VERSION_STRING="${PROJECT_VERSION}"
            ORION_SHADER_DIR_REL="${ORION_SHADER_DIR_REL}"
            ORION_DATA_DIR_REL="${ORION_DATA_DIR_REL}"
)

add_dependencies(orion_engine orion_shaders)

if(APPLE)
    # ARC for the Objective-C++ translation units. The C++ ones never see ObjC.
    set_source_files_properties(
        src/gpu/MetalDevice.mm
        src/gpu/MetalResources.mm
        src/gpu/TexturePool.cpp
        src/util/ImageWriter.mm
        src/util/JxlMetadata.mm
        PROPERTIES COMPILE_OPTIONS "-fobjc-arc"
    )

    find_library(METAL_FRAMEWORK      Metal      REQUIRED)
    find_library(FOUNDATION_FRAMEWORK Foundation REQUIRED)
    find_library(COREGRAPHICS_FRAMEWORK CoreGraphics REQUIRED)
    find_library(IMAGEIO_FRAMEWORK    ImageIO    REQUIRED)
    set(ORION_PLATFORM_LIBRARIES ${METAL_FRAMEWORK} ${FOUNDATION_FRAMEWORK}
        ${COREGRAPHICS_FRAMEWORK} ${IMAGEIO_FRAMEWORK})
endif()

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

# OpenCV — for HDR-merge alignment, and off macOS for export too
# (ImageWriterPortable.cpp, #376). Those are the only translation units that
# include it, and the include path is scoped to that
# file so the dependency cannot leak into a header by accident. OpenCV 5
# renamed features2d to features and split calib3d: findHomography is in
# geometry, ECC refinement in video. Apache-2.0; listed in NOTICE.
if(APPLE)
    find_path(OPENCV_INCLUDE_DIR
        NAMES opencv2/core.hpp
        HINTS /opt/homebrew/opt/opencv/include/opencv5
              /opt/homebrew/opt/opencv/include/opencv4
              /usr/local/opt/opencv/include/opencv5
              /usr/local/opt/opencv/include/opencv4
        REQUIRED
    )
    set(OPENCV_LIBRARIES "")
    foreach(mod core imgproc features geometry video)
        find_library(OPENCV_${mod}_LIBRARY
            NAMES opencv_${mod}
            HINTS /opt/homebrew/opt/opencv/lib /usr/local/opt/opencv/lib
            REQUIRED
        )
        list(APPEND OPENCV_LIBRARIES ${OPENCV_${mod}_LIBRARY})
    endforeach()
    message(STATUS "OpenCV: ${OPENCV_core_LIBRARY}")
    set(OPENCV_LIBRARIES ${OPENCV_LIBRARIES} PARENT_SCOPE)
else()
    # Windows and Linux take OpenCV 4 from vcpkg: features2d and calib3d
    # under their old names, and imgcodecs for ImageWriterPortable.cpp (#376).
    find_package(OpenCV REQUIRED GLOBAL COMPONENTS core imgproc features2d calib3d video imgcodecs)
    set(OPENCV_INCLUDE_DIR ${OpenCV_INCLUDE_DIRS})
    set(OPENCV_LIBRARIES ${OpenCV_LIBS})
    message(STATUS "OpenCV: ${OpenCV_VERSION}")
    set(OPENCV_LIBRARIES ${OPENCV_LIBRARIES} PARENT_SCOPE)
    set_source_files_properties(src/util/ImageWriterPortable.cpp
        PROPERTIES INCLUDE_DIRECTORIES "${OPENCV_INCLUDE_DIR}")
endif()
set_source_files_properties(src/merge/Align.cpp
    PROPERTIES INCLUDE_DIRECTORIES "${OPENCV_INCLUDE_DIR}")


# libjxl — JPEG XL export ONLY (src/util/JxlWriter.cpp, the one file that
# includes it). BSD-3-Clause, dynamically linked; listed in NOTICE. ImageIO
# reads JPEG XL but cannot write it. DECISIONS #363.
find_path(JXL_INCLUDE_DIR
    NAMES jxl/encode.h
    HINTS /opt/homebrew/opt/jpeg-xl/include /usr/local/opt/jpeg-xl/include
    REQUIRED
)
set(JXL_LIBRARIES "")
foreach(lib jxl jxl_threads)
    find_library(JXL_${lib}_LIBRARY
        NAMES ${lib}
        HINTS /opt/homebrew/opt/jpeg-xl/lib /usr/local/opt/jpeg-xl/lib
        REQUIRED
    )
    list(APPEND JXL_LIBRARIES ${JXL_${lib}_LIBRARY})
endforeach()
message(STATUS "libjxl: ${JXL_jxl_LIBRARY}")
set(JXL_INCLUDE_DIR ${JXL_INCLUDE_DIR} PARENT_SCOPE)
set(JXL_LIBRARIES ${JXL_LIBRARIES} PARENT_SCOPE)
set_source_files_properties(src/util/JxlWriter.cpp
    PROPERTIES INCLUDE_DIRECTORIES ${JXL_INCLUDE_DIR})

if(ORION_DAWN)
    target_sources(orion_engine PRIVATE
        src/gpu/DawnDevice.cpp
        src/gpu/DawnTexture.cpp
        src/gpu/DawnKernel.cpp
        src/gpu/DawnCommands.cpp
    )
    target_compile_definitions(orion_engine PRIVATE ORION_HAS_DAWN=1)
    if(APPLE)
        target_sources(orion_engine PRIVATE src/gpu/DawnMetal.mm)
        set_source_files_properties(src/gpu/DawnMetal.mm PROPERTIES COMPILE_OPTIONS "-fobjc-arc")
        target_include_directories(orion_engine PRIVATE ${ORION_DAWN_DIR}/include)
        find_library(IOSURFACE_FRAMEWORK  IOSurface  REQUIRED)
        find_library(QUARTZCORE_FRAMEWORK QuartzCore REQUIRED)
        find_library(IOKIT_FRAMEWORK      IOKit      REQUIRED)
        find_library(COCOA_FRAMEWORK      Cocoa      REQUIRED)
        target_link_libraries(orion_engine PRIVATE ${ORION_DAWN_LIB}
            ${IOSURFACE_FRAMEWORK} ${QUARTZCORE_FRAMEWORK} ${IOKIT_FRAMEWORK} ${COCOA_FRAMEWORK})
    else()
        # D3D12 on Windows, Vulkan on Linux. Dawn's installed package names the
        # system libraries its backends need.
        target_sources(orion_engine PRIVATE src/gpu/DawnNative.cpp)
        find_package(Threads REQUIRED GLOBAL)   # DawnTargets names Threads::Threads and finds it not
        find_package(Dawn CONFIG REQUIRED GLOBAL PATHS ${ORION_DAWN_DIR} NO_DEFAULT_PATH)
        target_link_libraries(orion_engine PRIVATE dawn::webgpu_dawn)
        if(WIN32)
            # DawnNative.cpp's shared texture for Avalonia's ANGLE (#386): one D3D11 device.
            target_link_libraries(orion_engine PRIVATE d3d11)
            # Dawn's D3D12 backend loads the HLSL compiler from beside the module
            # and will not take System32's (Windows error 87). The Windows SDK's
            # redistributable copy goes next to every program that links the engine.
            find_file(ORION_D3DCOMPILER d3dcompiler_47.dll REQUIRED NO_DEFAULT_PATH
                      PATHS "$ENV{WindowsSdkDir}/Redist/D3D/x64"
                            "C:/Program Files (x86)/Windows Kits/10/Redist/D3D/x64")
            file(COPY ${ORION_D3DCOMPILER} DESTINATION ${CMAKE_BINARY_DIR}/apps/tests)
            file(COPY ${ORION_D3DCOMPILER} DESTINATION ${CMAKE_BINARY_DIR}/shell-native)
        endif()
    endif()
endif()

target_link_libraries(orion_engine
    PRIVATE ${ORION_PLATFORM_LIBRARIES}
            ${LIBRAW_LIBRARY} ${OPENCV_LIBRARIES} ${JXL_LIBRARIES}
)
