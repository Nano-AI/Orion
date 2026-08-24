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
    tests_grade.cpp
    tests_linear.cpp
    tests_effects.cpp
    tests_display.cpp
    tests_highlights.cpp
    tests_fusion.cpp
    tests_mask.cpp
    tests_mask_geom.cpp
    tests_mask_pullback.cpp
    tests_brush.cpp
    tests_brush_accum.cpp
    tests_brush_spacing.cpp
    tests_guide_eps.cpp
    tests_spot.cpp
    tests_mask_range.cpp
    tests_mask_matte.cpp
    tests_grain.cpp
    tests_vignette.cpp
    tests_pipeline.cpp
    tests_dng.cpp
)

target_include_directories(orion-tests PRIVATE ${CMAKE_SOURCE_DIR}/engine/src ${LIBRAW_INCLUDE_DIR})
target_compile_definitions(orion-tests PRIVATE ORION_SHADER_DIR="${ORION_SHADER_DIR}"
            ORION_DATA_DIR="${CMAKE_SOURCE_DIR}/data")
target_link_libraries(orion-tests PRIVATE Orion::Engine)
