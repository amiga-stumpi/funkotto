#!/usr/bin/env python3
"""Bundle validated W1/W2 outputs, instructions and dependency notices."""
import argparse
from pathlib import Path
import zipfile

ROOT = Path(__file__).resolve().parents[1]

def main():
    p = argparse.ArgumentParser()
    p.add_argument('build_dir', type=Path)
    p.add_argument('sdk', type=Path)
    p.add_argument('output', type=Path)
    p.add_argument('--toolchain-docs', type=Path, default=Path('/usr/share/doc'))
    args = p.parse_args()
    args.output.parent.mkdir(parents=True, exist_ok=True)
    with zipfile.ZipFile(args.output, 'w', zipfile.ZIP_DEFLATED) as z:
        for suffix in ['uf2', 'elf', 'elf.map', 'bin']:
            name = 'funkotto_w12.' + suffix
            z.write(args.build_dir / name, name)
        z.write(args.build_dir / 'funkotto_w12-manifest.json', 'manifest.json')
        z.write(ROOT / 'firmware/WLAN_TEST.md', 'ANLEITUNG.md')
        z.write(ROOT / 'firmware/dependencies.json', 'dependencies.json')
        z.write(ROOT / 'docs/results/2026-10-08-wlan-w12.md', 'PRUEFBERICHT.md')
        for name, path in {
            'pico-sdk.txt': 'LICENSE.TXT',
            'tinyusb.txt': 'lib/tinyusb/LICENSE',
            'pico-printf.txt': 'src/rp2_common/pico_printf/LICENSE',
            'cmsis.txt': 'src/rp2_common/cmsis/stub/CMSIS/LICENSE.txt',
            'cyw43-raspberry-pi.txt': 'lib/cyw43-driver/LICENSE.RP',
            'cyw43-alternative.txt': 'lib/cyw43-driver/LICENSE',
        }.items():
            z.write(args.sdk / path, 'licenses/' + name)
        for name in ['libnewlib-arm-none-eabi', 'gcc-arm-none-eabi']:
            z.write(args.toolchain_docs / name / 'copyright', 'licenses/' + name + '-copyright.txt')
        z.writestr('QUELLEN.txt', 'https://github.com/amiga-stumpi/funkotto\n'
                   'Exact source commit: source_id in manifest.json.\n'
                   'Pico 2 W W1/W2 test firmware; RAM-only credentials; no Amiga network traffic.\n')
    print(args.output.resolve())

if __name__ == '__main__':
    main()
