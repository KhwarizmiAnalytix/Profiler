#!/usr/bin/env python3
"""Profiler CMake Build Configuration Script.

Design follows the shared setup.py convention used across this org's
repos (dotted-token CLI, Flags/Configuration split, coverage-tool
integration), scaled down to the CMake options this standalone repo actually
defines (PROFILER_ENABLE_*/PROFILER_* in CMakeLists.txt). Profiler builds one
flat `Profiler` library and one `ProfilerCxxTests` binary. Unlike Parallel,
Profiler does define two independent CMake-level selectors worth exposing:
PROFILER_BACKEND (Kineto vs. Intel ITT instrumentation) and
PROFILER_GPU_BACKEND (none/cuda/hip/metal).

Usage:
    python Scripts/setup.py config.build.test
    python Scripts/setup.py config.build.test.coverage
    python Scripts/setup.py config.build.test --backend.itt
    python Scripts/setup.py config.build.test --gpu.cuda
    python Scripts/setup.py config.build.test.gcc.release
"""

import os
import platform
import re
import shutil
import subprocess
import sys
import time
from datetime import datetime
from pathlib import Path
from typing import Optional

try:
    import colorama
    from colorama import Fore, Style

    colorama.init()
except ImportError:  # Windows CLI smoke and some CI jobs skip pip install

    class Fore:  # pylint: disable=too-few-public-methods
        CYAN = GREEN = YELLOW = RED = WHITE = ""

    class Style:  # pylint: disable=too-few-public-methods
        RESET_ALL = ""

from helpers import build as build_helper, config as config_helper, cppcheck as cppcheck_helper, test as test_helper

DEBUG_FLAG = False


class ErrorLogger:
    """Centralized error logging system for comprehensive error tracking."""

    def __init__(self, log_dir: str = "logs"):
        self.log_dir = Path(log_dir)
        self.log_dir.mkdir(exist_ok=True)
        self.log_file = (
            self.log_dir / f"profiler_build_{datetime.now().strftime('%Y%m%d_%H%M%S')}.log"
        )
        self.errors = []

    def log_error(
        self,
        command: str,
        error_output: str,
        context: str = "",
        suggestions: Optional[list[str]] = None,
    ):
        timestamp = datetime.now().strftime("%Y-%m-%d %H:%M:%S")
        self.errors.append(
            {
                "timestamp": timestamp,
                "command": command,
                "error_output": error_output,
                "context": context,
                "suggestions": suggestions or [],
            }
        )
        with open(self.log_file, "a", encoding="utf-8") as f:
            f.write(f"\n{'=' * 80}\n")
            f.write(f"ERROR LOG ENTRY - {timestamp}\n")
            f.write(f"{'=' * 80}\n")
            f.write(f"Context: {context}\n")
            f.write(f"Command: {command}\n")
            f.write(f"Error Output:\n{error_output}\n")
            if suggestions:
                f.write("Troubleshooting Suggestions:\n")
                for i, suggestion in enumerate(suggestions, 1):
                    f.write(f"  {i}. {suggestion}\n")
            f.write(f"{'=' * 80}\n\n")

    def get_log_file_path(self) -> str:
        return str(self.log_file)

    def has_errors(self) -> bool:
        return len(self.errors) > 0


