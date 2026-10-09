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

with tempfile.TemporaryDirectory(prefix="funkotto-wifi-tests-") as tmp:
    binary = str(Path(tmp) / "test_wifi")
    subprocess.run([
        os.environ.get("CC", "cc"), "-std=c11", "-Wall", "-Wextra", "-Werror",
        "-Wconversion", "-Wshadow", "-g", "-fno-omit-frame-pointer",
        "-fsanitize=address,undefined", "-I" + str(ROOT / "firmware/include"),
        str(ROOT / "tests/test_wifi.c"), str(ROOT / "firmware/src/wifi_model.c"),
        str(ROOT / "firmware/src/wifi_console.c"), "-o", binary,
    ], check=True)
    subprocess.run([binary], check=True)

with tempfile.TemporaryDirectory(prefix="funkotto-service-tests-") as tmp:
    binary = str(Path(tmp) / "test_wifi_service")
    subprocess.run([
        os.environ.get("CC", "cc"), "-std=c11", "-Wall", "-Wextra", "-Werror",
        "-Wconversion", "-Wshadow", "-g", "-fno-omit-frame-pointer",
        "-fsanitize=address,undefined", "-I" + str(ROOT / "tests/wifi_fakes"),
        "-I" + str(ROOT / "firmware/include"),
        str(ROOT / "tests/test_wifi_service.c"), str(ROOT / "firmware/src/wifi_model.c"),
        "-o", binary,
    ], check=True)
    subprocess.run([binary], check=True)

with tempfile.TemporaryDirectory(prefix="funkotto-w3-tests-") as tmp:
    binary = str(Path(tmp) / "test_w3")
    subprocess.run([
        os.environ.get("CC", "cc"), "-std=c11", "-Wall", "-Wextra", "-Werror",
        "-Wconversion", "-Wshadow", "-g", "-fno-omit-frame-pointer",
        "-fsanitize=address,undefined", "-I" + str(ROOT / "firmware/include"),
        str(ROOT / "tests/test_w3.c"), str(ROOT / "firmware/src/ethernet.c"),
        str(ROOT / "firmware/src/raw_wire.c"), "-o", binary,
    ], check=True)
    subprocess.run([binary], check=True)
    binary = str(Path(tmp) / "test_w3_service")
    subprocess.run([
        os.environ.get("CC", "cc"), "-std=c11", "-Wall", "-Wextra", "-Werror",
        "-Wconversion", "-Wshadow", "-g", "-fno-omit-frame-pointer", "-DFUNKOTTO_W3=1",
        "-fsanitize=address,undefined", "-I" + str(ROOT / "tests/wifi_fakes"),
        "-I" + str(ROOT / "firmware/include"),
        str(ROOT / "tests/test_wifi_service.c"), str(ROOT / "firmware/src/wifi_model.c"),
        str(ROOT / "firmware/src/ethernet.c"), "-o", binary,
    ], check=True)
    subprocess.run([binary], check=True)

with tempfile.TemporaryDirectory(prefix="funkotto-w4-tests-") as tmp:
    common = [os.environ.get("CC", "cc"), "-std=c11", "-Wall", "-Wextra", "-Werror",
              "-Wconversion", "-Wshadow", "-g", "-fno-omit-frame-pointer", "-fsanitize=address,undefined",
              "-I" + str(ROOT / "firmware/include")]
    for name, sources, includes in [
        ("test_profile_store", ["tests/test_profile_store.c", "firmware/src/profile_store.c", "firmware/src/wifi_model.c"], []),
        ("test_profile_flash", ["tests/test_profile_flash.c"], ["-I" + str(ROOT / "tests/flash_fakes")]),
        ("test_w4_service", ["tests/test_wifi_service.c", "firmware/src/wifi_model.c", "firmware/src/ethernet.c", "firmware/src/profile_store.c"],
         ["-DFUNKOTTO_W3=1", "-DFUNKOTTO_W4=1", "-I" + str(ROOT / "tests/wifi_fakes")]),
        ("test_w4_console", ["tests/test_wifi.c", "firmware/src/wifi_console.c", "firmware/src/wifi_model.c"], ["-DFUNKOTTO_W4=1"]),
    ]:
        binary = str(Path(tmp) / name)
        subprocess.run(common + includes + [str(ROOT / s) for s in sources] + ["-o", binary], check=True)
        subprocess.run([binary], check=True)
