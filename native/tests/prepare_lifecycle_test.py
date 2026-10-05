"""Completed-update rollback and uninstall, using isolated copies and real retained DLLs."""
import json
import os
from pathlib import Path
import unittest

from prepare_transaction_test import Transactions, ini, digest


class Lifecycle(unittest.TestCase):
    setUpClass = classmethod(Transactions.setUpClass.__func__)
    setUp = Transactions.setUp
    tearDown = Transactions.tearDown
    command = Transactions.command
    run_app = Transactions.run_app
    contents = Transactions.contents
    snapshot = Transactions.snapshot
    seed = Transactions.seed
    junction = Transactions.junction

    def data_sentinels(self):
        paths = [self.game / name for name in ('vii-speedrun-patch/cache/keep.vmt',
                 'vii-speedrun-patch/unpacked/keep', 'vii-speedrun-patch/unpacked.dlc/keep',
                 'vii-texture-logs/keep.log')]
        for p in paths:
            p.parent.mkdir(parents=True, exist_ok=True)
            p.write_bytes(b'preserve prepared data')
        return {p: p.read_bytes() for p in paths}

    def test_fresh_rollback_retains_settings_and_data(self):
        self.run_app('--enable-only')
        config = (self.game / 'vii-patches.ini').read_bytes()
        sentinels = self.data_sentinels()
        self.assertIn('current settings retained', self.run_app('--rollback'))
        self.assertFalse((self.game / 'dinput8.dll').exists())
        self.assertEqual((self.game / 'vii-patches.ini').read_bytes(), config)
        self.assertEqual(ini(self.game / 'vii-prepare-state.ini')['Install']['Active'], '0')
        for p, data in sentinels.items():
            self.assertEqual(p.read_bytes(), data)

    def test_update_rollback_restores_unchanged_settings(self):
        self.seed()
        config = (self.game / 'vii-patches.ini').read_bytes()
        self.run_app('--enable-only')
        self.assertNotEqual((self.game / 'vii-patches.ini').read_bytes(), config)
        self.assertIn('previous settings restored', self.run_app('--rollback'))
        self.assertEqual((self.game / 'vii-patches.ini').read_bytes(), config)
        # The earlier fresh-install snapshot is still available.
        self.run_app('--rollback')
        self.assertFalse((self.game / 'dinput8.dll').exists())

    def test_rollback_preserves_user_edits_and_missing_config(self):
        self.seed()
        self.run_app('--enable-only')
        config = self.game / 'vii-patches.ini'
        config.write_bytes(b'[CustomSection]\nEdited=after-update\n')
        self.assertIn('current settings retained', self.run_app('--rollback'))
        self.assertEqual(config.read_bytes(), b'[CustomSection]\nEdited=after-update\n')
        self.run_app('--enable-only')
        config.unlink()
        self.run_app('--rollback')
        self.assertFalse(config.exists())

    def test_uninstall_without_state_preserves_configuration_and_data(self):
        for stale in (False, True):
            self.run_app('--enable-only')
            state = self.game / 'vii-prepare-state.ini'
            state.unlink()
            if stale:
                state.write_text('[Install]\nProxySHA256=stale\nBackup=unknown\n')
            config = (self.game / 'vii-patches.ini').read_bytes()
            sentinels = self.data_sentinels()
            self.run_app('--uninstall')
            self.assertFalse((self.game / 'dinput8.dll').exists())
            self.assertEqual((self.game / 'vii-patches.ini').read_bytes(), config)
            parsed = ini(state)
            self.assertEqual(parsed['Install']['Active'], '0')
            self.assertNotIn('proxysha256', parsed['Install'])
            for p, data in sentinels.items():
                self.assertEqual(p.read_bytes(), data)

    def test_unknown_proxy_and_legacy_state_are_refused(self):
        self.run_app('--enable-only')
        state = self.game / 'vii-prepare-state.ini'
        original = state.read_bytes()
        state.write_text('[Install]\nBackup=legacy\n')
        self.assertIn('legacy backups', self.run_app('--rollback', success=False))
        state.write_bytes(original)
        (self.game / 'dinput8.dll').write_bytes(b'another mod')
        before = self.contents()
        for action in ('--rollback', '--uninstall'):
            self.assertIn('Unrecognized', self.run_app(action, success=False))
            self.assertEqual(self.contents(), before)

    def test_damaged_backup_and_wrong_installation_refused(self):
        self.seed()
        self.run_app('--enable-only')
        state = self.game / 'vii-prepare-state.ini'
        backup = Path(ini(state)['Install']['Backup'])
        prior = backup / 'before-dinput8.dll'
        saved = prior.read_bytes()
        prior.write_bytes(b'changed DLL backup')
        before = self.contents()
        self.assertIn('backup changed', self.run_app('--rollback', success=False))
        self.assertEqual(self.contents(), before)
        prior.write_bytes(saved)
        state.write_text('[Install]\nVersion=2\nActive=1\nBackup=' + str(self.work / 'other-game/backup'))
        before = self.contents()
        self.run_app('--rollback', success=False)
        self.assertEqual(self.contents(), before)

    def test_interrupted_rollback_and_uninstall_recover(self):
        self.run_app('--enable-only')
        for action in ('--rollback', '--uninstall'):
            for point in ('after-proxy', 'after-config', 'after-state'):
                before = self.contents()
                self.run_app(action, success=False, failpoint='crash:' + point)
                self.run_app('--recover')
                self.assertEqual(self.contents(), before)

    def test_linked_parent_snapshot_works_through_physical_path(self):
        alias = self.evidence / (self.work.name + '-alias')
        self.junction(alias, self.work)
        try:
            self.run_app('--enable-only', game=alias / self.game.name)
        finally:
            alias.rmdir()
        self.run_app('--rollback')
        self.assertFalse((self.game / 'dinput8.dll').exists())

    def test_available_historical_dlls_update_and_rollback(self):
        path = os.environ.get('VII_HISTORICAL_INVENTORY')
        if not path:
            self.skipTest('Set VII_HISTORICAL_INVENTORY for retained historical bytes')
        count = 0
        for item in json.loads(Path(path).read_text()):
            old = next((Path(p) for p in item['files'] if Path(p).is_file() and digest(Path(p)) == item['sha256']), None)
            if old is None:
                continue
            original = old.read_bytes()
            (self.game / 'dinput8.dll').write_bytes(original)
            (self.game / 'vii-patches.ini').write_bytes(b'[CustomSection]\nKeep=manual-install\n')
            (self.game / 'vii-prepare-state.ini').write_text('[Install]\nProxySHA256=stale\n')
            self.run_app('--enable-only')
            self.run_app('--rollback')
            self.assertEqual((self.game / 'dinput8.dll').read_bytes(), original)
            self.assertEqual((self.game / 'vii-patches.ini').read_bytes(), b'[CustomSection]\nKeep=manual-install\n')
            self.assertNotIn('proxysha256', ini(self.game / 'vii-prepare-state.ini')['Install'])
            # A known manual replacement after an update still invalidates that snapshot.
            self.run_app('--enable-only')
            (self.game / 'dinput8.dll').write_bytes(original)
            before = self.contents()
            self.assertIn('DLL changed', self.run_app('--rollback', success=False))
            self.assertEqual(self.contents(), before)
            count += 1
        self.assertGreater(count, 0)
        (self.work / 'historical-count.json').write_text(json.dumps({'builds': count}))


if __name__ == '__main__':
    result = unittest.TextTestRunner(verbosity=2).run(unittest.defaultTestLoader.loadTestsFromTestCase(Lifecycle))
    (Lifecycle.evidence / 'result.json').write_text(json.dumps({'tests_run': result.testsRun,
        'passed': result.wasSuccessful(), 'skipped': len(result.skipped), 'game_launched': False}, indent=2))
    print('Evidence:', Lifecycle.evidence)
    raise SystemExit(not result.wasSuccessful())
