#pragma once

// Kineto v0.4.0 uses fmt APIs that later fmt versions moved:
//   fmt::join      -> <fmt/ranges.h>
//   fmt::localtime -> <fmt/chrono.h>
// Force-include those headers instead of editing third_party/.
#include <fmt/chrono.h>
#include <fmt/ranges.h>
