# Shader build: one .slang -> one .metal -> one .air -> one .metallib, per kernel.
#
# Shaders are authored once in Slang and compiled to MSL here. When a Vulkan or
# D3D backend is ever wanted, the same sources target SPIR-V or HLSL instead —
# that portability is the entire reason for the extra hop (decision #4).
#
# Each kernel gets its OWN metallib, deliberately. Compiling them as one module
# makes Slang assign cumulative binding indices across the whole module — the
# second kernel's textures land at index 2 and 3, the third at 4 and 5, and so
# on — while the host binds every dispatch from index 0. Separate modules make
# each kernel's bindings start at zero, which is the contract the host assumes.

set(SLANGC ${CMAKE_SOURCE_DIR}/third_party/slang/bin/slangc)

if(NOT EXISTS ${SLANGC})
    message(FATAL_ERROR
        "slangc not found at ${SLANGC}\n"
        "Download the macOS release from github.com/shader-slang/slang and\n"
        "extract it to third_party/slang/.")
endif()

# "sourceFile:entryPoint". The metallib is named after the entry point, which
# is what the pipeline looks up at runtime.
set(ORION_SHADERS
    linearize:linearize
    rcd_dirs:rcdDirs
    rcd_lpf:rcdLpf
    rcd_green:rcdGreen
    rcd_rb:rcdRedBlue
    sharpen:sharpen
    color_matrix:cameraToWorking
    guide_prep:guidePrep
    box_blur:boxBlur
    guide_ab:guideAb
    develop_linear:developLinear
    develop_display:developDisplay
    geometry:geometry
)

set(SHADER_DIR ${CMAKE_CURRENT_SOURCE_DIR})
set(STAGE_DIR  ${CMAKE_CURRENT_BINARY_DIR}/stage)
set(OUT_DIR    ${CMAKE_BINARY_DIR}/shaders)
file(MAKE_DIRECTORY ${STAGE_DIR} ${OUT_DIR})

# Every kernel includes cfa.slang, so a change there rebuilds all of them.
file(GLOB SHADER_SOURCES ${SHADER_DIR}/*.slang ${SHADER_DIR}/ops/*.slang)

set(METALLIBS "")

foreach(spec IN LISTS ORION_SHADERS)
    string(REPLACE ":" ";" parts "${spec}")
    list(GET parts 0 file)
    list(GET parts 1 entry)

    set(src ${SHADER_DIR}/${file}.slang)
    set(msl ${STAGE_DIR}/${entry}.metal)
    set(air ${STAGE_DIR}/${entry}.air)
    set(lib ${OUT_DIR}/${entry}.metallib)

    add_custom_command(
        OUTPUT ${msl}
        COMMAND ${SLANGC} ${src}
                -target metal -entry ${entry} -stage compute
                -I ${SHADER_DIR}
                -o ${msl}
        DEPENDS ${SHADER_SOURCES}
        COMMENT "slang     ${file}.slang -> ${entry}.metal"
        VERBATIM
    )

    add_custom_command(
        OUTPUT ${air}
        COMMAND xcrun -sdk macosx metal -O3 -ffast-math -c ${msl} -o ${air}
        DEPENDS ${msl}
        COMMENT "metal     ${entry}.metal -> ${entry}.air"
        VERBATIM
    )

    add_custom_command(
        OUTPUT ${lib}
        COMMAND xcrun -sdk macosx metallib ${air} -o ${lib}
        DEPENDS ${air}
        COMMENT "metallib  ${entry}.metallib"
        VERBATIM
    )

    list(APPEND METALLIBS ${lib})
endforeach()

add_custom_target(orion_shaders ALL DEPENDS ${METALLIBS})

set(ORION_SHADER_DIR ${OUT_DIR} PARENT_SCOPE)
