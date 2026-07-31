# One translation unit per subject. `main.cpp` is the running order, `harness.*`
# is the counter and the three helpers, and every file here is under the
# thousand-line limit CLAUDE.md calls a hard constraint — which the single
# main.cpp this replaced had exceeded sevenfold.
add_executable(orion-tests
    main.cpp
    harness.cpp
    tests_color.cpp
    tests_io.cpp
    tests_geometry.cpp
    tests_tone.cpp
    tests_grade.cpp
    tests_effects.cpp
    tests_fusion.cpp
    tests_mask.cpp
    tests_mask_geom.cpp
    tests_brush.cpp
    tests_mask_range.cpp
    tests_mask_matte.cpp
    tests_grain.cpp
)

target_include_directories(orion-tests PRIVATE ${CMAKE_SOURCE_DIR}/engine/src)
target_compile_definitions(orion-tests PRIVATE ORION_SHADER_DIR="${ORION_SHADER_DIR}"
            ORION_DATA_DIR="${CMAKE_SOURCE_DIR}/data")
target_link_libraries(orion-tests PRIVATE Orion::Engine)
