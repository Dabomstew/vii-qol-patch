"""Check update ownership without running a game. Optional historical inventory is local-only."""
import ctypes
from ctypes import wintypes
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import sys

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'native'))
from package_local import embedded_resource, proxy_hashes


def sha(p):
    return hashlib.sha256(p.read_bytes()).hexdigest()


def replace_registry(app, data):
    kernel = ctypes.WinDLL('kernel32', use_last_error=True)
    kernel.BeginUpdateResourceW.argtypes = [wintypes.LPCWSTR, wintypes.BOOL]
    kernel.BeginUpdateResourceW.restype = wintypes.HANDLE
    kernel.UpdateResourceW.argtypes = [wintypes.HANDLE, ctypes.c_void_p, ctypes.c_void_p,
                                      wintypes.WORD, ctypes.c_void_p, wintypes.DWORD]
    kernel.EndUpdateResourceW.argtypes = [wintypes.HANDLE, wintypes.BOOL]
    handle = kernel.BeginUpdateResourceW(str(app), False)
    assert handle
    block = ctypes.create_string_buffer(data)
    # RC defaults to US English in the build environment.
    ok = kernel.UpdateResourceW(handle, ctypes.c_void_p(10), ctypes.c_void_p(102),
                                1033, block, len(data))
    assert kernel.EndUpdateResourceW(handle, not ok) and ok


def main():
    baseline = Path(os.environ['VII_GAME_EXE'])
    expected = '7ff2aad55e965add3b6fc45bd0ad2158769de014d6f619dc564a50c3e0fa42d9'
    assert sha(baseline) == expected
    app = ROOT / 'build/prepare-build/VII-Prepare-Game.exe'
    bundled = embedded_resource(app, 101)
    registry = (ROOT / 'build/generated/proxy-hashes.txt').read_bytes()
    assert embedded_resource(app, 102) == registry
    known = proxy_hashes(registry)
    assert hashlib.sha256(bundled).hexdigest() in known
    reports = ROOT / 'build/tests'
    reports.mkdir(parents=True, exist_ok=True)
    work = Path(tempfile.mkdtemp(prefix='prepare-registry-', dir=reports))
    game = work / 'game'
    (game / 'CONTENTS').mkdir(parents=True)
    shutil.copyfile(baseline, game / 'NeptuniaVII.exe')
    dll, state, config = [game / p for p in ('dinput8.dll', 'vii-prepare-state.ini', 'vii-patches.ini')]
    config.write_text('[CustomSection]\nKeep=this\n', encoding='ascii')

    def run(success=True, tool=app):
        result = subprocess.run([tool, '--source', game, '--enable-only'], capture_output=True,
                                text=True, timeout=30)
        assert (result.returncode == 0) == success, result.stdout + result.stderr
        return result

    history = []
    if os.environ.get('VII_HISTORICAL_INVENTORY'):
        history = json.loads(Path(os.environ['VII_HISTORICAL_INVENTORY']).read_text())
    exercised = []
    for item in history:
        candidates = [Path(p) for p in item['files']]
        old = next((p for p in candidates if p.is_file() and sha(p) == item['sha256']), None)
        if old is None:
            continue
        assert item['sha256'] in known
        for stale in (False, True):
            shutil.copyfile(old, dll)
            state.unlink(missing_ok=True)
            if stale:
                state.write_text('[Install]\nProxySHA256=' + '0' * 64 + '\n')
            run()
            assert dll.read_bytes() == bundled
            state_bytes = state.read_bytes()
            assert 'ProxySHA256' not in state_bytes.decode('utf-16' if state_bytes.startswith(b'\xff\xfe') else 'utf-8')
            assert 'Keep=this' in config.read_text()
        exercised.append(item['sha256'])
    dll.write_bytes(bundled)
    state.unlink(missing_ok=True)
    run()
    for unknown in (b'other mod', bundled[:-1] + bytes([bundled[-1] ^ 1])):
        dll.write_bytes(unknown)
        state.write_text('[Install]\nProxySHA256=' + sha(dll) + '\n')
        before = {p: p.read_bytes() for p in (dll, config, state)}
        run(False)
        assert all(p.read_bytes() == data for p, data in before.items())
    dll.write_bytes(bundled)
    for i, bad in enumerate((b'bad registry\n', b'0' * 64 + b' | not the bundled DLL\n')):
        modified = work / f'invalid-registry-{i}.exe'
        shutil.copyfile(app, modified)
        replace_registry(modified, bad)
        assert embedded_resource(modified, 102) == bad
        before = {p: p.read_bytes() for p in (dll, config, state)}
        run(False, modified)
        assert all(p.read_bytes() == data for p, data in before.items())
    assert sha(game / 'NeptuniaVII.exe') == expected
    result = {'historical_hashes_exercised': exercised, 'missing_and_stale_state': True,
              'unknown_even_with_matching_state_refused': True, 'modified_dll_refused': True,
              'invalid_or_incomplete_registry_refused': True, 'baseline_unchanged': True}
    (work / 'result.json').write_text(json.dumps(result, indent=2) + '\n')
    print(json.dumps({'evidence': str(work), 'historical_builds': len(exercised), 'passed': True}))


if __name__ == '__main__':
    main()
