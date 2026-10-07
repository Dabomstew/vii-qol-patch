"""Verify load timing's x86 boundaries against the original EXE; never launch it."""
import hashlib
import os
from pathlib import Path
import struct

ROOT = Path(__file__).resolve().parents[2]


def pe(data):
    nt = struct.unpack_from('<I', data, 0x3c)[0]
    assert data[nt:nt+6] == b'PE\0\0\x4c\x01'
    count, optional_size = struct.unpack_from('<H', data, nt+6)[0], struct.unpack_from('<H', data, nt+20)[0]
    optional = nt + 24
    assert struct.unpack_from('<H', data, optional)[0] == 0x10b
    base = struct.unpack_from('<I', data, optional+28)[0]
    sections = [struct.unpack_from('<8sIIII', data, optional+optional_size+i*40) for i in range(count)]

    def read(rva, size):
        for _, virtual_size, start, raw_size, raw in sections:
            if start <= rva and rva+size <= start+max(virtual_size, raw_size):
                offset = rva-start
                available = min(size, max(0, raw_size-offset))
                return data[raw+offset:raw+offset+available] + bytes(size-available)
        raise ValueError(f'Unmapped RVA {rva:x}')

    return base, read


def main():
    path = Path(os.environ['VII_GAME_EXE'])
    source = path.read_bytes()
    assert hashlib.sha256(source).hexdigest() == '7ff2aad55e965add3b6fc45bd0ad2158769de014d6f619dc564a50c3e0fa42d9'
    base, read = pe(source)
    assert base == 0x400000
    assert read(0x258e40, 8) == bytes.fromhex('55 8b ec ff 75 10 8b 0d')
    # Whole copied instructions end before MOV ECX,[absolute manager].
    assert read(0x258e46, 6) == bytes.fromhex('8b 0d 88 9a bc 04')
    assert read(0x3af891, 8) == bytes.fromhex('e8 2a 52 05 00 e8 65 ac')
    assert 0x3af891+5+struct.unpack('<i', read(0x3af892,4))[0] == 0x404ac0
    assert read(0x404ac0, 8) == bytes.fromhex('55 8b ec 83 ec 08 53 56')
    assert read(0x404b4c, 8) == bytes.fromhex('8b 7e 20 85 ff 0f 84 8a')
    # Parent's sole callback dispatch remains cdecl with one stack argument.
    assert read(0x258e5f, 9) == bytes.fromhex('50 e8 2b ff ff ff 83 c4 04')
    for call, target, signature in (
        (0x2a3841, 0x2a36c0, 'e8 7a fe ff ff 83 c4 04'),
        (0x2a3863, 0x2a3560, 'e8 f8 fc ff ff 8b 0d 88'),
        (0x2a31a6, 0x246360, 'e8 b5 31 fa ff 3c 01 75'),
        (0x8e5af, 0x87a80, 'e8 cc 94 ff ff 8b f0 8b'),
        (0x8e698, 0x8fb30, 'e8 93 14 00 00 84 c0 0f'),
        (0x8e6ee, 0x39c890, 'e8 9d e1 30 00 83 c4 04'),
        (0x8e85f, 0x137ac0, 'e8 5c 92 0a 00 84 c0 75'),
        (0x2bc385, 0x87a80, 'e8 f6 b6 dc ff 8b f0 8b'),
        (0x2b32cd, 0x2473e0, 'e8 0e 41 f9 ff 84 c0 0f'),
        (0x2a9f0d, 0x2a9cb0, 'e8 9e fd ff ff 83 c4 04'),
        (0x24b4c3, 0xee9a0, 'e8 d8 34 ea ff 83 c4 04'),
        (0xee9c6, 0xf5720, 'e8 55 6d 00 00 83 c4 08'),
        (0xee9f6, 0xf5720, 'e8 25 6d 00 00 83 c4 08'),
        (0xeed2c, 0xf5720, 'e8 ef 69 00 00 83 c4 08'),
        (0x47abd0, 0x47a580, 'e8 ab f9 ff ff 8d 45 d4'),
        (0x47a60c, 0x370370, 'e8 5f 5d ef ff 83 c4 28'),
        (0x47a623, 0x370370, 'e8 48 5d ef ff 83 c4 04'),
        (0x47a65b, 0x370940, 'e8 e0 62 ef ff 8b f8 83'),
        (0x47a671, 0x370590, 'e8 1a 5f ef ff 57 e8 74'),
        (0x47a677, 0x370af0, 'e8 74 64 ef ff 83 c4 10'),
        (0x3cae8e, 0x3ca3a0, 'e8 0d f5 ff ff 83 c4 04'),
    ):
        assert read(call, 8) == bytes.fromhex(signature)
        assert call+5+struct.unpack('<i', read(call+1, 4))[0] == target
    # ADV's first getter uses thiscall (one callee-popped stack argument).
    assert read(0x87a80,16)==bytes.fromhex('55 8b ec 8b 45 08 85 c0 74 07 8b 40 24 5d c2 04')
    # Busy branch skips to the final cancellation test; it cannot run input.
    assert read(0x8e69d,8)==bytes.fromhex('84 c0 0f 85 ba 01 00 00')
    assert read(0x8e6ec,2)==bytes.fromhex('6a 03')
    assert read(0x8e6f6,8)==bytes.fromhex('84 c0 0f 85 61 01 00 00')
    assert read(0x137ac0,12)==bytes.fromhex('33 c0 39 05 e0 5f b0 00 0f 95 c0 c3')
    # Title getter returns window+6C; setup uses one cdecl argument and AL result.
    assert read(0x3a3920,11)==bytes.fromhex('55 8b ec 8b 45 08 8b 40 6c 5d c3')
    assert read(0x3cae93,5)==bytes.fromhex('83 c4 04 3c 01')
    dll = (ROOT/'build/native-build/dinput8.dll').read_bytes()
    dll_base, dll_read = pe(dll)
    magic = b'VIILT001'
    assert dll.count(magic) == 1, 'Descriptor must be uniquely discoverable'
    at = dll.index(magic)
    version, size, state, state_size = struct.unpack_from('<IIII', dll, at+8)
    assert (version, size, state_size) == (1, 24, 112)
    assert state % 8 == 0
    assert dll_read(state-dll_base, state_size) == bytes(state_size), 'Bridge starts disabled and empty'
    print('PASS: baseline identity, callback/trampoline ABI, clock call target, unique versioned descriptor and initial state')


if __name__ == '__main__':
    main()
