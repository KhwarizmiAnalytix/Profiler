// Profile several worker threads concurrently and read back per-thread
// attribution plus native memory tracking -- the two capabilities
// example_quickstart.cpp and example_reports.cpp don't demonstrate.
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <thread>
#include <vector>

#include "profiler.h"

namespace
{
constexpr int kThreadCount     = 4;
constexpr int kIterationsEach  = 8;
constexpr int kAllocationBytes = 64 * 1024;

// Each thread's own nested scopes; PROFILER_SCOPE/PROFILER_FUNCTION are
// thread-safe and require no coordination between callers.
//
// memory_tracker doesn't hook the global allocator -- it only knows about
// allocations reported to it explicitly via track_allocation()/
// track_deallocation() (see native/memory/memory_tracker.h). Passing the
// tracker in and reporting our own std::malloc/std::free calls is the
// intended way to make an application's allocations visible to it.
void worker(int thread_index, profiler::memory_tracker* tracker)
{
    PROFILER_FUNCTION();
    for (int i = 0; i < kIterationsEach; ++i)
    {
        PROFILER_SCOPE("iteration");
        // Freed within the same scope so peak usage reflects concurrent
        // worker overlap, not an accumulating leak.
        void* block = std::malloc(kAllocationBytes);
        if (tracker != nullptr)
        {
            tracker->track_allocation(block, kAllocationBytes, "worker_block");
        }
        volatile double sink = 0.0;
        for (int j = 0; j < 2000; ++j)
        {
            sink += static_cast<double>(thread_index * i * j) * 0.0001;
        }
        (void)sink;
        if (tracker != nullptr)
        {
            tracker->track_deallocation(block);
        }
        std::free(block);
    }
}
}  // namespace

int main(int argc, char** argv)
{
    const std::filesystem::path output = argc > 1 ? argv[1] : "multithreaded";
    std::filesystem::create_directories(output);

    profiler::session_options options;
    options.memory_tracking = true;  // native memory_tracker, independent of the backend
    profiler::session session(options);
    if (!session.start())
    {
        std::cerr << "Could not start the profiling session\n";
        return 1;
    }

    profiler::memory_tracker* tracker = session.get_memory_tracker();

    std::vector<std::thread> threads;
    threads.reserve(kThreadCount);
    {
        PROFILER_SCOPE("dispatch_workers");
        for (int t = 0; t < kThreadCount; ++t)
        {
            threads.emplace_back(worker, t, tracker);
        }
        for (auto& thread : threads)
        {
            thread.join();
        }
    }

    // Read back peak usage before stop(): the tracker is native_'s, torn
    // down when the session stops.
    size_t const peak_usage_bytes = tracker != nullptr ? tracker->get_peak_usage() : 0;

    if (!session.stop())
    {
        std::cerr << "Could not stop the profiling session\n";
        return 1;
    }

    if (!session.write_chrome_trace((output / "multithreaded_trace.json").string()))
    {
        std::cerr << "Could not write multithreaded_trace.json to " << output << '\n';
        return 1;
    }

    auto hotspots = session.generate_hotspot_report();
    std::cout << kThreadCount << " worker threads x " << kIterationsEach
              << " iterations each\n"
              << "Peak tracked memory: " << peak_usage_bytes << " bytes\n"
              << hotspots->table()
              << "Wrote multithreaded_trace.json to " << output.string() << '\n'
              << "Open it in chrome://tracing or https://ui.perfetto.dev -- each "
                 "worker thread appears as its own timeline row.\n";
    return 0;
}
