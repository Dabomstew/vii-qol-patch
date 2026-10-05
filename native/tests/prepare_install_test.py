"""Exercise Prepare Game installation behavior without launching the game."""
import hashlib
import configparser
import json
import os
import shutil
import subprocess
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
TOOL = ROOT / 'build/prepare-build/VII-Prepare-Game.exe'
BASELINE = Path(os.environ.get('VII_GAME_EXE', ''))
EXPECTED = '7ff2aad55e965add3b6fc45bd0ad2158769de014d6f619dc564a50c3e0fa42d9'


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def run(root, *args, success=True):
    completed = subprocess.run([TOOL, *args], cwd=root, text=True,
                               capture_output=True, timeout=120)
    print(completed.stdout, end='')
    assert (completed.returncode == 0) == success, completed.stderr
    return completed


def main():
    assert BASELINE.is_file(), 'Set VII_GAME_EXE to the supported NeptuniaVII.exe for this fixture'
    assert sha(BASELINE) == EXPECTED
    assert TOOL.is_file(), 'Build VII Prepare Game before running this fixture'
    reports = ROOT / 'build/tests'
    reports.mkdir(parents=True, exist_ok=True)
    root = Path(tempfile.mkdtemp(prefix='prepare-install-', dir=reports))
    game = root / 'game-Ω'
    (game / 'CONTENTS').mkdir(parents=True)
    shutil.copyfile(BASELINE, game / 'NeptuniaVII.exe')

    # Fresh installation: defaults are game-relative, diagnostics/gameplay are explicit.
    run(root, '--source', game / 'contents', '--enable-only')
    config = game / 'vii-patches.ini'
    fresh = config.read_text(encoding='utf-16')
    for setting in ('MipCache=1', 'MotionCache=1', 'LooseFiles=1',
                    'AutoSkipEvents=0', 'EventSkipBuffer=0',
                    'SuppressTutorials=0', 'BattleAutoSkip=0',
                    'NewGameDetector=1', 'LoadTiming=0', 'Neptasm=0', 'FPSUnlock=0', 'CameraUnlock=0',
                    'Resolution=0', 'ResolutionScale=1.0', 'FitWindow=0',
                    'WindowControl=0', 'WindowWidth=1920',
                    'WindowHeight=1080', 'Ultrawide=0',
                    'Trace=0', 'CaptureSources=0', 'DiskMode=2',
                    'Directory=vii-speedrun-patch\\unpacked',
                    'DiskDirectory=vii-speedrun-patch\\cache'):
        assert setting in fresh, setting
    assert sha(game / 'dinput8.dll') == sha(ROOT / 'build/native-build/dinput8.dll')
    parsed = configparser.ConfigParser()
    parsed.read_string(fresh)
    assert parsed.getint('LoadTiming', 'FPSUnlock') == 0
    assert parsed.getint('LoadTiming', 'Trace') == 0
    assert sha(game / 'NeptuniaVII.exe') == EXPECTED

    # Relative defaults remain effective when the complete game folder moves.
    moved = root / 'moved-game-Ω'
    game.rename(moved)
    game = moved
    config = game / 'vii-patches.ini'
    run(root, '--source', game, '--enable-only')
    moved_config = config.read_text(encoding='utf-16')
    assert 'Directory=vii-speedrun-patch\\unpacked' in moved_config
    assert 'DiskDirectory=vii-speedrun-patch\\cache' in moved_config
    assert 'Cache=vii-speedrun-patch\\cache' in (game / 'vii-prepare-state.ini').read_text(encoding='utf-16')

    # An explicit external path, enabled gameplay toggle and diagnostics survive reinstall.
    custom = (game / 'custom-cache-Ω').resolve()
    config.write_text('[Patches]\nAutoSkipEvents=1\nBattleAutoSkip=1\nLoadTiming=1\n'
                      '[LoadTiming]\nFPSUnlock=1\nTrace=1\n'
                      '[MipCache]\nVerify=1\nTrace=1\nCaptureSources=1\n'
                      f'DiskDirectory={custom}\n[LooseFiles]\nDirectory=kept-relative\n',
                      encoding='utf-16')
    run(root, '--source', game, '--enable-only')
    preserved = config.read_text(encoding='utf-16')
    parsed.read_string(preserved)
    assert parsed.getint('Patches', 'LoadTiming') == 1
    assert parsed.getint('LoadTiming', 'FPSUnlock') == 1
    assert parsed.getint('LoadTiming', 'Trace') == 1
    for setting in (f'DiskDirectory={custom}', 'Directory=kept-relative',
                    'AutoSkipEvents=1', 'BattleAutoSkip=1', 'Verify=1',
                    'Trace=1', 'CaptureSources=1', 'EventSkipBuffer=0',
                    'SuppressTutorials=0'):
        assert setting in preserved, setting
    state = (game / 'vii-prepare-state.ini').read_text(encoding='utf-16')
    assert f'Cache={custom}' in state
    run(root, '--source', game, '--enable-only')
    assert f'DiskDirectory={custom}' in config.read_text(encoding='utf-16')

    # A selected replacement output supersedes an older explicit path.
    selected = (root / 'selected-cache').resolve()
    run(root, '--source', game, '--output', selected, '--enable-only')
    changed = config.read_text(encoding='utf-16')
    assert f'DiskDirectory={selected}' in changed
    assert f'Cache={selected}' in (game / 'vii-prepare-state.ini').read_text(encoding='utf-16')

    # A conflicting proxy leaves existing configuration untouched.
    (game / 'dinput8.dll').write_bytes(b'unknown proxy fixture')
    before = config.read_bytes()
    run(root, '--source', game, '--enable-only', success=False)
    assert config.read_bytes() == before
    assert (game / 'dinput8.dll').read_bytes() == b'unknown proxy fixture'
    assert sha(game / 'NeptuniaVII.exe') == EXPECTED
    result = {
        'arbitrary_cwd': True, 'unicode_game_path': True,
        'fresh_game_local_defaults': True, 'relative_defaults_survive_move': True,
        'diagnostics_default_off': True,
        'custom_absolute_and_toggles_preserved': True,
        'selected_output_replaces_old_location': True,
        'unknown_proxy_refused': True, 'baseline_preserved': True,
    }
    (root / 'result.json').write_text(json.dumps(result, indent=2) + '\n')
    print(json.dumps(result))


if __name__ == '__main__':
    main()
