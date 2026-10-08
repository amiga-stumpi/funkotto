#!/usr/bin/env python3
import os
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory(prefix="funkotto-tests-") as tmp:
    binary = str(Path(tmp) / "test_m1")
    subprocess.run([
        os.environ.get("CC", "cc"), "-std=c11", "-Wall", "-Wextra", "-Werror",
        "-Wconversion", "-Wshadow", "-g", "-fno-omit-frame-pointer",
        "-fsanitize=address,undefined", "-I" + str(ROOT / "tests/fakes"),
        "-I" + str(ROOT / "firmware/include"),
        str(ROOT / "tests/test_m1.c"), str(ROOT / "firmware/src/board.c"),
        str(ROOT / "firmware/src/console.c"), "-o", binary,
    ], check=True)
    subprocess.run([binary], check=True)
