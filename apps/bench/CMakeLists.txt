# One file per section of the report the bench prints. See bench.h for the rule
# about which file a new check goes in — decision #118.
add_executable(orion-bench
    main.cpp
    bench_brush.cpp
    bench_compose.cpp
    bench_controls.cpp
    bench_gate.cpp
    bench_highlights.cpp
    bench_invariants.cpp
    bench_metrics.cpp
    bench_profile.cpp
)

# The bench tool exercises engine internals directly rather than going through
# the narrow C facade — it is in-tree tooling, not a client.
target_include_directories(orion-bench PRIVATE ${CMAKE_SOURCE_DIR}/engine/src)

target_compile_definitions(orion-bench
    PRIVATE ORION_SHADER_DIR="${ORION_SHADER_DIR}"
)

target_link_libraries(orion-bench PRIVATE Orion::Engine)
