import ctypes
import os
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT/'tools'))
import wifi_config as cfg


class NativePort:
    """The actual C wire parser/dispatcher with only WLAN operations simulated."""
    def __init__(self, lib):
        self.lib = lib; self.buffer = bytearray(); self.drop = 0
        self.reset_response = False

    def write(self, data):
        out = ctypes.create_string_buffer(4096)
        n = self.lib.test_feed(bytes(data), len(data), out)
        if n:
            if self.drop:
                self.drop -= 1
            elif self.reset_response:
                op, session, seq, payload = cfg.decode_packet(out.raw[:n])
                self.buffer.extend(cfg.packet(op, session+1, seq, payload))
            else:
                self.buffer.extend(b'\0'+out.raw[:n]+b'\0')
        return len(data)

    def read(self, n):
        result = bytes(self.buffer[:n]); del self.buffer[:n]; return result


class ConfigClientTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.tmp = tempfile.TemporaryDirectory()
        lib = Path(cls.tmp.name)/'config.so'
        subprocess.run([os.environ.get('CC', 'cc'), '-std=c11', '-Wall', '-Wextra', '-Werror',
                        '-Wconversion', '-Wshadow', '-shared', '-fPIC', '-I'+str(ROOT/'firmware/include'),
                        *[str(ROOT/p) for p in ('tests/config_backend.c', 'firmware/src/config_protocol.c',
                          'firmware/src/config_wire.c', 'firmware/src/raw_wire.c', 'firmware/src/wifi_model.c')],
                        '-o', str(lib)], check=True)
        cls.lib = ctypes.CDLL(str(lib))
        cls.lib.test_feed.argtypes = [ctypes.c_char_p, ctypes.c_size_t, ctypes.c_void_p]
        cls.lib.test_feed.restype = ctypes.c_size_t
        cls.lib.test_calls.restype = ctypes.c_uint

    @classmethod
    def tearDownClass(cls):
        cls.tmp.cleanup()

    def setUp(self):
        self.lib.test_reset(); self.lib.test_auto(1)
        self.port = NativePort(self.lib); self.client = cfg.Client(self.port, timeout=0.002, retries=2)
        self.client.hello()

    def test_profile_roundtrip_and_explicit_flash(self):
        secret = b'secret42'
        self.client.command(5, cfg.profile_payload(b'Linux', secret))
        s = self.client.status()
        self.assertEqual(s['ssid'], 'Linux'); self.assertFalse(s['stored'])
        self.client.command(6); self.assertEqual(self.client.status()['state'], 'LINK_UP')
        self.client.command(8); self.assertTrue(self.client.status()['stored'])
        self.client.command(7); self.assertFalse(self.client.status()['auto_retry'])
        self.client.command(9); self.assertFalse(self.client.status()['configured'])
        self.assertNotIn('secret42', str(s))

    def test_dropped_save_response_replayed_once(self):
        self.port.drop = 1
        self.client.command(8)
        self.assertEqual(self.lib.test_calls(), 1)
        self.assertEqual(self.client.usb_retries, 1)
        self.assertEqual(self.client.status()['storage_sequence'], 1)

    def test_scan(self):
        s = self.client.scan()
        self.assertEqual(s['results'][0]['ssid'], 'Linux')
        self.assertEqual(s['results'][0]['rssi'], -40)
        self.assertEqual(s['generation'], 1)

    def test_timeout_no_automatic_new_session(self):
        self.port.drop = 10
        with self.assertRaisesRegex(cfg.TransportError, 'outcome unknown'):
            self.client.command(8)
        self.assertEqual(self.lib.test_calls(), 1)
        with self.assertRaisesRegex(cfg.TransportError, 'Session unusable'):
            self.client.command(8)
        self.assertEqual(self.lib.test_calls(), 1)

    def test_reset_no_automatic_resubmit(self):
        self.port.reset_response = True
        with self.assertRaisesRegex(cfg.TransportError, 'session-changed'):
            self.client.command(9)
        self.assertEqual(self.lib.test_calls(), 1)
        self.assertTrue(self.client.failed)

    def test_busy(self):
        self.lib.test_auto(0); self.lib.test_busy(1)
        with self.assertRaisesRegex(cfg.TransportError, 'BUSY'):
            self.client.command(8)
        self.assertEqual(self.lib.test_calls(), 0)

    def test_pending_job_and_failure(self):
        self.lib.test_auto(0)
        p = self.client.rpc(8); job = p[1:]
        self.assertEqual(p[0], 1)
        pending = self.client.rpc(10, job)
        self.assertEqual(pending[6:], b'\x01\xff')
        self.lib.test_complete(5)
        done = self.client.rpc(10, job)
        self.assertEqual(done[6:], b'\x00\x05')
        self.assertEqual(self.lib.test_calls(), 1)

    def test_missing_capability_blocks_mutation(self):
        self.client.capabilities = 1
        with self.assertRaises(cfg.TransportError):
            self.client.command(8)
        self.assertEqual(self.lib.test_calls(), 0)

    def test_bad_crc_and_resync(self):
        frame = bytearray(cfg.packet(8, self.client.session, 2))
        frame[-2] ^= 1
        self.port.write(frame)
        self.assertFalse(self.port.buffer)
        self.assertEqual(self.lib.test_calls(), 0)
        self.client.command(8)
        self.assertEqual(self.lib.test_calls(), 1)

    def test_changed_duplicate_rejected(self):
        self.client.rpc(8)
        self.client.seq -= 1
        with self.assertRaisesRegex(cfg.TransportError, 'SEQUENCE'):
            self.client.rpc(9)
        self.assertEqual(self.lib.test_calls(), 1)

    def test_profile_boundaries(self):
        for ssid, key in [(b'', b'12345678'), (b'x'*33, b'12345678'),
                          (b'x', b'1234567'), (b'x', b'x'*64), (b'x', b'1234567\x00')]:
            with self.assertRaises(ValueError):
                cfg.profile_payload(ssid, key)
        p = cfg.profile_payload(b'\x00'*32, b'x'*63)
        self.client.command(5, p)
        self.assertEqual(self.client.status()['ssid_hex'], '00'*32)

    def test_packet_bounds(self):
        with self.assertRaises(ValueError): cfg.packet(1, 0, 1, b'x'*129)
        with self.assertRaises(ValueError): cfg.decode_packet(b'\x01')


if __name__ == '__main__':
    unittest.main()
