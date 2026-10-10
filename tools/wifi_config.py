#!/usr/bin/env python3
"""FunkOtto M4 USB configuration. Python 3.10+, pyserial 3.5; no adapter board needed."""
import argparse
import getpass
import json
from pathlib import Path
import struct
import sys
import time
import warnings
import zlib
from wifi_diag import cobs_encode, cobs_decode, TransportError

HEADER = struct.Struct('!BBHIQ')
MAX_PAYLOAD = 128
MAX_ENCODED = 150
RESULTS = ('OK', 'QUEUED', 'BUSY', 'INVALID', 'UNSUPPORTED', 'STALE', 'EMPTY', 'SESSION', 'SEQUENCE')
REPLIES = ('OK', 'BUSY', 'NO_PROFILE', 'DRIVER_ERROR', 'INVALID', 'FLASH_ERROR')
STATES = ('INITIALIZING', 'UNCONFIGURED', 'DISCONNECTED', 'CONNECTING', 'LINK_UP', 'RETRY_WAIT', 'ERROR')
ERRORS = ('OK', 'NO_PROFILE', 'TIMEOUT', 'BADAUTH', 'NONET', 'DRIVER', 'LINK_LOST', 'JOIN_FAILED')
OPS = dict(info=1, status=2, scan=3, results=4, set=5, connect=6, disconnect=7, save=8, erase=9, job=10)


def packet(op, session, seq, payload=b''):
    if len(payload) > MAX_PAYLOAD:
        raise ValueError('Configuration payload too large')
    data = HEADER.pack(2, op, len(payload), seq, session) + payload
    return b'\0' + cobs_encode(data + struct.pack('!I', zlib.crc32(data))) + b'\0'


def decode_packet(data):
    raw = cobs_decode(data)
    if len(raw) < 20:
        raise ValueError('Short configuration frame')
    version, op, n, seq, session = HEADER.unpack_from(raw)
    if version != 2 or n > MAX_PAYLOAD or len(raw) != 20+n or zlib.crc32(raw[:-4]) != struct.unpack('!I', raw[-4:])[0]:
        raise ValueError('Invalid configuration frame')
    return op, session, seq, raw[16:-4]


