"""Build a pinned buildcache on the runner instead of relying on package aliases."""

import argparse
import os
import shutil
import subprocess
from pathlib import Path


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--prefix", type=Path, required=True)
    args = parser.parse_args()
    prefix = args.prefix.resolve()
    source, build = prefix / "source", prefix / "build"
    subprocess.run(
        [
            "git",
            "clone",
            "--depth",
            "1",
            "--branch",
            "v0.33.1",
            "--recurse-submodules",
            "https://gitlab.com/bits-n-bites/buildcache.git",
            str(source),
        ],
        check=True,
    )
    cmake = [
        "cmake",
        "-S",
        str(source / "src"),
        "-B",
        str(build),
        "-G",
        "Ninja",
        "-DCMAKE_BUILD_TYPE=Release",
        # CI uses local storage; avoid runner-specific OpenSSL DLLs.
        "-DDISABLE_SSL=ON",
        # CMake 4 rejects cmake_minimum_required() below 3.5. BuildCache's
        # MinGW threading helper still declares 3.0.
        "-DCMAKE_POLICY_VERSION_MINIMUM=3.5",
        f"-DCMAKE_INSTALL_PREFIX={prefix}",
    ]
    # Windows images put MinGW ahead of cl on PATH. Ninja then configures the
    # MinGW helper, which is unnecessary once the MSVC developer environment
    # is active.
    if os.name == "nt" and os.environ.get("VSCMD_ARG_TGT_ARCH"):
        cl = shutil.which("cl")
        if cl:
            cmake.extend(
                [f"-DCMAKE_C_COMPILER={cl}", f"-DCMAKE_CXX_COMPILER={cl}"]
            )
    subprocess.run(cmake, check=True)
    subprocess.run(
        ["cmake", "--build", str(build), "--target", "buildcache", "--parallel", "4"],
        check=True,
    )
    executable = "buildcache.exe" if os.name == "nt" else "buildcache"
    binaries = [path for path in build.rglob(executable) if path.is_file()]
    if not binaries:
        raise RuntimeError(
            "buildcache built without producing the requested executable"
        )
    destination = prefix / "bin"
    destination.mkdir(parents=True, exist_ok=True)
    shutil.copy2(binaries[0], destination / executable)
    for pattern in ("*.dll", "*.dylib", "*.so*"):
        for library in build.rglob(pattern):
            if library.is_file():
                shutil.copy2(library, destination / library.name)
    subprocess.run([str(destination / executable), "--version"], check=True)
    with Path(os.environ["GITHUB_PATH"]).open("a", encoding="utf-8") as output:
        output.write(str(destination) + "\n")


if __name__ == "__main__":
    main()
