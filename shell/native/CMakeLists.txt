# Windows and Linux: the shim as one shared library C# loads by the name
# "orion_shim" (orion_shim.dll, liborion_shim.so), holding the whole engine as
# build.sh's -force_load does on macOS. Added by the top-level build when it is
# configured with -DORION_SHELL_DIR=<this shell> (decision #376).

find_package(Python3 REQUIRED COMPONENTS Interpreter)
set(gen ${CMAKE_CURRENT_BINARY_DIR})
set(header ${CMAKE_SOURCE_DIR}/engine/include/orion/orion.h)
add_custom_command(
    OUTPUT ${gen}/fields.inc ${gen}/mask_fields.inc
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/gen_fields.py ${header}
            ${gen}/fields.inc ${gen}/mask_fields.inc
    DEPENDS ${header} ${CMAKE_CURRENT_SOURCE_DIR}/gen_fields.py
    VERBATIM
)

add_library(orion_shim SHARED shim.cpp ${gen}/fields.inc ${gen}/mask_fields.inc)
target_include_directories(orion_shim PRIVATE ${gen})
# Every engine object, not only those shim.cpp reaches: C# calls orion_* directly.
target_link_libraries(orion_shim PRIVATE "$<LINK_LIBRARY:WHOLE_ARCHIVE,Orion::Engine>")

if(WIN32)
    set(def ${gen}/orion_shim.def)
    file(GLOB facade_headers ${CMAKE_SOURCE_DIR}/engine/include/orion/orion*.h)
    add_custom_command(
        OUTPUT ${def}
        COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/gen_exports.py ${header}
                ${CMAKE_CURRENT_SOURCE_DIR}/shim.cpp ${def}
        DEPENDS ${facade_headers} ${CMAKE_CURRENT_SOURCE_DIR}/shim.cpp ${CMAKE_CURRENT_SOURCE_DIR}/gen_exports.py
        VERBATIM
    )
    target_sources(orion_shim PRIVATE ${def})
endif()
