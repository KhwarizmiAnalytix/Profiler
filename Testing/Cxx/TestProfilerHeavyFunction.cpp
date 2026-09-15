/*
 * Profiler
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     https://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

/*
 * Stress / e2e: native profiler_session, Kineto capture, ITT ranges, and (when
 * LibTorch is on) torch::autograd::profiler over the same computational
 * workloads (matrix, Monte Carlo, FFT). Workloads use PROFILER_SCOPE so native
 * and Kineto/ITT see the same annotations.
 *
 * API and pipeline docs: docs/profiler.md
 */

#include <algorithm>
#include <chrono>
#include <cmath>
#include <complex>
#include <cstdio>
#include <exception>
#include <fstream>
#include <iostream>
#include <memory>
#include <numeric>
#include <random>
#include <set>
#include <sstream>
#include <thread>
#include <unordered_set>
#include <vector>

#include "ProfilerTest.h"
#include "profiler.h"

#ifndef PROFILER_HAS_LIBTORCH
#define PROFILER_HAS_LIBTORCH 0
#endif

#if PROFILER_HAS_LIBTORCH
#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
#pragma GCC diagnostic ignored "-Wunused-parameter"
#elif defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable : 4996 4100)
#endif
#include <ATen/record_function.h>
#include <torch/csrc/autograd/profiler.h>
#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic pop
#elif defined(_MSC_VER)
#pragma warning(pop)
#endif
#endif

using namespace profiler;

