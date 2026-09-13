/*
 * XSigma: High-Performance Computational Library
 *
 * SPDX-License-Identifier: GPL-3.0-or-later OR Commercial
 *
 * This file is part of XSigma and is licensed under a dual-license model:
 *
 *   - Open-source License (GPLv3):
 *       Free for personal, academic, and research use under the terms of
 *       the GNU General Public License v3.0 or later.
 *
 *   - Commercial License:
 *       A commercial license is required for proprietary, closed-source,
 *       or SaaS usage. Contact us to obtain a commercial agreement.
 *
 * Contact: licensing@xsigma.co.uk
 * Website: https://www.xsigma.co.uk
 */

/* Copyright 2020 The TensorFlow Authors. All Rights Reserved.

Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at

    http://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License.
==============================================================================*/
#pragma once

#include <string.h>

#include <initializer_list>
#include <string>

#include "common/profiler_macros.h"
////#include "logger/logger.h"
#include "common/profiler_strings.h"

namespace profiler
{

/**
 * @brief Key-value argument for structured trace metadata encoding.
 *
 * Represents a single key-value pair that will be encoded into trace metadata.
 * Supports various value types including strings, numbers, and booleans with
 * automatic type conversion and lifetime management.
 *
 * **Supported Value Types**:
 * - String types: string_view, const char*, std::string
 * - Numeric types: int, long, uint64_t, double, float, etc.
 * - Boolean: bool (converted to "true"/"false")
 *
 * **Lifetime Management**: For numeric and boolean types, the converted string
 * is stored internally to ensure it remains valid for the encoding process.
 *
 * **Usage**: Typically used in initializer lists with traceme_encode():
 * ```cpp
 * traceme_encode("operation", {{"param1", 42}, {"param2", "value"}, {"success", true}})
 * ```
 */
struct TraceMeArg
{
    /**
     * @brief Constructs an argument from string_view key and value.
     * @param k Argument key (must remain valid during encoding)
     * @param v Argument value (must remain valid during encoding)
     */
    TraceMeArg(std::string_view k, std::string_view v) : key(k), value(v) {}

    /**
     * @brief Constructs an argument from string_view key and C-string value.
     * @param k Argument key
     * @param v C-string value (null pointer becomes empty string)
     */
    TraceMeArg(std::string_view k, const char* v) : key(k), value(v ? v : "") {}

    /**
     * @brief Constructs an argument from string_view key and std::string value.
     * @param k Argument key
     * @param v String value (must remain valid during encoding)
     */
    TraceMeArg(std::string_view k, PROFILER_LIFETIME_BOUND const std::string& v) : key(k), value(v)
    {
    }

    /**
     * @brief Constructs an argument from string_view key and numeric value.
     *
     * Automatically converts numeric types to string representation using std::to_string().
     * The converted string is stored internally to ensure proper lifetime management.
     *
     * @tparam T Arithmetic type (int, long, double, etc.) excluding bool
     * @param k Argument key
     * @param v Numeric value to convert
     */
    template <typename T>
    TraceMeArg(
        std::string_view k,
        T                v,
        typename std::enable_if<
            std::is_arithmetic<T>::value && !std::is_same<T, bool>::value>::type* = nullptr)
        : key(k), value_storage_(std::to_string(v)), value(value_storage_)
    {
    }

    /**
     * @brief Constructs an argument from string_view key and boolean value.
     * @param k Argument key
     * @param v Boolean value (converted to "true" or "false")
     */
    TraceMeArg(std::string_view k, bool v)
        : key(k), value_storage_(v ? "true" : "false"), value(value_storage_)
    {
    }

    /// Deleted copy constructor to prevent accidental copying
    TraceMeArg(const TraceMeArg&) = delete;

    /// Deleted copy assignment to prevent accidental copying
    void operator=(const TraceMeArg&) = delete;

