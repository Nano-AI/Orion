#include "ResourcePaths.h"

#include <mach-o/dyld.h>

#include <filesystem>
#include <vector>

namespace orion::res {
namespace {

/// The directory holding the running executable.
///
/// `_NSGetExecutablePath` rather than `argv[0]`, which is whatever the caller
/// chose to exec with and is not a path at all when the process was started
/// through a bundle. Called once per resource, at first use.
std::filesystem::path executableDir() {
    std::uint32_t size = 0;
    _NSGetExecutablePath(nullptr, &size);          // asks for the length
    std::vector<char> buffer(size + 1, '\0');
    if (_NSGetExecutablePath(buffer.data(), &size) != 0) return {};

    std::error_code ec;
    // Resolve symlinks: a path through one would put Contents/Resources
    // somewhere other than beside the real binary.
    auto exe = std::filesystem::weakly_canonical(
        std::filesystem::path(buffer.data()), ec);
    if (ec) exe = std::filesystem::path(buffer.data());
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

/// `Contents/Resources/<name>` beside the running binary, when it exists.
///
/// Otherwise `fromBuild`, a path relative to the build directory, so the tests
/// and the bench, which run straight out of the build tree and have no bundle,
/// keep reading the same directories as before. Existence is checked rather
/// than assumed: a bundle missing its shaders fails while naming what it looked
/// for, and outside any build tree that is the relative path itself.
std::string resolve(const char* name, const char* fromBuild) {
    std::error_code ec;
    const auto bundled = executableDir() / ".." / "Resources" / name;
    if (std::filesystem::is_directory(bundled, ec)) {
        auto clean = std::filesystem::weakly_canonical(bundled, ec);
        return ec ? bundled.string() : clean.string();
    }
    const auto build = buildDir();
    if (build.empty()) return fromBuild;
    const auto dev = build / fromBuild;
    auto clean = std::filesystem::weakly_canonical(dev, ec);
    return ec ? dev.string() : clean.string();
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
