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

set(SLANGC ${CMAKE_SOURCE_DIR}/third_party/slang/bin/slangc${CMAKE_EXECUTABLE_SUFFIX})

if(NOT EXISTS ${SLANGC})
    message(FATAL_ERROR
        "slangc not found at ${SLANGC}\n"
        "Download this system's release from github.com/shader-slang/slang and\n"
        "extract it to third_party/slang/.")
endif()

# "sourceFile:entryPoint". The metallib is named after the entry point, which
# is what the pipeline looks up at runtime.
set(ORION_SHADERS
    linearize:linearize
    linear_source:linearSource
    rcd_dirs:rcdDirs
    rcd_lpf:rcdLpf
    rcd_green:rcdGreen
    rcd_rb:rcdRedBlue
    highlights:highlightRecover
    hl_mask:hlMask
    hl_pull:hlPull
    hl_push:hlPush
    hl_apply:hlApply
    denoise_blur:atrousBlur
    denoise_accum:atrousShrink
    denoise_field:denoiseField
    denoise_nlm:nlmLuma
    denoise_chroma_down:denoiseChromaDown
    denoise_chroma_up:denoiseChromaUp
    lens:lensCorrect
    color_grade:colorGrade
    sharpen:sharpen
    color_matrix:cameraToWorking
    huesat:hueSatMap
    guide_prep:guidePrep
    guide_down:guideDown
    box_blur:boxBlur
    guide_ab:guideAb
    llf_luma:llfLuma
    llf_down:llfDown
    llf_remap:llfRemapDown
    llf_remap_h:llfRemapH
    llf_down_v:llfDownV
    llf_down4:llfDownPacked
    llf_collapse:llfCollapse
    llf_collapse0:llfCollapse0
    llf_apply:llfApply
    dehaze_chan:dehazeChannelMin
    dehaze_rank:dehazeRank
    dehaze_peak:dehazePeak
    dehaze_prep:dehazePrep
    box_blur4:boxBlur4
    dehaze_ab:dehazeAb
    dehaze_recover:dehazeRecover
    fuse_proxy:fuseProxy
    fuse_split:fuseSplit
    fuse_blend:fuseBlend
    fuse_apply:fuseApply
    mask_base:maskBase
    mask_component:maskComponent
    mask_guide_prep:maskGuidePrep
    mask_guide_ab:maskGuideAb
    mask_guide_apply:maskGuideApply
    spot_measure:spotMeasure
    spot_apply:spotApply
    develop_linear:developLinear
    develop_display:developDisplay
    grain:grain
    geometry:geometry
)

if(ORION_DAWN)
    find_package(Python3 REQUIRED COMPONENTS Interpreter)
endif()

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

    # The reflection is only read by the WGSL step (wgsl_prep.py); it is the
    # Metal layout of the parameter block, which is the C++ struct's.
    add_custom_command(
        OUTPUT ${msl} ${STAGE_DIR}/${entry}.metal.json
        COMMAND ${SLANGC} ${src}
                -target metal -entry ${entry} -stage compute
                -I ${SHADER_DIR}
                -o ${msl} -reflection-json ${STAGE_DIR}/${entry}.metal.json
        DEPENDS ${SHADER_SOURCES}
        COMMENT "slang     ${file}.slang -> ${entry}.metal"
        VERBATIM
    )

    # The metallib only where Metal is (#376). The MSL above is still made
    # everywhere: its reflection is what wgsl_prep.py lays WGSL out against.
    if(APPLE)
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
    endif()

    # Dawn (#375): the same source as WGSL, then wgsl_prep.py writes the header
    # the backend binds by. See that script for what the header holds.
    if(ORION_DAWN)
        set(raw ${STAGE_DIR}/${entry}.raw.wgsl)
        set(wgsl ${OUT_DIR}/${entry}.wgsl)
        add_custom_command(
            OUTPUT ${raw} ${STAGE_DIR}/${entry}.wgsl.json
            COMMAND ${SLANGC} ${src}
                    -target wgsl -entry ${entry} -stage compute
                    -I ${SHADER_DIR}
                    -o ${raw} -reflection-json ${STAGE_DIR}/${entry}.wgsl.json
            DEPENDS ${SHADER_SOURCES}
            COMMENT "slang     ${file}.slang -> ${entry}.wgsl"
            VERBATIM
        )
        add_custom_command(
            OUTPUT ${wgsl}
            COMMAND ${Python3_EXECUTABLE} ${SHADER_DIR}/wgsl_prep.py ${raw}
                    ${STAGE_DIR}/${entry}.metal.json ${STAGE_DIR}/${entry}.wgsl.json ${wgsl}
            DEPENDS ${raw} ${STAGE_DIR}/${entry}.metal.json ${SHADER_DIR}/wgsl_prep.py
            COMMENT "wgsl_prep ${entry}.wgsl"
            VERBATIM
        )
        list(APPEND METALLIBS ${wgsl})
    endif()
endforeach()

add_custom_target(orion_shaders ALL DEPENDS ${METALLIBS})

set(ORION_SHADER_DIR ${OUT_DIR} PARENT_SCOPE)