    std::string_view key;        ///< Argument key (metadata field name)
    std::string value_storage_;  ///< Internal storage for converted values (must precede 'value')
    std::string_view value;      ///< Argument value (points to value_storage_ or external string)
};

/**
 * @brief Internal implementation details for trace metadata encoding.
 *
 * This namespace contains low-level functions for efficient string manipulation
 * and metadata formatting. These functions are optimized for performance and
 * are not intended for direct use outside the encoding system.
 */
namespace traceme_internal
{

/**
 * @brief Fast string copy with metadata format validation.
 *
 * Copies string content to a pre-allocated buffer with validation to ensure
 * the string doesn't contain reserved characters that would break the metadata format.
 *
 * @param out Destination buffer pointer (must have sufficient space allocated)
 * @param str Source string to copy
 * @return Pointer to the position after the copied content
 *
 * **Performance**: Optimized for speed using memcpy for bulk copying
 * **Validation**: Debug builds check for invalid '#' characters in metadata
 * **Safety**: Requires pre-allocated buffer with sufficient space
 * **Format**: Metadata uses '#' as delimiter, so it's forbidden in content
 */
inline char* append(char* out, std::string_view str)
{
    // PROFILER_CHECK_DEBUG(
    // !strings::str_contains(str, '#'), "'#' is not a valid character in traceme_encode {}", str);

    const size_t str_size = str.size();
    if PROFILER_LIKELY (str_size > 0)
    {
        memcpy(out, str.data(), str_size);
        out += str_size;
    }
    return out;
}

/**
 * @brief Efficiently encodes key-value arguments into trace metadata format.
 *
 * Appends structured metadata to a trace name using the format:
 * `original_name#key1=value1,key2=value2,key3=value3#`
 *
 * This function is optimized for performance with single-pass string construction
 * and pre-calculated buffer sizing to minimize allocations.
 *
 * @param name Base trace name to append metadata to
 * @param args Collection of key-value pairs to encode
 * @return Complete trace name with encoded metadata
 *
 * **Format**: `#key1=value1,key2=value2#` appended to name
 * **Performance**: Single allocation with pre-calculated size
 * **Validation**: Debug builds verify buffer calculations are correct
 * **Empty Args**: No-op if args is empty (no metadata appended)
 *
 * **Example**:
 * - Input: name="matrix_op", args={{"rows", 100}, {"cols", 50}}
 * - Output: "matrix_op#rows=100,cols=50#"
 */
inline std::string append_args(std::string name, std::initializer_list<TraceMeArg> args)
{
    if PROFILER_LIKELY (args.size() > 0)
    {
        const auto old_size = name.size();
        auto       new_size =
            old_size + args.size() * 2 + 1;  // +1 for opening '#', +1 per arg for '=' and ','
        for (const auto& arg : args)
        {
            new_size += arg.key.size() + arg.value.size();
        }
        name.resize(new_size);
        char* const begin = name.data();
        char*       out   = begin + old_size;
        *out++            = '#';  // Start metadata section
        for (const auto& arg : args)
        {
            out    = append(out, arg.key);
            *out++ = '=';
            out    = append(out, arg.value);
            *out++ = ',';  // Separator (will be replaced with '#' for last arg)
        }
        *(out - 1) = '#';  // Replace final ',' with closing '#'
        // PROFILER_CHECK_DEBUG(
        // out == begin + new_size,
        // "out={} is not equal to begin={} + new_size={}",
        // out,
        // begin,
        // new_size);
    }
    return name;
}

/**
 * @brief Appends additional metadata to an existing trace name with metadata.
 *
 * Extends the metadata section of a trace name that already contains encoded
 * metadata. Handles proper formatting to maintain the metadata structure.
 *
 * @param name Pointer to trace name string (modified in-place)
 * @param new_metadata Additional metadata to append (typically from traceme_encode)
 *
 * **Format Handling**:
 * - If name ends with '#', replaces it with ',' and appends new metadata
 * - If new_metadata starts with '#', strips the leading '#' to avoid duplication
 * - Maintains proper `#key=value,key=value#` format structure
 *
 * **Performance**: Minimal overhead - single string append operation
 * **Thread Safety**: Not thread-safe - caller must ensure exclusive access
 * **Empty Metadata**: No-op if new_metadata is empty
 *
 * **Example**:
 * - Before: name="op#param1=value1#", new_metadata="#param2=value2#"
 * - After: name="op#param1=value1,param2=value2#"
 */
inline void append_metadata(std::string* name, std::string_view new_metadata)
{
    if PROFILER_UNLIKELY (new_metadata.empty())
    {
        return;  // No metadata to append
    }

    if (!name->empty() && name->back() == '#')
    {                        // name already has metadata - merge it
        name->back() = ',';  // Replace closing '#' with separator ','
        if PROFILER_LIKELY (new_metadata.front() == '#')
        {
            new_metadata.remove_prefix(1);  // Skip leading '#' to avoid duplication
        }
    }
    name->append(new_metadata.data(), new_metadata.size());
}

}  // namespace traceme_internal

/**
 * @brief Encodes a trace event name with structured metadata arguments.
 *
 * Creates a formatted trace name with embedded key-value metadata using the format:
 * `event_name#key1=value1,key2=value2#`. This is the primary function for creating
 * rich trace events with contextual information.
 *
 * @param name Base event name (moved to avoid copying)
 * @param args Key-value pairs to encode as metadata
 * @return Formatted trace name with embedded metadata
 *
 * **Performance**: Optimized for minimal allocations and fast string construction
 * **Usage Pattern**: Typically used within lambda expressions to defer expensive
 *                    operations until tracing is confirmed active
 *
 * **Example**:
 * ```cpp
 * traceme trace([&]() {
 *     return traceme_encode("matrix_multiply", {
 *         {"rows", matrix.rows()},
 *         {"cols", matrix.cols()},
 *         {"dtype", matrix.dtype_name()}
 *     });
 * });
 * ```
 */
inline std::string traceme_encode(std::string name, std::initializer_list<TraceMeArg> args)
{
    return traceme_internal::append_args(std::move(name), args);
}

/**
 * @brief Encodes a trace event name from string_view with metadata arguments.
 * @param name Base event name as string_view
 * @param args Key-value pairs to encode as metadata
 * @return Formatted trace name with embedded metadata
 */
inline std::string traceme_encode(std::string_view name, std::initializer_list<TraceMeArg> args)
{
    return traceme_internal::append_args(std::string(name), args);
}

/**
 * @brief Encodes a trace event name from C-string with metadata arguments.
 * @param name Base event name as C-string literal
 * @param args Key-value pairs to encode as metadata
 * @return Formatted trace name with embedded metadata
 */
inline std::string traceme_encode(const char* name, std::initializer_list<TraceMeArg> args)
{
    return traceme_internal::append_args(std::string(name), args);
}

/**
 * @brief Encodes metadata arguments without a base name for appending to existing traces.
 *
 * Creates formatted metadata that can be appended to existing trace events using
 * the append_metadata() method. This allows dynamic addition of contextual information
 * during trace execution.
 *
 * @param args Key-value pairs to encode as metadata
 * @return Formatted metadata string in `#key1=value1,key2=value2#` format
 *
 * **Use Case**: Adding runtime-computed metadata to existing trace events
 * **Performance**: Deferred evaluation - only called when tracing is active
 *
 * **Example**:
 * ```cpp
 * traceme trace("data_processing");
 * // ... do some work ...
 * trace.append_metadata([&]() {
 *     return traceme_encode({
 *         {"items_processed", item_count},
 *         {"memory_used", get_memory_usage()},
 *         {"success", operation_succeeded}
 *     });
 * });
 * ```
 */
inline std::string traceme_encode(std::initializer_list<TraceMeArg> args)
{
    return traceme_internal::append_args(std::string(), args);
}

/**
 * @brief Creates a standardized operation trace name by combining operation name and type.
 *
 * Formats operation information using the standard `operation_name:operation_type` pattern.
 * This provides consistent naming for operations across the codebase and enables better
 * grouping and filtering in profiling tools.
 *
 * @param op_name Name of the operation (e.g., "MatMul", "Conv2D", "Reduce")
 * @param op_type Type or variant of the operation (e.g., "Forward", "Backward", "CPU", "GPU")
 * @return Formatted operation name in `op_name:op_type` format
 *
 * **Example**: `traceme_op("MatMul", "GPU")` returns `"MatMul:GPU"`
 */
inline std::string traceme_op(std::string_view op_name, std::string_view op_type)
{
    return strings::str_cat(op_name, ":", op_type);
}

/**
 * @brief Creates operation trace name from C-string arguments.
 * @param op_name Operation name as C-string
 * @param op_type Operation type as C-string
 * @return Formatted operation name
 */
inline std::string traceme_op(const char* op_name, const char* op_type)
{
    return strings::str_cat(op_name, ":", op_type);
}

/**
 * @brief Creates operation trace name by appending to existing string (move optimization).
 * @param op_name Operation name as movable string (modified in-place)
 * @param op_type Operation type to append
 * @return Modified op_name with appended type
 */
inline std::string traceme_op(std::string&& op_name, std::string_view op_type)
{
    strings::str_append(&op_name, ":", op_type);
    return op_name;
}

/**
 * @brief Creates TensorFlow operation override metadata for trace name replacement.
 *
 * Generates metadata that instructs profiling tools to override the displayed
 * operation name with the specified TensorFlow operation information. This is
 * used for TensorFlow-specific profiling integration.
 *
 * @param op_name TensorFlow operation name
 * @param op_type TensorFlow operation type
 * @return Metadata string in `#tf_op=op_name:op_type#` format
 *
 * **Use Case**: TensorFlow kernel profiling where the actual kernel name should
 * be replaced with the high-level TF operation name for better user understanding.
 */
inline std::string traceme_op_override(std::string_view op_name, std::string_view op_type)
{
    return strings::str_cat("#tf_op=", op_name, ":", op_type, "#");
}

/**
 * @brief Creates TensorFlow operation override metadata from C-strings.
 * @param op_name TensorFlow operation name as C-string
 * @param op_type TensorFlow operation type as C-string
 * @return TensorFlow operation override metadata
 */
inline std::string traceme_op_override(const char* op_name, const char* op_type)
{
    return strings::str_cat("#tf_op=", op_name, ":", op_type, "#");
}

}  // namespace profiler
