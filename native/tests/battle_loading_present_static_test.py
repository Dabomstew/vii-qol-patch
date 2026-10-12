"""Read-only qualification of the exact drain call and untouched resource paths."""
import hashlib
import os
from pathlib import Path
import struct
from load_timing_static_test import pe


def main():
    data = Path(os.environ['VII_GAME_EXE']).read_bytes()
    assert hashlib.sha256(data).hexdigest() == '7ff2aad55e965add3b6fc45bd0ad2158769de014d6f619dc564a50c3e0fa42d9'
    base, read = pe(data)
    assert base == 0x400000
    assert read(0x258e5f, 9) == bytes.fromhex('50 e8 2b ff ff ff 83 c4 04')
    assert 0x258e65 + struct.unpack('<i', read(0x258e61, 4))[0] == 0x258d90
    assert read(0x39354d, 11) == bytes.fromhex('ff 51 20 5e 3d 01 00 7a 08 75 0c')
    # All resource service and lock release occur before the selected Present.
    for call, target in ((0x3934a5, 0x38b310), (0x3934aa, 0x38b930),
                         (0x3934af, 0x38ca40), (0x3934b4, 0x38b110),
                         (0x3934d4, 0x390d00)):
        assert read(call, 1) == b'\xe8'
        assert call + 5 + struct.unpack('<i', read(call + 1, 4))[0] == target
    assert read(0x3934be, 10) == bytes.fromhex('c7 05 70 8c bc 04 00 00 00 00')
    assert read(0x3934c8, 6) == bytes.fromhex('ff 15 24 d1 8e 00')
    assert read(0x3934fa, 13) == bytes.fromhex('6a 01 6a 00 8b 40 34 50 8b 08 ff 51 20')
    assert read(0x393544, 9) == bytes.fromhex('6a 00 56 8b 40 34 50 8b 08')
    assert read(0x393558, 12) == bytes.fromhex('a1 64 64 bc 04 c7 40 7c 01 00 00 00')
    print('PASS: baseline, callback ownership dispatch, nine whole instructions, resource service/lock release, TEST/occlusion path')


if __name__ == '__main__':
    main()
