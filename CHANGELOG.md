# Changelog

## 1.0.1 — 2026-09-13

- Fix Windows CUDA builds by including the NVTX3 C API header.
- Use compiled fmt consistently across Profiler and Kineto to avoid duplicate
  symbols on Windows.
- Correct native Chrome Trace timestamps and durations to microseconds.
- Add standalone documentation, Holistic Trace Analysis usage, runnable examples,
  and captured output samples.
- Validate the examples and HTA CPU workflow in CI.
- Document the upstream dependency pins: PyTorch Kineto, fmt 12.2.0,
  Intel ITT API 3.28.4, and GoogleTest 1.18.0. Keep Kineto's own nested pins
  unchanged from upstream.
