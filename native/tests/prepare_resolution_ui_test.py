"""Exercise the actual dropdown and save workflow on copied game baselines."""
import ctypes
from ctypes import wintypes
import json
import subprocess
import time
import unittest
from prepare_transaction_test import Transactions, ini

user = ctypes.WinDLL('user32', use_last_error=True)
user.GetDlgItem.argtypes = [wintypes.HWND, ctypes.c_int]
user.GetDlgItem.restype = wintypes.HWND
user.SendMessageW.argtypes = [wintypes.HWND, wintypes.UINT, wintypes.WPARAM, wintypes.LPARAM]
user.SendMessageW.restype = ctypes.c_ssize_t
user.PostMessageW.argtypes = user.SendMessageW.argtypes
user.GetWindowThreadProcessId.argtypes = [wintypes.HWND, ctypes.POINTER(wintypes.DWORD)]
callback_type = ctypes.WINFUNCTYPE(wintypes.BOOL, wintypes.HWND, wintypes.LPARAM)
user.EnumWindows.argtypes = [callback_type, wintypes.LPARAM]
user.IsWindowEnabled.argtypes = [wintypes.HWND]


def wait_for(predicate, timeout=20):
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        value = predicate()
        if value:
            return value
        time.sleep(0.05)
    raise AssertionError('Timed out waiting for preparer UI')


class ResolutionUI(Transactions):
    def test_battle_loading_checkbox_apply_and_reopen(self):
        self.run_app('--install', '--set', 'SkipBattleLoadingPresent=0')
        for expected, target in ((0, 1), (1, 0), (0, None)):
            startup = subprocess.STARTUPINFO()
            startup.dwFlags = subprocess.STARTF_USESHOWWINDOW
            startup.wShowWindow = 0
            process = subprocess.Popen(self.command('--gui'), stdout=subprocess.DEVNULL, startupinfo=startup)
            try:
                def window():
                    found = []
                    @callback_type
                    def visit(hwnd, _):
                        pid = wintypes.DWORD()
                        user.GetWindowThreadProcessId(hwnd, ctypes.byref(pid))
                        if pid.value == process.pid and user.GetDlgItem(hwnd, 404):
                            found.append(hwnd)
                        return True
                    user.EnumWindows(visit, 0)
                    return found[0] if found else None
                hwnd = wait_for(window)
                checkbox = user.GetDlgItem(hwnd, 404)
                self.assertEqual(user.SendMessageW(checkbox, 0xf0, 0, 0), expected)  # BM_GETCHECK
                if target is not None:
                    user.SendMessageW(checkbox, 0xf5, 0, 0)  # BM_CLICK
                    self.assertEqual(user.SendMessageW(checkbox, 0xf0, 0, 0), target)
                    user.SendMessageW(hwnd, 0x111, 600, user.GetDlgItem(hwnd, 600))
                    wait_for(lambda: json.loads(self.run_app('--settings'))['features']['SkipBattleLoadingPresent'] == bool(target))
                    wait_for(lambda: bool(user.IsWindowEnabled(user.GetDlgItem(hwnd, 404))))
                    self.assertEqual(ini(self.game / 'vii-patches.ini').getboolean('Patches', 'SkipBattleLoadingPresent'), bool(target))
                user.PostMessageW(hwnd, 0x10, 0, 0)
                self.assertEqual(process.wait(timeout=10), 0)
            finally:
                if process.poll() is None:
                    process.kill()
                    process.wait(timeout=5)

    def test_dropdown_apply_reload_and_disable(self):
        config = self.game / 'vii-patches.ini'
        config.write_text('[Patches]\nNeptasm=1\n[Neptasm]\nResolution=1\n'
                          'RenderWidth=1111\nRenderHeight=625\nCameraUnlock=1\nResolutionScale=0.25\n')
        self.run_app('--install')
        original = config.read_bytes()
        startup = subprocess.STARTUPINFO()
        startup.dwFlags = subprocess.STARTF_USESHOWWINDOW
        startup.wShowWindow = 0  # Hidden native controls; never take desktop focus.
        process = subprocess.Popen(self.command('--gui'), stdout=subprocess.DEVNULL, startupinfo=startup)
        try:
            def window():
                found = []
                @callback_type
                def visit(hwnd, _):
                    pid = wintypes.DWORD()
                    user.GetWindowThreadProcessId(hwnd, ctypes.byref(pid))
                    if pid.value == process.pid and user.GetDlgItem(hwnd, 500):
                        found.append(hwnd)
                    return True
                user.EnumWindows(visit, 0)
                return found[0] if found else None
            hwnd = wait_for(window)
            combo = user.GetDlgItem(hwnd, 500)
            self.assertEqual(user.SendMessageW(combo, 0x146, 0, 0), 10)  # CB_GETCOUNT, custom keep-current
            self.assertEqual(user.SendMessageW(combo, 0x147, 0, 0), 9)  # CB_GETCURSEL
            user.SendMessageW(hwnd, 0x111, 600, user.GetDlgItem(hwnd, 600))
            wait_for(lambda: bool(user.IsWindowEnabled(user.GetDlgItem(hwnd, 500))))
            self.assertEqual(config.read_bytes(), original)
            def select(index):
                control = user.GetDlgItem(hwnd, 500)
                user.SendMessageW(control, 0x14e, index, 0)  # CB_SETCURSEL
                user.SendMessageW(hwnd, 0x111, 500 | (1 << 16), control)  # CBN_SELCHANGE
                user.SendMessageW(hwnd, 0x111, 600, user.GetDlgItem(hwnd, 600))  # Install
            select(4)  # 1280 x 720
            wait_for(lambda: self.run_app('--settings').find('"enabled":true,"width":1280,"height":720') >= 0)
            wait_for(lambda: bool(user.IsWindowEnabled(user.GetDlgItem(hwnd, 500))))
            parsed = ini(config)
            self.assertTrue(parsed.getboolean('Patches', 'Neptasm'))
            self.assertTrue(parsed.getboolean('Neptasm', 'Resolution'))
            self.assertTrue(parsed.getboolean('Neptasm', 'CameraUnlock'))
            self.assertEqual(user.SendMessageW(user.GetDlgItem(hwnd, 500), 0x147, 0, 0), 4)
            select(0)
            wait_for(lambda: self.run_app('--settings').find('"enabled":false,"width":0,"height":0') >= 0)
            wait_for(lambda: bool(user.IsWindowEnabled(user.GetDlgItem(hwnd, 500))))
            parsed = ini(config)
            self.assertFalse(parsed.getboolean('Neptasm', 'Resolution'))
            self.assertTrue(parsed.getboolean('Patches', 'Neptasm'))
            self.assertTrue(parsed.getboolean('Neptasm', 'CameraUnlock'))
            user.PostMessageW(hwnd, 0x10, 0, 0)
            self.assertEqual(process.wait(timeout=10), 0)
        finally:
            if process.poll() is None:
                process.kill()
                process.wait(timeout=5)


if __name__ == '__main__':
    suite = unittest.TestSuite(ResolutionUI(name) for name in ResolutionUI.__dict__ if name.startswith('test_'))
    result = unittest.TextTestRunner(verbosity=2).run(suite)
    (ResolutionUI.evidence / 'resolution-ui-result.json').write_text(json.dumps({
        'passed': result.wasSuccessful(), 'tests_run': result.testsRun, 'game_launched': False}, indent=2))
    print('Evidence:', ResolutionUI.evidence)
    raise SystemExit(not result.wasSuccessful())