class SummaryReporter:
    """Generate and display summary reports for various analysis tools."""

    def __init__(self):
        self.reports = {}

    def add_cppcheck_report(self, log_file: str, exit_code: int):
        if not os.path.exists(log_file):
            self.reports["cppcheck"] = {"status": "not_run", "message": "Cppcheck was not executed"}
            return
        try:
            with open(log_file, encoding="utf-8") as f:
                content = f.read()
            issues = {
                "error": len(re.findall(r",error,", content)),
                "warning": len(re.findall(r",warning,", content)),
                "style": len(re.findall(r",style,", content)),
                "performance": len(re.findall(r",performance,", content)),
                "portability": len(re.findall(r",portability,", content)),
                "information": len(re.findall(r",information,", content)),
            }
            self.reports["cppcheck"] = {
                "status": "completed",
                "exit_code": exit_code,
                "total_issues": sum(issues.values()),
                "issues_by_type": issues,
                "log_file": log_file,
            }
        except Exception as e:
            self.reports["cppcheck"] = {"status": "error", "message": f"Failed to parse cppcheck results: {e}"}

    def add_coverage_report(self, build_path: str, exit_code: int):
        """Parse the coverage-tool JSON report and extract summary metrics."""
        import json

        coverage_json_paths = [
            os.path.join(build_path, "coverage_report", "coverage_summary.json"),
            os.path.join(build_path, "coverage_report", "coverage.json"),
        ]
        coverage_json = next((p for p in coverage_json_paths if os.path.exists(p)), None)
        if not coverage_json:
            self.reports["coverage"] = {"status": "not_run", "message": "Coverage report not found"}
            return
        try:
            with open(coverage_json, encoding="utf-8") as f:
                coverage_data = json.load(f)
            if "global_metrics" in coverage_data:
                metrics = coverage_data["global_metrics"]
                self.reports["coverage"] = {
                    "status": "completed",
                    "exit_code": exit_code,
                    "total_lines": metrics.get("total_lines", 0),
                    "covered_lines": metrics.get("covered_lines", 0),
                    "line_coverage_percent": metrics.get("line_coverage_percent", 0.0),
                    "total_functions": metrics.get("total_functions", 0),
                    "covered_functions": metrics.get("covered_functions", 0),
                    "function_coverage_percent": metrics.get("function_coverage_percent", 0.0),
                    "total_regions": metrics.get("total_regions", 0),
                    "covered_regions": metrics.get("covered_regions", 0),
                    "region_coverage_percent": metrics.get("region_coverage_percent", 0.0),
                    "report_file": coverage_json,
                }
            elif "summary" in coverage_data:
                summary = coverage_data["summary"]
                line_cov = summary.get("line_coverage", {})
                func_cov = summary.get("function_coverage", {})
                self.reports["coverage"] = {
                    "status": "completed",
                    "exit_code": exit_code,
                    "total_lines": line_cov.get("total", 0),
                    "covered_lines": line_cov.get("covered", 0),
                    "line_coverage_percent": line_cov.get("percent", 0.0),
                    "total_functions": func_cov.get("total", 0),
                    "covered_functions": func_cov.get("covered", 0),
                    "function_coverage_percent": func_cov.get("percent", 0.0),
                    "total_regions": 0,
                    "covered_regions": 0,
                    "region_coverage_percent": 0.0,
                    "report_file": coverage_json,
                }
            else:
                self.reports["coverage"] = {"status": "error", "message": "Coverage JSON format not recognized"}
        except Exception as e:
            self.reports["coverage"] = {"status": "error", "message": f"Failed to parse coverage results: {e}"}

    def display_summary(self):
        if not self.reports:
            return
        print_status("\n" + "=" * 80, "INFO")
        print_status("BUILD AND ANALYSIS SUMMARY REPORT", "INFO")
        print_status("=" * 80, "INFO")
        for tool, report in self.reports.items():
            self._display_tool_summary(tool, report)
        print_status("=" * 80, "INFO")

    def _display_tool_summary(self, tool: str, report: dict):
        tool_name = tool.upper()
        if report["status"] == "not_run":
            print_status(f"{tool_name}: Not executed", "INFO")
            return
        if report["status"] == "error":
            print_status(f"{tool_name}: Error - {report['message']}", "ERROR")
            return

        if tool == "cppcheck":
            total = report["total_issues"]
            if total == 0:
                print_status(f"{tool_name}: No issues found", "SUCCESS")
            else:
                print_status(f"{tool_name}: Found {total} issues", "WARNING")
                for issue_type, count in report["issues_by_type"].items():
                    if count > 0:
                        print_status(f"  - {issue_type}: {count}", "INFO")
                print_status(f"  Log file: {report['log_file']}", "INFO")

        elif tool == "coverage":
            print_status("\n" + "=" * 80, "INFO")
            print_status("CODE COVERAGE SUMMARY", "INFO")
            print_status("=" * 80, "INFO")
            total_lines = report.get("total_lines", 0)
            covered_lines = report.get("covered_lines", 0)
            coverage_percent = report.get("line_coverage_percent", 0.0)
            print_status(f"Total Lines:    {total_lines}", "INFO")
            print_status(f"Covered Lines:  {covered_lines}", "INFO")
            print_status(
                f"Coverage:       {coverage_percent:.2f}%",
                "SUCCESS" if coverage_percent >= 95.0 else "WARNING" if coverage_percent >= 80.0 else "ERROR",
            )
            if report.get("total_functions", 0) > 0:
                print_status(f"Function Coverage: {report.get('function_coverage_percent', 0.0):.2f}%", "INFO")
            if report.get("total_regions", 0) > 0:
                print_status(f"Region Coverage:   {report.get('region_coverage_percent', 0.0):.2f}%", "INFO")
            report_file = report.get("report_file", "")
            if report_file:
                report_dir = os.path.dirname(report_file)
                for html_path in (
                    os.path.join(report_dir, "html", "index.html"),
                    os.path.join(report_dir, "index.html"),
                ):
                    if os.path.exists(html_path):
                        print_status(f"\nHTML Report: {html_path}", "INFO")
                        break
            print_status("=" * 80, "INFO")


def check_dependencies() -> list[str]:
    """Check if required dependencies are installed."""
    missing_deps = []
    try:
        subprocess.run(["cmake", "--version"], capture_output=True, check=True)
    except (subprocess.CalledProcessError, FileNotFoundError):
        missing_deps.append("CMake")

    if platform.system() == "Windows":
        try:
            subprocess.run(["clang", "--version"], capture_output=True, check=True)
        except (subprocess.CalledProcessError, FileNotFoundError):
            try:
                subprocess.run(["cl"], capture_output=True)
            except (subprocess.CalledProcessError, FileNotFoundError):
                missing_deps.append("C++ compiler (MSVC or Clang)")
    elif platform.system() == "Darwin":
        try:
            subprocess.run(["xcode-select", "--print-path"], capture_output=True, check=True)
        except (subprocess.CalledProcessError, FileNotFoundError):
            missing_deps.append("Xcode Command Line Tools (run: xcode-select --install)")
        try:
            subprocess.run(["clang++", "--version"], capture_output=True, check=True)
        except (subprocess.CalledProcessError, FileNotFoundError):
            missing_deps.append("C++ compiler (Clang)")
    else:
        try:
            subprocess.run(["clang++", "--version"], capture_output=True, check=True)
        except (subprocess.CalledProcessError, FileNotFoundError):
            try:
                subprocess.run(["g++", "--version"], capture_output=True, check=True)
            except (subprocess.CalledProcessError, FileNotFoundError):
                missing_deps.append("C++ compiler (GCC or Clang)")

    return missing_deps


