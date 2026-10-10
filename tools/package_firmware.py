#!/usr/bin/env python3
"""Bundle manifest-validated outputs and verify the complete ZIP before publishing."""
import argparse
import hashlib
import io
import json
import os
from pathlib import Path
import tempfile
import zipfile

ROOT = Path(__file__).resolve().parents[1]


def write_verified_zip(output, entries):
    """Finalize in memory, then atomically write/fsync; never publish a partial ZIP."""
    buffer = io.BytesIO()
    with zipfile.ZipFile(buffer, 'w', zipfile.ZIP_STORED) as archive:
        for name, data in entries.items():
            archive.writestr(name, data)
    data = buffer.getvalue()
    with zipfile.ZipFile(io.BytesIO(data)) as archive:
        if archive.testzip() is not None or set(archive.namelist()) != set(entries):
            raise ValueError('ZIP member validation failed')
        for name, expected in entries.items():
            if archive.read(name) != expected:
                raise ValueError('ZIP content mismatch: ' + name)
    output.parent.mkdir(parents=True, exist_ok=True)
    temporary = None
    try:
        with tempfile.NamedTemporaryFile(dir=output.parent, prefix=output.name+'.', delete=False) as file:
            temporary = Path(file.name)
            file.write(data); file.flush(); os.fsync(file.fileno())
        if temporary.read_bytes() != data:
            raise ValueError('ZIP disk verification failed')
        os.replace(temporary, output)
        temporary = None
        if output.read_bytes() != data:
            raise ValueError('ZIP final verification failed')
    finally:
        if temporary is not None:
            temporary.unlink(missing_ok=True)
    return hashlib.sha256(data).hexdigest()


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('build_dir', type=Path)
    parser.add_argument('sdk', type=Path)
    parser.add_argument('output', type=Path)
    parser.add_argument('--target', choices=['funkotto_w12', 'funkotto_w3', 'funkotto_w4', 'funkotto_m4_usb'], default='funkotto_w12')
    parser.add_argument('--toolchain-docs', type=Path, default=Path('/usr/share/doc'))
    args = parser.parse_args()
    entries = {}
    manifest_path = args.build_dir / (args.target + '-manifest.json')
    manifest = json.loads(manifest_path.read_text())
    for suffix in ['uf2', 'elf', 'elf.map', 'bin']:
        name = args.target + '.' + suffix
        data = (args.build_dir / name).read_bytes()
        expected = manifest['artifacts'][name]
        if len(data) != expected['size'] or hashlib.sha256(data).hexdigest() != expected['sha256']:
            raise ValueError('Artifact differs from manifest: ' + name)
        entries[name] = data
    entries['manifest.json'] = manifest_path.read_bytes()
    entries['ANLEITUNG.md'] = (ROOT / 'firmware' / ('M4_USB_TEST.md' if args.target == 'funkotto_m4_usb' else 'W4_TEST.md' if args.target == 'funkotto_w4' else 'W3_TEST.md' if args.target == 'funkotto_w3' else 'WLAN_TEST.md')).read_bytes()
    entries['dependencies.json'] = (ROOT / 'firmware/dependencies.json').read_bytes()
    if args.target in ('funkotto_w3', 'funkotto_w4', 'funkotto_m4_usb'):
        entries['WLAN_TEST.md'] = (ROOT / 'firmware/WLAN_TEST.md').read_bytes()
        entries['USB_PROTOCOL.md'] = (ROOT / 'firmware/USB_PROTOCOL.md').read_bytes()
        entries['tools/wifi_diag.py'] = (ROOT / 'tools/wifi_diag.py').read_bytes()
        entries['PRUEFBERICHT.md'] = (ROOT / ('docs/results/2026-10-10-m4-usb.md' if args.target == 'funkotto_m4_usb' else 'docs/results/2026-10-09-w4.md' if args.target == 'funkotto_w4' else 'docs/results/2026-10-09-w3.md')).read_bytes()
        if args.target in ('funkotto_w4', 'funkotto_m4_usb'):
            entries['W3_TEST.md'] = (ROOT / 'firmware/W3_TEST.md').read_bytes()
            entries['PROFILE_FORMAT.md'] = (ROOT / 'firmware/PROFILE_FORMAT.md').read_bytes()
    else:
        entries['PRUEFBERICHT.md'] = (ROOT / 'docs/results/2026-10-08-wlan-w12.md').read_bytes()
    if args.target == 'funkotto_m4_usb':
        entries['CONFIG_PROTOCOL.md'] = (ROOT / 'protocol/CONFIG.md').read_bytes()
        entries['tools/wifi_config.py'] = (ROOT / 'tools/wifi_config.py').read_bytes()
        entries['W4_TEST.md'] = (ROOT / 'firmware/W4_TEST.md').read_bytes()
    for name, path in {
        'pico-sdk.txt': 'LICENSE.TXT',
        'tinyusb.txt': 'lib/tinyusb/LICENSE',
        'pico-printf.txt': 'src/rp2_common/pico_printf/LICENSE',
        'cmsis.txt': 'src/rp2_common/cmsis/stub/CMSIS/LICENSE.txt',
        'cyw43-raspberry-pi.txt': 'lib/cyw43-driver/LICENSE.RP',
        'cyw43-alternative.txt': 'lib/cyw43-driver/LICENSE',
    }.items():
        entries['licenses/' + name] = (args.sdk / path).read_bytes()
    for name in ['libnewlib-arm-none-eabi', 'gcc-arm-none-eabi']:
        entries['licenses/' + name + '-copyright.txt'] = (args.toolchain_docs / name / 'copyright').read_bytes()
    entries['QUELLEN.txt'] = ('https://github.com/amiga-stumpi/funkotto\n'
        'Exact firmware source commit: source_id in manifest.json.\n'
        'Pico 2 W test firmware; parallel bus disabled; see ANLEITUNG.md for profile behavior.\n').encode()
    digest = write_verified_zip(args.output, entries)
    print(f'{args.output.resolve()} members={len(entries)} sha256={digest}')


if __name__ == '__main__':
    main()
