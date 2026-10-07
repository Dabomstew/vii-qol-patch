"""Verify the optional dungeon media helper ABI against the immutable EXE; no launch."""
import hashlib
import os
from pathlib import Path
from load_timing_static_test import pe

raw=Path(os.environ['VII_GAME_EXE']).read_bytes()
assert hashlib.sha256(raw).hexdigest()=='7ff2aad55e965add3b6fc45bd0ad2158769de014d6f619dc564a50c3e0fa42d9'
base,read=pe(raw)
assert base==0x400000
assert read(0x218fc0,16)==bytes.fromhex('55 8b ec 6a ff 68 ee b9 89 00 64 a1 00 00 00 00')
assert read(0x219176,3)==bytes.fromhex('c2 04 00')
assert read(0x219106,5)==bytes.fromhex('e8 25 45 f0 ff')
print('PASS: baseline, complete five-byte trampoline instructions, optional media call and thiscall RET4')
