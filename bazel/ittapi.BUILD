# =============================================================================
# ittapi (ittnotify) BUILD Configuration (standalone, vendored under
# third_party/ittapi)
# =============================================================================
# Mirrors third_party/ittapi/CMakeLists.txt's default `ittnotify` static
# target (ITT_API_IPT_SUPPORT / ITT_API_FORTRAN_SUPPORT / _CPP_SUPPORT /
# _REFERENCE_COLLECTOR all OFF by default, so no ASM, Fortran, C++ wrapper,
# or reference-collector sources are needed) -- same source list as that
# CMakeLists.txt's `file(GLOB ITT_SRCS "src/ittnotify/*.c" "src/ittnotify/*.h")`.
# Consumed as @ittapi//:ittnotify, matching
# cmake/ProfilerDependencies.cmake's profiler_setup_itt() -> Itt::itt alias.
# =============================================================================

package(default_visibility = ["//visibility:public"])

cc_library(
    name = "ittnotify",
    srcs = glob(["src/ittnotify/*.c"]),
    hdrs = glob([
        "include/*.h",
        "include/**/*.h",
        "src/ittnotify/*.h",
    ]),
    includes = ["include"],
    linkopts = select({
        "@platforms//os:windows": [],
        "@platforms//os:macos": [],
        "//conditions:default": ["-ldl"],
    }),
    # CMake's target_include_directories(ittnotify ... PRIVATE src/ittnotify)
    # -- the .c files #include their own siblings with a bare filename.
    deps = [":ittnotify_private_includes"],
)

cc_library(
    name = "ittnotify_private_includes",
    hdrs = glob(["src/ittnotify/*.h"]),
    includes = ["src/ittnotify"],
    visibility = ["//visibility:private"],
)
