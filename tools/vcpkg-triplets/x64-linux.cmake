# vcpkg's own x64-linux, the host triplet: what builds the tools a port runs while
# it builds (ICU's icupkg and gencnval for the target's data, #404). Their C++ and
# GCC runtimes linked in, so they run where no libstdc++ is on the loader's path,
# as on NixOS outside a compiler wrapper.
set(VCPKG_TARGET_ARCHITECTURE x64)
set(VCPKG_CRT_LINKAGE dynamic)
set(VCPKG_LIBRARY_LINKAGE static)
set(VCPKG_CMAKE_SYSTEM_NAME Linux)
set(VCPKG_LINKER_FLAGS "-static-libstdc++ -static-libgcc")
