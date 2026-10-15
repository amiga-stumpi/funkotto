import ctypes
import contextlib
import io
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
import unittest
import zlib
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT/'tools'))
import etherlink_diag as el
from wifi_diag import arp, checksum

SEND_HOOK = ctypes.CFUNCTYPE(None, ctypes.c_size_t, ctypes.c_void_p)


class Port:
    def __init__(self, lib):
        self.lib = lib; self.input = bytearray(); self.writes = []
        self.drop = 0; self.short = False; self.corrupt = False
        self.before_request = None

    def write(self, data):
        self.writes.append(data)
        if self.before_request:
            self.before_request(el.decode_packet(data[1:-1])[0])
        out = ctypes.create_string_buffer(1600)
        n = self.lib.bridge_exchange(data, len(data), out)
        reply = out.raw[:n]
        if self.drop:
            self.drop -= 1
        elif self.corrupt:
            self.corrupt = False
            self.input.extend(reply[:max(1, len(reply)//2)]+b'\0')
        else:
            self.input.extend(reply)
        return len(data)-1 if self.short else len(data)

    def read(self, count):
        result = bytes(self.input[:count]); del self.input[:count]
        return result


class EtherlinkClient(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.tmp = tempfile.TemporaryDirectory(prefix='m5-client-')
        output = str(Path(cls.tmp.name)/'bridge.so')
        sources = ['tests/etherlink_usb_bridge.c', 'protocol/etherlink.c',
                   'firmware/src/ethernet.c', 'firmware/src/wifi_model.c',
                   'firmware/src/profile_store.c', 'firmware/src/config_protocol.c',
                   'firmware/src/raw_wire.c', 'firmware/src/etherlink_usb.c',
                   'firmware/src/etherlink_backend.c']
        subprocess.run(['cc', '-std=c11', '-shared', '-fPIC', '-O1', '-Wall', '-Wextra', '-Werror',
                        '-DFUNKOTTO_W3=1', '-DFUNKOTTO_W4=1', '-DFUNKOTTO_ETHERLINK_USB=1',
                        '-I'+str(ROOT/'tests/wifi_fakes'), '-I'+str(ROOT/'firmware/include'),
                        '-I'+str(ROOT/'protocol'), *[str(ROOT/p) for p in sources], '-o', output], check=True)
        cls.lib = ctypes.CDLL(output)
        cls.lib.bridge_exchange.argtypes = [ctypes.c_void_p, ctypes.c_size_t, ctypes.c_void_p]
        cls.lib.bridge_exchange.restype = ctypes.c_size_t
        cls.lib.bridge_tx_queued.restype = ctypes.c_uint32
        cls.lib.bridge_rx_full.restype = ctypes.c_uint32
        cls.lib.bridge_receive.argtypes = [ctypes.c_void_p, ctypes.c_size_t]
        cls.lib.bridge_set_send_hook.argtypes = [SEND_HOOK]

    @classmethod
    def tearDownClass(cls):
        cls.tmp.cleanup()

    def setUp(self):
        self.lib.bridge_init(); self.port = Port(self.lib)
        self.client = el.Client(self.port, timeout=0.003, retries=1)
        self.hello = self.client.hello()
        mac = bytes.fromhex(self.hello['mac'].replace(':', ''))
        self.frame = bytes(6)+mac+b'\x08\x00'+bytes(46)

    def test_config_over_same_session(self):
        config = el.ConfigOverEtherlink(self.client)
        self.assertEqual(config.hello()['protocol'], 'FOC1')
        self.assertEqual(config.status()['state'], 'LINK_UP')
        self.assertEqual(config.command(7)['result'], 'OK')
        self.assertFalse(self.client.stats()['link'])

    def test_eight_async_tickets_and_rx(self):
        tickets = [self.client.tx_submit(self.frame) for _ in range(8)]
        self.assertEqual(len(set(tickets)), 8)
        self.assertEqual(self.client.stats()['tx_free'], 0)
        with self.assertRaisesRegex(el.TransportError, 'BUSY'):
            self.client.tx_submit(self.frame)
        self.client.wait_tx(tickets)
        self.assertEqual(self.client.stats()['tx_free'], 8)
        self.assertEqual([self.client.rx() for _ in range(8)], [self.frame]*8)
        self.assertIsNone(self.client.rx())

    def test_maximum_frame_through_production_stream(self):
        frame = self.frame+bytes((i*71) & 255 for i in range(1514-len(self.frame)))
        self.client.tx(frame)
        self.assertEqual(self.client.rx(), frame)

    def test_lost_tx_reply_no_double_send(self):
        self.port.drop = 1
        ticket = self.client.tx_submit(self.frame)
        self.assertEqual(self.lib.bridge_tx_queued(), 1)
        self.assertEqual(self.port.writes[-1], self.port.writes[-2])
        self.assertEqual(self.client.retries_used, 1)
        self.client.wait_tx([ticket])

    def test_truncated_reply_recovered(self):
        self.port.corrupt = True
        self.assertTrue(self.client.stats()['link'])
        self.assertEqual(self.client.bad_frames, 1)
        self.assertEqual(self.client.retries_used, 1)

    def test_timeout_poison_session(self):
        self.port.drop = 10
        with self.assertRaisesRegex(el.TransportError, 'timeout'):
            self.client.tx_submit(self.frame)
        self.assertEqual(self.lib.bridge_tx_queued(), 1)
        count = len(self.port.writes)
        with self.assertRaises(el.TransportError):
            self.client.tx_submit(self.frame)
        self.assertEqual(count, len(self.port.writes))

    def test_short_write_not_resubmitted(self):
        self.port.short = True
        with self.assertRaisesRegex(el.TransportError, 'Short USB'):
            self.client.tx_submit(self.frame)
        self.assertTrue(self.client.failed)
        self.assertEqual(self.lib.bridge_tx_queued(), 1)

    def test_session_loss_stops_client(self):
        self.lib.bridge_drop_session()
        with self.assertRaises(el.TransportError):
            self.client.stats()
        self.assertTrue(self.client.failed)

    def test_close_and_new_session(self):
        old = self.client.session; self.client.close()
        fresh = el.Client(self.port)
        fresh.hello(); self.assertNotEqual(old, fresh.session)

    def test_epoch_error_no_queue_mutation(self):
        self.client.epoch -= 1
        with self.assertRaisesRegex(el.TransportError, 'EPOCH'):
            self.client.tx_submit(self.frame)
        self.assertEqual(self.lib.bridge_tx_queued(), 0)
        self.client.stats(); self.client.tx(self.frame)

    def test_nonce_rejects_stale_hello(self):
        self.client.close()
        fresh = el.Client(self.port, timeout=0.003)
        payload = b'\0'+b'wrong123'+bytes(16)
        self.port.input.extend(el.packet(0x81, 123, 1, 0, payload))
        with self.assertRaisesRegex(el.TransportError, 'nonce'):
            fresh.hello()
        self.assertTrue(fresh.failed)

    def test_wire_magic_flags_crc_and_lengths(self):
        for n in (0, 1, 60, 1514, 1515):
            payload = bytes(i % 256 for i in range(n))
            raw = el.packet(3, 123, 5, 7, payload)
            self.assertEqual(el.decode_packet(raw[1:-1]), (3, 123, 5, 7, payload))
        raw = bytearray(el.cobs_decode(el.packet(3, 123, 5, 7)[1:-1]))
        raw[5] = 1; struct.pack_into('!I', raw, len(raw)-4, zlib.crc32(raw[:-4]))
        with self.assertRaises(ValueError):
            el.decode_packet(el.cobs_encode(raw))

    def setup_peer(self, delayed=False, arp_requests=False, lost_seq=None):
        mac = bytes.fromhex(self.hello['mac'].replace(':', ''))
        peer = bytes.fromhex('020304050607')
        source = bytes([192,168,25,234]); target = bytes([192,168,25,1])
        self.pending_replies = []; self.arp_replies = []
        self.background = b'\xff'*6+peer+b'\x88\x99'+bytes(46)

        def inject(frame):
            self.lib.bridge_receive(frame, len(frame))

        def sent(n, p):
            frame = ctypes.string_at(p, n)
            if frame[12:14] == b'\x08\x06':
                self.arp_replies.append(frame); return
            ip = bytearray(frame[14:]); ihl = (ip[0]&15)*4
            total = int.from_bytes(ip[2:4], 'big')
            seq = int.from_bytes(ip[ihl+6:ihl+8], 'big')
            ip[12:16], ip[16:20] = ip[16:20], ip[12:16]
            ip[10:12] = b'\0\0'; ip[10:12] = struct.pack('!H', checksum(bytes(ip[:ihl])))
            ip[ihl] = 0; ip[ihl+2:ihl+4] = b'\0\0'
            ip[ihl+2:ihl+4] = struct.pack('!H', checksum(bytes(ip[ihl:total])))
            noise = arp(peer,target,source) if arp_requests else self.background
            frames = [noise]
            if seq != lost_seq:
                frames.append(mac+peer+b'\x08\x00'+bytes(ip))
            if delayed:
                self.pending_replies.extend(frames)
            else:
                for reply in frames: inject(reply)

        self.send_callback = SEND_HOOK(sent)
        self.lib.bridge_set_send_hook(self.send_callback)
        if delayed:
            def before(op):
                if op == 4:
                    for _ in range(min(2,len(self.pending_replies))):
                        inject(self.pending_replies.pop(0))
            self.port.before_request = before
        capture = type('Capture', (), {'frames': [], 'write': lambda s,f: s.frames.append(f)})()
        network = el.WindowNetworkTest(self.client,mac,source,target,capture)
        network.claimed = True
        return network,peer,inject

    def test_old_batch_reproduces_rx_overflow(self):
        network,peer,_ = self.setup_peer()
        tickets = [self.client.tx_submit(el.echo_frame(network.mac,peer,network.source,
                   network.target,1,i,b'x')) for i in range(8)]
        self.client.wait_tx(tickets)  # The old send-all / receive-later schedule.
        self.assertGreater(self.lib.bridge_rx_full(),0)

    def test_interleaved_eight_tickets_with_background_and_arp(self):
        for delayed,arp_requests in ((False,False),(True,False),(False,True),(True,True)):
            with self.subTest(delayed=delayed,arp=arp_requests):
                self.setUp()
                network,peer,_ = self.setup_peer(delayed,arp_requests)
                before = self.client.stats()
                with contextlib.redirect_stdout(io.StringIO()):
                    result = el.ping_window(network,peer,24,[0,56,512,1472],0.02,8)
                after = self.client.stats()
                self.assertEqual((result['received'],result['lost']), (24,0))
                self.assertEqual((result['maximum_outstanding'],result['minimum_tx_free']), (8,0))
                self.assertEqual((after['tx_free'],after['tx_done']), (8,0))
                self.assertEqual(self.lib.bridge_rx_full(),0)
                self.assertEqual(el.rx_accounting(before,after,network.frames_read)['overflow_inferred'],0)
                self.assertEqual(len(network.deferred),0)
                self.assertEqual(bool(self.arp_replies),arp_requests)
                self.assertGreaterEqual(len(network.capture.frames),72)

    def test_missing_reply_is_still_a_loss(self):
        network,peer,_ = self.setup_peer(lost_seq=3)
        with contextlib.redirect_stdout(io.StringIO()):
            result = el.ping_window(network,peer,8,[56],0.005,8)
        self.assertEqual((result['received'],result['lost']), (7,1))
        self.assertEqual(self.client.stats()['tx_free'],8)

    def test_continuous_background_does_not_starve_tx(self):
        network,peer,inject = self.setup_peer()
        self.port.before_request = lambda op: inject(self.background) if op==5 else None
        with contextlib.redirect_stdout(io.StringIO()):
            result = el.ping_window(network,peer,8,[56],0.02,8)
        # This producer injects one frame per RX request plus every echo, so
        # sustained overflow is unavoidable. Still finish and release tickets.
        self.assertGreater(result['received'],0)
        self.assertEqual(result['maximum_outstanding'],8)
        self.assertEqual(self.client.stats()['tx_free'],8)
        self.assertGreater(self.lib.bridge_rx_full(),0)

    def test_arp_defer_is_bounded_and_conflicts_still_fail(self):
        network,peer,inject = self.setup_peer()
        ticket = self.client.tx_submit(el.echo_frame(network.mac,peer,network.source,
                                      network.target,1,1,b'x'))
        for i in range(8): network.send(bytes([i])*60)
        network.send(bytes(60))  # Duplicate deferred reply is coalesced.
        with self.assertRaisesRegex(el.TransportError,'queue full'): network.send(bytes([8])*60)
        with self.assertRaisesRegex(el.TransportError,'tickets outstanding'): network.flush_deferred(lambda:None)
        inject(arp(peer,network.source,network.source))
        with self.assertRaisesRegex(el.TransportError,'Address conflict'): network.receive()
        self.client.wait_tx([ticket])

    def test_rx_accounting_wrap_queue_and_epoch(self):
        before = dict(epoch=2,rx_serial=0xfffffffe,rx_count=2)
        after = dict(epoch=2,rx_serial=8,rx_count=1)
        self.assertEqual(el.rx_accounting(before,after,8)['overflow_inferred'],3)
        after['epoch']=3
        with self.assertRaises(el.TransportError): el.rx_accounting(before,after,8)

    def test_tx_timeout_still_applies_with_rx_service(self):
        calls = []
        with patch.object(self.client,'tx_result',return_value=False), \
                patch.object(el.time,'monotonic',side_effect=[0,6]):
            with self.assertRaisesRegex(el.TransportError,'completion timeout'):
                self.client.wait_tx([123],service_rx=lambda:calls.append(1))
        self.assertEqual(calls,[1]); self.assertTrue(self.client.failed)


if __name__ == '__main__':
    unittest.main()