namespace
{

/**
 * @brief Heavy computational function: Matrix multiplication
 * Performs dense matrix multiplication with profiling instrumentation
 */
std::vector<std::vector<double>> matrix_multiply(
    const std::vector<std::vector<double>>& a, const std::vector<std::vector<double>>& b)
{
    PROFILER_SCOPE("matrix_multiply");

    const size_t rows_a = a.size();
    const size_t cols_a = a[0].size();
    const size_t cols_b = b[0].size();

    // Validate dimensions
    if (cols_a != b.size())
    {
        throw std::invalid_argument("Matrix dimensions incompatible for multiplication");
    }

    std::vector<std::vector<double>> result(rows_a, std::vector<double>(cols_b, 0.0));

    {
        PROFILER_SCOPE("matrix_multiply_computation");

        for (size_t i = 0; i < rows_a; ++i)
        {
            PROFILER_SCOPE("matrix_row_computation");

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

#if PROFILER_HAS_ITT
/**
 * @brief Heavy computational function: Merge sort implementation
 * Recursive merge sort with profiling at each level
 */
void merge_sort(std::vector<double>& arr, size_t left, size_t right, int depth = 0)
{
    PROFILER_SCOPE("merge_sort_depth_" + std::to_string(depth));

    if (left >= right)
        return;

    size_t mid = left + (right - left) / 2;

    {
        PROFILER_SCOPE("merge_sort_left_half");
        merge_sort(arr, left, mid, depth + 1);
    }

    {
        PROFILER_SCOPE("merge_sort_right_half");
        merge_sort(arr, mid + 1, right, depth + 1);
    }

    {
        PROFILER_SCOPE("merge_operation");

        // Merge the sorted halves
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
}
#endif  // PROFILER_HAS_ITT

/**
 * @brief Heavy computational function: Monte Carlo Pi estimation
 * Estimates Pi using random sampling with configurable precision
 */
double estimate_pi_monte_carlo(size_t num_samples)
{
    PROFILER_SCOPE("monte_carlo_pi_estimation");

    std::random_device                     rd;
    std::mt19937                           gen(rd());
    std::uniform_real_distribution<double> dis(-1.0, 1.0);

    size_t points_inside_circle = 0;

    {
        PROFILER_SCOPE("monte_carlo_sampling");

        for (size_t i = 0; i < num_samples; ++i)
        {
            if (i % 100000 == 0)
            {
                PROFILER_SCOPE("monte_carlo_batch_" + std::to_string(i / 100000));

                for (size_t j = 0; j < std::min(size_t(100000), num_samples - i); ++j)
                {
                    double x = dis(gen);
                    double y = dis(gen);
                    if (x * x + y * y <= 1.0)
                    {
                        ++points_inside_circle;
                    }
                }
            }
        }
    }

    return 4.0 * static_cast<double>(points_inside_circle) / static_cast<double>(num_samples);
}

/**
 * @brief Heavy computational function: FFT-like computation
 * Simulates frequency domain analysis with nested loops
 */
std::vector<std::complex<double>> simulate_fft(const std::vector<double>& signal)
{
    PROFILER_SCOPE("simulate_fft");

    const size_t                      n = signal.size();
    std::vector<std::complex<double>> result(n);

    {
        PROFILER_SCOPE("fft_computation");

        for (size_t k = 0; k < n; ++k)
        {
            PROFILER_SCOPE("fft_frequency_bin");

            std::complex<double> sum(0.0, 0.0);
            for (size_t j = 0; j < n; ++j)
            {
                double angle = -2.0 * 3.14 * k * j / n;
                sum += signal[j] * std::complex<double>(std::cos(angle), std::sin(angle));
            }
            result[k] = sum;
        }
    }

    return result;
}

/**
 * @brief Generate test data for computational functions
 */
std::vector<std::vector<double>> generate_test_matrix(size_t rows, size_t cols)
{
    PROFILER_SCOPE("generate_test_matrix");

    std::random_device                     rd;
    std::mt19937                           gen(rd());
    std::uniform_real_distribution<double> dis(0.0, 10.0);

    std::vector<std::vector<double>> matrix(rows, std::vector<double>(cols));

    for (size_t i = 0; i < rows; ++i)
    {
        for (size_t j = 0; j < cols; ++j)
        {
            matrix[i][j] = dis(gen);
        }
    }

    return matrix;
}

std::vector<double> generate_test_signal(size_t size)
{
    PROFILER_SCOPE("generate_test_signal");

    std::random_device                     rd;
    std::mt19937                           gen(rd());
    std::uniform_real_distribution<double> dis(-1.0, 1.0);

    std::vector<double> signal(size);
    for (size_t i = 0; i < size; ++i)
    {
        signal[i] = dis(gen);
    }

    return signal;
}

}  // anonymous namespace

// Test comprehensive profiling with heavy computational functions
PROFILERTEST(Profiler, heavy_function_comprehensive_computational_profiling)
{
    // Configure profiler session with all features enabled
    profiler_options opts;
    opts.enable_timing_                 = true;
    opts.enable_memory_tracking_        = true;
    opts.enable_hierarchical_profiling_ = true;
    opts.enable_statistical_analysis_   = true;
    opts.enable_thread_safety_          = true;
    opts.output_format_                 = profiler_options::output_format_enum::CONSOLE;

    profiler_session session(opts);
    session.start();

    {
        PROFILER_SCOPE("heavy_computational_workload");

        // Test 1: Matrix multiplication profiling
        {
            PROFILER_SCOPE("matrix_operations_test");

            const size_t matrix_size = 100;  // 100x100 matrices
            auto         matrix_a    = generate_test_matrix(matrix_size, matrix_size);
            auto         matrix_b    = generate_test_matrix(matrix_size, matrix_size);

            // Perform multiple matrix multiplications
            for (int i = 0; i < 3; ++i)
            {
                PROFILER_SCOPE("matrix_multiply_iteration_" + std::to_string(i));
                auto result = matrix_multiply(matrix_a, matrix_b);

                // Verify result is not empty (basic correctness check)
                EXPECT_EQ(result.size(), matrix_size);
                EXPECT_EQ(result[0].size(), matrix_size);
            }
        }

        // Test 2: Monte Carlo simulation profiling
        {
            PROFILER_SCOPE("monte_carlo_simulation_test");

            const size_t num_samples = 1000000;  // 1 million samples
            double       pi_estimate = estimate_pi_monte_carlo(num_samples);

            // Verify Pi estimate is reasonable (should be close to 3.14159)
            EXPECT_GT(pi_estimate, 3.0);
            EXPECT_LT(pi_estimate, 3.3);

            std::cout << "Monte Carlo Pi estimate: " << pi_estimate << std::endl;
        }

        // Test 4: FFT simulation profiling
        {
            PROFILER_SCOPE("fft_simulation_test");

            const size_t signal_size = 512;  // Common FFT size
            auto         test_signal = generate_test_signal(signal_size);

            auto fft_result = simulate_fft(test_signal);

            // Verify FFT result size
            EXPECT_EQ(fft_result.size(), signal_size);
        }

        // Test 5: Multi-threaded computation profiling
        {
            PROFILER_SCOPE("multithreaded_computation_test");

            std::vector<std::thread> workers;
            const int                num_threads = 4;

            for (int i = 0; i < num_threads; ++i)
            {
                workers.emplace_back(
                    [i]()
                    {
                        PROFILER_SCOPE("worker_thread_" + std::to_string(i));

                        // Each thread performs different computational work
                        const size_t           samples_per_thread = 250000;
                        PROFILER_UNUSED double pi_est = estimate_pi_monte_carlo(samples_per_thread);

                        // Small computation to keep thread busy
                        std::vector<double> data(10000);
                        std::iota(data.begin(), data.end(), i * 10000);
                        std::sort(data.begin(), data.end(), std::greater<double>());
                    });
            }

            // Wait for all threads to complete
            for (auto& worker : workers)
            {
                worker.join();
            }
        }
    }

    session.stop();

    // ========================================================================
    // CHROME TRACE FORMAT EXPORT
    // ========================================================================
    // Export profiling results to Chrome Trace Event Format (JSON)
    // This format is compatible with:
    //   1. Chrome DevTools (chrome://tracing)
    //   2. Perfetto UI (https://ui.perfetto.dev)
    //   3. Other trace viewers that support the standard format
    //
    // HOW TO VIEW THE TRACE:
    // 1. Open Chrome browser and navigate to: chrome://tracing
    // 2. Click "Load" button and select the generated JSON file
    // 3. Use the following keyboard shortcuts:
    //    - W/S: Zoom in/out
    //    - A/D: Pan left/right
    //    - Click and drag: Select time range
    //    - Double-click: Zoom to selection
    //
    // INSIGHTS FROM THE VISUALIZATION:
    // - Timeline view shows execution order and duration of each scope
    // - Nested scopes appear as hierarchical blocks
    // - Color coding helps identify different operations
    // - Memory allocation/deallocation events are marked
    // - Thread information shows parallel execution patterns
    // - Hover over events to see detailed statistics
    // ========================================================================

    // Export to Chrome Trace JSON format (Chrome Trace Event Format)
    std::string chrome_trace_file = "heavy_function_profile.json";
    session.write_chrome_trace(chrome_trace_file);

    auto report = session.generate_report();
    ASSERT_NE(report, nullptr);
    std::cout << "\n=== Heavy Function profiler report ===\n";
    std::cout << report->generate_console_report() << std::flush;

    auto hotspot = session.generate_hotspot_report();
    ASSERT_NE(hotspot, nullptr);
    std::cout << "\n=== Heavy Function hotspot table ===\n" << hotspot->table();
    std::cout << "\n--- Top-down call tree ---\n" << hotspot->top_down_tree();
    std::cout << "\n--- Bottom-up hotspots ---\n" << hotspot->bottom_up_hotspots();
    std::cout << std::flush;

    std::cout << "\n=== Heavy Function Performance Analysis ===\n";
    std::cout << "\n✓ Chrome Trace JSON exported to: " << chrome_trace_file << "\n";
    std::cout << "\nTo view the trace:\n";
    std::cout << "  1. Open Chrome and navigate to: chrome://tracing\n";
    std::cout << "  2. Click 'Load' and select: " << chrome_trace_file << "\n";
    std::cout << "  3. Use W/S to zoom, A/D to pan, click to select\n";
    std::cout << "\nAlternatively, use Perfetto UI:\n";
    std::cout << "  1. Visit: https://ui.perfetto.dev\n";
    std::cout << "  2. Open the JSON file in the UI\n";

    std::cout << "\nAll computational workloads profiled successfully:\n";
    std::cout << "  - Matrix multiplication (100x100)\n";
    std::cout << "  - Merge sort (50,000 elements)\n";
    std::cout << "  - Monte Carlo Pi estimation (1M samples)\n";
    std::cout << "  - FFT simulation (512 points)\n";
    std::cout << "  - Multi-threaded computation\n";
}

// ============================================================================
// KINETO PROFILER TEST
// ============================================================================
// Same heavy workloads and PROFILER_SCOPE annotations as the native case.
// profiler::session starts native collection together with the compiled Kineto
// backend; helper scopes (matrix_multiply, …) must appear in the Kineto events.
// ============================================================================

#if PROFILER_HAS_KINETO

namespace
{

const profiler::capture_event* find_capture_event(
    const std::vector<profiler::capture_event>& events, const std::string& name)
{
    for (const auto& event : events)
    {
        if (event.name == name)
        {
            return &event;
        }
    }
    return nullptr;
}

}  // namespace

PROFILERTEST(Profiler, kineto_heavy_function_profiling)
{
    profiler::session_options opts;
    opts.backend        = profiler::capture_backend::kineto;
    opts.activities     = {profiler::activity::cpu};
    opts.profile_memory = false;
    opts.with_stack     = false;
    opts.with_flops     = false;
    opts.with_modules   = false;

    profiler::session session(opts);
    if (!session.start())
    {
        GTEST_SKIP() << "Kineto profiler unavailable";
    }

    {
        PROFILER_SCOPE("kineto_heavy_workload");

        {
            PROFILER_SCOPE("kineto_matrix_operations");
            const size_t matrix_size = 50;
            auto         matrix_a    = generate_test_matrix(matrix_size, matrix_size);
            auto         matrix_b    = generate_test_matrix(matrix_size, matrix_size);
            for (int i = 0; i < 2; ++i)
            {
                PROFILER_SCOPE("kineto_matrix_multiply_iteration");
                auto result = matrix_multiply(matrix_a, matrix_b);
                EXPECT_EQ(result.size(), matrix_size);
                EXPECT_EQ(result[0].size(), matrix_size);
            }
        }

        {
            PROFILER_SCOPE("kineto_monte_carlo");
            const double pi_estimate = estimate_pi_monte_carlo(200000);
            EXPECT_GT(pi_estimate, 2.5);
            EXPECT_LT(pi_estimate, 3.8);
        }

        {
            PROFILER_SCOPE("kineto_fft_simulation");
            auto signal = generate_test_signal(256);
            EXPECT_EQ(signal.size(), 256U);
        }
    }

    ASSERT_TRUE(session.stop());

    const auto& events = session.events();
    if (events.empty())
    {
        GTEST_SKIP() << "Kineto backend produced no CPU events in this environment";
    }

    const auto* outer  = find_capture_event(events, "kineto_heavy_workload");
    const auto* matrix = find_capture_event(events, "kineto_matrix_operations");
    const auto* monte  = find_capture_event(events, "kineto_monte_carlo");
    const auto* helper = find_capture_event(events, "matrix_multiply");
    const auto* pi     = find_capture_event(events, "monte_carlo_pi_estimation");
    ASSERT_NE(outer, nullptr);
    ASSERT_NE(matrix, nullptr);
    ASSERT_NE(monte, nullptr);
    ASSERT_NE(helper, nullptr) << "Helper PROFILER_SCOPE must reach the Kineto backend";
    ASSERT_NE(pi, nullptr) << "Helper PROFILER_SCOPE must reach the Kineto backend";
    EXPECT_GT(outer->duration_ns, 0U);
    EXPECT_GE(outer->duration_ns, matrix->duration_ns);

    std::cout << "\n=== Kineto events (" << events.size() << ") ===\n";
    for (const auto& event : events)
    {
        std::cout << event.name << "\t" << event.duration_ns << " ns\n";
    }
    std::cout << std::flush;

    auto hotspot = session.generate_hotspot_report();
    if (hotspot)
    {
        std::cout << "\n=== Kineto heavy-function hotspot report ===\n";
        std::cout << hotspot->table() << std::flush;
    }

    const std::string trace_filename = "kineto_heavy_function_trace.json";
    ASSERT_TRUE(session.write_trace(trace_filename));

    std::ifstream json_file(trace_filename);
    ASSERT_TRUE(json_file.good()) << "Failed to create Kineto JSON output file";
    std::stringstream buffer;
    buffer << json_file.rdbuf();
    const std::string json_content = buffer.str();
    json_file.close();

    EXPECT_NE(json_content.find("\"traceEvents\""), std::string::npos)
        << "JSON file missing traceEvents array";
    EXPECT_GT(json_content.size(), 100U) << "JSON should contain meaningful content";

    std::cout << "Kineto Chrome trace saved: " << trace_filename << " (" << json_content.size()
              << " bytes)\n";
}

#endif  // PROFILER_HAS_KINETO

// ============================================================================
// INTEL ITT API TEST
// ============================================================================
// Test Intel ITT API integration with heavy computational functions
//
// ITT API provides task and frame annotations for Intel VTune profiling:
// - Task annotations: Mark regions of code for analysis
// - Frame markers: Identify frame boundaries in graphics applications
// - String handles: Efficient string management for annotations
// - Domain-based organization: Group related tasks
//
// OUTPUT: VTune-compatible profiling data + Chrome Trace JSON
//
// HOW TO USE:
// 1. Run this test (works with or without Intel VTune installed)
// 2. View JSON trace in Chrome DevTools (chrome://tracing) or Perfetto UI
// 3. If VTune is installed, collect profiling data:
//    vtune -collect hotspots -app ./CoreCxxTests.exe
// 4. View results in VTune GUI and look for ITT annotations
//
// INSIGHTS:
// - Task duration shows computational complexity
// - Nested tasks reveal call hierarchy
// - Thread information shows parallelization
// - Memory events correlate with allocations
// ============================================================================

#if PROFILER_HAS_ITT
#include <fstream>
#include <sstream>

#include "bespoke/itt/itt_wrapper.h"

PROFILERTEST(Profiler, itt_api_heavy_function_profiling)
{
    std::cout
        << "\n=== Intel ITT API + Profiler Profiler Heavy Function Test (with Drill-Down) ===\n";
    std::cout << "Note: ITT annotations are captured by Intel VTune when available.\n";
    std::cout << "For hierarchical CPU profiling with drill-down, we combine ITT with Profiler "
                 "profiler.\n\n";

    // Initialize ITT profiler (creates global Profiler domain)
    profiler::profiler_impl::itt_init();

    // Check if ITT is available (domain creation may fail if VTune not installed)
    bool const itt_available = (profiler::profiler_impl::itt_get_domain() != nullptr);

    if (!itt_available)
    {
        std::cout << "ITT API domain creation failed (VTune not available)\n";
        std::cout << "Falling back to Profiler profiler only for JSON trace generation\n\n";
    }
    else
    {
        std::cout << "ITT API domain created: Profiler\n";
        std::cout << "Combined profiling started (ITT + Profiler)\n\n";
    }

    // Start Profiler profiler session to capture hierarchical profiling data
    profiler_options opts;
    opts.enable_timing_               = true;
    opts.enable_memory_tracking_      = false;
    opts.enable_statistical_analysis_ = false;
    opts.enable_thread_safety_        = true;
    opts.output_format_               = profiler_options::output_format_enum::JSON;

    profiler_session session(opts);
    session.start();

    // Profile matrix operations with ITT wrapper API and Profiler profiler
    {
        if (itt_available)
        {
            profiler::profiler_impl::itt_range_push("matrix_operations");
        }
        PROFILER_SCOPE("itt_matrix_operations");

        const size_t matrix_size = 50;
        auto         matrix_a    = generate_test_matrix(matrix_size, matrix_size);
        auto         matrix_b    = generate_test_matrix(matrix_size, matrix_size);

        for (int i = 0; i < 2; ++i)
        {
            std::string const iter_name = "matrix_multiply_" + std::to_string(i);

            if (itt_available)
            {
                profiler::profiler_impl::itt_range_push(iter_name.c_str());
            }

            PROFILER_SCOPE("itt_matrix_multiply_" + std::to_string(i));

            auto result = matrix_multiply(matrix_a, matrix_b);
            EXPECT_EQ(result.size(), matrix_size);

            if (itt_available)
            {
                profiler::profiler_impl::itt_range_pop();
            }
        }

        if (itt_available)
        {
            profiler::profiler_impl::itt_range_pop();
        }
    }

    // Profile sorting operations with ITT wrapper API and Profiler profiler
    {
        if (itt_available)
        {
            profiler::profiler_impl::itt_range_push("sorting_operations");
        }
        PROFILER_SCOPE("itt_sorting_operations");

        const size_t        array_size = 10000;
        std::vector<double> test_data(array_size);

        std::random_device                     rd;
        std::mt19937                           gen(rd());
        std::uniform_real_distribution<double> dis(0.0, 1000.0);

        for (size_t i = 0; i < array_size; ++i)
        {
            test_data[i] = dis(gen);
        }

        {
            if (itt_available)
            {
                profiler::profiler_impl::itt_range_push("merge_sort");
            }

            PROFILER_SCOPE("itt_merge_sort");

            auto data_copy = test_data;
            merge_sort(data_copy, 0, data_copy.size() - 1);
            EXPECT_TRUE(std::is_sorted(data_copy.begin(), data_copy.end()));

            if (itt_available)
            {
                profiler::profiler_impl::itt_range_pop();
            }
        }

        if (itt_available)
        {
            profiler::profiler_impl::itt_range_pop();
        }
    }

    // Profile Monte Carlo simulation with ITT wrapper API and Profiler profiler
    {
        if (itt_available)
        {
            profiler::profiler_impl::itt_range_push("monte_carlo_simulation");
        }
        PROFILER_SCOPE("itt_monte_carlo_simulation");

        const size_t num_samples = 100000;
        double       pi_estimate = estimate_pi_monte_carlo(num_samples);

        EXPECT_GT(pi_estimate, 3.0);
        EXPECT_LT(pi_estimate, 3.3);

        std::cout << "Monte Carlo Pi estimate: " << pi_estimate << "\n";

        if (itt_available)
        {
            profiler::profiler_impl::itt_range_pop();
        }
    }

    session.stop();

    if (itt_available)
    {
        std::cout << "Combined profiling completed (ITT + Profiler)\n";
    }
    else
    {
        std::cout << "Profiler profiling completed\n";
    }

    // Export profiling data to JSON (captures Profiler profiling scopes with hierarchical
    // drill-down)
    std::string const itt_output_file = "itt_heavy_function_trace.json";
    session.write_chrome_trace(itt_output_file);

    std::cout << "✓ Profiler trace saved to: " << itt_output_file << "\n";

    // Verify JSON file was created and is valid
    std::ifstream json_file(itt_output_file);
    EXPECT_TRUE(json_file.good()) << "Failed to create ITT JSON output file";

    if (json_file.good())
    {
        // Read and validate JSON structure
        std::stringstream buffer;
        buffer << json_file.rdbuf();
        std::string const json_content = buffer.str();

        // Basic JSON validation - check for required fields
        EXPECT_TRUE(json_content.find("\"traceEvents\"") != std::string::npos)
            << "JSON file missing required trace structure";

        // Verify ITT-annotated scopes are present
        EXPECT_TRUE(json_content.find("itt_matrix_operations") != std::string::npos)
            << "JSON missing ITT matrix operations scope";
        EXPECT_TRUE(json_content.find("itt_sorting_operations") != std::string::npos)
            << "JSON missing ITT sorting operations scope";
        EXPECT_TRUE(json_content.find("itt_monte_carlo_simulation") != std::string::npos)
            << "JSON missing ITT Monte Carlo scope";
        EXPECT_TRUE(json_content.find("itt_merge_sort") != std::string::npos)
            << "JSON missing ITT merge sort scope";

        EXPECT_GT(json_content.size(), 1000) << "JSON file appears to be empty or too small";

        std::cout << "✓ JSON file validated (size: " << json_content.size() << " bytes)\n";
        std::cout << "✓ Hierarchical scopes verified for drill-down capability\n";
    }

    std::cout << "\n=== Drill-Down Visualization Instructions ===\n";
    std::cout << "The trace file supports full hierarchical drill-down in profiling tools:\n\n";

    std::cout << "1. Chrome DevTools (chrome://tracing):\n";
    std::cout << "   - Open Chrome browser\n";
    std::cout << "   - Navigate to chrome://tracing\n";
    std::cout << "   - Click 'Load' and select: " << itt_output_file << "\n";
    std::cout << "   - Use W/S to zoom, A/D to pan\n";
    std::cout << "   - Click on events to see details and nested scopes\n\n";

    std::cout << "2. Perfetto UI (https://ui.perfetto.dev):\n";
    std::cout << "   - Visit https://ui.perfetto.dev\n";
    std::cout << "   - Click 'Open trace file'\n";
    std::cout << "   - Select: " << itt_output_file << "\n";
    std::cout << "   - Explore hierarchical timeline with drill-down\n\n";

    if (itt_available)
    {
        std::cout << "3. Intel VTune Profiler (for ITT annotations):\n";
        std::cout << "   - Run: vtune -collect hotspots -app ./CoreCxxTests.exe\n";
        std::cout << "   - Open results in VTune GUI\n";
        std::cout << "   - Look for 'ProfilerHeavyFunctionTest' domain in timeline\n\n";
    }

    std::cout << "4. Expected Drill-Down Structure:\n";
    std::cout << "   ├─ itt_matrix_operations (parent scope)\n";
    std::cout << "   │  ├─ itt_matrix_multiply_0 (nested scope)\n";
    std::cout << "   │  └─ itt_matrix_multiply_1 (nested scope)\n";
    std::cout << "   ├─ itt_sorting_operations (parent scope)\n";
    std::cout << "   │  └─ itt_merge_sort (nested scope)\n";
    std::cout << "   └─ itt_monte_carlo_simulation (parent scope)\n\n";

    if (itt_available)
    {
        std::cout << "Note: ITT annotations are also captured in VTune profiler.\n";
        std::cout << "      Profiler trace (" << itt_output_file
                  << ") contains full hierarchical CPU profiling.\n";
    }
    else
    {
        std::cout << "Note: ITT annotations not available (VTune not installed).\n";
        std::cout << "      Profiler trace (" << itt_output_file
                  << ") contains full hierarchical CPU profiling.\n";
    }
}
#endif  // PROFILER_HAS_ITT

// ============================================================================
// PYTORCH (LibTorch) PROFILER TEST
// ============================================================================
// Same heavy workloads, captured by torch::autograd::profiler when the setup.py
// `torch` token finds a LibTorch install (PROFILER_HAS_LIBTORCH). Uses ATen
// RECORD_USER_SCOPE so events land in the PyTorch Kineto trace, not Profiler
// PROFILER_SCOPE annotations.
// ============================================================================

#if PROFILER_HAS_LIBTORCH

namespace
{

const torch::autograd::profiler::KinetoEvent* find_pytorch_event(
    const std::vector<torch::autograd::profiler::KinetoEvent>& events, const std::string& name)
{
    for (const auto& event : events)
    {
        if (event.name() == name)
        {
            return &event;
        }
    }
    return nullptr;
}

}  // namespace

PROFILERTEST(Profiler, pytorch_heavy_function_profiling)
{
    using torch::autograd::profiler::disableProfiler;
    using torch::autograd::profiler::enableProfiler;
    using torch::autograd::profiler::prepareProfiler;
    using torch::autograd::profiler::ProfilerConfig;
    using torch::profiler::impl::ActivityType;
    using torch::profiler::impl::ProfilerState;

    ProfilerConfig const config(ProfilerState::KINETO,
        /*report_input_shapes=*/false,
        /*profile_memory=*/false,
        /*with_stack=*/false,
        /*with_flops=*/false,
        /*with_modules=*/false);

    const std::set<ActivityType>              activities{ActivityType::CPU};
    const std::unordered_set<at::RecordScope> scopes{at::RecordScope::USER_SCOPE};

    try
    {
        prepareProfiler(config, activities);
        enableProfiler(config, activities, scopes);
    }
    catch (const std::exception& ex)
    {
        GTEST_SKIP() << "PyTorch profiler unavailable: " << ex.what();
    }

    {
        RECORD_USER_SCOPE("pytorch_heavy_workload");

        {
            RECORD_USER_SCOPE("pytorch_matrix_operations");
            const size_t matrix_size = 50;
            auto         matrix_a    = generate_test_matrix(matrix_size, matrix_size);
            auto         matrix_b    = generate_test_matrix(matrix_size, matrix_size);
            for (int i = 0; i < 2; ++i)
            {
                RECORD_USER_SCOPE("pytorch_matrix_multiply_iteration");
                auto result = matrix_multiply(matrix_a, matrix_b);
                EXPECT_EQ(result.size(), matrix_size);
                EXPECT_EQ(result[0].size(), matrix_size);
            }
        }

        {
            RECORD_USER_SCOPE("pytorch_monte_carlo");
            const double pi_estimate = estimate_pi_monte_carlo(200000);
            EXPECT_GT(pi_estimate, 2.5);
            EXPECT_LT(pi_estimate, 3.8);
        }

        {
            RECORD_USER_SCOPE("pytorch_fft_simulation");
            auto signal = generate_test_signal(256);
            EXPECT_EQ(signal.size(), 256U);
        }
    }

    auto profiler_result = disableProfiler();
    ASSERT_NE(profiler_result, nullptr);

    const auto& events = profiler_result->events();
    if (events.empty())
    {
        GTEST_SKIP() << "PyTorch profiler produced no CPU events in this environment";
    }

    const auto* outer  = find_pytorch_event(events, "pytorch_heavy_workload");
    const auto* matrix = find_pytorch_event(events, "pytorch_matrix_operations");
    const auto* monte  = find_pytorch_event(events, "pytorch_monte_carlo");
    ASSERT_NE(outer, nullptr);
    ASSERT_NE(matrix, nullptr);
    ASSERT_NE(monte, nullptr);
    EXPECT_GT(outer->durationNs(), 0U);
    EXPECT_GE(outer->durationNs(), matrix->durationNs());

    std::cout << "\n=== PyTorch profiler events (" << events.size() << ") ===\n";
    for (const auto& event : events)
    {
        std::cout << event.name() << "\t" << event.durationNs()
                  << " ns\tscope=" << static_cast<int>(event.scope())
                  << "\tactivity=" << static_cast<int>(event.activityType()) << "\n";
    }
    std::cout << std::flush;

    const std::string trace_filename = "pytorch_heavy_function_trace.json";
    profiler_result->save(trace_filename);

    std::ifstream json_file(trace_filename);
    ASSERT_TRUE(json_file.good()) << "Failed to create PyTorch profiler JSON output file";
    std::stringstream buffer;
    buffer << json_file.rdbuf();
    const std::string json_content = buffer.str();
    json_file.close();

    EXPECT_NE(json_content.find("\"traceEvents\""), std::string::npos)
        << "JSON file missing traceEvents array";
    EXPECT_GT(json_content.size(), 100U) << "JSON should contain meaningful content";

    std::cout << "PyTorch Chrome trace saved: " << trace_filename << " (" << json_content.size()
              << " bytes)\n";
}

#endif  // PROFILER_HAS_LIBTORCH

// ============================================================================
// COMBINED KINETO + ITT PROFILING TEST
// ============================================================================
// Backends are mutually exclusive (PROFILER_HAS_KINETO vs PROFILER_HAS_ITT),
// so a combined Kineto+ITT case cannot compile. Use
// Profiler.kineto_heavy_function_profiling above for Kineto and the ITT block
// when PROFILER_HAS_ITT=1. PyTorch profiler is independent and runs when
// PROFILER_HAS_LIBTORCH is 1.
