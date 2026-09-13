# Third-party sources for XSigmaProfiler

Do not edit vendored checkouts. Populate one of:

1. **Git submodules** (standalone checkout):

   ```bash
   git submodule add https://github.com/fmtlib/fmt.git third_party/fmt
   git submodule add https://github.com/pytorch/kineto.git third_party/kineto
   git submodule add https://github.com/intel/ittapi.git third_party/ittapi
   git submodule update --init --recursive
   ```

2. **CMake FetchContent** — if these directories are missing, configure
   downloads fmt / kineto / ittapi into `third_party/`.

3. **Reuse another tree** — pass `-DPROFILER_THIRD_PARTY_DIR=/path/to/ThirdParty`
   (XSigma sets this to its `ThirdParty/` so fmt and kineto are not built twice).
