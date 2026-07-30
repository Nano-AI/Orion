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

/// `Contents/Resources/<name>` beside the running binary, when it exists.
///
/// Falls back to the compile-time path, so the tests and the bench — which run
/// straight out of the build tree and have no bundle — keep reading the source
/// tree exactly as before. Existence is checked rather than assumed: a bundle
/// missing its shaders should fail while naming the path the build was
/// configured with, not one nobody configured.
std::string resolve(const char* name, const char* fallback) {
    std::error_code ec;
    const auto bundled = executableDir() / ".." / "Resources" / name;
    if (std::filesystem::is_directory(bundled, ec)) {
        auto clean = std::filesystem::weakly_canonical(bundled, ec);
        return ec ? bundled.string() : clean.string();
    }
    return fallback;
}

}  // namespace

const std::string& shaderDir() {
    static const std::string dir = resolve("shaders", ORION_SHADER_DIR);
    return dir;
}

const std::string& dataDir() {
    static const std::string dir = resolve("data", ORION_DATA_DIR);
    return dir;
}

}  // namespace orion::res
