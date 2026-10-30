#pragma once

// Paths cross the facade as UTF-8 (`const char*`), and that is what they are
// everywhere inside the engine. std::filesystem, the streams and LibRaw read a
// narrow path in the ANSI code page on Windows, so `C:\Users\Zoë` arrived as
// mojibake there and nothing opened (#376). Every file the engine opens goes
// through utf8Path; macOS and Linux are UTF-8 already and see no change.

#include <filesystem>
#include <string>

namespace orion::util {

inline std::filesystem::path utf8Path(const std::string& utf8) {
    return std::filesystem::path(std::u8string(utf8.begin(), utf8.end()));
}

inline std::string utf8String(const std::filesystem::path& path) {
    const std::u8string u = path.u8string();
    return std::string(u.begin(), u.end());
}

}  // namespace orion::util