class Client:
    def __init__(self, port, timeout=2.0, retries=3):
        self.port = port; self.timeout = timeout; self.retries = retries
        self.session = 0; self.seq = 0; self.failed = False
        self.buffer = bytearray(); self.discarding = False
        self.usb_retries = 0; self.bad_frames = 0; self.capabilities = None

    def rpc(self, op, payload=b''):
        if self.failed or self.seq == 0xffffffff:
            raise TransportError('Session unusable; close port. Do not automatically repeat changes.')
        self.seq += 1
        request = packet(op, self.session, self.seq, payload)
        for attempt in range(self.retries+1):
            if attempt:
                self.usb_retries += 1
            if self.port.write(request) != len(request):
                self.failed = True
                raise TransportError('Short write: command outcome unknown; no automatic resubmit')
            end = time.monotonic()+self.timeout
            while time.monotonic() < end:
                b = self.port.read(1)
                if not b:
                    continue
                if b != b'\0':
                    if len(self.buffer) == MAX_ENCODED:
                        self.buffer.clear(); self.discarding = True
                    if not self.discarding:
                        self.buffer.extend(b)
                    continue
                if self.discarding:
                    self.buffer.clear(); self.discarding = False; self.bad_frames += 1; continue
                data = bytes(self.buffer); self.buffer.clear()
                if not data:
                    continue
                try:
                    kind, session, seq, result = decode_packet(data)
                except ValueError:
                    self.bad_frames += 1; continue
                if kind != op | 0x80 or seq != self.seq:
                    continue
                if (self.session and session != self.session) or not result or result[0] >= len(RESULTS):
                    self.failed = True
                    raise TransportError('Invalid/session-changed response; command outcome unknown')
                if result[0] in (7, 8):
                    self.failed = True
                    raise TransportError('Protocol '+RESULTS[result[0]]+'; do not automatically repeat changes')
                if not self.session:
                    if op != 0 or not session or result[0]:
                        self.failed = True
                        raise TransportError('Invalid configuration HELLO')
                    self.session = session
                return result
        self.failed = True
        raise TransportError('USB timeout: command outcome unknown; no automatic resubmit in a new session')

    def hello(self):
        p = self.rpc(0)
        if (len(p) != 18 or p[:7] != b'\x00FOC1\x01\x00' or
                struct.unpack_from('!H', p, 11)[0] != 128 or p[13:18] != b'\x20\x08\x3fDE'):
            self.failed = True
            raise TransportError('Unsupported configuration capabilities/version')
        self.capabilities = struct.unpack_from('!I', p, 7)[0]
        return dict(protocol='FOC1', version='1.0', capabilities=self.capabilities,
                    max_payload=128, country='DE')

    def require(self, bit):
        if self.capabilities is None or not self.capabilities & (1 << bit):
            raise TransportError('Firmware does not advertise the required capability')

    def status(self):
        self.require(0)
        p = self.rpc(2)
        if len(p) < 51 or p[0] or p[50] > 32 or len(p) != 51+p[50] or p[1] >= len(STATES) or p[2] >= len(ERRORS):
            raise TransportError('Invalid STATUS reply')
        flags = p[3]
        r = dict(state=STATES[p[1]], error=ERRORS[p[2]], storage_state=p[4],
                 flash_profile=bool(p[5]&1), flash_ready=bool(p[5]&2), mac=p[6:12].hex(':'),
                 rssi=struct.unpack_from('!i', p, 12)[0], sdk_error=struct.unpack_from('!i', p, 16)[0])
        r.update({name: bool(flags & (1 << i)) for i, name in enumerate(
            ('configured', 'stored', 'auto_retry', 'mac_valid', 'rssi_valid', 'scanning', 'sdk_busy', 'command_busy'))})
        r.update(zip(('epoch', 'attempts', 'links', 'storage_sequence', 'storage_error', 'scan_generation', 'scan_done'),
                     struct.unpack_from('!7I', p, 20)))
        r.update(scan_count=p[48], scan_truncated=bool(p[49]),
                 ssid=p[51:].decode('utf-8', errors='replace'), ssid_hex=p[51:].hex())
        return r

    def command(self, op, payload=b'', timeout=30):
        bit = {3:1, 5:2, 6:3, 7:3, 8:4, 9:4}[op]
        self.require(bit); self.require(5)
        p = self.rpc(op, payload)
        if len(p) != 5 or p[0] != 1:
            raise TransportError('Command not queued: '+RESULTS[p[0]])
        job = struct.unpack_from('!I', p, 1)[0]
        if not job:
            raise TransportError('Invalid job identifier')
        end = time.monotonic()+timeout
        while time.monotonic() < end:
            p = self.rpc(10, struct.pack('!I', job))
            if len(p) != 8 or p[0] or struct.unpack_from('!I', p, 1)[0] != job or p[5] != op or p[6] > 1:
                raise TransportError('Invalid JOB reply; outcome unknown')
            if not p[6]:
                if p[7] >= len(REPLIES):
                    raise TransportError('Unknown service result')
                if p[7]:
                    raise TransportError('Command completed: '+REPLIES[p[7]])
                return dict(job=job, result='OK')
            if p[7] != 255:
                raise TransportError('Invalid pending result')
            time.sleep(0.05)
        raise TransportError('Command completion timeout; outcome unknown; do not automatically resubmit')

    def scan(self):
        self.command(3)
        end = time.monotonic()+20
        while time.monotonic() < end:
            s = self.status()
            if not s['scanning']:
                break
            time.sleep(0.1)
        else:
            raise TransportError('Scan completion timeout')
        if s['sdk_error'] or not s['scan_generation'] or s['scan_done'] != s['scan_generation']:
            raise TransportError('Scan failed or incomplete')
        rows = []
        for index in range(s['scan_count']):
            p = self.rpc(4, struct.pack('!IB', s['scan_generation'], index))
            if (len(p) < 20 or p[0] or p[19] > 32 or len(p) != 20+p[19] or
                    struct.unpack_from('!I', p, 1)[0] != s['scan_generation'] or p[5] != index or p[6] != s['scan_count']):
                raise TransportError('Scan result changed/invalid; repeat scan explicitly')
            rows.append(dict(ssid=p[20:].decode('utf-8', errors='replace'), ssid_hex=p[20:].hex(),
                             bssid=p[13:19].hex(':'), auth=p[8],
                             channel=struct.unpack_from('!H', p, 9)[0], rssi=struct.unpack_from('!h', p, 11)[0]))
        return dict(generation=s['scan_generation'], truncated=s['scan_truncated'], results=rows)


