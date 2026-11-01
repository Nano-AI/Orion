# vcpkg's x64-linux-dynamic, release only (decision #376): shared libraries, so
# LibRaw's LGPL is met by dynamic linking, and $ORIGIN run paths, so the AppImage
# can carry them beside each other. Windows uses vcpkg's own x64-windows-release.
set(VCPKG_TARGET_ARCHITECTURE x64)
set(VCPKG_CRT_LINKAGE dynamic)
set(VCPKG_LIBRARY_LINKAGE dynamic)
set(VCPKG_CMAKE_SYSTEM_NAME Linux)
set(VCPKG_BUILD_TYPE release)
set(VCPKG_FIXUP_ELF_RPATH ON)
# The C++ and GCC runtimes linked in, not loaded from the machine it runs on
# (decision #404): the package runs on glibc 2.35 with the system's libstdc++
# never asked for, whatever its age. --exclude-libs keeps the copy private: .NET's
# runtime loads the system's libstdc++ into the same process, and exported, each
# library's ~740 runtime symbols would interpose on it, or it on them.
# tools/check-glibc-floor.py holds it to both.
set(VCPKG_LINKER_FLAGS "-static-libstdc++ -static-libgcc -Wl,--exclude-libs,libstdc++.a:libgcc.a:libgcc_eh.a")
