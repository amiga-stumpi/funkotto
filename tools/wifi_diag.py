#!/usr/bin/env python3
"""FunkOtto W3 USB raw Ethernet diagnostic. Python 3.10+, pyserial 3.5.

ARP/IPv4/ICMP live here, not in the Pico. Only use a free test address in the
same subnet as the peer. No DHCP, routing, TAP interface or Amiga benchmark.
"""
import argparse
import ipaddress
import json
from pathlib import Path
import secrets
import struct
import sys
import time
import zlib

HEADER = struct.Struct('!BBHIQ')
MAX_FRAME = 1514
MAX_WIRE = 1543
STATUS = ['OK', 'EMPTY', 'BUSY', 'NO_LINK', 'INVALID', 'ABORTED', 'DRIVER',
          'PENDING', 'SESSION', 'SEQUENCE']
COUNTERS = ['tx_queued', 'tx_ok', 'tx_error', 'tx_aborted', 'tx_busy', 'tx_invalid',
            'rx_queued', 'rx_full', 'rx_inactive', 'rx_invalid', 'rx_flushed',
            'tx_high_water', 'rx_high_water', 'bad_frames', 'replays', 'sequence_errors']


def cobs_encode(data):
    out = bytearray(b'\0'); at = 0; code = 1
    for b in data:
        if b == 0:
            out[at] = code; at = len(out); out.append(0); code = 1
        else:
            out.append(b); code += 1
            if code == 255:
                out[at] = code; at = len(out); out.append(0); code = 1
    out[at] = code
    return bytes(out)


def cobs_decode(data):
    out = bytearray(); pos = 0
    while pos < len(data):
        code = data[pos]; pos += 1
        if not code or pos + code - 1 > len(data):
            raise ValueError('Invalid COBS length')
        part = data[pos:pos + code - 1]
        if 0 in part:
            raise ValueError('Zero in COBS block')
        out.extend(part); pos += code - 1
        if code < 255 and pos < len(data):
            out.append(0)
    return bytes(out)


def packet(kind, session, seq, payload=b''):
    data = HEADER.pack(1, kind, len(payload), seq, session) + payload
    return b'\0' + cobs_encode(data + struct.pack('!I', zlib.crc32(data))) + b'\0'


def decode_packet(data):
    raw = cobs_decode(data)
    if len(raw) < 20:
        raise ValueError('Short packet')
    version, kind, size, seq, session = HEADER.unpack_from(raw)
    if version != 1 or size > 1515 or len(raw) != size + 20:
        raise ValueError('Bad header')
    if zlib.crc32(raw[:-4]) != struct.unpack('!I', raw[-4:])[0]:
        raise ValueError('CRC mismatch')
    return kind, session, seq, raw[16:-4]


class TransportError(RuntimeError):
    pass


