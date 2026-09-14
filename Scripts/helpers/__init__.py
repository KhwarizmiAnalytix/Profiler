"""
Profiler Build System Helper Modules

Modular helper functions for Scripts/setup.py, split out for readability and
reuse (follows the same Scripts/helpers/ convention as the sibling
KhwarizmiAnalytix/Parallel repo).

Modules:
    - config: CMake configure-step invocation
    - build: Build-step invocation (ninja/xcodebuild/msvc)
    - test: ctest invocation (with per-sanitizer suppression-file support)
    - cppcheck: Static analysis with cppcheck

Coverage analysis is the coverage-tool PyPI package (pip install coverage-tool),
invoked from Scripts/setup.py when the coverage token is set.
"""

__version__ = "1.0.0"
__all__ = ["config", "build", "test", "cppcheck"]
