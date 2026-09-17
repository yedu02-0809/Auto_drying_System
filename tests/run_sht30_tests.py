#!/usr/bin/env python3
"""Build the real SHT30 driver with host mocks; no board or Python packages needed."""
import argparse
import ctypes
import os
from pathlib import Path
import shutil
import subprocess
import sys


ROOT = Path(__file__).resolve().parents[1]
EXPORTS = ("sht30_test_count", "sht30_test_name", "sht30_test_run")


def find_compiler(requested):
    if requested:
        return requested
    env_cc = os.environ.get("CC")
    if env_cc:
        return env_cc
    for name in (("gcc", "clang") if os.name == "nt" else ("cc", "clang", "gcc")):
        found = shutil.which(name)
        if found:
            return found
    raise RuntimeError("No host compiler found. Pass --cc PATH (Windows: MinGW-w64 GCC or host clang).")


def build_library(cc, linker, hz):
    output = ROOT / "build" / "sht30_host_tests" / str(hz)
    output.mkdir(parents=True, exist_ok=True)
    includes = ["-I" + str(ROOT / "tests" / "mocks"),
                "-I" + str(ROOT / "components" / "BSP" / "SHT20")]
    sources = [ROOT / "tests" / "test_sht30.c",
               ROOT / "components" / "BSP" / "SHT20" / "sht30.c"]
    flags = ["-std=c11", "-Wall", "-Wextra", "-Werror", "-O2",
             "-ffreestanding", "-fno-builtin", "-fno-stack-protector",
             f"-DCONFIG_FREERTOS_HZ={hz}", *includes]
    if os.name == "nt":
        if ctypes.sizeof(ctypes.c_void_p) != 8:
            raise RuntimeError("Windows host tests require 64-bit Python.")
        library = output / "sht30_tests.dll"
        if "clang" not in Path(cc).name.lower():
            subprocess.run([cc, *flags, "-shared", "-static-libgcc",
                            "-Wl,--export-all-symbols", *(str(p) for p in sources),
                            "-o", str(library)], check=True)
            return library
        linker = linker or str(Path(cc).resolve().with_name("ld.lld.exe"))
        objects = []
        for source in sources:
            obj = output / (source.stem + ".obj")
            subprocess.run([cc, "--target=x86_64-pc-windows-msvc", *flags,
                            "-c", str(source), "-o", str(obj)], check=True)
            objects.append(str(obj))
        subprocess.run([linker, "-flavor", "link", "/dll", "/noentry", "/nodefaultlib",
                        "/machine:x64", f"/out:{library}",
                        *(f"/export:{name}" for name in EXPORTS), *objects], check=True)
    else:
        library = output / "libsht30_tests.so"
        subprocess.run([cc, *flags, "-shared", "-fPIC", *(str(p) for p in sources),
                        "-o", str(library)], check=True)
    return library


def run_library(library, hz):
    dll = ctypes.CDLL(str(library))
    dll.sht30_test_count.argtypes = []
    dll.sht30_test_count.restype = ctypes.c_int
    dll.sht30_test_name.argtypes = [ctypes.c_int]
    dll.sht30_test_name.restype = ctypes.c_char_p
    dll.sht30_test_run.argtypes = [ctypes.c_int]
    dll.sht30_test_run.restype = ctypes.c_int
    count = dll.sht30_test_count()
    failures = 0
    for index in range(count):
        name = dll.sht30_test_name(index).decode("ascii")
        line = dll.sht30_test_run(index)
        if line:
            failures += 1
            print(f"FAIL [{hz} Hz] {name}: tests/test_sht30.c:{line}")
    print(f"[{hz} Hz] {count - failures}/{count} SHT30 host tests passed")
    return failures


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cc", help="Host C compiler executable (Windows: MinGW-w64 GCC or host clang)")
    parser.add_argument("--linker", help="Windows ld.lld.exe; defaults beside clang")
    args = parser.parse_args()
    try:
        cc = find_compiler(args.cc)
        failures = 0
        for hz in (100, 1000):
            failures += run_library(build_library(cc, args.linker, hz), hz)
        return 1 if failures else 0
    except (OSError, RuntimeError, subprocess.CalledProcessError) as error:
        print(f"SHT30 test runner error: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
