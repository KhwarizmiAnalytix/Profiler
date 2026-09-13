/*
 * Profiler DLL export/import header
 *
 * Defines PROFILER_API, PROFILER_VISIBILITY, etc. for the Profiler library.
 * Build flags (set in Library/Profiler/CMakeLists.txt):
 * - PROFILER_STATIC_DEFINE — static library
 * - PROFILER_SHARED_DEFINE — shared library
 * - PROFILER_BUILDING_DLL — building the shared library (Windows)
 */

#pragma once

#define PROFILER_VISIBILITY_ENUM

#if defined(PROFILER_STATIC_DEFINE)
#define PROFILER_API
#define PROFILER_VISIBILITY
#define PROFILER_IMPORT
#define PROFILER_HIDDEN

#elif defined(PROFILER_SHARED_DEFINE)
#if defined(_WIN32) || defined(__CYGWIN__)
#ifdef PROFILER_BUILDING_DLL
#define PROFILER_API __declspec(dllexport)
#else
#define PROFILER_API __declspec(dllimport)
#endif
#define PROFILER_VISIBILITY
#define PROFILER_IMPORT __declspec(dllimport)
#define PROFILER_HIDDEN
#elif defined(__GNUC__) && __GNUC__ >= 4
#define PROFILER_API __attribute__((visibility("default")))
#define PROFILER_VISIBILITY __attribute__((visibility("default")))
#define PROFILER_IMPORT __attribute__((visibility("default")))
#define PROFILER_HIDDEN __attribute__((visibility("hidden")))
#else
#define PROFILER_API
#define PROFILER_VISIBILITY
#define PROFILER_IMPORT
#define PROFILER_HIDDEN
#endif

#else
#define PROFILER_API
#define PROFILER_VISIBILITY
#define PROFILER_IMPORT
#define PROFILER_HIDDEN
#endif

// Kineto and ITT are alternative instrumentation backends and may not both be active.
// The native traceme/xplane pipeline is always compiled alongside whichever is selected.
#if (PROFILER_HAS_KINETO + PROFILER_HAS_ITT) > 1
#error "PROFILER_HAS_KINETO and PROFILER_HAS_ITT are mutually exclusive. Only one may equal 1."
#endif

#ifndef PROFILER_HAS_NVTX
#define PROFILER_HAS_NVTX 0
#endif
#ifndef PROFILER_HAS_ROCTX
#define PROFILER_HAS_ROCTX 0
#endif
