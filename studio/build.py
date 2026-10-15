#!/usr/bin/env python3
"""Build khdays-studio, the mod authoring tool.

  python studio/build.py [--config Debug|Release] [--test] [--jobs N]

Configures studio/CMakeLists.txt with Ninja inside Visual Studio's x64
environment -- found the way native/build.py finds it, by importing that
script -- and builds. Output: build/studio/debug/khdays-studio.exe (Release:
build/studio/release/). --test runs the tool's tests afterwards. The port's
own build is not touched.
"""
from __future__ import annotations

import argparse
import importlib.util
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def native_build():
    """native/build.py as a module: its Visual Studio environment and runner."""
    spec = importlib.util.spec_from_file_location("khdays_native_build", ROOT / "native" / "build.py")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--config", default="Debug", choices=["Debug", "Release"])
    ap.add_argument("--test", action="store_true", help="run the tool's tests after building")
    ap.add_argument("--jobs", type=int, default=0)
    args = ap.parse_args()

    native = native_build()
    env = native.msvc_env()
    obj = ROOT / "build" / "studio" / args.config.lower()
    native.run(["cmake", "-S", str(ROOT / "studio"), "-B", str(obj), "-G", "Ninja",
                f"-DCMAKE_BUILD_TYPE={args.config}",
                "-DCMAKE_C_COMPILER=cl", "-DCMAKE_CXX_COMPILER=cl"], env)
    build = ["cmake", "--build", str(obj)]
    if args.jobs:
        build += ["--parallel", str(args.jobs)]
    native.run(build + ["--", "-k", "0"], env)
    if args.test:
        native.run(["ctest", "--test-dir", str(obj), "--output-on-failure"], env)
    return 0


if __name__ == "__main__":
    sys.exit(main())
