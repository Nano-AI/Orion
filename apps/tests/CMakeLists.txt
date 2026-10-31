# One translation unit per fixture — the frame and the state a group of checks
# share, not the subject they are about (decision #127: a test file answers
# "what broke?", so a check separated from the fixture it reads is worse off in a
# short file than it was in a long one). `main.cpp` is the running order,
# `harness.*` is the counter and the three helpers, and every file here is under
# the thousand-line limit CLAUDE.md calls a hard constraint — which the single
# main.cpp this replaced had exceeded sevenfold.
add_executable(orion-tests
    main.cpp
    harness.cpp
    tests_color.cpp
    tests_io.cpp
    tests_geometry.cpp
    tests_perspective.cpp
    tests_tone.cpp
    tests_denoise.cpp
    tests_nlm.cpp
    tests_grade.cpp
    tests_linear.cpp
    tests_effects.cpp
    tests_display.cpp
    tests_display_copy.cpp
    tests_highlights.cpp
    tests_fusion.cpp
    tests_fusion_wiring.cpp
    tests_mask.cpp
    tests_mask_geom.cpp
    tests_mask_pullback.cpp
    tests_brush.cpp
    tests_brush_accum.cpp
    tests_brush_spacing.cpp
    tests_guide_eps.cpp
    tests_spot.cpp
    tests_mask_range.cpp
    tests_mask_slots.cpp
    tests_mask_matte.cpp
    tests_grain.cpp
    tests_vignette.cpp
    tests_pipeline.cpp
    tests_dawn_parity.cpp
    tests_dng.cpp
    tests_lens_sources.cpp
    tests_roi.cpp
    tests_merge.cpp
    tests_merge_render.cpp
    tests_align.cpp
    tests_hdr_merge.cpp
    tests_stack_rejection.cpp
    tests_stack_stars.cpp
    tests_stack_integrate.cpp
    tests_stack_split.cpp
    tests_stack_facade.cpp
    tests_stack_calibrate.cpp
)

target_include_directories(orion-tests PRIVATE ${CMAKE_SOURCE_DIR}/engine/src ${LIBRAW_INCLUDE_DIR})
target_compile_definitions(orion-tests PRIVATE ORION_SHADER_DIR="${ORION_SHADER_DIR}"
            ORION_DATA_DIR="${CMAKE_SOURCE_DIR}/data")
target_link_libraries(orion-tests PRIVATE Orion::Engine)
if(WIN32)
    # The 8 MB main-thread stack macOS and Linux give: Windows' 1 MB overflows
    # on entry to testDngRoundTrip, whose frames live on the stack (#376).
    target_link_options(orion-tests PRIVATE /STACK:8388608)
    # tests_display_copy.cpp reads the shared texture back as ANGLE would (#386).
    target_link_libraries(orion-tests PRIVATE d3d11 dxgi)
endif()

if(APPLE)
    # The export tests read files back with ImageIO, the second reader on purpose.
    target_sources(orion-tests PRIVATE tests_watermark.cpp tests_jxl.cpp)

    # The JPEG XL round trip decodes with libjxl itself, and converts lossy frames
    # back with libjxl's CMS. Scoped to the one file that includes it.
    find_library(JXL_CMS_LIBRARY NAMES jxl_cms
        HINTS /opt/homebrew/opt/jpeg-xl/lib /usr/local/opt/jpeg-xl/lib REQUIRED)
    set_source_files_properties(tests_jxl.cpp PROPERTIES INCLUDE_DIRECTORIES ${JXL_INCLUDE_DIR})
    target_link_libraries(orion-tests PRIVATE ${JXL_LIBRARIES} ${JXL_CMS_LIBRARY})
else()
    # Windows and Linux have no ImageIO: their writer is read back by OpenCV,
    # which wrote it, so these check shape and size, not colour (#376).
    target_sources(orion-tests PRIVATE tests_export_portable.cpp)
    target_link_libraries(orion-tests PRIVATE ${OPENCV_LIBRARIES})
endif()

# The photograph CI hands the shell's --selftest, generated so it carries no
# one's pictures (tools/fixtures/selftest-photo.cpp, decision #376).
add_executable(orion-selftest-photo
    ${CMAKE_SOURCE_DIR}/tools/fixtures/selftest-photo.cpp
    ${CMAKE_SOURCE_DIR}/engine/src/util/DngWriter.cpp)
target_include_directories(orion-selftest-photo PRIVATE ${CMAKE_SOURCE_DIR}/engine/src)
