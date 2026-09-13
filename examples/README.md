# Examples

- [`example_quickstart.cpp`](example_quickstart.cpp) — profile any C++
  program and write `quickstart_trace.json` for Chrome / Perfetto.
- [`example_profiling_basic.cpp`](example_profiling_basic.cpp) — native,
  Kineto, and ITT backends.

```bash
cmake -S . -B build -DPROFILER_ENABLE_EXAMPLES=ON
cmake --build build --target example_quickstart
```
