#!/usr/bin/env python3
"""Compile and test the pure JSON builder used by the web page."""
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
        output = ROOT / "build" / "web_status_host_tests"
        output.mkdir(parents=True, exist_ok=True)
        library = output / ("web_status_tests.dll" if os.name == "nt" else "libweb_status_tests.so")
        flags = ["-std=c11", "-Wall", "-Wextra", "-Werror", "-O2", "-shared"]
        if os.name == "nt":
            if ctypes.sizeof(ctypes.c_void_p) != 8:
                raise RuntimeError("Windows host tests require 64-bit Python and MinGW-w64 GCC.")
            flags += ["-static-libgcc", "-Wl,--export-all-symbols"]
        else:
            flags += ["-fPIC"]
        subprocess.run([cc, *flags,
                        "-I" + str(ROOT / "main"),
                        str(ROOT / "tests" / "test_web_status.c"),
                        str(ROOT / "main" / "web_status.c"),
                        "-o", str(library)], check=True)
        dll = ctypes.CDLL(str(library))
        dll.web_status_test_count.argtypes = []
        dll.web_status_test_count.restype = ctypes.c_int
        dll.web_status_test_name.argtypes = [ctypes.c_int]
        dll.web_status_test_name.restype = ctypes.c_char_p
        dll.web_status_test_run.argtypes = [ctypes.c_int]
        dll.web_status_test_run.restype = ctypes.c_int
        count = dll.web_status_test_count()
        failures = 0
        for index in range(count):
            line = dll.web_status_test_run(index)
            if line:
                failures += 1
                name = dll.web_status_test_name(index).decode("ascii")
                print(f"FAIL {name}: tests/test_web_status.c:{line}")
        print(f"{count - failures}/{count} web status host tests passed")
        return 1 if failures else 0
    except (OSError, RuntimeError, subprocess.CalledProcessError) as error:
        print(f"Web status test runner error: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