def profile_payload(ssid, key):
    if not 1 <= len(ssid) <= 32 or not 8 <= len(key) <= 63 or any(c < 32 or c > 126 for c in key):
        raise ValueError('SSID must have 1..32 bytes; WPA2 password 8..63 printable ASCII characters')
    return bytes((1, len(ssid), len(key)))+ssid+key


def enter_mode(port):
    port.dtr = False; time.sleep(0.2); port.reset_input_buffer()
    port.dtr = True; time.sleep(0.2)
    command = b'\x03\nconfig on\n'
    if port.write(command) != len(command):
        raise TransportError('Cannot enter configuration mode')
    end = time.monotonic()+3; line = bytearray()
    while time.monotonic() < end:
        b = port.read(1)
        if not b:
            continue
        if b == b'\n':
            if bytes(line).startswith(b'CONFIG v1 wire=2;'):
                return
            if bytes(line).startswith(b'BUSY:'):
                raise TransportError('Firmware busy; let the previous command finish first')
            line.clear()
        elif len(line) < 256:
            line.extend(b)
    raise TransportError('No CONFIG greeting. Flash funkotto_m4_usb; close other terminal programs.')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--port', required=True)
    parser.add_argument('--report', type=Path, help='Write a password-free JSON result')
    sub = parser.add_subparsers(dest='command', required=True)
    for name in ('status', 'scan', 'connect', 'disconnect', 'save'):
        sub.add_parser(name)
    erase = sub.add_parser('erase'); erase.add_argument('--confirm', action='store_true', required=True)
    setup = sub.add_parser('set'); group = setup.add_mutually_exclusive_group()
    group.add_argument('--ssid'); group.add_argument('--ssid-hex')
    args = parser.parse_args()
    payload = b''
    if args.command == 'set':
        try:
            ssid = bytes.fromhex(args.ssid_hex) if args.ssid_hex is not None else (args.ssid if args.ssid is not None else input('SSID: ')).encode('utf-8')
            with warnings.catch_warnings():
                warnings.simplefilter('error', getpass.GetPassWarning)
                key = getpass.getpass('WPA2 password (hidden): ').encode('ascii')
            payload = profile_payload(ssid, key)
            del key
        except (ValueError, getpass.GetPassWarning, EOFError):
            parser.error('Invalid profile or no hidden password input available; use an interactive terminal')
    try:
        import serial
    except ImportError:
        parser.error('Install pyserial: python -m pip install pyserial==3.5')
    report = dict(stage='M4_USB', command=args.command, passed=False)
    try:
        with serial.Serial(args.port, 115200, timeout=0.02, write_timeout=2) as port:
            try:
                enter_mode(port); client = Client(port); report['hello'] = client.hello()
                if args.command == 'scan':
                    report['scan'] = client.scan()
                elif args.command != 'status':
                    report['completion'] = client.command(OPS[args.command], payload)
                report['status'] = client.status()
                report['usb_retries'] = client.usb_retries; report['usb_bad_frames'] = client.bad_frames
                report['passed'] = True
            finally:
                port.dtr = False
    except KeyboardInterrupt:
        report['error'] = 'Cancelled; an accepted command may still complete. Check status before repeating changes.'
    except (OSError, TransportError, serial.SerialException) as error:
        report['error'] = str(error)
    finally:
        payload = b''  # Python/OS buffers cannot guarantee secure erasure; never log request bytes.
    if args.report:
        args.report.write_text(json.dumps(report, indent=2)+'\n', encoding='utf-8')
    print(json.dumps(report, indent=2))
    return 0 if report['passed'] else 1


if __name__ == '__main__':
    sys.exit(main())
