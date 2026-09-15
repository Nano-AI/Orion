/*  Where the shaders and the lens database live at runtime.
 *
 *  Inside an app bundle the resources sit in Contents/Resources. Outside one,
 *  as for the tests and the bench, they are found from the build directory (the
 *  nearest CMakeCache.txt above the executable) by the paths relative to it that
 *  the build hands in: ORION_SHADER_DIR_REL and ORION_DATA_DIR_REL.
 *
 *  Never an absolute path compiled in. That was right for the tests and wrong
 *  twice over for anything a user installs: the path does not exist on their
 *  machine, so a shipped app found no kernels, and it spelled out the builder's
 *  home directory inside the binary (#266).
 */

#pragma once

#include <string>

namespace orion::res {

/// The directory holding one `<entryPoint>.metallib` per kernel.
[[nodiscard]] const std::string& shaderDir();

/// The directory holding `lensfun/`.
[[nodiscard]] const std::string& dataDir();

}  // namespace orion::res
