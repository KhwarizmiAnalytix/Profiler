# Runnable examples

From the repository root:

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DPROFILER_ENABLE_EXAMPLES=ON
cmake --build build --config Release --parallel
ctest --test-dir build -C Release --output-on-failure
```

For Visual Studio builds, executables are in `build/bin/Release/` and end in
`.exe`. The commands below use a single-configuration Unix build.

| Example | Purpose | Files written |
| --- | --- | --- |
| [example_quickstart.cpp](example_quickstart.cpp) | Minimal native integration | `quickstart_trace.json` in the working directory |
| [example_reports.cpp](example_reports.cpp) | One native capture, all report formats and hotspots | `native_trace.json`, `report.txt`, `report.json`, `report.csv`, `report.xml`, `hotspots.txt` in the selected directory |
| [example_hta.cpp](example_hta.cpp) | Kineto CPU capture with rank metadata and iteration markers; requires the Kineto build | `rank0.json` in the selected directory |
| [example_profiling_basic.cpp](example_profiling_basic.cpp) | Larger matrix, FFT, and Monte Carlo demonstrations for compiled backends | Native / backend demonstration traces in the working directory |

```bash
./build/bin/example_quickstart
./build/bin/example_reports build/reports
./build/bin/example_hta build/hta-traces
./build/bin/example_profiling_basic
```

`example_reports` defaults to `reports/`; `example_hta` defaults to `traces/hta/`.
Both create their output directory and report write failures with a nonzero exit
status. The quickstart, report example, and Kineto HTA capture are registered as
CTest smoke tests when testing is enabled. Test artifacts stay under the build
directory.

## Analyze the Kineto trace with HTA

[Holistic Trace Analysis](https://hta.readthedocs.io/en/latest/) is an optional
Python analysis tool. Use a separate environment on Linux or macOS:

```bash
python3 -m venv build/hta-venv
source build/hta-venv/bin/activate
python -m pip install -r examples/requirements-hta.txt
python examples/analyze_hta.py build/hta-traces --output build/hta-results
```

[analyze_hta.py](analyze_hta.py) prints an inclusive CPU duration table and writes
one `rankN_cpu.csv` per rank. With `--gpu`, it also writes temporal and kernel
breakdowns and CUDA launch statistics. That option requires real CUDA activities;
the CPU example deliberately does not produce device events.

Keep each capture in its own directory, with one Kineto JSON or JSON.gz file per
rank. Keep generated CSV files in a separate results directory. The script
rejects native report files, duplicate ranks, and GPU analysis of CPU-only traces.

See [HTA usage](../docs/hta.md) and [example outputs](../docs/outputs.md).
