import importlib.util
from pathlib import Path
import struct
import unittest

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location("artifact_check", ROOT / "tools/check_firmware_artifacts.py")
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)

def block(address=0x10000000, family=0xE48BFF59, flags=0x2000, size=256):
    data = bytearray(512)
    struct.pack_into("<8I", data, 0, 0x0A324655, 0x9E5D5157, flags, address, size, 0, 1, family)
    struct.pack_into("<I", data, 508, 0x0AB16F30)
    return data

def uf2(data):
    prefix = block(0x103FDF00, 0xE48BFF57, 0xA000)
    struct.pack_into("<I", prefix, 24, 2)
    prefix[32:288] = bytes([0xEF]) * 256
    struct.pack_into("<I", prefix, 288, 0x9957E304)
    return prefix + data

class UF2Tests(unittest.TestCase):
    def test_last_application_page(self):
        self.assertEqual(module.validate_uf2(uf2(block(0x103FCF00)))["end_address_exclusive"], "0x103fd000")

    def test_profile_sectors_and_out_of_range(self):
        for address in (0x0FFFFF00, 0x103FD000, 0x103FE000, 0x103FFF00, 0x10400000):
            with self.subTest(address=address), self.assertRaises(ValueError):
                module.validate_uf2(uf2(block(address)))

    def test_corrupt_and_wrong_target(self):
        for data in (b"", block()[:-1], block(family=0xE48BFF56), block(flags=0xA000), block(size=476)):
            with self.subTest(data_length=len(data)), self.assertRaises(ValueError):
                module.validate_uf2(uf2(data))

    def test_inconsistent_block_count(self):
        with self.assertRaises(ValueError):
            module.validate_uf2(uf2(block() + block(0x10000100)))

    def test_sdk_default_absolute_block_is_rejected(self):
        data = uf2(block())
        struct.pack_into("<I", data, 12, 0x10FFFF00)
        with self.assertRaises(ValueError):
            module.validate_uf2(data)

    def test_missing_ignore_extension_is_rejected(self):
        data = uf2(block())
        struct.pack_into("<I", data, 288, 0)
        with self.assertRaises(ValueError):
            module.validate_uf2(data)

if __name__ == "__main__":
    unittest.main()
