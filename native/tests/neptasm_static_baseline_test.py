"""Verify the supported executable bytes used by the neptasm compatibility layer.

This is read-only and uses only the Python standard library. It never launches
the game; set VII_GAME_EXE to the local executable before running it.
"""
import hashlib
import os
import struct
from pathlib import Path

EXPECTED_SHA256 = "7ff2aad55e965add3b6fc45bd0ad2158769de014d6f619dc564a50c3e0fa42d9"
GAME = Path(os.environ.get("VII_GAME_EXE", ""))
if not GAME.is_file():
    raise SystemExit("Set VII_GAME_EXE to the supported local NeptuniaVII.exe")
data = GAME.read_bytes()
assert hashlib.sha256(data).hexdigest() == EXPECTED_SHA256, "unsupported executable SHA-256"

def u16(at): return struct.unpack_from("<H", data, at)[0]
def u32(at): return struct.unpack_from("<I", data, at)[0]

assert data[:2] == b"MZ"
nt = u32(0x3C)
assert data[nt:nt + 4] == b"PE\0\0"
sections = u16(nt + 6)
optional = nt + 24
assert u16(optional) == 0x10B
image_base = u32(optional + 28)
optional_size = u16(nt + 20)
section_table = optional + optional_size

def rva_offset(rva):
    for index in range(sections):
        at = section_table + index * 40
        va, size, raw = u32(at + 12), u32(at + 8), u32(at + 16)
        if va <= rva < va + max(size, raw):
            offset = u32(at + 20) + (rva - va)
            assert offset < len(data)
            return offset
    raise AssertionError(f"RVA {rva:08x} is not mapped")

def bytes_at(rva, size):
    offset = rva_offset(rva)
    return data[offset:offset + size]

def check(rva, expected):
    actual = bytes_at(rva, len(expected))
    assert actual == expected, f"RVA {rva:08x}: {actual.hex()} != {expected.hex()}"

check(0x404B4C, bytes.fromhex("8b 7e 20 85 ff 0f 84 8a"))
check(0x46BB12, bytes.fromhex("52 50"))
assert u32(rva_offset(0x3B20A8)) == image_base + 0x4F2B80
check(0x2C147B, bytes.fromhex("96 b4 01 00 00"))
check(0x2C1486, bytes.fromhex("ae b8 01 00 00"))
sizes = [(640, 360), (720, 405), (800, 450), (1024, 576), (1152, 648),
         (1280, 720), (1360, 765), (1366, 768), (1600, 900), (1920, 1080)]
for index, pair in enumerate(sizes):
    assert struct.unpack_from("<II", data, rva_offset(0x6F04C8) + index * 8) == pair
check(0x1081E1, bytes.fromhex("86 80 00 00 00"))
check(0x5BDC94, bytes.fromhex("39 8e e3 3f"))

# Walk the import directory and verify the game's D3D11 IAT slot.
directory = u32(optional + 96 + 8)  # IMAGE_DIRECTORY_ENTRY_IMPORT
import_offset = rva_offset(directory)
found = None
for index in range(4096):
    descriptor = import_offset + index * 20
    original, name_rva, first = u32(descriptor), u32(descriptor + 12), u32(descriptor + 16)
    if not any((original, name_rva, first)):
        break
    name_at = rva_offset(name_rva)
    name = data[name_at:data.index(b"\0", name_at)].decode("ascii").lower()
    if name == "d3d11.dll":
        thunk_rva = original or first
        for thunk_index in range(4096):
            thunk = u32(rva_offset(thunk_rva) + thunk_index * 4)
            if not thunk:
                break
            if thunk & 0x80000000:
                continue
            hint_name = rva_offset(thunk) + 2
            imported = data[hint_name:data.index(b"\0", hint_name)].decode("ascii")
            if imported == "D3D11CreateDeviceAndSwapChain":
                found = first + thunk_index * 4
                break
assert found == 0x4ED4B0, f"D3D11CreateDeviceAndSwapChain IAT RVA {found!r}"
print(f"neptasm static baseline OK: {GAME} (image base {image_base:08x})")
