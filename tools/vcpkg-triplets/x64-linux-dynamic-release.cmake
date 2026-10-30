# vcpkg's x64-linux-dynamic, release only (decision #376): shared libraries, so
# LibRaw's LGPL is met by dynamic linking, and $ORIGIN run paths, so the AppImage
# can carry them beside each other. Windows uses vcpkg's own x64-windows-release.
set(VCPKG_TARGET_ARCHITECTURE x64)
set(VCPKG_CRT_LINKAGE dynamic)
set(VCPKG_LIBRARY_LINKAGE dynamic)
set(VCPKG_CMAKE_SYSTEM_NAME Linux)
set(VCPKG_BUILD_TYPE release)
set(VCPKG_FIXUP_ELF_RPATH ON)
