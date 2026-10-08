#!/usr/bin/env python3
"""Validate the built M1 image and write its reproducibility manifest."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import struct
import subprocess

ROOT = Path(__file__).resolve().parents[1]
FLASH_START = 0x10000000
CONFIG_START = 0x103FE000
IMAGE_LIMIT = 0x103FD000
E10_BLOCK_ADDRESS = 0x103FDF00
RP2350_ARM_S = 0xE48BFF59

def validate_uf2(data):
    if not data or len(data) % 512:
        raise ValueError("UF2 length must be a nonzero multiple of 512")
    prefix = data[:512]
    expected = (0x0A324655, 0x9E5D5157, 0xA000, E10_BLOCK_ADDRESS, 256, 0, 2, 0xE48BFF57)
    if (struct.unpack_from("<8I", prefix) != expected or
            prefix[32:288] != bytes([0xEF]) * 256 or
            struct.unpack_from("<I", prefix, 288)[0] != 0x9957E304 or
            struct.unpack_from("<I", prefix, 508)[0] != 0x0AB16F30):
        raise ValueError("Missing/unsafe RP2350-E10 compatibility block")
    data = data[512:]
    count = len(data) // 512
    if not count:
        raise ValueError("No firmware blocks")
    addresses = set()
    for index in range(count):
        block = data[index * 512:(index + 1) * 512]
        magic0, magic1, flags, address, size, number, total, family = struct.unpack_from("<8I", block)
        if (magic0, magic1, struct.unpack_from("<I", block, 508)[0]) != (0x0A324655, 0x9E5D5157, 0x0AB16F30):
            raise ValueError("Bad UF2 magic")
        # After the strictly checked E10 prefix, accept only application blocks.
        if flags != 0x2000 or family != RP2350_ARM_S or size != 256:
            raise ValueError("Unexpected UF2 flags, architecture or payload size")
        if number != index or total != count or address % 256 or address in addresses:
            raise ValueError("Invalid UF2 block ordering/address")
        if address < FLASH_START or address + size > IMAGE_LIMIT:
            raise ValueError("UF2 writes outside application flash / into profile sectors")
        addresses.add(address)
    return {"blocks": count, "first_address": hex(min(addresses)),
            "end_address_exclusive": hex(max(addresses) + 256), "family": hex(RP2350_ARM_S),
            "e10_block_address": hex(E10_BLOCK_ADDRESS)}

def output(*command):
    return subprocess.check_output(command, text=True).strip()

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("build_dir", type=Path)
    parser.add_argument("--tool-prefix", default="arm-none-eabi-")
    args = parser.parse_args()
    build = args.build_dir.resolve()
    elf = build / "funkotto_m1.elf"
    uf2 = build / "funkotto_m1.uf2"
    info = validate_uf2(uf2.read_bytes())
    nm = output(args.tool_prefix + "nm", "-n", str(elf))
    symbols = {m[3]: int(m[1], 16) for line in nm.splitlines()
               if (m := re.fullmatch(r"([0-9a-fA-F]+)\s+(\w)\s+(\S+)", line))}
    if symbols.get("__funkotto_config_start") != CONFIG_START:
        raise SystemExit("Linker reservation differs from profile layout")
    if symbols.get("__funkotto_image_limit") != IMAGE_LIMIT:
        raise SystemExit("Linker does not reserve the E10 workaround sector")
    if not FLASH_START < symbols.get("__flash_binary_end", 0) <= IMAGE_LIMIT:
        raise SystemExit("ELF flash range invalid")
    if "stdio_uart_init" in symbols or "uart_init" in symbols:
        raise SystemExit("Unexpected UART linked: GP0/GP1 are parallel data pins")
    header = (build / "generated/funkotto/build_info.h").read_text()
    defines = dict(re.findall(r'#define\s+(\w+)\s+"([^"\n]*)"', header))
    source_paths = [p for d in ("firmware", "tools", "tests") for p in (ROOT / d).rglob("*")
                    if p.is_file() and "__pycache__" not in p.parts]
    sources = {str(p.relative_to(ROOT)): hashlib.sha256(p.read_bytes()).hexdigest()
               for p in sorted(source_paths)}
    artifacts = {}
    for suffix in ("elf", "uf2", "bin", "elf.map"):
        p = build / ("funkotto_m1." + suffix)
        artifacts[p.name] = {"size": p.stat().st_size, "sha256": hashlib.sha256(p.read_bytes()).hexdigest()}
    manifest = {
        "version": defines["FUNKOTTO_VERSION"], "source_id": defines["FUNKOTTO_SOURCE_ID"],
        "stage": "M1 USB diagnostics; no active parallel bus or WLAN",
        "board": "pico2_w", "platform": "rp2350-arm-s", "build_type": "Release",
        "dependencies": json.loads((ROOT / "firmware/dependencies.json").read_text()),
        "compiler": output(args.tool_prefix + "gcc", "--version").splitlines()[0],
        "cmake": output("cmake", "--version").splitlines()[0],
        "ninja": output("ninja", "--version"),
        "hardware_pcb_sha256": hashlib.sha256((ROOT / "hardware/AmiWiFi.kicad_pcb").read_bytes()).hexdigest(),
        "profile_flash_start": hex(CONFIG_START), "profile_flash_bytes": 8192,
        "e10_reserved_sector_start": hex(IMAGE_LIMIT),
        "uart_absent": True, "uf2": info, "sources_sha256": sources, "artifacts": artifacts,
        "hardware_tested": False,
    }
    (build / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n")
    print(json.dumps({"uf2": info, "elf_flash_end": hex(symbols["__flash_binary_end"]),
                      "profile_sectors_untouched": True, "uart_absent": True}, indent=2))

if __name__ == "__main__":
    main()
