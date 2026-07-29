add_executable(orion-rawstat main.cpp)

target_include_directories(orion-rawstat PRIVATE ${CMAKE_SOURCE_DIR}/engine/src)
target_link_libraries(orion-rawstat PRIVATE Orion::Engine)
