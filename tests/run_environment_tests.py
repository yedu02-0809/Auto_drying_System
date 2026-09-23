#!/usr/bin/env python3
"""Compile and test the real environment service using queue and console mocks."""
import argparse
import ctypes
import os
from pathlib import Path
import subprocess
import sys

from run_sht30_tests import find_compiler


ROOT = Path(__file__).resolve().parents[1]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cc", help="Host compiler (Windows: MinGW-w64 GCC; Linux: cc/gcc/clang)")
    args = parser.parse_args()
    try:
        cc = find_compiler(args.cc)
        output = ROOT / "build" / "environment_host_tests"
        output.mkdir(parents=True, exist_ok=True)
        library = output / ("environment_tests.dll" if os.name == "nt" else "libenvironment_tests.so")
        flags = ["-std=c11", "-Wall", "-Wextra", "-Werror", "-O2", "-shared"]
        # Older MinGW GCC warns about standard nested-struct {0} initialization.
        flags += ["-Wno-missing-braces", "-Wno-missing-field-initializers"]
        if os.name == "nt":
            if ctypes.sizeof(ctypes.c_void_p) != 8:
                raise RuntimeError("Windows host tests require 64-bit Python and MinGW-w64 GCC.")
            flags += ["-static-libgcc", "-Wl,--export-all-symbols"]
        else:
            flags += ["-fPIC"]
        subprocess.run([cc, *flags,
                        "-I" + str(ROOT / "tests" / "environment_mocks"),
                        "-I" + str(ROOT / "components" / "BSP" / "RTC"),
                        str(ROOT / "tests" / "test_environment.c"),
                        "-I" + str(ROOT / "components" / "BSP" / "SHT20"),
                        "-o", str(library)], check=True)
        dll = ctypes.CDLL(str(library))
        dll.environment_test_count.argtypes = []
        dll.environment_test_count.restype = ctypes.c_int
        dll.environment_test_name.argtypes = [ctypes.c_int]
        dll.environment_test_name.restype = ctypes.c_char_p
        dll.environment_test_run.argtypes = [ctypes.c_int]
        dll.environment_test_run.restype = ctypes.c_int
        count = dll.environment_test_count()
        failures = 0
        for index in range(count):
            line = dll.environment_test_run(index)
            if line:
                failures += 1
                name = dll.environment_test_name(index).decode("ascii")
                print(f"FAIL {name}: tests/test_environment.c:{line}")
        print(f"{count - failures}/{count} environment host tests passed")
        return 1 if failures else 0
    except (OSError, RuntimeError, subprocess.CalledProcessError) as error:
        print(f"environment test runner error: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