class Client:
    """One outstanding request. Retries never change sequence or payload."""
    def __init__(self, serial_port, timeout=2.0, retries=3):
        self.port = serial_port; self.timeout = timeout; self.retries = retries
        self.session = 0; self.seq = 0; self.buffer = bytearray()
        self.discarding = False; self.retries_used = 0; self.bad_frames = 0
        self.failed = False

    def rpc(self, kind, payload=b''):
        if self.failed:
            raise TransportError('Session unusable after timeout/error; close and reopen')
        self.seq += 1
        request = packet(kind, self.session, self.seq, payload)
        for attempt in range(self.retries + 1):
            if attempt:
                self.retries_used += 1
            written = self.port.write(request)
            if written != len(request):
                self.failed = True
                raise TransportError('Short USB write; TX outcome unknown')
            end = time.monotonic() + self.timeout
            while time.monotonic() < end:
                byte = self.port.read(1)
                if not byte:
                    continue
                if byte != b'\0':
                    if not self.discarding:
                        if len(self.buffer) >= MAX_WIRE:
                            self.buffer.clear(); self.discarding = True
                        else:
                            self.buffer.extend(byte)
                    continue
                if self.discarding:
                    self.discarding = False; self.buffer.clear(); self.bad_frames += 1
                    continue
                data = bytes(self.buffer); self.buffer.clear()
                if not data:
                    continue
                try:
                    response_kind, session, seq, result = decode_packet(data)
                except ValueError:
                    self.bad_frames += 1; continue
                if seq != self.seq or response_kind != (kind | 0x80):
                    continue
                if self.session and session != self.session:
                    self.failed = True
                    raise TransportError('Pico session changed; TX outcome unknown; no automatic resubmit')
                if not result or result[0] >= len(STATUS):
                    self.failed = True
                    raise TransportError('Invalid response status')
                if result[0] in (8, 9):
                    self.failed = True
                    raise TransportError('Protocol error: ' + STATUS[result[0]])
                if not self.session:
                    if kind != 1 or result[0] != 0 or not session:
                        raise TransportError('Invalid HELLO')
                    self.session = session
                return result
        self.failed = True
        raise TransportError('USB timeout; TX outcome unknown; close port before retrying the test')

    def hello(self):
        data = self.rpc(1)
        if len(data) != 15:
            raise TransportError('Unsupported HELLO length')
        _, mac, link, maximum, slots, epoch = struct.unpack('!B6sBHBI', data)
        if maximum != MAX_FRAME or slots != 8 or not any(mac) or mac[0] & 1:
            raise TransportError('Unexpected firmware capabilities/MAC')
        return {'mac': mac, 'link': bool(link), 'maximum': maximum, 'slots': slots, 'epoch': epoch}

    def stats(self):
        data = self.rpc(2)
        if len(data) != 72 or data[0]:
            raise TransportError('Invalid STATS response')
        return dict(link=bool(data[1]), tx_used=data[2], rx_used=data[3],
                    epoch=struct.unpack_from('!I', data, 4)[0],
                    **dict(zip(COUNTERS, struct.unpack_from('!16I', data, 8))))

    def tx(self, frame):
        if not 14 <= len(frame) <= MAX_FRAME:
            raise ValueError('Ethernet frame outside 14..1514 bytes')
        data = self.rpc(3, frame)
        if len(data) != 5 or data[0]:
            error = struct.unpack('!i', data[1:])[0] if len(data) == 5 else None
            raise TransportError(f'TX {STATUS[data[0]]}: sdk_error={error}')

    def rx(self):
        data = self.rpc(4)
        if data[0] == 1:
            return None
        if data[0] or not 15 <= len(data) <= 1515:
            raise TransportError('RX ' + STATUS[data[0]])
        return data[1:]


