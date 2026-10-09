import ctypes
import importlib.util
from pathlib import Path
import struct
import subprocess
import tempfile
import unittest
import zipfile

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('wifi_diag', ROOT / 'tools/wifi_diag.py')
w = importlib.util.module_from_spec(spec); spec.loader.exec_module(w)
MAC = bytes.fromhex('020102030405')
PEER = bytes.fromhex('02060708090a')
SOURCE = bytes([192,168,1,250]); TARGET = bytes([192,168,1,2])


class WireTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.tmp = tempfile.TemporaryDirectory()
        lib = Path(cls.tmp.name) / 'raw.so'
        subprocess.run(['cc','-std=c11','-Wall','-Wextra','-Werror','-shared','-fPIC',
                        '-I'+str(ROOT/'firmware/include'), str(ROOT/'tests/raw_bridge.c'),
                        str(ROOT/'firmware/src/ethernet.c'), str(ROOT/'firmware/src/raw_wire.c'),
                        '-o',str(lib)],check=True)
        cls.lib = ctypes.CDLL(str(lib))
        cls.lib.bridge_feed.argtypes = [ctypes.c_uint8,ctypes.c_void_p]
        cls.lib.bridge_feed.restype = ctypes.c_size_t
        cls.lib.bridge_poll.argtypes = [ctypes.c_void_p]
        cls.lib.bridge_poll.restype = ctypes.c_size_t
        cls.lib.bridge_rx.argtypes = [ctypes.c_void_p,ctypes.c_size_t]

    @classmethod
    def tearDownClass(cls):
        cls.tmp.cleanup()

    def setUp(self):
        self.lib.bridge_start()

    def client(self, dropped=()):
        lib = self.lib
        class Port:
            def __init__(self):
                self.buffer = bytearray(); self.drop = set(dropped); self.seen = set()
            def reply(self, raw):
                decoded = w.decode_packet(raw)
                seq = decoded[2]
                if seq in self.drop and seq not in self.seen:
                    self.seen.add(seq)
                    # Truncated response, including missing trailing delimiter.
                    self.buffer.extend(b'\0'+raw[:7]); return
                self.buffer.extend(b'\0'+raw+b'\0')
            def write(self, data):
                output = (ctypes.c_uint8*1600)()
                for b in data:
                    n = lib.bridge_feed(b,output)
                    if n: self.reply(bytes(output[:n]))
                n = lib.bridge_poll(output)
                if n: self.reply(bytes(output[:n]))
                return len(data)
            def read(self, count):
                data = bytes(self.buffer[:count]); del self.buffer[:count]; return data
        return w.Client(Port(),timeout=0.003,retries=2)

    def test_cross_language_replay_after_truncated_hello_tx_rx(self):
        client = self.client(dropped=(1,2,3))
        hello = client.hello(); self.assertEqual(hello['mac'],MAC); self.assertEqual(hello['epoch'],7)
        frame = PEER+MAC+b'\x08\x00'+bytes((x*71)&255 for x in range(1500))
        client.tx(frame)
        self.lib.bridge_rx(frame,len(frame)); self.lib.bridge_rx(frame[:60],60)
        self.assertEqual(client.rx(),frame)
        stats = client.stats()
        self.assertEqual(stats['tx_queued'],1); self.assertEqual(stats['tx_ok'],1)
        self.assertEqual(stats['rx_used'],1); self.assertEqual(stats['replays'],3)
        self.assertEqual(client.retries_used,3)
        self.assertEqual(client.rx(),frame[:60]); self.assertIsNone(client.rx())

    def test_stats_overflow_and_bad_frame_recovery(self):
        client=self.client(); client.hello()
        frame=PEER+MAC+b'\x08\x06'+bytes(46)
        for _ in range(12): self.lib.bridge_rx(frame,len(frame))
        client.port.write(b'\0'+b'\x01'*4000+b'\0')
        stats=client.stats(); self.assertEqual(stats['rx_full'],4)
        self.assertEqual(stats['rx_used'],8); self.assertEqual(stats['bad_frames'],1)

    def test_sequence_mutation_has_no_side_effect(self):
        client=self.client(); client.hello(); client.stats()
        client.seq=1  # Try to reuse STATS sequence for a different request.
        with self.assertRaises(w.TransportError): client.tx(PEER+MAC+b'\x08\x00'+bytes(46))
        self.assertTrue(client.failed)

    def test_new_session_rejected(self):
        client=self.client(); client.hello(); client.session+=1
        with self.assertRaises(w.TransportError): client.stats()

    def test_codec_crc_and_malformed(self):
        for size in [0,1,253,254,255,510,1514]:
            for data in [bytes(size),bytes((i%255)+1 for i in range(size))]:
                self.assertEqual(w.cobs_decode(w.cobs_encode(data)),data)
        for data in [b'\0',b'\x03a',b'\x02\0']:
            with self.assertRaises(ValueError): w.cobs_decode(data)
        data=w.packet(3,5,7,bytes(range(256)))
        self.assertEqual(w.decode_packet(data[1:-1]),(3,5,7,bytes(range(256))))
        damaged=bytearray(w.cobs_decode(data[1:-1])); damaged[-1]^=1
        with self.assertRaises(ValueError): w.decode_packet(w.cobs_encode(damaged))


