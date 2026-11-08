#pragma once

#include <chrono>

namespace orion::util {

/// Milliseconds on the steady clock since construction or the last restart.
class Stopwatch {
public:
    [[nodiscard]] double ms() const noexcept {
        return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start_).count();
    }
    /// The milliseconds so far, and a new start.
    double lap() noexcept {
        const double t = ms();
        start_ = std::chrono::steady_clock::now();
        return t;
    }

private:
    std::chrono::steady_clock::time_point start_ = std::chrono::steady_clock::now();
};

}  // namespace orion::util
