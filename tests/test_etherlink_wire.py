"""Independent Python/zlib reference for the C FOE1 wire format."""
import ctypes
from pathlib import Path
import struct
import subprocess
import tempfile
import unittest
import zlib

ROOT=Path(__file__).resolve().parents[1]


class EtherlinkWire(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.tmp=tempfile.TemporaryDirectory(prefix='foe1-reference-')
        so=Path(cls.tmp.name)/'codec.so'
        subprocess.run(['cc','-std=c11','-O2','-shared','-fPIC',
            '-I'+str(ROOT/'protocol'),str(ROOT/'protocol/etherlink.c'),'-o',str(so)],check=True)
        cls.lib=ctypes.CDLL(str(so))
        cls.lib.fo_el_pack.argtypes=[ctypes.c_void_p,ctypes.c_size_t,ctypes.c_uint8,
            ctypes.c_uint64,ctypes.c_uint32,ctypes.c_uint32,ctypes.c_void_p,ctypes.c_size_t]
        cls.lib.fo_el_pack.restype=ctypes.c_size_t
        cls.lib.fo_el_unpack.argtypes=[ctypes.c_void_p,ctypes.c_size_t,ctypes.c_void_p]
        cls.lib.fo_el_unpack.restype=ctypes.c_bool

    @classmethod
    def tearDownClass(cls):cls.tmp.cleanup()

    @staticmethod
    def packet(op,payload,flags=0):
        header=struct.pack('>4sBBHIQI',b'FOE1',op,flags,len(payload),0x10203040,
                           0x123456789abcdef0,0xaabbccdd)
        data=header+payload
        return data+struct.pack('>I',zlib.crc32(data))

    def test_reference_packets(self):
        for length in (0,1,14,60,128,1514,1515):
            p=bytes((i*73)&255 for i in range(length));out=ctypes.create_string_buffer(1543)
            n=self.lib.fo_el_pack(out,1543,3,0x123456789abcdef0,0x10203040,0xaabbccdd,p,len(p))
            self.assertEqual(out.raw[:n],self.packet(3,p))
            frame=ctypes.create_string_buffer(64)
            self.assertTrue(self.lib.fo_el_unpack(self.packet(3,p),len(p)+28,frame))

    def test_reserved_flags_rejected_with_valid_crc(self):
        data=self.packet(3,b'hello',flags=1);frame=ctypes.create_string_buffer(64)
        self.assertFalse(self.lib.fo_el_unpack(data,len(data),frame))

    def test_trailing_bytes_rejected(self):
        data=self.packet(3,b'hello')+b'\x00';frame=ctypes.create_string_buffer(64)
        self.assertFalse(self.lib.fo_el_unpack(data,len(data),frame))


if __name__=='__main__':unittest.main()
