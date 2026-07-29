add_executable(orion-tests main.cpp)

target_include_directories(orion-tests PRIVATE ${CMAKE_SOURCE_DIR}/engine/src)
target_compile_definitions(orion-tests PRIVATE ORION_SHADER_DIR="${ORION_SHADER_DIR}"
            ORION_DATA_DIR="${CMAKE_SOURCE_DIR}/data")
target_link_libraries(orion-tests PRIVATE Orion::Engine)
