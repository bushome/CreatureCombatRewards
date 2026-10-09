#pragma once

// Compatibility header for building against AsaApi without vcpkg.
//
// AsaApi's UnrealString.h does:
//     #include "Logger/Logger.h"   // brings in spdlog and the copy of fmt bundled with it
//     #include "fmt/format.h"      // expects an fmt package on the include path
//
// By the time the second line runs, the bundled fmt is already loaded and its include guard makes
// any other fmt/format.h a no-op. AsaApi's own build and the plugin template satisfy that second
// include by installing fmt through vcpkg. This file satisfies it instead, by pointing back at the
// fmt that AsaApi already ships, so no vcpkg setup is needed.
//
// If a future AsaApi release drops its bundled fmt, delete this folder and install fmt with vcpkg.

#include "Logger/spdlog/fmt/fmt.h"