def check_xcode_availability() -> bool:
    if platform.system() != "Darwin":
        return False
    try:
        result = subprocess.run(["xcodebuild", "-version"], capture_output=True, check=True, text=True)
        print_status(f"Found Xcode: {result.stdout.strip().split()[1]}", "INFO")
        return True
    except subprocess.CalledProcessError as e:
        stderr_output = e.stderr if isinstance(e.stderr, str) else (e.stderr.decode() if e.stderr else "")
        if "command line tools instance" in stderr_output:
            print_status("Xcode Command Line Tools found, but full Xcode is required for Xcode generator", "WARNING")
        else:
            print_status(f"Xcode check failed: {stderr_output.strip()}", "WARNING")
        return False
    except FileNotFoundError:
        print_status("xcodebuild not found - install Xcode or Xcode Command Line Tools", "WARNING")
        return False


def print_status(message: str, status: str = "INFO", end: str = "\n") -> None:
    status_colors = {"INFO": Fore.BLUE, "SUCCESS": Fore.GREEN, "ERROR": Fore.RED, "WARNING": Fore.YELLOW}
    color = status_colors.get(status, Fore.WHITE)
    print(f"{color}[{status}]{Style.RESET_ALL} {message}", end=end)


def debug_print(message):
    if DEBUG_FLAG:
        print(message)


