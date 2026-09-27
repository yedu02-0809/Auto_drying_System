#!/usr/bin/env python3
"""Build the production BH1750 driver with I2C mocks at 100 Hz and 1000 Hz ticks."""
import argparse
import ctypes
import os
from pathlib import Path
import subprocess
import sys

from run_sht30_tests import find_compiler


ROOT = Path(__file__).resolve().parents[1]


def run_configuration(cc, hz):
    output = ROOT / "build" / "bh1750_host_tests" / str(hz)
    output.mkdir(parents=True, exist_ok=True)
    library = output / ("bh1750_tests.dll" if os.name == "nt" else "libbh1750_tests.so")
    flags = ["-std=c11", "-Wall", "-Wextra", "-Werror", "-O2", "-shared",
             f"-DCONFIG_FREERTOS_HZ={hz}"]
    if os.name == "nt":
        if ctypes.sizeof(ctypes.c_void_p) != 8:
            raise RuntimeError("Windows host tests require 64-bit Python and MinGW-w64 GCC.")
        flags += ["-static-libgcc", "-Wl,--export-all-symbols"]
    else:
        flags += ["-fPIC"]
    component = ROOT / "components" / "BSP" / "BH1750"
    subprocess.run([cc, *flags, "-I" + str(ROOT / "tests" / "mocks"),
                    "-I" + str(component), str(ROOT / "tests" / "test_bh1750.c"),
                    str(component / "bh1750.c"), "-o", str(library)], check=True)
    dll = ctypes.CDLL(str(library))
    dll.bh1750_test_count.argtypes = []
    dll.bh1750_test_count.restype = ctypes.c_int
    dll.bh1750_test_name.argtypes = [ctypes.c_int]
    dll.bh1750_test_name.restype = ctypes.c_char_p
    dll.bh1750_test_run.argtypes = [ctypes.c_int]
    dll.bh1750_test_run.restype = ctypes.c_int
    count = dll.bh1750_test_count()
    failures = 0
    for index in range(count):
        line = dll.bh1750_test_run(index)
        if line:
            failures += 1
            name = dll.bh1750_test_name(index).decode("ascii")
            print(f"FAIL [{hz} Hz] {name}: tests/test_bh1750.c:{line}")
    print(f"[{hz} Hz] {count - failures}/{count} BH1750 host tests passed")
    return failures


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cc", help="Host compiler (Windows: MinGW-w64 GCC; Linux: cc/gcc/clang)")
    args = parser.parse_args()
    try:
        cc = find_compiler(args.cc)
        failures = sum(run_configuration(cc, hz) for hz in (100, 1000))
        return 1 if failures else 0
    except (OSError, RuntimeError, subprocess.CalledProcessError) as error:
        print(f"BH1750 test runner error: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
