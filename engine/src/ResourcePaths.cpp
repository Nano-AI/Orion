#include "ResourcePaths.h"

#include "util/Utf8Path.h"

#include <filesystem>
#include <vector>

#if defined(__APPLE__)
#include <mach-o/dyld.h>
#elif defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#endif

namespace orion::res {
namespace {

/// The directory holding the running executable.
///
/// `_NSGetExecutablePath` rather than `argv[0]`, which is whatever the caller
/// chose to exec with and is not a path at all when the process was started
/// through a bundle. Called once per resource, at first use.
/// Windows' GetModuleFileNameW and Linux's /proc/self/exe are the same answer
/// by other names (#376).
std::filesystem::path executableDir() {
#if defined(__APPLE__)
    std::uint32_t size = 0;
    _NSGetExecutablePath(nullptr, &size);          // asks for the length
    std::vector<char> buffer(size + 1, '\0');
    if (_NSGetExecutablePath(buffer.data(), &size) != 0) return {};
    const std::filesystem::path raw(buffer.data());
#elif defined(_WIN32)
    std::vector<wchar_t> buffer(32768, L'\0');
    if (GetModuleFileNameW(nullptr, buffer.data(), DWORD(buffer.size())) == 0) return {};
    const std::filesystem::path raw(buffer.data());
#else
    std::error_code link;
    const auto raw = std::filesystem::read_symlink("/proc/self/exe", link);
    if (link) return {};
#endif

    std::error_code ec;
    // Resolve symlinks: a path through one would put Contents/Resources
    // somewhere other than beside the real binary.
    auto exe = std::filesystem::weakly_canonical(raw, ec);
    if (ec) exe = raw;
    return exe.parent_path();
}

/// The build directory this binary came out of: the nearest ancestor of the
/// executable that holds a CMakeCache.txt. Empty for anything installed.
///
/// Found at runtime rather than compiled in. The build used to hand the engine
/// absolute paths, and an absolute path into a checkout is the builder's home
/// directory, `/Users/<name>/...`, written into every binary that links the
/// engine, the shipped app included (#266).
std::filesystem::path buildDir() {
    std::error_code ec;
    for (auto dir = executableDir(); !dir.empty(); dir = dir.parent_path()) {
        if (std::filesystem::is_regular_file(dir / "CMakeCache.txt", ec)) return dir;
        if (dir == dir.parent_path()) break;
    }
    return {};
}

/// `Contents/Resources/<name>` beside the running binary, when it exists; on
/// Windows and Linux, where a package has no bundle, `<name>` beside it.
///
/// Otherwise `fromBuild`, a path relative to the build directory, so the tests
/// and the bench, which run straight out of the build tree and have no bundle,
/// keep reading the same directories as before. Existence is checked rather
/// than assumed: a bundle missing its shaders fails while naming what it looked
/// for, and outside any build tree that is the relative path itself.
std::string resolve(const char* name, const char* fromBuild) {
    std::error_code ec;
#ifdef __APPLE__
    const auto bundled = executableDir() / ".." / "Resources" / name;
#else
    const auto bundled = executableDir() / name;
#endif
    if (std::filesystem::is_directory(bundled, ec)) {
        auto clean = std::filesystem::weakly_canonical(bundled, ec);
        return util::utf8String(ec ? bundled : clean);
    }
    const auto build = buildDir();
    if (build.empty()) return fromBuild;
    const auto dev = build / fromBuild;
    auto clean = std::filesystem::weakly_canonical(dev, ec);
    return util::utf8String(ec ? dev : clean);
}

}  // namespace

const std::string& shaderDir() {
    static const std::string dir = resolve("shaders", ORION_SHADER_DIR_REL);
    return dir;
}

const std::string& dataDir() {
    static const std::string dir = resolve("data", ORION_DATA_DIR_REL);
    return dir;
}

}  // namespace orion::res
