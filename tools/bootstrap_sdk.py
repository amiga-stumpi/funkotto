#!/usr/bin/env python3
"""Fetch exactly the SDK and USB submodule used by the M1 firmware."""
import argparse
import json
from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parents[1]

def run(*args):
    subprocess.run(args, check=True)

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("destination", nargs="?", default=str(ROOT / ".deps/pico-sdk"))
    args = parser.parse_args()
    dest = Path(args.destination).resolve()
    lock = json.loads((ROOT / "firmware/dependencies.json").read_text())
    sdk = lock["pico_sdk"]
    if not dest.exists():
        dest.parent.mkdir(parents=True, exist_ok=True)
        run("git", "clone", "--depth", "1", "--branch", sdk["tag"], sdk["url"], str(dest))
    head = subprocess.check_output(["git", "-C", str(dest), "rev-parse", "HEAD"], text=True).strip()
    if head != sdk["commit"]:
        raise SystemExit("SDK commit differs; choose a fresh destination (nothing overwritten)")
    run("git", "-C", str(dest), "submodule", "update", "--init", "--depth", "1", "lib/tinyusb")
    usb = subprocess.check_output(["git", "-C", str(dest / "lib/tinyusb"), "rev-parse", "HEAD"], text=True).strip()
    if usb != lock["tinyusb"]["commit"]:
        raise SystemExit("TinyUSB commit mismatch")
    print(f"PICO_SDK_PATH={dest}")

if __name__ == "__main__":
    main()
