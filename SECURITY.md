# Security Policy

## Reporting a vulnerability

Please report suspected security vulnerabilities privately, using GitHub's
[private vulnerability reporting](https://github.com/KhwarizmiAnalytix/Profiler/security/advisories/new)
("Security" tab → "Report a vulnerability") rather than a public issue.

Include:

- The Profiler revision (commit SHA or tag) affected.
- Build configuration (`PROFILER_BACKEND`, `PROFILER_GPU_BACKEND`, OS/compiler).
- A minimal reproduction, if possible.

## Scope

Profiler is a profiling/instrumentation library: applications embed it and
feed it their own scope names, metadata, and configuration. It is not designed
to process untrusted trace files as a security boundary — parsing a malicious
Kineto/XSpace JSON trace, or one from `examples/analyze_hta.py`, is not a
supported hardened path. Reports about memory-safety or crash issues in the
library's own C++ code (e.g. in `parse_annotation`, the XSpace exporters, or
the exported public API) are in scope.

Third-party dependencies (fmt, Kineto, Intel ITT API — see
[third_party/README.md](third_party/README.md)) should generally be reported
upstream, unless Profiler's own integration of them introduces the issue.

## Supported versions

Security fixes are made against the `main` branch. See [CHANGELOG.md](CHANGELOG.md)
for released versions.
