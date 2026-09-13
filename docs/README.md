# Profiler documentation

Start with the [project quickstart](../README.md), then use the guides below.
All build paths and examples refer to this standalone repository.

| Guide | Contents |
| --- | --- |
| [User guide](profiler.md) | Installation, CMake integration, capture APIs, threads, memory, backends, and troubleshooting |
| [Holistic Trace Analysis](hta.md) | Kineto capture for HTA, CPU and GPU analysis, ranks, notebooks, and compatibility |
| [Output examples](outputs.md) | Actual console, hotspot, JSON, CSV, XML, and HTA output, with field explanations |
| [Runnable examples](../examples/README.md) | Executables, commands, generated files, and optional Python dependencies |
| [Dependencies](../third_party/README.md) | Vendored libraries and alternative dependency locations |

Profiler has a C++ API. The optional HTA script analyzes exported files in Python;
it does not provide Python bindings to the Profiler library.
