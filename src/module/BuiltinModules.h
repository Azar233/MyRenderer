#pragma once

#include "module/ModuleRegistry.h"

// Stable ids of the modules compiled into `MyRendererModules`. Tests, the
// acceptance target and documentation refer to these constants instead of copying
// string literals.
namespace BuiltinModules {

inline constexpr const char* turntableId = "myrenderer.core.turntable";

} // namespace BuiltinModules
