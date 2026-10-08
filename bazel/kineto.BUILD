# =============================================================================
# Kineto (libkineto) BUILD Configuration (standalone, vendored under
# third_party/kineto/libkineto)
# =============================================================================
# Mirrors third_party/kineto/libkineto/CMakeLists.txt's KINETO_BACKEND="cpu"
# path (this repo's PROFILER_BACKEND=KINETO default never requests CUDA/ROCm/
# XPU, matching bazel/BUILD.bazel's enable_cuda/enable_hip config_settings
# which likewise default off). Source lists are drawn from this repo's own
# libkineto_defs.bzl, kept here as the one source of truth CMake's
# get_filelist() also reads (see that file's own comment) rather than
# duplicating the file list by hand.
#
# Matches cmake/ProfilerDependencies.cmake's profiler_setup_kineto():
#   - kineto_base + kineto_api object libraries combined into one `kineto`
#     archive (get_libkineto_cpu_only_srcs(with_api = True) already unions
#     both).
#   - KINETO_NAMESPACE=libkineto / ENABLE_IPC_FABRIC defines.
#   - include/ and src/ as include dirs; dynolog's header-only ipcfabric
#     (vendored under third_party/dynolog_headers/) as an additional system
#     include dir -- there is no dynolog library to build or link.
#   - fmt consumed as the single header-only @fmt target used everywhere
#     else in this repo (avoids the duplicate-fmt-symbol issue
#     profiler_setup_kineto() works around for CMake by swapping Kineto's
#     default fmt::fmt-header-only for the compiled target it links --
#     here there is only ever the one header-only @fmt target to begin
#     with).
# =============================================================================

load(":libkineto_defs.bzl", "KINETO_COMPILER_FLAGS", "get_libkineto_cpu_only_srcs")

package(default_visibility = ["//visibility:public"])

cc_library(
    name = "dynolog_ipcfabric_headers",
    hdrs = glob([
        "third_party/dynolog_headers/dynolog/src/ipcfabric/*.h",
    ]),
    includes = [
        "third_party/dynolog_headers",
        "third_party/dynolog_headers/dynolog/src/ipcfabric",
    ],
)

cc_library(
    name = "kineto",
    srcs = get_libkineto_cpu_only_srcs(with_api = True),
    hdrs = glob([
        "include/*.h",
        "src/*.h",
    ]),
    copts = KINETO_COMPILER_FLAGS,
    defines = [
        "KINETO_NAMESPACE=libkineto",
        "ENABLE_IPC_FABRIC",
    ],
    includes = [
        "include",
        "src",
    ],
    deps = [
        "@fmt//:fmt",
        # dynolog's ipcfabric is header-only (vendored under
        # third_party/dynolog_headers/, pinned in version.txt); only its
        # include paths are needed, there is no library to depend on.
        ":dynolog_ipcfabric_headers",
    ],
)