class NetworkTests(unittest.TestCase):
    def test_arp_layout_and_reply(self):
        frame=w.arp(MAC,SOURCE,TARGET)
        self.assertEqual(len(frame),60)
        self.assertEqual(w.parse_arp(frame),(1,MAC,SOURCE,bytes(6),TARGET))
        reply=w.arp(PEER,TARGET,SOURCE,MAC,2,MAC)
        self.assertEqual(w.parse_arp(reply),(2,PEER,TARGET,MAC,SOURCE))
        self.assertIsNone(w.parse_arp(frame[:41]))

    def test_echo_boundaries_checksums_sequence_payload(self):
        for size in [0,1,56,512,1472]:
            payload=bytes(i%256 for i in range(size))
            request=w.echo_frame(MAC,PEER,SOURCE,TARGET,0x1234,17,payload)
            self.assertEqual(len(request),max(60,42+size))
            self.assertEqual(w.checksum(request[14:34]),0)
            # Peer reply: swapped addresses, ICMP type/checksum, correct payload.
            reply=bytearray(w.echo_frame(PEER,MAC,TARGET,SOURCE,0x1234,17,payload))
            total=struct.unpack_from('!H',reply,16)[0]
            reply[34]=0; reply[36:38]=bytes(2)
            reply[36:38]=struct.pack('!H',w.checksum(bytes(reply[34:14+total])))
            self.assertTrue(w.echo_reply(bytes(reply),MAC,PEER,SOURCE,TARGET,0x1234,17,payload))
            self.assertFalse(w.echo_reply(bytes(reply),MAC,PEER,SOURCE,TARGET,0x1234,18,payload))
            reply[36]^=1
            self.assertFalse(w.echo_reply(bytes(reply),MAC,PEER,SOURCE,TARGET,0x1234,17,payload))

    def test_address_conflict(self):
        class Client:
            def rx(self): return w.arp(PEER,SOURCE,TARGET)
        test=w.NetworkTest(Client(),MAC,SOURCE,TARGET,None)
        with self.assertRaises(w.TransportError): test.receive()

    def test_answer_peer_arp(self):
        class Client:
            sent=[]
            def rx(self): return w.arp(PEER,TARGET,SOURCE)
            def tx(self,frame): self.sent.append(frame)
        client=Client(); test=w.NetworkTest(client,MAC,SOURCE,TARGET,None); test.claimed=True
        test.receive(); self.assertEqual(w.parse_arp(client.sent[0]),(2,MAC,SOURCE,PEER,TARGET))

    def test_pcap_records(self):
        with tempfile.TemporaryDirectory() as tmp:
            p=Path(tmp)/'test.pcap'; c=w.Pcap(p); frame=w.arp(MAC,SOURCE,TARGET); c.write(frame); c.close()
            data=p.read_bytes(); self.assertEqual(len(data),24+16+60)
            self.assertEqual(struct.unpack_from('<I',data,20)[0],1)
            self.assertEqual(data[40:],frame)

class PackageTests(unittest.TestCase):
    def test_finalized_zip_contains_exact_bytes(self):
        spec=importlib.util.spec_from_file_location('package_firmware',ROOT/'tools/package_firmware.py')
        module=importlib.util.module_from_spec(spec); spec.loader.exec_module(module)
        with tempfile.TemporaryDirectory() as tmp:
            path=Path(tmp)/'firmware.zip'
            entries={'firmware.uf2':bytes(range(256))*4096,'ANLEITUNG.md':b'Test\n'}
            digest=module.write_verified_zip(path,entries)
            self.assertEqual(len(digest),64)
            with zipfile.ZipFile(path) as archive:
                self.assertIsNone(archive.testzip())
                self.assertEqual(set(archive.namelist()),set(entries))
                for name,data in entries.items(): self.assertEqual(archive.read(name),data)

if __name__=='__main__': unittest.main()