def checksum(data):
    if len(data) & 1:
        data += b'\0'
    total = sum(struct.unpack('!' + 'H' * (len(data) // 2), data))
    while total >> 16:
        total = (total & 65535) + (total >> 16)
    return (~total) & 65535


def arp(mac, source, target, destination=b'\xff' * 6, operation=1, target_mac=b'\0' * 6):
    body = struct.pack('!HHBBH6s4s6s4s', 1, 0x800, 6, 4, operation,
                       mac, source, target_mac, target)
    return (destination + mac + b'\x08\x06' + body).ljust(60, b'\0')


def parse_arp(frame):
    if len(frame) < 42 or frame[12:14] != b'\x08\x06':
        return None
    hw, proto, hlen, plen, op, sender, source, dest, target = struct.unpack('!HHBBH6s4s6s4s', frame[14:42])
    if (hw, proto, hlen, plen) != (1, 0x800, 6, 4) or op not in (1, 2) or frame[6:12] != sender:
        return None
    return op, sender, source, dest, target


def echo_frame(mac, peer, source, target, identifier, seq, payload):
    icmp = struct.pack('!BBHHH', 8, 0, 0, identifier, seq) + payload
    icmp = icmp[:2] + struct.pack('!H', checksum(icmp)) + icmp[4:]
    ip = struct.pack('!BBHHHBBH4s4s', 0x45, 0, 20 + len(icmp), seq, 0x4000, 64, 1, 0, source, target)
    ip = ip[:10] + struct.pack('!H', checksum(ip)) + ip[12:]
    return (peer + mac + b'\x08\x00' + ip + icmp).ljust(60, b'\0')


def echo_reply(frame, mac, peer, source, target, identifier, seq, payload):
    if len(frame) < 42 or frame[:6] != mac or frame[6:12] != peer or frame[12:14] != b'\x08\x00':
        return False
    ip = frame[14:]; ihl = (ip[0] & 15) * 4
    if ip[0] >> 4 != 4 or ihl < 20 or ihl > len(ip) or ip[9] != 1:
        return False
    total = struct.unpack_from('!H', ip, 2)[0]
    fragments = struct.unpack_from('!H', ip, 6)[0]
    if total < ihl + 8 or total > len(ip) or fragments & 0x3fff or checksum(ip[:ihl]):
        return False
    if ip[12:16] != target or ip[16:20] != source:
        return False
    icmp = ip[ihl:total]
    return (not checksum(icmp) and icmp[:2] == b'\0\0' and
            struct.unpack_from('!HH', icmp, 4) == (identifier, seq) and icmp[8:] == payload)


class Pcap:
    def __init__(self, path):
        self.file = path.open('wb')
        self.file.write(struct.pack('<IHHIIII', 0xa1b2c3d4, 2, 4, 0, 0, 65535, 1))

    def write(self, frame):
        now = time.time_ns(); seconds, ns = divmod(now, 1_000_000_000)
        self.file.write(struct.pack('<IIII', seconds, ns // 1000, len(frame), len(frame)) + frame)
        self.file.flush()

    def close(self):
        self.file.close()


class NetworkTest:
    def __init__(self, client, mac, source, target, capture):
        self.client = client; self.mac = mac; self.source = source; self.target = target
        self.capture = capture; self.claimed = False

    def send(self, frame):
        self.client.tx(frame)
        if self.capture:
            self.capture.write(frame.ljust(60, b'\0'))

    def receive(self):
        frame = self.client.rx()
        if frame is None:
            time.sleep(0.002); return None
        if self.capture:
            self.capture.write(frame)
        info = parse_arp(frame)
        if info:
            op, sender, source, dest, target = info
            if sender != self.mac and (source == self.source or (source == b'\0' * 4 and target == self.source)):
                raise TransportError('Address conflict: choose another free test IP')
            if self.claimed and op == 1 and target == self.source and sender != self.mac:
                self.send(arp(self.mac, self.source, source, sender, 2, sender))
        return frame

    def probe_address(self):
        for _ in range(3):
            self.send(arp(self.mac, b'\0' * 4, self.source))
            end = time.monotonic() + 1.0
            while time.monotonic() < end:
                self.receive()
        self.claimed = True

    def resolve(self):
        for _ in range(3):
            self.send(arp(self.mac, self.source, self.target))
            end = time.monotonic() + 1.0
            while time.monotonic() < end:
                frame = self.receive()
                info = parse_arp(frame) if frame else None
                if info:
                    op, sender, source, dest, target = info
                    if (op == 2 and source == self.target and target == self.source and dest == self.mac
                            and frame[:6] == self.mac and any(sender) and not sender[0] & 1):
                        return sender
        raise TransportError('No ARP reply: check peer IP, subnet and AP/client isolation')

    def ping(self, peer, count, sizes, timeout):
        identifier = secrets.randbelow(65536); nonce = secrets.token_bytes(32)
        rtts = []; lost = 0
        for index in range(count):
            size = sizes[index % len(sizes)]; seq = index + 1
            payload = bytes((nonce[i % 32] + i + seq) & 255 for i in range(size))
            start = time.monotonic()
            self.send(echo_frame(self.mac, peer, self.source, self.target, identifier, seq, payload))
            end = start + timeout; matched = False
            while time.monotonic() < end:
                frame = self.receive()
                if frame and echo_reply(frame, self.mac, peer, self.source, self.target, identifier, seq, payload):
                    elapsed = (time.monotonic() - start) * 1000; rtts.append(elapsed); matched = True
                    print(f'echo seq={seq} payload={size} bytes rtt_ms={elapsed:.2f} verified=1', flush=True)
                    break
            if not matched:
                lost += 1; print(f'echo seq={seq} payload={size} TIMEOUT', flush=True)
        return {'sent': count, 'received': len(rtts), 'lost': lost,
                'rtt_ms_min': min(rtts) if rtts else None,
                'rtt_ms_mean': sum(rtts) / len(rtts) if rtts else None,
                'rtt_ms_max': max(rtts) if rtts else None}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--port', required=True, help='COM5 or /dev/ttyACM0')
    parser.add_argument('--source-ip', help='Free test address with subnet prefix, e.g. 192.168.178.250/24')
    parser.add_argument('--target-ip', type=ipaddress.IPv4Address, help='ICMP peer in same LAN/subnet')
    parser.add_argument('--count', type=int, default=12)
    parser.add_argument('--sizes', default='0,56,512,1472', help='ICMP payload sizes, 0..1472')
    parser.add_argument('--timeout', type=float, default=2.0)
    parser.add_argument('--pcap', type=Path, help='Write transmitted/received Ethernet frames')
    parser.add_argument('--report', type=Path, help='Write JSON test results, including failures')
    args = parser.parse_args()
    if not 1 <= args.count <= 65535 or not 0.5 <= args.timeout <= 30:
        parser.error('count must be 1..65535; timeout must be 0.5..30 seconds')
    try:
        sizes = [int(x) for x in args.sizes.split(',')]
        if not sizes or any(x < 0 or x > 1472 for x in sizes):
            raise ValueError('sizes must be 0..1472')
        source = ipaddress.IPv4Interface(args.source_ip) if args.source_ip else None
        if bool(source) != bool(args.target_ip):
            raise ValueError('provide both --source-ip and --target-ip, or neither for status only')
        if source:
            if '/' not in args.source_ip or source.network.prefixlen > 30:
                raise ValueError('source-ip requires a LAN subnet prefix, e.g. /24')
            for address in (source.ip, args.target_ip):
                if (address not in source.network or address in (source.network.network_address, source.network.broadcast_address)
                        or address.is_multicast or address.is_unspecified or address.is_loopback):
                    raise ValueError('source/target must be host addresses in the same LAN subnet')
            if source.ip == args.target_ip:
                raise ValueError('source and target must differ')
    except ValueError as error:
        parser.error(str(error))
    try:
        import serial
    except ImportError:
        parser.error('Install pyserial: python -m pip install pyserial==3.5')
    report = {'stage': 'W3', 'passed': False}; capture = None
    try:
        if args.pcap:
            capture = Pcap(args.pcap)
        with serial.Serial(args.port, 115200, timeout=0.02, write_timeout=2) as port:
            port.dtr = False; time.sleep(0.15); port.reset_input_buffer()
            port.dtr = True; time.sleep(0.15)
            port.write(b'\x03\nraw on\n'); time.sleep(0.1)
            client = Client(port, args.timeout)
            hello = client.hello(); hello['mac'] = hello['mac'].hex(':')
            report['hello'] = hello; print('hello=' + json.dumps(hello), flush=True)
            report['before'] = client.stats()
            if source:
                if not hello['link']:
                    raise TransportError('WLAN is not LINK_UP. Configure/connect through terminal first.')
                network = NetworkTest(client, bytes.fromhex(hello['mac'].replace(':', '')),
                                      source.ip.packed, args.target_ip.packed, capture)
                network.probe_address(); peer = network.resolve(); report['peer_mac'] = peer.hex(':')
                print('arp peer=' + peer.hex(':'), flush=True)
                report['ping'] = network.ping(peer, args.count, sizes, args.timeout)
                report['passed'] = report['ping']['lost'] == 0
            else:
                report['passed'] = True
            report['after'] = client.stats()
            report['usb_retries'] = client.retries_used; report['usb_bad_frames'] = client.bad_frames
            if report['after']['epoch'] != hello['epoch']:
                raise TransportError('Link epoch changed during test; reconnect and repeat')
            port.dtr = False
    except (OSError, TransportError, serial.SerialException) as error:
        report['passed'] = False; report['error'] = str(error)
    finally:
        if capture:
            capture.close()
        if args.report:
            args.report.write_text(json.dumps(report, indent=2) + '\n', encoding='utf-8')
    print(json.dumps(report, indent=2))
    return 0 if report['passed'] else 1


if __name__ == '__main__':
    sys.exit(main())
