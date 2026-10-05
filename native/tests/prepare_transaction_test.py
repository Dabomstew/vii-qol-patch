"""Installer transaction and recovery contracts; never launch a game executable."""
import configparser
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
APP = ROOT / 'build/prepare-test/VII-Prepare-Game.exe'
BASELINE = Path(os.environ.get('VII_GAME_EXE', ''))
EXPECTED = '7ff2aad55e965add3b6fc45bd0ad2158769de014d6f619dc564a50c3e0fa42d9'
NAMES = ('dinput8.dll', 'vii-patches.ini', 'vii-prepare-state.ini')


def digest(p):
    return hashlib.sha256(p.read_bytes()).hexdigest()


def ini(p):
    raw = p.read_bytes()
    result = configparser.ConfigParser()
    result.read_string(raw.decode('utf-16' if raw.startswith(b'\xff\xfe') else 'utf-8'))
    return result


class Transactions(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        assert digest(BASELINE) == EXPECTED
        assert APP.is_file(), 'Build native/build-prepare-game.cmd --test'
        output = ROOT / 'build/tests'
        output.mkdir(parents=True, exist_ok=True)
        cls.evidence = Path(tempfile.mkdtemp(prefix='prepare-transactions-', dir=output))

    def setUp(self):
        self.work = Path(tempfile.mkdtemp(prefix=self._testMethodName + '-', dir=self.evidence))
        self.game = self.work / 'game-Ω'
        (self.game / 'CONTENTS').mkdir(parents=True)
        shutil.copyfile(BASELINE, self.game / 'NeptuniaVII.exe')

    def tearDown(self):
        self.assertEqual(digest(self.game / 'NeptuniaVII.exe'), EXPECTED)

    def command(self, *args, game=None):
        return [str(APP), '--source', str(game or self.game), *map(str, args)]

    def run_app(self, *args, success=True, failpoint=None, recovery=None, game=None):
        env = os.environ.copy()
        for key in ('VII_PREPARE_FAILPOINT', 'VII_PREPARE_RECOVERY_FAILPOINT'):
            env.pop(key, None)
        if failpoint:
            env['VII_PREPARE_FAILPOINT'] = failpoint
        if recovery:
            env['VII_PREPARE_RECOVERY_FAILPOINT'] = recovery
        result = subprocess.run(self.command(*args, game=game), env=env, capture_output=True,
                                text=True, encoding='utf-8', timeout=30)
        self.assertEqual(result.returncode == 0, success, result.stdout + result.stderr)
        return result.stdout

    def contents(self):
        return {name: (self.game / name).read_bytes() if (self.game / name).exists() else None
                for name in NAMES}

    def snapshot(self):
        pending = ini(self.game / 'vii-prepare-pending.ini')
        return self.game / 'vii-prepare-backups' / pending['Transaction']['Backup']

    def seed(self):
        self.run_app('--enable-only')
        (self.game / 'vii-patches.ini').write_text('[CustomSection]\nKeep=edited\n[Patches]\nAutoSkipEvents=1\n')

    def test_unknown_proxy_refused_before_preparation(self):
        dll = self.game / 'dinput8.dll'
        dll.write_bytes(b'another proxy')
        (self.game / 'vii-prepare-state.ini').write_text('[Install]\nProxySHA256=' + digest(dll))
        before = sorted(p.relative_to(self.game) for p in self.game.rglob('*'))
        out = self.run_app(success=False)
        self.assertIn('Unrecognized dinput8.dll', out)
        self.assertEqual(before, sorted(p.relative_to(self.game) for p in self.game.rglob('*')))

    def test_publication_failures_restore_exact_prior_files(self):
        for existing in (False, True):
            if existing:
                self.seed()
            for point in ('before-publish', 'after-proxy', 'after-config', 'after-state'):
                before = self.contents()
                self.run_app('--enable-only', success=False, failpoint=point)
                self.assertEqual(self.contents(), before)
                self.assertFalse((self.game / 'vii-prepare-pending.ini').exists())

    def test_crashes_are_detected_and_recover_exact_prior_files(self):
        for existing in (False, True):
            if existing:
                self.seed()
            for point in ('before-publish', 'after-proxy', 'after-config', 'after-state'):
                before = self.contents()
                self.run_app('--enable-only', success=False, failpoint='crash:' + point)
                self.assertTrue((self.game / 'vii-prepare-pending.ini').is_file())
                out = self.run_app('--enable-only', success=False)
                self.assertIn('interrupted update', out)
                self.run_app('--recover')
                self.assertEqual(self.contents(), before)
                self.assertFalse((self.game / 'vii-prepare-pending.ini').exists())

    def test_recovery_failure_keeps_evidence_and_reports_original_error(self):
        before = self.contents()
        out = self.run_app('--enable-only', success=False, failpoint='after-config', recovery='recover-0')
        self.assertIn('Injected failure: after-config', out)
        self.assertIn('Injected recovery failure: recover-0', out)
        self.assertTrue(self.snapshot().is_dir())
        self.assertIsNone(self.contents()['vii-patches.ini'])
        self.run_app('--recover')
        self.assertEqual(self.contents(), before)

    def test_recovery_can_itself_be_interrupted(self):
        self.seed()
        before = self.contents()
        self.run_app('--enable-only', success=False, failpoint='crash:after-state')
        self.run_app('--recover', success=False, failpoint='crash:recover-1')
        self.run_app('--recover')
        self.assertEqual(self.contents(), before)

    def test_completed_transaction_finishes_pending_cleanup(self):
        self.run_app('--enable-only', success=False, failpoint='crash:after-commit')
        after = self.contents()
        self.run_app('--recover')
        self.assertEqual(self.contents(), after)
        self.assertFalse((self.game / 'vii-prepare-pending.ini').exists())

    def test_changed_file_is_preserved_during_recovery(self):
        self.seed()
        self.run_app('--enable-only', success=False, failpoint='crash:after-config')
        config = self.game / 'vii-patches.ini'
        config.write_text('User changed this after the crash')
        out = self.run_app('--recover', success=False)
        self.assertIn('Changed file prevents recovery', out)
        self.assertEqual(config.read_text(), 'User changed this after the crash')
        self.assertTrue((self.game / 'vii-prepare-pending.ini').exists())

    def test_tampered_backup_and_manifest_refused(self):
        self.seed()
        self.run_app('--enable-only', success=False, failpoint='crash:after-config')
        snapshot = self.snapshot()
        before = self.contents()
        backup = snapshot / 'before-vii-patches.ini'
        data = backup.read_bytes()
        backup.write_bytes(b'changed backup')
        self.assertIn('backup changed', self.run_app('--recover', success=False))
        self.assertEqual(self.contents(), before)
        backup.write_bytes(data)
        with (snapshot / 'snapshot.ini').open('ab') as stream:
            stream.write(b'\n\x00')
        self.assertIn('manifest changed', self.run_app('--recover', success=False))
        self.assertEqual(self.contents(), before)

    def test_concurrent_preparer_is_refused(self):
        env = dict(os.environ, VII_PREPARE_FAILPOINT='pause:after-lock')
        process = subprocess.Popen(self.command('--enable-only'), env=env,
                                   stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
        try:
            self.assertEqual(process.stdout.readline().strip(), 'test-paused')
            self.assertIn('Another preparer', self.run_app('--enable-only', success=False))
            out, err = process.communicate(timeout=15)
            self.assertEqual(process.returncode, 0, out + err)
        finally:
            if process.poll() is None:
                process.kill()
                process.wait()

    def test_edit_during_staging_is_preserved(self):
        self.seed()
        env = dict(os.environ, VII_PREPARE_FAILPOINT='pause:before-publish')
        process = subprocess.Popen(self.command('--enable-only'), env=env,
                                   stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
        try:
            self.assertEqual(process.stdout.readline().strip(), 'test-paused')
            (self.game / 'vii-patches.ini').write_text('Concurrent user edit')
            out, err = process.communicate(timeout=15)
            self.assertNotEqual(process.returncode, 0, out + err)
            self.assertEqual((self.game / 'vii-patches.ini').read_text(), 'Concurrent user edit')
            self.assertIn('Recovery requires attention', out)
        finally:
            if process.poll() is None:
                process.kill()
                process.wait()

    def junction(self, path, target):
        result = subprocess.run(['cmd', '/c', 'mklink', '/J', str(path), str(target)], capture_output=True)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_linked_parent_allowed_but_linked_backup_refused(self):
        alias = self.evidence / (self.work.name + '-alias')
        self.junction(alias, self.work)
        try:
            self.run_app('--enable-only', game=alias / self.game.name)
        finally:
            alias.rmdir()  # Removes only this junction, never recursively visits its target.
        other = self.work / 'foreign-backups'
        other.mkdir()
        backups = self.game / 'vii-prepare-backups'
        backups.rename(self.game / 'saved-backups')
        self.junction(backups, other)
        before = self.contents()
        try:
            self.assertIn('Linked installer path', self.run_app('--enable-only', success=False))
            self.assertEqual(self.contents(), before)
            self.assertEqual(list(other.iterdir()), [])
        finally:
            backups.rmdir()

    def test_source_overlap_and_semicolon_paths_refused(self):
        for output in (self.game, self.game / 'CONTENTS/cache', self.game / 'DLC/cache', self.work):
            self.assertIn('overlaps', self.run_app('--enable-only', '--output', output, success=False))
        renamed = self.work / 'game;test'
        self.game.rename(renamed)
        self.game = renamed
        self.assertIn('semicolon', self.run_app('--enable-only', success=False))

    def test_output_alias_cannot_hide_source_overlap(self):
        alias = self.work / 'source-alias'
        self.junction(alias, self.game / 'CONTENTS')
        try:
            self.assertIn('overlaps', self.run_app('--enable-only', '--output', alias / 'cache', success=False))
            self.assertFalse((self.game / 'CONTENTS/cache').exists())
        finally:
            alias.rmdir()

    def test_long_game_path_install_and_recovery(self):
        long_parent = self.work / ('a' * 70) / ('b' * 70)
        long_parent.mkdir(parents=True)
        destination = long_parent / self.game.name
        self.game.rename(destination)
        self.game = destination
        self.assertGreater(len(str(self.game)), 260)
        self.seed()
        before = self.contents()
        self.run_app('--enable-only', success=False, failpoint='crash:after-state')
        self.run_app('--recover')
        self.assertEqual(self.contents(), before)


if __name__ == '__main__':
    suite = unittest.defaultTestLoader.loadTestsFromTestCase(Transactions)
    result = unittest.TextTestRunner(verbosity=2).run(suite)
    report = {'tests_run': result.testsRun, 'failures': len(result.failures), 'errors': len(result.errors),
              'passed': result.wasSuccessful(), 'game_launched': False}
    (Transactions.evidence / 'result.json').write_text(json.dumps(report, indent=2))
    print('Evidence:', Transactions.evidence)
    raise SystemExit(not result.wasSuccessful())
