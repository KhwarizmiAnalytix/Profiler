# Holistic Trace Analysis (HTA)

Use [Holistic Trace Analysis](https://hta.readthedocs.io/en/latest/) to inspect
Profiler's **Kineto** exports in Python. HTA was designed around PyTorch/Kineto
traces. Profiler supplies Kineto events directly from C++; no PyTorch model or
Python binding to Profiler is needed for the workflow below.

[Documentation index](README.md) · [User guide](profiler.md) · [Output examples](outputs.md)

## Compatibility

| Input | Use with HTA |
| --- | --- |
| Kineto CPU trace from `ProfilerResult::save()` | Parse operators, annotations, iterations, and inclusive CPU durations |
| Kineto CUDA/CUPTI trace with CPU launches and device activities | GPU temporal/kernel breakdowns and launch statistics, subject to required fields |
| Native `write_chrome_trace()` output | Use a trace viewer or native reports; it lacks the Kineto categories/correlation HTA expects |
| Native JSON / CSV / XML report | Not an HTA trace |
| ITT / NVTX result or GPU fallback timings | Not a full Kineto CUDA activity trace |

The CPU workflow is tested with **HolisticTraceAnalysis 0.5.0** and the dependency
versions pinned by this repository's C++ build. The optional Python dependency is
pinned in [requirements-hta.txt](../examples/requirements-hta.txt). HTA's `latest`
documentation may describe a newer release; check the installed API before
adopting additional features.

The provided sample contains real CPU intervals. It contains no GPU kernels,
NCCL collectives, tensor shapes, or distributed training workload. Advanced HTA
features cannot reconstruct data absent from the capture. CUDA instructions below
require validation on an NVIDIA GPU; hosted CI exercises CPU analysis only.

## Generate and analyze a trace

From the Profiler repository root on Linux or macOS:

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release \
  -DPROFILER_BACKEND=KINETO -DPROFILER_ENABLE_EXAMPLES=ON
cmake --build build --config Release --parallel
./build/bin/example_hta build/hta-traces

python3 -m venv build/hta-venv
source build/hta-venv/bin/activate
python -m pip install -r examples/requirements-hta.txt
python examples/analyze_hta.py build/hta-traces --output build/hta-results
```

Use Python 3.10 or newer on an HTA-supported platform. For a trace captured on
Windows, copy the JSON files to a Linux/macOS analysis environment. HTA runs
offline; the analysis machine does not need the capture machine's GPU.
See [upstream installation guidance](https://github.com/facebookresearch/HolisticTraceAnalysis#installation).

The C++ example writes `build/hta-traces/rank0.json`. The Python script prints
operator durations and writes `build/hta-results/rank0_cpu.csv`. For downloadable
sample files and the actual table, see [HTA output](outputs.md#hta-analysis).

## What the C++ example records

[example_hta.cpp](../examples/example_hta.cpp) uses this lifecycle:

```cpp
using namespace profiler::profiler_impl;
const ProfilerConfig config(ProfilerState::KINETO);
const std::set<ActivityType> activities{ActivityType::CPU};
prepareProfiler(config, activities);
enableProfiler(config, activities);
addMetadataJson("distributedInfo", R"({"rank": 0, "world_size": 1})");

for (int i = 0; i < 4; ++i) {
    const std::string step = "ProfilerStep#" + std::to_string(i);
    PROFILER_RECORD_USER_SCOPE(step);
    compute(); // Contains PROFILER_RECORD_FUNCTION("compute").
}

auto result = disableProfiler();
if (!result || !result->save("rank0.json")) return 1;
```

Include `profiler.h`, `bespoke/kineto/profiler_kineto.h`, and
`bespoke/kineto/kineto_shim.h` for these calls. The complete executable creates
its output directory and checks the file export.

`ProfilerStep#N` gives HTA an iteration boundary. `PROFILER_RECORD_FUNCTION`
emits a `cpu_op`; `PROFILER_RECORD_USER_SCOPE` emits a `user_annotation`.
`distributedInfo.rank` identifies the process in a multi-file capture. This
metadata describes the application; it does not launch processes or synchronize
ranks. Native `PROFILER_PROFILE_SCOPE` is not a substitute for these Kineto calls.

## Load the trace in Python

The same capture can be explored directly:

```python
from hta.trace_analysis import TraceAnalysis

analyzer = TraceAnalysis(
    trace_dir="build/hta-traces",
    trace_files={0: "rank0.json"},
    include_last_profiler_step=True,
)
analyzer.t.decode_symbol_ids(use_shorten_name=False)
events = analyzer.t.get_trace(0)
print(events[["s_name", "s_cat", "dur", "iteration"]].to_string(index=False))
```

HTA normally excludes the final profiler step when filtering captures with
multiple steps. This example completes all work before disabling collection,
so `include_last_profiler_step=True` retains all four steps. For an incomplete
capture, leave the default filtering in place. HTA may round fractional
microseconds during parsing; its table can therefore differ slightly from raw
Kineto durations. See the
[TraceAnalysis API](https://hta.readthedocs.io/en/latest/source/api/trace_analysis_api.html).

The script's CPU table sums **inclusive elapsed duration** by name. The total
for `compute` covers four calls. Each step also contains its `compute` call, so
the two levels must not be added to estimate wall time. Use Profiler's native
hotspot table when you need self-time accounting for native scopes.

## Capture CUDA activities

Build with Kineto, CUDA, CUPTI, and NVTX as described in the
[GPU build guide](profiler.md#itt-nvtx-and-gpu-backends). Link an application's
own CUDA runtime calls through `CUDA::cudart`:

```cmake
find_package(CUDAToolkit REQUIRED)
target_link_libraries(my_app PRIVATE Profiler::Profiler CUDA::cudart)
```

Adapt the CPU capture to request device activities:

```cpp
const std::set<ActivityType> activities{ActivityType::CPU, ActivityType::CUDA};
```

Instrument the application's real launching function:

```cpp
void run_iteration() {
    PROFILER_RECORD_FUNCTION("simulation::advance");
    // Launch your CUDA kernels / copies on the application's CUDA streams.
}
```

Before profiling, initialize CUDA, allocate buffers, and warm up the workload.
During capture, use `ProfilerStep#N` around each measured iteration and call the
instrumented launching function. Keep external correlation enabled. Finish all
GPU work using the application's stream synchronization, or a checked
`cudaDeviceSynchronize()`, before closing the final step and disabling capture.
Synchronizing every iteration changes overlap, so do that only if it matches the
measurement you intend to make. Check CUDA errors through the application's
normal error handling and always close the profiling session.

Inspect the exported JSON before requesting GPU analysis:

| Event / field | Why it matters |
| --- | --- |
| `cpu_op` or `user_annotation` | Names and CPU-side scope intervals |
| `ProfilerStep#N` annotations | Iteration windows for filtering |
| `cuda_runtime` events | CPU-side CUDA launch/scheduling intervals |
| `kernel`, `gpu_memcpy`, `gpu_memset` activities | Device work for GPU breakdowns |
| Device `args.stream` and `args.device` | Stream/device placement |
| Launch/device `args.correlation` | Matching a runtime call to its device activity |
| CPU `args["External id"]` | External correlation for operation attribution |

Kineto/CUPTI writes these activity fields when the corresponding events are
captured. Do not manufacture GPU events or correlation IDs to make a CPU trace
appear compatible. Merely naming a scope `kernel` does not create a device event.

## GPU analysis and outputs

For an actual CUDA capture in `build/cuda-traces/`:

```bash
python examples/analyze_hta.py build/cuda-traces \
  --output build/cuda-analysis --gpu
```

| File | Interpretation |
| --- | --- |
| `rankN_cpu.csv` | Inclusive annotated CPU durations and call counts |
| `temporal_breakdown.csv` | Per-rank GPU time breakdown, including idle time |
| `kernel_types.csv` | Total activity duration grouped by computation, communication, or memory |
| `kernels.csv` | Kernel duration aggregates and counts |
| `rankN_launch_stats.csv` | Matched CPU runtime duration, GPU duration, and launch delay |

In a notebook, visualize the same analysis:

```python
from hta.trace_analysis import TraceAnalysis

analyzer = TraceAnalysis(trace_dir="build/cuda-traces", include_last_profiler_step=True)
temporal = analyzer.get_temporal_breakdown(visualize=True)
kernel_types, kernels = analyzer.get_gpu_kernel_breakdown(
    visualize=True, num_kernels=10, image_renderer="jupyterlab"
)
launches = analyzer.get_cuda_kernel_launch_stats(ranks=[0], visualize=False)
print(launches[0].head().to_string(index=False))
```

`get_gpu_kernel_breakdown()` returns **two** dataframes; launch statistics return
a dictionary keyed by rank. HTA's figures use Plotly. See
[temporal breakdown](https://hta.readthedocs.io/en/latest/source/features/temporal_breakdown.html)
and [kernel breakdown](https://hta.readthedocs.io/en/latest/source/features/kernel_breakdown.html).

A large idle interval suggests checking launch gaps and synchronization. A long
kernel suggests investigating that kernel's implementation and input size. A
large launch delay may reflect queued work or dependencies, rather than CPU
launch overhead alone. GPU kernel duration sums can exceed the elapsed capture
window when streams overlap; use temporal analysis for timeline utilization.

For workloads containing actual communication kernels, HTA also exposes
`get_comm_comp_overlap(visualize=False)`. For before/after comparisons, see
[Trace Diff](https://hta.readthedocs.io/en/latest/source/features/trace_diff.html).
Counter and critical-path analyses require additional capture data and have not
been validated by this repository's CPU example.

## Multiple ranks

Keep one capture per process/rank in a directory dedicated to one run:

```text
traces/run-a/
  rank0.json
  rank1.json
traces/run-b/
  rank0.json
  rank1.json
```

Each process should set its real `distributedInfo.rank` and `world_size` after
`enableProfiler()` and write to a unique filename. Keep iteration naming and
capture windows consistent across ranks. HTA accepts `.json` and `.json.gz`.
The included script validates rank uniqueness before loading. If a legacy file
has no rank metadata, the script assigns rank 0 and consequently rejects a second
unidentified file in that directory.

HTA also supports an explicit file map:

```python
analyzer = TraceAnalysis(
    trace_dir="traces/run-a",
    trace_files={0: "rank0.json", 1: "rank1.json"},
    include_last_profiler_step=True,
)
```

Use a separate directory for each comparison run; otherwise a rank can be
silently replaced during upstream auto-discovery. Clock/capture alignment still
matters when interpreting communication across machines.

## Troubleshooting

| Symptom | Resolution |
| --- | --- |
| `ModuleNotFoundError: hta` | Activate the analysis environment and install `examples/requirements-hta.txt` with that interpreter |
| Expected a Kineto trace | Export with `ProfilerResult::save()`; do not pass native timeline/report JSON |
| Missing `ProfilerStep` warning | Annotate iterations as `ProfilerStep#N`; capture several complete iterations |
| Last iteration missing | Use `include_last_profiler_step=True` only after ensuring it completed |
| Duplicate rank | Split runs into separate directories and emit the correct rank metadata |
| `--gpu` rejects the file | Capture real CUDA/CUPTI activities on every selected rank |
| Empty GPU results despite a CUDA build | Check requested activities, device/driver availability, synchronization, and correlation IDs |
| `cpu_op` / `correlation` / `stream` parsing errors | Check event categories and trace provenance; a generic Chrome Trace file is not sufficient |
| Different displayed and raw durations | HTA can round fractional microseconds; compare equivalent units and aggregation levels |
| Plot does not render | Run in a compatible notebook environment or use `visualize=False` and inspect CSVs |

See [upstream HTA](https://github.com/facebookresearch/HolisticTraceAnalysis) for
analysis-specific issues. When reporting an integration problem, include the
Profiler revision, backend/CUDA configuration, HTA version, and a small trace
that reproduces it.
