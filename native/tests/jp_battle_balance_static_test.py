"""Qualify balance detour instructions/operands against the original EXE; never launch."""
import hashlib
import os
import struct
from pathlib import Path
from load_timing_static_test import pe

raw = Path(os.environ['VII_GAME_EXE']).read_bytes()
assert hashlib.sha256(raw).hexdigest() == '7ff2aad55e965add3b6fc45bd0ad2158769de014d6f619dc564a50c3e0fa42d9'
base, read = pe(raw)
assert base == 0x400000
for rva, signature in [(0x18f550,'558bec8b45083de8'),(0x18df70,'558bec8b45083da0'),
                       (0x29ebbf,'f30f599dd4fdffff'),(0x2a2417,'f30f599dd0feffff')]:
    assert read(rva,8) == bytes.fromhex(signature)
# Native operand producers and continuations: complete instruction boundaries.
assert read(0x29ebc7,8) == bytes.fromhex('f30f5995d4fdffff')
assert read(0x2a241f,8) == bytes.fromhex('f30f5985d0feffff')
assert read(0x29ddcc,6) == bytes.fromhex('8985c8fdffff')  # mapped attacker record
assert read(0x29ee1c,6) == bytes.fromhex('ffb5c8fdffff')  # part second argument
assert read(0x2a2207,8) == bytes.fromhex('f30f1185e0feffff')  # selected physical
assert read(0x2a2245,8) == bytes.fromhex('f30f1185b8feffff')  # selected magical
assert read(0x29eb60,8) == bytes.fromhex('f30f109decfdffff')  # effective STR
assert read(0x29eb70,8) == bytes.fromhex('f30f1095f4fdffff')  # effective INT
assert read(0x29eb50,8) == bytes.fromhex('f30f1085fcfdffff')  # effective TEC
# Actual effect dispatch is distinct from the category nibble. Effects2/3
# (recovery/revival) and9 (drain) never enter the ordinary component block.
def near_condition(rva,opcode,target):
    instruction=read(rva,6)
    assert instruction[:2]==bytes.fromhex(opcode)
    assert rva+6+struct.unpack('<i',instruction[2:])[0]==target

near_condition(0x29df94,'0f84',0x29e67b)  # effect0 -> ordinary
near_condition(0x29df9d,'0f84',0x29e67b)  # effect1 -> ordinary
assert read(0x29dfa3,3)==bytes.fromhex('83f902')
near_condition(0x29dfa6,'0f84',0x29e3da)  # recovery
assert read(0x29dfac,3)==bytes.fromhex('83f903')
near_condition(0x29dfaf,'0f84',0x29e3b2)  # revival
assert read(0x29dfb5,3)==bytes.fromhex('83f909')
near_condition(0x29dfb8,'0f85',0x29e38e)  # effect9 falls into drain
assert read(0x29dfbe,1)==b'\x56'
# Completed drain/recovery paths jump over ordinary/part arithmetic to the
# shared later result work, rather than falling through the high-level detour.
for rva,target in [(0x29e389,0x29ee5f),(0x29e435,0x29ee65)]:
    instruction=read(rva,5)
    assert instruction[0]==0xe9
    assert rva+5+struct.unpack('<i',instruction[1:])[0]==target
assert 0x29e67b<0x29ebbf<0x29ee5f
print('PASS: hash, four sites, complete spans, continuations, operand producers and separate recovery/drain routing')