class ProfilerFlags:
    """Maps setup.py dotted tokens to Profiler's PROFILER_* CMake cache variables.

    Scoped 1:1 to the options CMakeLists.txt actually defines -- Profiler is a
    single flat CMake target, with no --project.* scoping. Two independent selectors ARE real
    here though: PROFILER_BACKEND (Kineto vs. Intel ITT instrumentation) and
    PROFILER_GPU_BACKEND (none/cuda/hip/metal), both plain `set(... CACHE
    STRING ...)` variables in CMakeLists.txt.
    """

    OFF = "OFF"
    ON = "ON"

    def __init__(self, arg_list):
        self.__initialize_flags()
        if arg_list:
            self.__build_cmake_flag()
            self.__fill_option_flags(arg_list)
            self.__validate_flags()

    def __initialize_flags(self):
        self.__key = [
            "static",
            "test",
            "examples",
            "libtorch",
            "install",
            "require_cuda",
            "require_nvtx",
            "coverage",
            "sanitizer",
            "cxxstd",
            "backend",
            "gpu_backend",
            "cppcheck",
        ]
        self.__description = [
            "build shared (default) or static libraries",
            "build the Profiler test suite (PROFILER_ENABLE_TESTING; default ON)",
            "build Profiler example programs",
            "link LibTorch into ProfilerCxxTests when found",
            "install headers, library, and CMake package (default: ON for standalone builds)",
            "fail configure if CUDA was requested but not found",
            "fail configure if NVTX is missing",
            "enable code coverage instrumentation",
            "build with sanitizer support: --sanitizer.address, .undefined, .thread, .memory, .leak "
            "(combinable, e.g. --sanitizer.address --sanitizer.undefined)",
            "C++ standard: cxx11, cxx14, cxx17, cxx20, cxx23",
            "instrumentation backend: --backend.kineto (default) or --backend.itt",
            "GPU backend: --gpu.none (default), --gpu.cuda, --gpu.hip, or --gpu.metal",
            "enable cppcheck static analysis",
        ]

    def __build_cmake_flag(self):
        debug_print("Build cmake flag")
        self.__name = {
            "static": "BUILD_SHARED_LIBS",
            "test": "PROFILER_ENABLE_TESTING",
            "examples": "PROFILER_ENABLE_EXAMPLES",
            "libtorch": "PROFILER_ENABLE_LIBTORCH",
            "install": "PROFILER_ENABLE_INSTALL",
            "require_cuda": "PROFILER_REQUIRE_CUDA",
            "require_nvtx": "PROFILER_REQUIRE_NVTX",
            "coverage": "PROFILER_ENABLE_COVERAGE",
            "sanitizer": "PROFILER_SANITIZER",
            "cxxstd": "PROFILER_CXX_STANDARD",
            "backend": "PROFILER_BACKEND",
            "gpu_backend": "PROFILER_GPU_BACKEND",
            # "cppcheck" runs via Scripts/helpers/cppcheck.py after the build; no
            # CMakeLists.txt option consumes it (avoid an unused-var warning).
        }

    def __fill_option_flags(self, arg_list):
        debug_print("Fill option flags")
        self.__set_default_flags()
        self.__process_arg_list(arg_list)

    def __set_default_flags(self):
        # Mirrors CMakeLists.txt's option()/set() defaults exactly. Empty
        # string means "don't pass -D...; let CMake apply its own default"
        # (cxxstd=20, backend=KINETO, gpu_backend computed from
        # MEMORY_GPU_BACKEND/"none", install tracks PROFILER_STANDALONE).
        self.__value = dict.fromkeys(self.__key, self.OFF)
        self.__value.update(
            {
                "static": self.ON,  # BUILD_SHARED_LIBS default is ON, so static=ON means "shared"
                "test": self.ON,  # PROFILER_ENABLE_TESTING default ON
                "install": "",
                "sanitizer": "",  # comma-joined; empty = PROFILER_SANITIZER left unset
                "cxxstd": "",
                "backend": "",
                "gpu_backend": "",
            }
        )
        self.__sanitizer_types: list[str] = []

    def __process_arg_list(self, arg_list):
        sanitizer_list = ["address", "undefined", "thread", "memory", "leak"]
        cxx_std_list = ["cxx11", "cxx14", "cxx17", "cxx20", "cxx23"]
        backend_list = ["kineto", "itt"]
        gpu_backend_list = ["none", "cuda", "hip", "metal"]

        self.builder_suffix = ""
        for arg in arg_list:
            if arg == "static":
                self.__value["static"] = self.OFF
                self.builder_suffix += "_static"
            elif arg in sanitizer_list:
                if arg not in self.__sanitizer_types:
                    self.__sanitizer_types.append(arg)
                    self.builder_suffix += f"_{arg}"
                self.__value["sanitizer"] = ",".join(self.__sanitizer_types)
            elif arg.startswith("backend."):
                backend_value = arg.split(".", 1)[1].lower()
                if backend_value in backend_list:
                    self.__value["backend"] = backend_value.upper()
                    self.builder_suffix += f"_backend_{backend_value}"
                    print_status(f"Selecting instrumentation backend: {backend_value.upper()}", "INFO")
                else:
                    print_status(
                        f"Invalid backend '{backend_value}'. Valid options: {', '.join(backend_list)}",
                        "ERROR",
                    )
                    sys.exit(1)
            elif arg.startswith("gpu."):
                gpu_value = arg.split(".", 1)[1].lower()
                if gpu_value in gpu_backend_list:
                    self.__value["gpu_backend"] = gpu_value
                    if gpu_value != "none":
                        self.builder_suffix += f"_gpu_{gpu_value}"
                    print_status(f"Selecting GPU backend: {gpu_value}", "INFO")
                else:
                    print_status(
                        f"Invalid GPU backend '{gpu_value}'. Valid options: {', '.join(gpu_backend_list)}",
                        "ERROR",
                    )
                    sys.exit(1)
            elif any(arg.lower() == item.lower() for item in cxx_std_list):
                std_version = arg[3:]  # Remove "cxx" prefix
                self.__value["cxxstd"] = std_version
                print_status(f"Setting C++ standard to C++{std_version}", "INFO")
            elif re.match(r"^c\+\+(\d+)$", arg.lower()):
                std_version = re.match(r"^c\+\+(\d+)$", arg.lower()).group(1)
                self.__value["cxxstd"] = std_version
                print_status(f"Setting C++ standard to C++{std_version}", "INFO")
            elif arg == "noinstall":
                self.__value["install"] = self.OFF
                self.builder_suffix += "_noinstall"
            elif arg == "install":
                self.__value["install"] = self.ON
            elif arg in self.__key:
                # Every remaining flag in self.__key defaults OFF: providing
                # the token turns it ON.
                self.__value[arg] = self.ON
                if arg not in ("test", "build"):
                    self.builder_suffix += f"_{arg}"

    def __validate_flags(self):
        if self.__value.get("coverage") == self.ON and self.__value.get("test") != self.ON:
            print_status("Coverage enabled but testing is disabled - enabling tests automatically.", "WARNING")
            self.__value["test"] = self.ON

        if self.__value.get("coverage") == self.ON:
            try:
                import coverage_tool  # noqa: F401
            except ImportError:
                print_status(
                    "coverage-tool is not installed. Install with: pip install coverage-tool",
                    "ERROR",
                )
                sys.exit(1)

        if "thread" in self.__sanitizer_types and len(self.__sanitizer_types) > 1:
            print_status(
                "ThreadSanitizer cannot combine with other sanitizers; use a separate build directory.",
                "WARNING",
            )

    @staticmethod
    def find_case_insensitive(element, lst):
        element_lower = element.lower()
        return next((item for item in lst if element_lower == item.lower()), None)

    def create_cmake_flags(self, cmake_cmd_flags, build_enum, system):
        debug_print("Create cmake flags")
        del system  # unused; kept for parity with the CMake-driven build-type selection below
        if self.__value.get("sanitizer") or self.__value.get("coverage") == self.ON:
            print_status("Enabling debug build for sanitizer or coverage analysis", "INFO")
            build_type = "Debug"
        else:
            build_type = str(build_enum).capitalize()

        for key, value in self.__value.items():
            if key in self.__name:
                flag_name = self.__name[key]
                flag_value = "ON" if isinstance(value, bool) and value else str(value)
                if flag_value != "":
                    cmake_cmd_flags.append(f"-D{flag_name}={flag_value}")

        cmake_cmd_flags.append("-DCMAKE_EXPORT_COMPILE_COMMANDS=ON")
        return build_type

    def helper(self):
        for key, description in zip(self.__key, self.__description):
            print(f"{key:<20}{description}")

    def is_coverage(self):
        return self.__value["coverage"] == self.ON

    def is_cppcheck(self):
        return self.__value["cppcheck"] == self.ON

    def get_sanitizer_types(self) -> list[str]:
        return list(self.__sanitizer_types)


