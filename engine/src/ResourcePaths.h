/*  Where the shaders and the lens database live at runtime.
 *
 *  The build hands the engine absolute paths into the source tree —
 *  ORION_SHADER_DIR and ORION_DATA_DIR — which is right for the tests and the
 *  bench, and wrong for anything a user installs: those paths do not exist on
 *  their machine, so a shipped app finds no kernels and dies on the first open.
 *
 *  So the paths are resolved rather than baked. Inside an app bundle the
 *  resources sit in Contents/Resources; outside one, the compile-time path is
 *  still correct and still used. One rule, both cases, and nothing to remember
 *  when running the bench.
 */

#pragma once

#include <string>

namespace orion::res {

/// The directory holding one `<entryPoint>.metallib` per kernel.
[[nodiscard]] const std::string& shaderDir();

/// The directory holding `lensfun/`.
[[nodiscard]] const std::string& dataDir();

}  // namespace orion::res
