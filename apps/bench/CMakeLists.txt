add_executable(orion-bench main.cpp)

# The bench tool exercises engine internals directly rather than going through
# the narrow C facade — it is in-tree tooling, not a client.
target_include_directories(orion-bench PRIVATE ${CMAKE_SOURCE_DIR}/engine/src)

target_compile_definitions(orion-bench
    PRIVATE ORION_SHADER_DIR="${ORION_SHADER_DIR}"
)

target_link_libraries(orion-bench PRIVATE Orion::Engine)