class ProfilerConfiguration:
    def __init__(self, args_list):
        missing_deps = check_dependencies()
        if missing_deps:
            print_status("Missing required dependencies:", "ERROR")
            for dep in missing_deps:
                print_status(f"  - {dep}", "ERROR")
            print_status("Please install missing dependencies and try again.", "ERROR")
            sys.exit(1)

        self.error_logger = ErrorLogger()
        self.summary_reporter = SummaryReporter()

        self.__initialize_values()
        self.__profiler_flags = ProfilerFlags(args_list)
        self.__fill_compilation_flags(args_list)

    def __initialize_values(self):
        default_cxx_compiler = "clang++"
        default_c_compiler = "clang"
        self.__value = {
            "system": platform.system(),
            "build_folder": "build_ninja",
            "builder": "ninja",
            "config": "",
            "build": "",
            "test": "",
            "build_enum": "Release",
            "cmake_generator": "Ninja",
            "cmake_cxx_compiler": f"-DCMAKE_CXX_COMPILER={default_cxx_compiler}",
            "cmake_c_compiler": f"-DCMAKE_C_COMPILER={default_c_compiler}",
            "verbosity": "",
            "arg_cmake_verbose": "--loglevel=NOTICE",
        }
        self.__compiler_user_specified = False
        print(f"================= {self.__value['system']} platform =================")

    def __fill_compilation_flags(self, args_list):
        for arg in args_list:
            self.__process_arg(arg)

    def __process_arg(self, arg):
        if arg == "ninja":
            self.__set_ninja_flags()
        elif arg == "xcode":
            self.__set_xcode_flags()
        elif self.__is_clang_compiler(arg):
            self.__set_clang_compiler(arg)
        elif arg == "clang-cl":
            self.__value["cmake_cxx_compiler"] = "-DCMAKE_GENERATOR_TOOLSET=ClangCL"
            self.__value["cmake_c_compiler"] = ""
            self.__compiler_user_specified = True
        elif self.__is_gcc_compiler(arg):
            self.__set_gcc_compiler(arg)
        elif self.__is_visual_studio(arg):
            self.__set_visual_studio(arg)
        elif arg in ["config", "build", "test"]:
            self.__value[arg] = arg
        elif arg in ["release", "debug", "relwithdebinfo"]:
            self.__value["build_enum"] = arg.capitalize() if arg != "relwithdebinfo" else "RelWithDebInfo"
        elif arg in ["vv", "v"]:
            self.__set_verbose_flags()

    def __set_ninja_flags(self):
        self.__value["cmake_generator"] = "Ninja"
        self.__value["builder"] = "ninja"
        self.__value["build_folder"] = f"build_ninja{self.__profiler_flags.builder_suffix}"

    def __set_xcode_flags(self):
        if self.__value["system"] == "Darwin" and check_xcode_availability():
            self.__value["cmake_generator"] = "Xcode"
            self.__value["builder"] = "xcodebuild"
            self.__value["build_folder"] = f"build_xcode{self.__profiler_flags.builder_suffix}"
            print_status("Using Xcode generator", "SUCCESS")
        else:
            if self.__value["system"] == "Darwin":
                print_status("Xcode not found, falling back to Ninja", "WARNING")
            else:
                print_status("Xcode generator is only available on macOS", "WARNING")
            self.__set_ninja_flags()

    def __is_clang_compiler(self, arg):
        return "clang" in arg and arg not in ["clang-cl"]

    def __set_clang_compiler(self, arg):
        self.__value["cmake_c_compiler"] = f"-DCMAKE_C_COMPILER={arg}"
        self.__value["cmake_cxx_compiler"] = f"-DCMAKE_CXX_COMPILER={arg.replace('clang', 'clang++')}"
        self.__compiler_user_specified = True

    def __is_gcc_compiler(self, arg):
        return ("gcc" in arg or "g++" in arg) and arg not in ["cppcheck"]

    def __set_gcc_compiler(self, arg):
        if "g++" in arg:
            self.__value["cmake_cxx_compiler"] = f"-DCMAKE_CXX_COMPILER={arg}"
            self.__value["cmake_c_compiler"] = f"-DCMAKE_C_COMPILER={arg.replace('g++', 'gcc')}"
        else:
            self.__value["cmake_c_compiler"] = f"-DCMAKE_C_COMPILER={arg}"
            self.__value["cmake_cxx_compiler"] = f"-DCMAKE_CXX_COMPILER={arg.replace('gcc', 'g++')}"
        self.__compiler_user_specified = True

    def __is_visual_studio(self, arg):
        return arg in ["vs17", "vs19", "vs22", "vs26"] and self.__value["system"] == "Windows"

    def __set_visual_studio(self, arg):
        vs_versions = {
            "vs17": ("Visual Studio 15 2017 Win64", "build_vs17"),
            "vs19": ("Visual Studio 16 2019", "build_vs19"),
            "vs22": ("Visual Studio 17 2022", "build_vs22"),
            "vs26": ("Visual Studio 18 2026", "build_vs26"),
        }
        self.__value["cmake_generator"], base_build_folder = vs_versions[arg]
        self.__value["builder"] = "cmake"
        self.__value["build_folder"] = f"{base_build_folder}{self.__profiler_flags.builder_suffix}"
        if not self.__compiler_user_specified:
            self.__value["cmake_cxx_compiler"] = ""
            self.__value["cmake_c_compiler"] = ""

    def __set_verbose_flags(self):
        self.__value["arg_cmake_verbose"] = "--loglevel=VERBOSE"
        self.__value["verbosity"] = "-VV"

    def config(self, source_path, build_path):
        if self.__value["config"] != "config":
            return 0
        print_status("Configuring build...", "INFO")
        try:
            cmake_flags = []
            self.__value["build_enum"] = self.__profiler_flags.create_cmake_flags(
                cmake_flags, self.__value["build_enum"], self.__value["system"]
            )
            print(f"build enum: {self.__value['build_enum']}")
            cmake_flags.append(f"-DCMAKE_BUILD_TYPE={self.__value['build_enum']}")

            exit_code = config_helper.configure_build(
                source_path,
                build_path,
                self.__value["cmake_generator"],
                self.__value["cmake_cxx_compiler"],
                self.__value["cmake_c_compiler"],
                cmake_flags,
                self.__value["arg_cmake_verbose"],
                self.__shell_flag(),
            )
            if exit_code == 0:
                print_status("Build configured successfully", "SUCCESS")
                if self.__value["cmake_generator"] == "Xcode":
                    config_helper.handle_xcode_project_opening()
            else:
                print_status("Configuration failed", "ERROR")
                sys.exit(1)
        except subprocess.CalledProcessError as e:
            self.error_logger.log_error("cmake", str(e), "Configuring the build system")
            print_status(f"Configuration failed: {e}", "ERROR")
            sys.exit(1)

    def build(self):
        if self.__value["build"] != "build":
            return 0
        print_status("Building project...", "INFO")
        try:
            exit_code = build_helper.build_project(
                self.__value["builder"], self.__value["build_enum"], self.__value["system"], self.__shell_flag()
            )
            if exit_code == 0:
                print_status("Build completed successfully", "SUCCESS")
            else:
                print_status("Build failed", "ERROR")
                sys.exit(1)
        except subprocess.CalledProcessError as e:
            self.error_logger.log_error("build", str(e), "Building the project")
            print_status(f"Build failed: {e}", "ERROR")
            sys.exit(1)

    def cppcheck(self, source_path, build_path):
        if self.__value["build"] != "build" or not self.__profiler_flags.is_cppcheck():
            return 0
        print_status("Starting static code analysis with cppcheck...", "INFO")
        try:
            version_result = subprocess.run(["cppcheck", "--version"], capture_output=True, check=True, text=True)
            print_status(f"Found cppcheck: {version_result.stdout.strip()}", "SUCCESS")
        except (subprocess.CalledProcessError, FileNotFoundError):
            print_status("cppcheck not found. Install it (e.g. 'brew install cppcheck').", "ERROR")
            return 1

        os.makedirs(build_path, exist_ok=True)
        output_file = os.path.join(build_path, "cppcheck_output.log")
        cppcheck_cmd = cppcheck_helper.build_cppcheck_command(source_path, output_file)

        try:
            original_dir = os.getcwd()
            os.chdir(source_path)
            result = subprocess.run(cppcheck_cmd, capture_output=True, text=True, check=False)
            os.chdir(original_dir)
            exit_code = cppcheck_helper.process_cppcheck_results(result, output_file)
            self.summary_reporter.add_cppcheck_report(output_file, exit_code)
            return exit_code
        except Exception as e:
            self.error_logger.log_error(" ".join(cppcheck_cmd), str(e), "Running cppcheck static analysis")
            print_status(f"Unexpected error during cppcheck execution: {e}", "ERROR")
            return 1

    def test(self, source_path, build_path):
        if self.__value["test"] != "test":
            return 0
        return test_helper.run_ctest(
            self.__value["builder"],
            self.__value["build_enum"],
            self.__value["system"],
            self.__value["verbosity"],
            self.__shell_flag(),
            sanitizer_types=self.__profiler_flags.get_sanitizer_types(),
            source_path=source_path,
        )

    def coverage(self, source_path, build_path):
        if self.__value["build"] != "build" or not self.__profiler_flags.is_coverage():
            return 0
        print_status("Starting code coverage collection and report generation...", "INFO")
        try:
            from coverage_tool import get_coverage
        except ImportError:
            print_status("coverage-tool is not installed. Install with: pip install coverage-tool", "ERROR")
            return 1

        from coverage_tool import gcc_coverage
        from coverage_tool.common import get_config

        os.makedirs(build_path, exist_ok=True)
        output_file = os.path.join(build_path, "coverage_output.log")
        with open(output_file, "w", encoding="utf-8") as log_file:
            log_file.write("")

        # coverage-tool (2026.9.14) passes --ignore-errors only on
        # `lcov --capture`, and filters with `lcov --remove`. Rewrite that
        # remove into `lcov --extract */Profiler/include/*` so the report
        # is first-party library sources only (not LLVM libc++, Apple SDK,
        # or other toolchain headers). Apply the coverage.toml ignore list
        # to every lcov invocation so lcov 2.x does not fail on
        # derive_function_end_line inconsistencies.
        orig_run = gcc_coverage.subprocess.run
        library_glob = "*/Profiler/include/*"

        def run_filtered_lcov(cmd, *args, **kwargs):
            if isinstance(cmd, list) and cmd and cmd[0] == "lcov":
                ignore = ",".join(get_config().get("lcov_ignore_errors") or [])
                if "--remove" in cmd:
                    info = cmd[cmd.index("--remove") + 1]
                    output = cmd[cmd.index("--output-file") + 1]
                    cmd = [
                        "lcov",
                        "--extract",
                        info,
                        library_glob,
                        "--output-file",
                        output,
                    ]
                    print_status(
                        f"Keeping coverage files matching {library_glob}",
                        "INFO",
                    )
                if ignore and "--ignore-errors" not in cmd:
                    cmd = [cmd[0], "--ignore-errors", ignore, *cmd[1:]]
                print_status(f"Running: {' '.join(cmd)}", "INFO")
            result = orig_run(cmd, *args, **kwargs)
            stdout = getattr(result, "stdout", "") or ""
            stderr = getattr(result, "stderr", "") or ""
            with open(output_file, "a", encoding="utf-8") as log_file:
                log_file.write(f"$ {' '.join(cmd) if isinstance(cmd, list) else cmd}\n")
                if stdout:
                    log_file.write(stdout)
                    if not stdout.endswith("\n"):
                        log_file.write("\n")
                if stderr:
                    log_file.write(stderr)
                    if not stderr.endswith("\n"):
                        log_file.write("\n")
            if getattr(result, "returncode", 0):
                self.error_logger.log_error(
                    " ".join(cmd) if isinstance(cmd, list) else str(cmd),
                    stderr or stdout,
                    "Running lcov for coverage collection",
                )
                if stderr.strip():
                    print_status(stderr.strip().splitlines()[-1], "ERROR")
            return result

        gcc_coverage.subprocess.run = run_filtered_lcov
        try:
            coverage_result = get_coverage(
                # Not "auto": ProfilerCoverage.cmake's profiler_enable_coverage()
                # instruments GCC *and* Clang identically with gcov-compatible
                # `--coverage` (see cmake/ProfilerCoverage.cmake) -- it never emits
                # Clang's native `-fprofile-instr-generate -fcoverage-mapping`.
                # coverage-tool's "auto" detection maps a Clang toolchain straight
                # to its LLVM/profraw backend, which then reports "No profraw
                # generated" because Profiler simply never produces one. The
                # lcov/gcov backend ("gcc" here is a routing choice in
                # coverage-tool, not a requirement to actually compile with GCC)
                # is what matches the coverage data Profiler's CMake produces,
                # regardless of which compiler built it -- this is also what CI's
                # coverage job and docs/profiler.md's manual recipe both use.
                compiler="gcc",
                build_folder=build_path,
                source_folder=source_path,
                output_folder=os.path.join(build_path, "coverage_report"),
                summary=True,
                project_root=source_path,
            )
        except Exception as e:
            self.error_logger.log_error("coverage-tool", str(e), "Coverage collection")
            print_status(f"Unexpected error during coverage collection: {e}", "ERROR")
            return 1
        finally:
            gcc_coverage.subprocess.run = orig_run
        if coverage_result == 0:
            print_status("Coverage collection completed successfully", "SUCCESS")
            print_status(f"Log file: {output_file}", "INFO")
            self.summary_reporter.add_coverage_report(build_path, 0)
            return 0
        self.error_logger.log_error(
            "coverage-tool get_coverage",
            f"exit code {coverage_result}",
            "Coverage collection",
        )
        print_status("Coverage collection failed", "ERROR")
        print_status(f"Log file: {output_file}", "INFO")
        return 1

    def __shell_flag(self):
        return self.__value["system"] == "Windows"

    def move_to_build_folder(self):
        os.chdir("..")
        build_folder = self.__value["build_folder"]
        if os.path.isdir(build_folder) and self.__value.get("config") == "config":
            shutil.rmtree(build_folder, ignore_errors=True)
        if not os.path.isdir(build_folder):
            os.mkdir(build_folder)
        os.chdir(build_folder)
        return os.getcwd()


