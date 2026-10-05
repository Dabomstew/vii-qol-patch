"""Settings contracts on copied baselines; never launch a game."""
import json
import os
from pathlib import Path
import re
import shutil
import tempfile
import unittest
from prepare_transaction_test import Transactions, ROOT, ini


class Settings(Transactions):
    def query(self):
        return json.loads(self.run_app('--settings'))

    def backups(self):
        return sorted(p.name for p in (self.game / 'vii-prepare-backups').glob('*'))

    def test_selection_refuses_unsupported_executable_without_writes(self):
        exe = self.game / 'NeptuniaVII.exe'
        baseline = exe.read_bytes()
        try:
            exe.write_bytes(b'unsupported executable')
            before = {p.relative_to(self.game): p.read_bytes()
                      for p in self.game.rglob('*') if p.is_file()}
            self.assertIn('not the supported', self.run_app('--settings', success=False))
            self.assertEqual({p.relative_to(self.game): p.read_bytes()
                              for p in self.game.rglob('*') if p.is_file()}, before)
        finally:
            exe.write_bytes(baseline)

    def test_template_and_runtime_defaults(self):
        entries = re.findall(r'\{L"(\w+)", L"[^"]+", (true|false), (true|false)\}',
                             (ROOT / 'native/include/prepare_settings.hpp').read_text())
        self.assertEqual(len(entries), 7)
        template = ini(ROOT / 'native/vii-patches.ini')
        runtime = (ROOT / 'native/src/core.cpp').read_text()
        self.run_app('--install')
        actual = ini(self.game / 'vii-patches.ini')
        for section in template:
            for key, value in template[section].items():
                self.assertEqual(actual[section][key], value)
        for key, default, fallback in entries:
            self.assertEqual(template.getboolean('Patches', key), default == 'true')
            match = re.search(r'\{L"' + key + r'",\w+(?:,([01]))?\}', runtime)
            self.assertIsNotNone(match)
            self.assertEqual(match[1] != '0', fallback == 'true')
        self.assertEqual(self.query()['features'], {k: d == 'true' for k, d, _ in entries})

    def test_roundtrip_preserves_explicit_and_unknown_values(self):
        config = self.game / 'vii-patches.ini'
        config.write_text('; Personal notes Ω\n[Patches]\nMipCache=0\nMotionCache=0\nLooseFiles=0\nAutoSkipEvents=0\nMystery=7\n[MipCache]\nBudgetMiB=33\n[CustomSection]\nKeep=yes\n', encoding='utf-16')
        self.run_app('--install', '--set', 'EventSkipBuffer=1', '--set', 'BattleAutoSkip=1')
        parsed = ini(config)
        for key in ('MipCache', 'MotionCache', 'LooseFiles', 'AutoSkipEvents'):
            self.assertFalse(parsed.getboolean('Patches', key))
        for section, key, value in [('Patches', 'Mystery', '7'), ('MipCache', 'BudgetMiB', '33'), ('CustomSection', 'Keep', 'yes')]:
            self.assertEqual(parsed[section][key], value)
        self.assertIn('; Personal notes Ω', config.read_text(encoding='utf-16'))
        self.assertTrue(self.query()['features']['EventSkipBuffer'])
        self.assertTrue(self.query()['features']['BattleAutoSkip'])
        self.run_app('--install', '--set', 'BattleAutoSkip=0')
        self.assertFalse(self.query()['features']['BattleAutoSkip'])

    def test_noop_keeps_last_backup_and_bytes(self):
        self.run_app('--install')
        before, snapshots = self.contents(), self.backups()
        self.assertIn('already up to date', self.run_app('--install'))
        self.assertEqual(self.contents(), before)
        self.assertEqual(self.backups(), snapshots)
        self.assertEqual(list(self.game.glob('.vii-settings-*')), [])
        self.run_app('--rollback')
        self.assertFalse((self.game / 'dinput8.dll').exists())

    def test_install_never_extracts_or_scans(self):
        (self.game / 'CONTENTS/bad.pac').write_bytes(b'invalid archive must not be read')
        assets = self.work / 'unpacked'
        assets.mkdir()
        (assets / 'invalid.dds').write_bytes(b'invalid texture must not be read')
        self.run_app('--install', '--assets', assets)
        self.assertEqual(list(assets.iterdir()), [assets / 'invalid.dds'])
        self.assertEqual(list((self.game / 'vii-speedrun-patch/cache').iterdir()), [])

    def test_stale_choices_refused_without_changes(self):
        self.run_app('--install')
        stale = self.query()['config_sha256']
        with (self.game / 'vii-patches.ini').open('a', encoding='utf-16') as f:
            f.write('\n[CustomSection]\nKeep=changed\n')
        before, snapshots = self.contents(), self.backups()
        self.assertIn('Settings changed', self.run_app('--install', '--if-config-hash', stale, '--set', 'AutoSkipEvents=1', success=False))
        self.assertEqual(self.contents(), before)
        self.assertEqual(self.backups(), snapshots)

    def test_paths_external_unicode_and_moved_game(self):
        cache, assets = self.work / 'cache-Ω', self.work / 'assets-Ω'
        self.run_app('--install', '--output', cache, '--assets', assets)
        self.assertEqual(Path(self.query()['cache']), cache)
        self.assertEqual(Path(self.query()['assets']), assets)
        config = self.game / 'vii-patches.ini'
        text = config.read_text(encoding='utf-16').replace(str(cache), str(cache) + '\\.')
        config.write_text(text, encoding='utf-16')
        self.run_app('--install', '--set', 'AutoSkipEvents=1')
        self.assertIn(str(cache) + '\\.', config.read_text(encoding='utf-16'))
        moved = self.work / 'moved-game-Ω'
        self.game.rename(moved)
        self.game = moved
        self.run_app('--install')
        self.assertEqual(Path(self.query()['cache']), cache)

    def test_another_drive_output(self):
        destination = os.environ.get('VII_SETTINGS_EXTERNAL_ROOT')
        if not destination:
            self.skipTest('Set VII_SETTINGS_EXTERNAL_ROOT to a writable folder on another drive')
        external = Path(tempfile.mkdtemp(prefix='vii-settings-', dir=destination))
        self.assertNotEqual(external.drive.lower(), self.game.drive.lower())
        cache, assets = external / 'cache-Ω', external / 'assets-Ω'
        self.run_app('--install', '--output', cache, '--assets', assets)
        self.assertEqual(Path(self.query()['cache']), cache)
        self.assertEqual(Path(self.query()['assets']), assets)
        (self.work / 'external-evidence.json').write_text(json.dumps({'path': str(external)}))

    def test_overlapping_or_source_outputs_refused(self):
        for cache, assets in ((self.work / 'same', self.work / 'same'), (self.work / 'tree', self.work / 'tree/nested'),
                              (self.game / 'CONTENTS/cache', self.work / 'assets'), (self.work / 'cache', self.game / 'DLC/assets')):
            before = self.contents()
            self.run_app('--install', '--output', cache, '--assets', assets, success=False)
            self.assertEqual(self.contents(), before)
            self.assertEqual(self.backups(), [])

    def test_cli_bad_combinations_refused(self):
        for args in (('--settings', '--set', 'AutoSkipEvents=1'), ('--install', '--rollback'),
                     ('--install', '--set', 'Unknown=1'), ('--install', '--set', 'MipCache=2'),
                     ('--install', '--set', 'MipCache=0', '--set', 'MipCache=1')):
            self.run_app(*args, success=False)
            self.assertEqual(self.contents(), dict.fromkeys(self.contents()))

    def test_unsupported_bom_refused_without_loss(self):
        config = self.game / 'vii-patches.ini'
        for raw in (b'\xef\xbb\xbf[Patches]\nAutoSkipEvents=1\n', b'\xfe\xff\x00['):
            config.write_bytes(raw)
            before = self.contents()
            self.assertIn('Unsupported INI encoding', self.run_app('--install', success=False))
            self.assertEqual(self.contents(), before)
            self.assertEqual(self.backups(), [])

    def test_ansi_and_empty_profiles(self):
        game = self.work / 'ascii-game'
        shutil.copytree(self.game, game)
        self.game = game
        config = self.game / 'vii-patches.ini'
        config.write_bytes(b'; custom comment\r\n[Patches]\r\nMipCache=0\r\n')
        self.run_app('--install')
        self.assertFalse(config.read_bytes().startswith(b'\xff\xfe'))
        self.assertIn(b'; custom comment', config.read_bytes())
        self.assertFalse(self.query()['features']['MipCache'])
        config.write_bytes(b'')
        self.run_app('--install')
        self.assertTrue(self.query()['features']['MipCache'])


if __name__ == '__main__':
    suite = unittest.TestSuite(Settings(name) for name in Settings.__dict__ if name.startswith('test_'))
    result = unittest.TextTestRunner(verbosity=2).run(suite)
    (Settings.evidence / 'settings-result.json').write_text(json.dumps({'tests_run': result.testsRun,
        'passed': result.wasSuccessful(), 'skipped': len(result.skipped), 'game_launched': False}, indent=2))
    print('Evidence:', Settings.evidence)
    raise SystemExit(not result.wasSuccessful())
