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
    # A DLL exports only what it is told to. Every C function the facade
    # declares, and every shim_* this file defines, read from the two sources so
    # a new one cannot be forgotten.
    set(def ${gen}/orion_shim.def)
    file(STRINGS ${header} facade REGEX "^[A-Za-z].*[ *]orion_[a-z0-9_]+\\(")
    file(STRINGS ${CMAKE_CURRENT_SOURCE_DIR}/shim.cpp shims REGEX "^[A-Za-z].*[ *]shim_[a-z0-9_]+\\(")
    set(names "")
    foreach(line IN LISTS facade shims)
        string(REGEX MATCH "(orion|shim)_[a-z0-9_]+\\(" name "${line}")
        string(REPLACE "(" "" name "${name}")
        list(APPEND names ${name})
    endforeach()
    list(REMOVE_DUPLICATES names)
    list(REMOVE_ITEM names shim_settle)   # C++, internal to the shim
    list(JOIN names "\n    " exports)
    file(WRITE ${def} "EXPORTS\n    ${exports}\n")
    target_sources(orion_shim PRIVATE ${def})
endif()