def parse_args(args):
    """Parse command line arguments, handling dotted-flag notation first."""
    processed_args = []
    for arg in args:
        if arg.startswith("--sanitizer."):
            sanitizer_type = arg.split(".", 1)[1].lower()
            valid_sanitizers = ["address", "undefined", "thread", "memory", "leak"]
            if sanitizer_type in valid_sanitizers:
                processed_args.append(sanitizer_type)
            else:
                print_status(f"Invalid sanitizer type: {sanitizer_type}. Valid options: {', '.join(valid_sanitizers)}", "ERROR")
                sys.exit(1)
        elif arg.startswith("--backend."):
            processed_args.append(f"backend.{arg.split('.', 1)[1].lower()}")
        elif arg.startswith("--gpu."):
            processed_args.append(f"gpu.{arg.split('.', 1)[1].lower()}")
        elif re.search(r"[/\\]", arg) and re.search(r"[Cc]lang|[Gg][Cc][Cc]|[Gg]\+\+", arg):
            # Compiler path argument (e.g. C:/msys64/mingw64/bin/clang.exe) — pass through verbatim.
            processed_args.append(arg)
        else:
            for part in re.split(r"\.|\ ", arg.lower()):
                processed_args.extend(re.split(r"_", part))
    return processed_args


def main():
    if len(sys.argv) == 2 and sys.argv[1] == "--help":
        print_status("Profiler Build Configuration Helper", "INFO")
        print("\n" + "=" * 80)
        print("DEFAULT CONFIGURATION:")
        print("  Build System: Ninja (fast, cross-platform)")
        print("  Compiler:     Clang (clang/clang++)")
        print("  Backend:      KINETO")
        print("=" * 80)
        print("\nUsage examples:")
        print("  1. Default build (Ninja + Clang):")
        print("     setup.py config.build.test")
        print("  2. Release build with GCC:")
        print("     setup.py config.build.test.gcc.release")
        print("  3. macOS build with Xcode:")
        print("     setup.py config.build.test.xcode")
        print("  4. Build with coverage (analysis runs automatically):")
        print("     setup.py config.build.test.coverage")
        print("  5. Build against the Intel ITT instrumentation backend:")
        print("     setup.py config.build.test --backend.itt")
        print("  6. Build with the CUDA GPU backend:")
        print("     setup.py config.build.test --gpu.cuda")
        print("\nBuild commands:")
        print("  config    - Configure the build system")
        print("  build     - Build the project")
        print("  test      - Run tests")
        print("  coverage  - Enable coverage (automatically displays summary)")
        print("\nSanitizer flags (combinable, e.g. --sanitizer.address --sanitizer.undefined):")
        print("  --sanitizer.address | .undefined | .thread | .memory | .leak")
        print("\nAvailable options:")
        ProfilerFlags([]).helper()
        return

    try:
        arg_list = parse_args(sys.argv[1:])
        if not arg_list:
            print_status("No build configuration specified. Use --help for usage information.", "ERROR")
            sys.exit(1)

        print_status(f"Starting build configuration for {platform.system()}", "INFO")
        compilation_calc = ProfilerConfiguration(arg_list)

        source_path = os.path.dirname(os.getcwd())
        build_path = compilation_calc.move_to_build_folder()
        print_status(f"Build directory: {build_path}", "INFO")

        try:
            start = time.perf_counter()
            compilation_calc.config(source_path, build_path)
            config_end = time.perf_counter()

            compilation_calc.build()
            build_end = time.perf_counter()

            compilation_calc.cppcheck(source_path, build_path)
            cppcheck_end = time.perf_counter()

            test_rc = compilation_calc.test(source_path, build_path)
            test_end = time.perf_counter()
            if test_rc != 0:
                print_status("Tests failed", "ERROR")
                sys.exit(test_rc)

            coverage_rc = compilation_calc.coverage(source_path, build_path)
            end = time.perf_counter()
            if coverage_rc != 0:
                print_status("Coverage collection failed", "ERROR")
                sys.exit(coverage_rc)

            print_status(f"Config time: {config_end - start:.4f} seconds", "INFO")
            print_status(f"Build time: {build_end - config_end:.4f} seconds", "INFO")
            print_status(f"Cppcheck time: {cppcheck_end - build_end:.4f} seconds", "INFO")
            print_status(f"Test time: {test_end - cppcheck_end:.4f} seconds", "INFO")
            print_status(f"Coverage time: {end - test_end:.4f} seconds", "INFO")
            print_status(f"Total time: {end - start:.4f} seconds", "INFO")
            print_status("Build process completed successfully!", "SUCCESS")

            compilation_calc.summary_reporter.display_summary()
            if compilation_calc.error_logger.has_errors():
                print_status(f"Error log available at: {compilation_calc.error_logger.get_log_file_path()}", "INFO")

        except SystemExit:
            compilation_calc.summary_reporter.display_summary()
            raise

    except KeyboardInterrupt:
        print_status("\nBuild process interrupted by user", "WARNING")
        sys.exit(1)
    except Exception as e:
        print_status(f"An unexpected error occurred: {e}", "ERROR")
        if DEBUG_FLAG:
            raise
        sys.exit(1)


if __name__ == "__main__":
    main()
