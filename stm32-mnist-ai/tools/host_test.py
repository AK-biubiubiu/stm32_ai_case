#!/usr/bin/env python3
"""Compile and run tools/host_test.c on the PC (Windows / Linux / macOS)."""
import os
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT = os.path.join(ROOT, "build_host")
EXE = os.path.join(OUT, "host_test.exe" if os.name == "nt" else "host_test")


def main():
    os.makedirs(OUT, exist_ok=True)
    cmd = [
        "gcc", "-std=c99", "-O2", "-Wall", "-Wextra", "-Wno-unused-parameter",
        "-I" + os.path.join(ROOT, "App"),
        "-I" + os.path.join(ROOT, "Model"),
        os.path.join(ROOT, "App", "ai_mlp.c"),
        os.path.join(ROOT, "App", "ai_preprocess.c"),
        os.path.join(ROOT, "tools", "host_test.c"),
        "-lm", "-o", EXE,
    ]
    print(" ".join(cmd))
    subprocess.run(cmd, check=True)
    return subprocess.run([EXE, os.path.join(ROOT, "tools", "data")]).returncode


if __name__ == "__main__":
    sys.exit(main())
