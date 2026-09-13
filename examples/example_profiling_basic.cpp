/**
 * @file example_profiling_basic.cpp
 * @brief Comprehensive example demonstrating XSigma's profiling systems.
 *
 * This example shows:
 * - XSigma native profiler with hierarchical CPU profiling
 * - Kineto profiler for GPU-related CPU operations
 * - ITT profiler for Intel VTune integration
 * - Combined profiling with multiple systems
 * - Graceful degradation when profilers are unavailable
 * - Best practices for profiling instrumentation
 */

#include <algorithm>
#include <chrono>
#include <cmath>
#include <iostream>
#include <random>
#include <vector>

#include "native/session/profiler.h"

#if PROFILER_HAS_KINETO
#include <set>
#include <unordered_set>

#include "bespoke/common/record_function.h"
#include "bespoke/kineto/profiler_kineto.h"
#endif

#if PROFILER_HAS_ITT
#include "bespoke/itt/itt_wrapper.h"
#endif

namespace xsigma::examples::profiling
{
using profiler::profiler_options;
using profiler::profiler_session;

// ============================================================================
// Helper Functions - Computational Workloads
// ============================================================================

/**
 * @brief Matrix multiplication with profiling instrumentation.
 */
std::vector<std::vector<double>> matrix_multiply(
    const std::vector<std::vector<double>>& a, const std::vector<std::vector<double>>& b)
{
    PROFILER_PROFILE_SCOPE("matrix_multiply");

    const size_t rows_a = a.size();
    const size_t cols_a = a[0].size();
    const size_t cols_b = b[0].size();

    std::vector<std::vector<double>> result(rows_a, std::vector<double>(cols_b, 0.0));

    {
        PROFILER_PROFILE_SCOPE("matrix_multiply_computation");

        for (size_t i = 0; i < rows_a; ++i)
        {
            for (size_t j = 0; j < cols_b; ++j)
            {
                for (size_t k = 0; k < cols_a; ++k)
                {
                    result[i][j] += a[i][k] * b[k][j];
                }
            }
        }
    }

    return result;
}

/**
 * @brief Generate a random matrix for testing.
 */
std::vector<std::vector<double>> generate_matrix(size_t rows, size_t cols)
{
    PROFILER_PROFILE_SCOPE("generate_matrix");

    std::vector<std::vector<double>> matrix(rows, std::vector<double>(cols));

    std::random_device                     rd;
    std::mt19937                           gen(rd());
    std::uniform_real_distribution<double> dis(0.0, 1.0);

    for (size_t i = 0; i < rows; ++i)
    {
        for (size_t j = 0; j < cols; ++j)
        {
            matrix[i][j] = dis(gen);
        }
    }

    return matrix;
}

/**
 * @brief Sorting algorithm with profiling.
 */
void merge_sort(std::vector<double>& arr, size_t left, size_t right)
{
    PROFILER_PROFILE_SCOPE("merge_sort");

    if (left >= right)
        return;

    size_t mid = left + (right - left) / 2;

    merge_sort(arr, left, mid);
    merge_sort(arr, mid + 1, right);

    // Merge step
    std::vector<double> temp(right - left + 1);
    size_t              i = left, j = mid + 1, k = 0;

    while (i <= mid && j <= right)
    {
        if (arr[i] <= arr[j])
        {
            temp[k++] = arr[i++];
        }
        else
        {
            temp[k++] = arr[j++];
        }
    }

    while (i <= mid)
        temp[k++] = arr[i++];
    while (j <= right)
        temp[k++] = arr[j++];

    for (size_t idx = 0; idx < temp.size(); ++idx)
    {
        arr[left + idx] = temp[idx];
    }
}

// ============================================================================
// Example 1: XSigma Native Profiler
// ============================================================================

/**
 * @brief Demonstrates XSigma's native profiler with hierarchical CPU profiling.
 *
 * The native profiler provides:
 * - Hierarchical scope tracking with PROFILER_PROFILE_SCOPE()
 * - Chrome Trace JSON export for visualization
 * - Full drill-down capability in Chrome DevTools and Perfetto UI
 */
void example_xsigma_native_profiler()
{
    std::cout << "\n=== Example 1: XSigma Native Profiler ===" << std::endl;

    // Configure profiler options
    profiler_options opts;
    opts.enable_timing_               = true;   // Enable timing measurements
    opts.enable_memory_tracking_      = false;  // Disable memory tracking for this example
    opts.enable_statistical_analysis_ = false;  // Disable statistics
    opts.enable_thread_safety_        = true;   // Thread-safe operations
    opts.output_format_               = profiler_options::output_format_enum::JSON;

    // Create and start profiler session
    profiler_session session(opts);
    session.start();

    std::cout << "✓ XSigma profiler started" << std::endl;

    // Profile matrix operations
    {
        PROFILER_PROFILE_SCOPE("matrix_operations");

        const size_t matrix_size = 100;
        auto         matrix_a    = generate_matrix(matrix_size, matrix_size);
        auto         matrix_b    = generate_matrix(matrix_size, matrix_size);

        auto result = matrix_multiply(matrix_a, matrix_b);

        std::cout << "  Matrix multiplication completed (" << matrix_size << "x" << matrix_size
                  << ")" << std::endl;
    }

    // Profile sorting operations
    {
        PROFILER_PROFILE_SCOPE("sorting_operations");

        const size_t        array_size = 10000;
        std::vector<double> test_data(array_size);

        std::random_device                     rd;
        std::mt19937                           gen(rd());
        std::uniform_real_distribution<double> dis(0.0, 1000.0);

        for (size_t i = 0; i < array_size; ++i)
        {
            test_data[i] = dis(gen);
        }

        merge_sort(test_data, 0, test_data.size() - 1);

        std::cout << "  Sorting completed (" << array_size << " elements)" << std::endl;
    }

    // Stop profiling
    session.stop();

    // Export Chrome Trace JSON
    std::string const output_file = "xsigma_native_profile.json";
    session.write_chrome_trace(output_file);

    std::cout << "✓ XSigma profiler stopped" << std::endl;
    std::cout << "✓ Trace saved to: " << output_file << std::endl;
    std::cout << "\nVisualization:" << std::endl;
    std::cout << "  1. Chrome DevTools: chrome://tracing" << std::endl;
    std::cout << "  2. Perfetto UI: https://ui.perfetto.dev" << std::endl;
}

// ============================================================================
// Example 2: Kineto Profiler
// ============================================================================

#if PROFILER_HAS_KINETO

/**
 * @brief Demonstrates Kineto profiler combined with XSigma profiler.
 *
 * Kineto captures GPU-related CPU operations. For hierarchical CPU profiling,
 * we combine it with XSigma's native profiler.
 */
void example_kineto_profiler()
{
    std::cout << "\n=== Example 2: Kineto Profiler ===" << std::endl;

    profiler::profiler_impl::ProfilerConfig const config(
        profiler::profiler_impl::ProfilerState::KINETO,
        /*report_input_shapes=*/false,
        /*profile_memory=*/false,
        /*with_stack=*/false,
        /*with_flops=*/false,
        /*with_modules=*/false);

    const std::set<profiler::profiler_impl::ActivityType> activities{
        profiler::profiler_impl::ActivityType::CPU};
    const std::unordered_set<profiler::RecordScope> scopes{profiler::RecordScope::USER_SCOPE};

    profiler::profiler_impl::prepareProfiler(config, activities);
    profiler::profiler_impl::enableProfiler(config, activities, scopes);

    std::cout << "✓ Kineto profiler started" << std::endl;

    {
        PROFILER_RECORD_USER_SCOPE("kineto_workload");

        const size_t matrix_size = 80;
        auto         matrix_a    = generate_matrix(matrix_size, matrix_size);
        auto         matrix_b    = generate_matrix(matrix_size, matrix_size);

        auto result = matrix_multiply(matrix_a, matrix_b);
        (void)result;

        std::cout << "  Workload completed" << std::endl;
    }

    auto              kineto_result = profiler::profiler_impl::disableProfiler();
    std::string const kineto_file   = "kineto_only_trace.json";
    if (kineto_result)
    {
        kineto_result->save(kineto_file);
        std::cout << "✓ Kineto trace saved to: " << kineto_file << std::endl;
    }
    else
    {
        std::cout << "✗ Kineto produced no result" << std::endl;
    }
}

#endif  // PROFILER_HAS_KINETO

// ============================================================================
// Example 3: ITT Profiler
// ============================================================================

#if PROFILER_HAS_ITT

/**
 * @brief Demonstrates ITT profiler combined with XSigma profiler.
 *
 * ITT provides annotations for Intel VTune Profiler. We combine it with
 * XSigma's profiler for JSON export and graceful degradation.
 */
void example_itt_profiler()
{
    std::cout << "\n=== Example 3: ITT Profiler ===" << std::endl;

    // Initialize ITT profiler
    profiler::profiler_impl::itt_init();

    // Check if ITT is available
    bool const itt_available = (profiler::profiler_impl::itt_get_domain() != nullptr);

    if (!itt_available)
    {
        std::cout << "✗ ITT not available (VTune not installed)" << std::endl;
        std::cout << "  Falling back to XSigma profiler only" << std::endl;
    }
    else
    {
        std::cout << "✓ ITT profiler initialized (domain: XSigma)" << std::endl;
    }

    // Start XSigma profiler for JSON export
    profiler_options opts;
    opts.enable_timing_ = true;
    opts.output_format_ = profiler_options::output_format_enum::JSON;

    profiler_session session(opts);
    session.start();

    std::cout << "✓ Profiling started" << std::endl;

    // Profile with both ITT and XSigma
    {
        if (itt_available)
        {
            profiler::profiler_impl::itt_range_push("itt_workload");
        }
        PROFILER_PROFILE_SCOPE("itt_workload");

        const size_t matrix_size = 60;
        auto         matrix_a    = generate_matrix(matrix_size, matrix_size);
        auto         matrix_b    = generate_matrix(matrix_size, matrix_size);

        {
            if (itt_available)
            {
                profiler::profiler_impl::itt_range_push("matrix_computation");
            }
            PROFILER_PROFILE_SCOPE("matrix_computation");

            auto result = matrix_multiply(matrix_a, matrix_b);

            if (itt_available)
            {
                profiler::profiler_impl::itt_range_pop();
            }
        }

        std::cout << "  Workload completed" << std::endl;

        if (itt_available)
        {
            profiler::profiler_impl::itt_range_pop();
        }
    }

    // Stop profiling
    session.stop();

    // Export XSigma trace
    std::string const output_file = "itt_xsigma_trace.json";
    session.write_chrome_trace(output_file);

    std::cout << "✓ Profiling stopped" << std::endl;
    std::cout << "✓ XSigma trace saved to: " << output_file << std::endl;

    if (itt_available)
    {
        std::cout << "\nVTune Integration:" << std::endl;
        std::cout << "  Run with VTune: vtune -collect hotspots -app ./example_profiling_basic"
                  << std::endl;
        std::cout << "  View results: vtune-gui" << std::endl;
    }
}

#endif  // PROFILER_HAS_ITT

}  // namespace xsigma::examples::profiling

// ============================================================================
// Main Function
// ============================================================================

int main()
{
    std::cout << "============================================" << std::endl;
    std::cout << "XSigma Profiling Examples" << std::endl;
    std::cout << "============================================" << std::endl;

    // Example 1: XSigma Native Profiler
    xsigma::examples::profiling::example_xsigma_native_profiler();

#if PROFILER_HAS_KINETO
    // Example 2: Kineto Profiler
    xsigma::examples::profiling::example_kineto_profiler();
#else
    std::cout << "\n=== Example 2: Kineto Profiler ===" << std::endl;
    std::cout << "✗ Kineto not available (PROFILER_HAS_KINETO=0)" << std::endl;
#endif

#if PROFILER_HAS_ITT
    // Example 3: ITT Profiler
    xsigma::examples::profiling::example_itt_profiler();
#else
    std::cout << "\n=== Example 3: ITT Profiler ===" << std::endl;
    std::cout << "✗ ITT not available (PROFILER_HAS_ITT=0)" << std::endl;
#endif

    std::cout << "\n============================================" << std::endl;
    std::cout << "All examples completed!" << std::endl;
    std::cout << "============================================" << std::endl;

    return 0;
}
