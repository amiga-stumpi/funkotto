import ctypes
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
import unittest
import zlib

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT/'tools'))
import etherlink_diag as el


class Port:
    def __init__(self, lib):
        self.lib = lib; self.input = bytearray(); self.writes = []
        self.drop = 0; self.short = False; self.corrupt = False

    def write(self, data):
        self.writes.append(data)
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


if __name__ == '__main__':
    unittest.main()
