#!/usr/bin/env python3
"""FunkOtto M5 USB/FOE1 diagnostic: status, FOC1 and async Ethernet/ARP/ICMP.

Python 3.10+, pyserial 3.5. Use a free IPv4 test address in the peer's LAN.
USB timings are not Amiga parallel-port performance measurements.
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
from wifi_diag import (cobs_encode, cobs_decode, TransportError, NetworkTest,
                       echo_frame, echo_reply, Pcap)
from wifi_config import Client as ConfigClient

HEADER = struct.Struct('!4sBBHIQI')
MAX_PACKET = 1543
MAX_ENCODED = 1551
RESULTS = ('OK', 'EMPTY', 'BUSY', 'NO_LINK', 'INVALID', 'ABORTED', 'DRIVER',
           'PENDING', 'SESSION', 'SEQUENCE', 'UNSUPPORTED', 'EPOCH', 'STALE', 'INTERNAL')
CLIENT_REVISION = 'm5-rx-interleave-1'


def packet(op, session, seq, epoch, payload=b''):
    if len(payload) > 1515:
        raise ValueError('FOE1 payload too large')
    data = HEADER.pack(b'FOE1', op, 0, len(payload), seq, session, epoch)+payload
    return b'\0'+cobs_encode(data+struct.pack('!I', zlib.crc32(data)))+b'\0'


def decode_packet(data):
    raw = cobs_decode(data)
    if not 28 <= len(raw) <= MAX_PACKET:
        raise ValueError('FOE1 packet length')
    magic, op, flags, n, seq, session, epoch = HEADER.unpack_from(raw)
    if magic != b'FOE1' or flags or n+28 != len(raw) or zlib.crc32(raw[:-4]) != struct.unpack_from('!I', raw, len(raw)-4)[0]:
        raise ValueError('FOE1 header/CRC')
    return op, session, seq, epoch, raw[24:-4]


class Client:
    """One request in flight, up to eight separately tracked Ethernet tickets.

    Retransmit only the byte-identical request. A lost session/unknown outcome
    requires an explicit new test; never silently resubmit an Ethernet frame.
    """
    def __init__(self, port, timeout=2.0, retries=3):
        self.port = port; self.timeout = timeout; self.retries = retries
        self.session = 0; self.seq = 0; self.epoch = 0; self.failed = False
        self.buffer = bytearray(); self.discarding = False
        self.retries_used = 0; self.bad_frames = 0; self.outstanding = set()

    def rpc(self, op, payload=b''):
        if self.failed or self.seq == 0xffffffff:
            raise TransportError('Session unusable; close port; do not resubmit blindly')
        self.seq += 1
        request = packet(op, self.session, self.seq, self.epoch, payload)
        for attempt in range(self.retries+1):
            if attempt:
                self.retries_used += 1
            if self.port.write(request) != len(request):
                self.failed = True
                raise TransportError('Short USB write; outcome unknown')
            end = time.monotonic()+self.timeout
            while time.monotonic() < end:
                byte = self.port.read(1)
                if not byte:
                    continue
                if byte != b'\0':
                    if not self.discarding:
                        if len(self.buffer) >= MAX_ENCODED:
                            self.buffer.clear(); self.discarding = True
                        else:
                            self.buffer.extend(byte)
                    continue
                if self.discarding:
                    self.discarding = False; self.buffer.clear(); self.bad_frames += 1; continue
                data = bytes(self.buffer); self.buffer.clear()
                if not data:
                    continue
                try:
                    kind, session, seq, epoch, result = decode_packet(data)
                except ValueError:
                    self.bad_frames += 1; continue
                if kind != op | 0x80 or seq != self.seq:
                    continue
                if not result or result[0] >= len(RESULTS):
                    self.failed = True; raise TransportError('Invalid FOE1 result')
                if self.session and session != self.session:
                    self.failed = True; raise TransportError('Pico session changed; outcome unknown')
                if result[0] in (8, 9):
                    self.failed = True; raise TransportError('FOE1 '+RESULTS[result[0]])
                if not self.session:
                    if op != 1 or result[0] or not session or len(result) != 25 or result[1:9] != payload:
                        self.failed = True; raise TransportError('Invalid FOE1 HELLO/nonce')
                    self.session = session
                self.epoch = epoch
                return result
        self.failed = True
        raise TransportError('USB timeout; outcome unknown; close port before another test')

    def hello(self):
        p = self.rpc(1, secrets.token_bytes(8))
        caps, maximum, slots = struct.unpack_from('!IHB', p, 9)
        mac = p[16:22]
        if maximum != 1514 or slots != 8 or caps & 7 != 7 or struct.unpack_from('!H', p, 23)[0] != 1515 or p[22] > 1:
            self.failed = True; raise TransportError('Unsupported FOE1 capabilities')
        # During early boot the MAC can still be unknown; STATUS remains usable.
        return dict(protocol='FOE1', capabilities=caps, maximum=maximum, slots=slots,
                    mac=mac.hex(':'), link=bool(p[22]), epoch=self.epoch)

    def stats(self):
        p = self.rpc(2)
        if len(p) != 29 or p[0] or p[1] > 1 or any(x > 8 for x in p[2:5]):
            raise TransportError('Invalid FOE1 STATUS')
        return dict(link=bool(p[1]), tx_free=p[2], rx_count=p[3], tx_done=p[4],
                    events=p[5], armed=bool(p[6]), event_generation=struct.unpack_from('!I', p, 7)[0],
                    mac=p[11:17].hex(':'), epoch=self.epoch,
                    **dict(zip(('rx_serial', 'tx_serial', 'config_serial'), struct.unpack_from('!3I', p, 17))))

    def tx_submit(self, frame):
        if not 14 <= len(frame) <= 1514:
            raise ValueError('Ethernet frame outside 14..1514 bytes')
        p = self.rpc(3, frame)
        if len(p) != 5 or p[0]:
            raise TransportError('TX '+RESULTS[p[0]])
        ticket = struct.unpack_from('!I', p, 1)[0]
        if not ticket or ticket in self.outstanding:
            self.failed = True; raise TransportError('Invalid/duplicate TX ticket')
        self.outstanding.add(ticket)
        return ticket

    def tx_result(self, ticket):
        if ticket not in self.outstanding:
            raise ValueError('Unknown local TX ticket')
        p = self.rpc(4, struct.pack('!I', ticket))
        if len(p) != 5 or p[0] not in (0, 5, 6, 7):
            raise TransportError('Invalid TX_RESULT')
        if p[0] == 7:
            return False
        self.outstanding.remove(ticket)
        if p[0]:
            raise TransportError(f'TX_RESULT {RESULTS[p[0]]} sdk_error={struct.unpack_from("!i", p, 1)[0]}')
        return True

    def wait_tx(self, tickets, service_rx=None):
        waiting = set(tickets); end = time.monotonic()+max(5.0, self.timeout)
        while waiting:
            for ticket in sorted(waiting):
                if service_rx:
                    service_rx()
                if self.tx_result(ticket):
                    waiting.remove(ticket)
                if time.monotonic() >= end and waiting:
                    self.failed = True; raise TransportError('TX completion timeout; do not resubmit')
            if waiting:
                if time.monotonic() >= end:
                    self.failed = True; raise TransportError('TX completion timeout; do not resubmit')
                time.sleep(0.002)

    def tx(self, frame):
        self.wait_tx([self.tx_submit(frame)])

    def rx(self):
        p = self.rpc(5)
        if p[0] == 1 and len(p) == 1:
            return None
        if p[0] or not 15 <= len(p) <= 1515:
            raise TransportError('RX '+RESULTS[p[0]])
        return p[1:]

    def close(self):
        p = self.rpc(8)
        if p != b'\0':
            raise TransportError('CLOSE '+RESULTS[p[0]])
        self.outstanding.clear(); self.failed = True


class ConfigOverEtherlink(ConfigClient):
    def __init__(self, client):
        self.client = client; self.capabilities = None

    def rpc(self, op, payload=b''):
        # FOC1 USB op 0 is its greeting; the shared dispatcher calls it INFO=1.
        p = self.client.rpc(6, bytes([1 if op == 0 else op])+payload)
        if p[0] or len(p) < 2 or p[1] > 8:
            raise TransportError('FOE1 CONFIG envelope invalid')
        return p[1:]


class WindowNetworkTest(NetworkTest):
    """Process RX during a batch without submitting a ninth TX for an ARP reply."""
    def __init__(self, *args):
        super().__init__(*args)
        self.frames_read = 0
        self.deferred = []

    def send(self, frame):
        if self.client.outstanding:
            if frame not in self.deferred:
                if len(self.deferred) >= 8:
                    raise TransportError('Deferred ARP reply queue full')
                self.deferred.append(frame)
        else:
            super().send(frame)

    def receive(self):
        frame = super().receive()
        if frame is not None:
            self.frames_read += 1
        return frame

    def flush_deferred(self, service_rx):
        if self.client.outstanding:
            raise TransportError('Cannot flush ARP replies with TX tickets outstanding')
        while self.deferred:
            super().send(self.deferred.pop(0))
            service_rx()


def rx_accounting(before, after, frames_read):
    """Infer drops from the existing FOE1 counter, only within one link epoch.

    rx_serial = rx_queued + rx_full. No queue flush is allowed between snapshots.
    This is deliberately labelled inferred; it is not a direct rx_full read.
    """
    if before['epoch'] != after['epoch']:
        raise TransportError('WLAN epoch changed during test; repeat explicitly')
    delta = (after['rx_serial']-before['rx_serial']) & 0xffffffff
    dropped = delta-frames_read-after['rx_count']+before['rx_count']
    if dropped < 0:
        raise TransportError('Inconsistent RX accounting')
    return dict(serial_delta=delta, frames_read=frames_read,
                initial_queued=before['rx_count'], final_queued=after['rx_count'],
                overflow_inferred=dropped)


def enter_mode(port):
    port.dtr = False; time.sleep(0.2); port.reset_input_buffer()
    port.dtr = True; time.sleep(0.2)
    command = b'\x03\netherlink on\n'
    if port.write(command) != len(command):
        raise TransportError('Cannot enter FOE1 mode')
    end = time.monotonic()+3; line = bytearray()
    while time.monotonic() < end:
        b = port.read(1)
        if b == b'\n':
            if line.startswith(b'ETHERLINK FOE1 COBS;'):
                return
            if line.startswith(b'BUSY:'):
                raise TransportError('Firmware busy; wait for the current config job')
            line.clear()
        elif b and len(line) < 256:
            line.extend(b)
    raise TransportError('No FOE1 greeting; flash funkotto_m5_usb and close other terminals')


def ping_window(network, peer, count, sizes, timeout, window):
    identifier = secrets.randbelow(65536); nonce = secrets.token_bytes(32)
    rtts = []; lost = 0; min_free = 8; maximum_outstanding = 0
    for base in range(0, count, window):
        batch = {}; tickets = []

        def service_rx():
            # Bounded work keeps TX completion timeouts effective under RX load.
            # Do not wait for EMPTY forever on a busy LAN.
            for _ in range(8):
                frame = network.receive()
                if frame is None:
                    break
                for seq, (payload, start) in list(batch.items()):
                    if echo_reply(frame, network.mac, peer, network.source, network.target, identifier, seq, payload):
                        elapsed = (time.monotonic()-start)*1000
                        rtts.append(elapsed); del batch[seq]
                        print(f'echo seq={seq} payload={len(payload)} bytes rtt_ms={elapsed:.2f} verified=1', flush=True)
                        break

        service_rx()
        for index in range(base, min(base+window, count)):
            seq = index+1; size = sizes[index % len(sizes)]
            payload = bytes((nonce[i % 32]+i+seq) & 255 for i in range(size))
            frame = echo_frame(network.mac, peer, network.source, network.target, identifier, seq, payload)
            batch[seq] = (payload, time.monotonic())
            tickets.append(network.client.tx_submit(frame))
            if network.capture:
                network.capture.write(frame)
            service_rx()
        maximum_outstanding = max(maximum_outstanding, len(tickets))
        min_free = min(min_free, network.client.stats()['tx_free'])
        # Hold all tickets until the batch is submitted, so window=8 still
        # verifies full TX-slot occupancy while RX is drained independently.
        network.client.wait_tx(tickets, service_rx=service_rx)
        network.flush_deferred(service_rx)
        end = time.monotonic()+timeout
        while batch and time.monotonic() < end:
            service_rx()
        for seq in batch:
            lost += 1; print(f'echo seq={seq} TIMEOUT', flush=True)
    return dict(sent=count, received=len(rtts), lost=lost, window=window,
                maximum_outstanding=maximum_outstanding, minimum_tx_free=min_free,
                rtt_ms_min=min(rtts) if rtts else None,
                rtt_ms_mean=sum(rtts)/len(rtts) if rtts else None,
                rtt_ms_max=max(rtts) if rtts else None)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--port', required=True)
    parser.add_argument('--source-ip')
    parser.add_argument('--target-ip', type=ipaddress.IPv4Address)
    parser.add_argument('--count', type=int, default=12)
    parser.add_argument('--window', type=int, default=1, help='Outstanding TX tickets, 1..8')
    parser.add_argument('--sizes', default='0,56,512,1472')
    parser.add_argument('--timeout', type=float, default=2.0)
    parser.add_argument('--pcap', type=Path)
    parser.add_argument('--report', type=Path)
    parser.add_argument('--config-action', choices=['connect', 'disconnect', 'save'],
                        help='Explicit WLAN action through FOE1/FOC1; status-only run')
    args = parser.parse_args()
    try:
        if not 1 <= args.count <= 65535 or not 1 <= args.window <= 8 or not 0.5 <= args.timeout <= 30:
            raise ValueError('count 1..65535, window 1..8, timeout 0.5..30')
        sizes = [int(x) for x in args.sizes.split(',')]
        if not sizes or any(x < 0 or x > 1472 for x in sizes):
            raise ValueError('sizes must be 0..1472')
        source = ipaddress.IPv4Interface(args.source_ip) if args.source_ip else None
        if bool(source) != bool(args.target_ip):
            raise ValueError('provide both --source-ip and --target-ip')
        if source:
            if args.config_action or '/' not in args.source_ip or source.network.prefixlen > 30:
                raise ValueError('Network test requires a LAN prefix and no config-action')
            for address in (source.ip, args.target_ip):
                if (address not in source.network or address in (source.network.network_address, source.network.broadcast_address)
                        or address.is_multicast or address.is_unspecified or address.is_loopback):
                    raise ValueError('source and target must be unicast host addresses in the same LAN')
            if source.ip == args.target_ip:
                raise ValueError('source and target must differ')
    except ValueError as error:
        parser.error(str(error))
    try:
        import serial
    except ImportError:
        parser.error('Install pyserial: python -m pip install pyserial==3.5')
    report = dict(stage='M5_USB', client_revision=CLIENT_REVISION, passed=False); capture = None
    try:
        if args.pcap:
            capture = Pcap(args.pcap)
        with serial.Serial(args.port, 115200, timeout=0.02, write_timeout=2) as port:
            try:
                enter_mode(port); client = Client(port, args.timeout)
                report['hello'] = client.hello(); report['before'] = client.stats()
                config = ConfigOverEtherlink(client); report['config_info'] = config.hello()
                if args.config_action:
                    report['config_action'] = args.config_action
                    report['completion'] = config.command({'connect':6, 'disconnect':7, 'save':8}[args.config_action])
                report['wifi'] = config.status()
                if source:
                    state = client.stats(); epoch = state['epoch']; mac = bytes.fromhex(state['mac'].replace(':', ''))
                    if not state['link'] or not any(mac) or mac[0] & 1:
                        raise TransportError('WLAN not ready; configure/connect and wait for LINK_UP')
                    network = WindowNetworkTest(client, mac, source.ip.packed, args.target_ip.packed, capture)
                    network.probe_address(); peer = network.resolve(); report['peer_mac'] = peer.hex(':')
                    report['ping'] = ping_window(network, peer, args.count, sizes, args.timeout, args.window)
                    report['after'] = client.stats()
                    if report['after']['epoch'] != epoch:
                        raise TransportError('WLAN epoch changed during test; repeat explicitly')
                    report['rx_accounting'] = rx_accounting(state, report['after'], network.frames_read)
                    report['passed'] = (report['ping']['lost'] == 0 and
                                        report['rx_accounting']['overflow_inferred'] == 0 and
                                        report['after']['tx_free'] == 8 and report['after']['tx_done'] == 0)
                else:
                    report['after'] = client.stats(); report['passed'] = True
                report['usb_retries'] = client.retries_used; report['usb_bad_frames'] = client.bad_frames
                client.close()
            finally:
                port.dtr = False
    except (OSError, TransportError, serial.SerialException) as error:
        report['passed'] = False; report['error'] = str(error)
    finally:
        if capture:
            capture.close()
        if args.report:
            args.report.write_text(json.dumps(report, indent=2)+'\n', encoding='utf-8')
    print(json.dumps(report, indent=2))
    return 0 if report['passed'] else 1


if __name__ == '__main__':
    sys.exit(main())
