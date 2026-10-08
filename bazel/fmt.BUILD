# =============================================================================
# fmt Library BUILD Configuration (standalone, vendored under third_party/fmt)
# =============================================================================
# Modern formatting library, built header-only to match this repo's CMake
# path (cmake/ProfilerDependencies.cmake's profiler_setup_fmt() / Kineto's own
# fmt::fmt-header-only consumption) and the sibling Logging repo's
# ThirdParty/fmt.BUILD overlay.
# =============================================================================

package(default_visibility = ["//visibility:public"])

cc_library(
    name = "fmt",
    hdrs = glob([
        "include/fmt/*",
    ]),
    defines = ["FMT_HEADER_ONLY=1"],
    includes = ["include"],
    textual_hdrs = glob([
        "include/fmt/*",
    ]),
)
