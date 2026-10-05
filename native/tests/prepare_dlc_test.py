"""Exercise optional DLC preparation end to end in an isolated game fixture."""
import hashlib
import json
import os
import shutil
import struct
import subprocess
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
TOOL = ROOT / 'build/prepare-build/VII-Prepare-Game.exe'
EXE = Path(os.environ.get('VII_GAME_EXE', ''))
BASELINE = '7ff2aad55e965add3b6fc45bd0ad2158769de014d6f619dc564a50c3e0fa42d9'


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def tid(colour):
    data = bytearray(128+64)
    data[:4] = b'TID\x90'
    for offset, value in {4: len(data), 8: 128, 12: 1, 20: 32,
                          0x44: 4, 0x48: 4, 0x4c: 32,
                          0x58: 64, 0x5c: 128}.items():
        struct.pack_into('<I', data, offset, value)
    struct.pack_into('<H', data, 0x52, 1)
    data[128:] = bytes(colour)*16
    return data


def pac(path, data):
    archive = bytearray(20+288)
    archive[:8] = b'DW_PACK\0'
    struct.pack_into('<I', archive, 12, 1)
    name = b'texture.tid'
    archive[28:28+len(name)] = name
    struct.pack_into('<II', archive, 20+272, len(data), len(data))
    path.write_bytes(archive+data)


def run(game, cache, success=True):
    result = subprocess.run([TOOL, '--source', game, '--output', cache],
                            text=True, capture_output=True, timeout=120)
    print(result.stdout, end='')
    assert (result.returncode == 0) == success, (result.returncode, result.stdout, result.stderr)
    return json.loads(result.stdout.splitlines()[-1])


def main():
    assert EXE.is_file(), 'Set VII_GAME_EXE to the supported NeptuniaVII.exe for this fixture'
    assert sha(EXE) == BASELINE
    reports = ROOT / 'build/tests'
    reports.mkdir(parents=True, exist_ok=True)
    root = Path(tempfile.mkdtemp(prefix='prepare-dlc-', dir=reports))
    game, assets, cache = root/'game', root/'loose', root/'cache'
    (game/'CONTENTS').mkdir(parents=True)
    shutil.copyfile(EXE, game/'NeptuniaVII.exe')
    (game/'vii-patches.ini').write_text('[LooseFiles]\nDirectory='+str(assets)+'\n')
    pac(game/'CONTENTS/GAME00000.pac', tid((255, 0, 0, 255)))
    first = run(game, cache)
    assert first['generated'] == 1 and first['unsupported'] == 0
    assert not Path(str(assets)+'.dlc').exists()
    assert run(game, cache)['generated'] == 0
    (game/'DLC').mkdir()
    pac(game/'DLC/GAME00000.pac', tid((0, 255, 0, 255)))
    added = run(game, cache)
    assert added['generated'] == 1 and added['reused'] == 1
    assert (Path(str(assets)+'.dlc')/'game/texture.tid').exists()
    assert (assets/'game/texture.tid').read_bytes() == tid((255, 0, 0, 255))
    both = run(game, cache)
    assert both['generated'] == 0 and both['reused'] == 2
    (game/'DLC').rename(game/'DLC-disabled')
    removed = run(game, cache)
    assert removed['generated'] == 0 and removed['reused'] == 1
    (game/'DLC-disabled').rename(game/'DLC')
    assert run(game, cache)['reused'] == 2
    # Bad DLC must stop before publishing the loader/configuration, while
    # leaving completed base extraction and unrelated fixture files intact.
    before = {name: sha(game/name) for name in ('dinput8.dll', 'vii-patches.ini', 'vii-prepare-state.ini')}
    (game/'DLC/GAME00000.pac').write_bytes(b'bad archive')
    assert 'error' in run(game, cache, success=False)
    assert before == {name: sha(game/name) for name in before}
    assert sha(game/'NeptuniaVII.exe') == BASELINE
    result = {'fixture': str(root), 'absent_dlc_succeeds': True, 'automatic_dlc_discovery': True,
              'base_and_dlc_prepared': True, 'resume_reuses_both': True,
              'removed_dlc_not_scanned': True, 'reappearing_dlc_reused': True,
              'same_namespace_extractions_separate': True, 'bad_dlc_preserves_installation': True}
    (root/'result.json').write_text(json.dumps(result, indent=2)+'\n')
    print(json.dumps(result))


if __name__ == '__main__':
    main()
