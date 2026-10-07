"""Portable release builds and byte-verified packages. Python 3.10+, Windows/MSVC."""
import argparse
import ctypes
from ctypes import wintypes
import hashlib
import json
import os
from pathlib import Path, PurePosixPath
import re
import subprocess
import sys
import tempfile
import xml.etree.ElementTree as ET
import zipfile

ROOT = Path(__file__).resolve().parents[1]


def need(condition, message):
    if not condition: raise RuntimeError(message)


def digest(data):
    return hashlib.sha256(data).hexdigest()


def git(*args):
    return subprocess.check_output(['git', *args], cwd=ROOT, text=True).strip()


def write_json(path, value):
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(value, indent=2) + '\n', encoding='utf-8')


def inputs():
    return {p: digest((ROOT / p).read_bytes()) for p in git('ls-files').splitlines()}


def version():
    result = (ROOT / 'VERSION').read_text().strip()
    need(re.fullmatch(r'\d+\.\d+\.\d+', result), 'VERSION must be major.minor.patch')
    return result


def machine(data):
    need(len(data) > 64 and data[:2] == b'MZ', 'Missing PE header')
    offset = int.from_bytes(data[60:64], 'little')
    need(data[offset:offset + 4] == b'PE\0\0', 'Missing PE signature')
    return int.from_bytes(data[offset + 4:offset + 6], 'little')


def resource(path, number, kind=10):
    kernel = ctypes.WinDLL('kernel32', use_last_error=True)
    kernel.LoadLibraryExW.argtypes = [wintypes.LPCWSTR, wintypes.HANDLE, wintypes.DWORD]
    kernel.LoadLibraryExW.restype = wintypes.HMODULE
    kernel.FindResourceW.argtypes = [wintypes.HMODULE, ctypes.c_void_p, ctypes.c_void_p]
    kernel.FindResourceW.restype = wintypes.HANDLE
    kernel.SizeofResource.argtypes = [wintypes.HMODULE, wintypes.HANDLE]
    kernel.SizeofResource.restype = wintypes.DWORD
    kernel.LoadResource.argtypes = [wintypes.HMODULE, wintypes.HANDLE]
    kernel.LoadResource.restype = wintypes.HANDLE
    kernel.LockResource.argtypes = [wintypes.HANDLE]
    kernel.LockResource.restype = ctypes.c_void_p
    kernel.FreeLibrary.argtypes = [wintypes.HMODULE]
    module = kernel.LoadLibraryExW(str(path), None, 2)
    need(module, 'Cannot open PE resources')
    try:
        item = kernel.FindResourceW(module, ctypes.c_void_p(number), ctypes.c_void_p(kind))
        need(item, f'Missing embedded resource {kind}/{number}')
        size = kernel.SizeofResource(module, item)
        pointer = kernel.LockResource(kernel.LoadResource(module, item))
        need(pointer and size, 'Invalid embedded resource')
        return ctypes.string_at(pointer, size)
    finally: kernel.FreeLibrary(module)


def verify_preparer(path):
    """Validate activation and launch exact standalone bytes, without sidecars."""
    path = Path(path).resolve()
    manifest = ET.fromstring(resource(path, 1, 24))
    ns = {'asm': 'urn:schemas-microsoft-com:asm.v1'}
    dependencies = manifest.findall('asm:dependency/asm:dependentAssembly/asm:assemblyIdentity', ns)
    need(any(item.get('name') == 'Microsoft.Windows.Common-Controls'
             and item.get('version') == '6.0.0.0'
             and item.get('publicKeyToken') == '6595b64144ccf1df'
             and item.get('type') == 'win32' for item in dependencies),
         'Preparer needs embedded common-controls v6 activation manifest')
    data = path.read_bytes()
    offset = int.from_bytes(data[60:64], 'little')
    machine(data)
    subsystem = int.from_bytes(data[offset + 24 + 68:offset + 24 + 70], 'little')
    need(subsystem in (2, 3), 'Preparer must use Windows GUI or console subsystem')
    # A fresh path avoids cached activation state; no application DLLs or manifests.
    with tempfile.TemporaryDirectory(prefix='preparer-startup-') as folder:
        standalone = Path(folder) / Path(path).name
        standalone.write_bytes(data)
        kernel = ctypes.WinDLL('kernel32', use_last_error=True)
        kernel.SetErrorMode.argtypes = [wintypes.UINT]
        kernel.SetErrorMode.restype = wintypes.UINT
        previous = kernel.SetErrorMode(0x8003)
        try:
            if subsystem == 2:
                user = ctypes.WinDLL('user32', use_last_error=True)
                user.WaitForInputIdle.argtypes = [wintypes.HANDLE, wintypes.DWORD]
                user.WaitForInputIdle.restype = wintypes.DWORD
                process = subprocess.Popen([str(standalone)], cwd=folder,
                                           stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
                try:
                    idle = user.WaitForInputIdle(int(process._handle), 10000)
                    code = process.poll()
                    need(idle == 0 and code is None,
                         f'Standalone GUI startup failed: idle={idle}, exit={code}')
                finally:
                    if process.poll() is None: process.terminate()
                    process.wait(timeout=5)
            else:
                result = subprocess.run([str(standalone), '--help'], cwd=folder,
                                        capture_output=True, timeout=10)
                need(result.returncode == 0 and result.stdout.strip(),
                     f'Standalone CLI startup failed: 0x{result.returncode & 0xffffffff:08x}')
        finally:
            kernel.SetErrorMode(previous)


def preparer_entries(configuration):
    """Every shipped executable is a preparer, including alternate CLI entrypoints."""
    if not configuration.get('preparer'): return []
    entries = [entry for entry in configuration['package_files']
               if Path(entry['target']).suffix.lower() == '.exe']
    need(any(entry['source'] == configuration['preparer'] for entry in entries),
         'Primary preparer must be in package_files')
    return entries


def execute(command, environment):
    command = [sys.executable if value == '{python}' else value for value in command]
    if command[0].endswith('.cmd'):
        command[0] = command[0].replace('/', '\\')
        command = [os.environ['ComSpec'], '/d', '/c', *command]
    print('Running: ' + ' '.join(command), flush=True)
    subprocess.run(command, cwd=ROOT, env=environment, check=True)


def build(configuration, tests, visual_studio):
    need(os.name == 'nt', 'Native builds require Windows')
    need(not git('status', '--porcelain'), 'Commit all source and instructions before building')
    generated = ROOT / 'build/generated'
    generated.mkdir(parents=True, exist_ok=True)
    release = version()
    (generated / 'release-version.hpp').write_text('#pragma once\n#define PATCH_RELEASE_VERSION "' + release + '"\n', encoding='ascii')
    numbers = release.replace('.', ',') + ',0'
    template = '''#include <winver.h>
1 VERSIONINFO
FILEVERSION {numbers}
PRODUCTVERSION {numbers}
FILEFLAGSMASK 0x3fL
FILEFLAGS 0
FILEOS VOS_NT_WINDOWS32
FILETYPE {kind}
BEGIN
 BLOCK "StringFileInfo"
 BEGIN
  BLOCK "040904b0"
  BEGIN
   VALUE "FileVersion", "{release}\\0"
   VALUE "ProductVersion", "{release}\\0"
   VALUE "ProductName", "{project}\\0"
  END
 END
 BLOCK "VarFileInfo"
 BEGIN
  VALUE "Translation", 0x0409, 1200
 END
END
'''
    (generated / 'version.rc').write_text(template.format(numbers=numbers, release=release, project=configuration['project'], kind='VFT_APP'), encoding='ascii')
    (generated / 'proxy-version.rc').write_text(template.format(numbers=numbers, release=release, project=configuration['project'], kind='VFT_DLL'), encoding='ascii')
    environment = os.environ.copy()
    if visual_studio: environment['PATCH_VISUAL_STUDIO'] = visual_studio
    # Resource compilation has no path interpolation supplied by callers.
    subprocess.run([os.environ['ComSpec'], '/d', '/c', 'call scripts\\init-msvc.cmd x86 && rc /nologo /fo build\\generated\\proxy-version.res build\\generated\\proxy-version.rc'], cwd=ROOT, env=environment, check=True)
    proxy_command = configuration['proxy_command'][:]
    if tests and configuration.get('proxy_tests'): proxy_command += configuration['proxy_tests']
    if visual_studio and proxy_command[0] == 'powershell.exe': proxy_command += ['-VisualStudioDirectory', visual_studio]
    execute(proxy_command, environment)
    proxy = ROOT / configuration['proxy']
    need(machine(proxy.read_bytes()) == 0x14c, 'Release proxy must be x86')
    if configuration.get('preparer'):
        hashes = (ROOT / 'native/proxy-hashes.txt').read_text(encoding='ascii').splitlines()
        rows = [line for line in hashes if line and not line.startswith('#')]
        proxy_hash = digest(proxy.read_bytes())
        if proxy_hash not in [line.split(' | ')[0] for line in rows]: rows.append(proxy_hash + ' | release ' + release)
        (generated / 'proxy-hashes.txt').write_text('\n'.join(rows) + '\n', encoding='ascii')
        execute(configuration['prepare_command'], environment)
        need(resource(ROOT / configuration['preparer'], 101) == proxy.read_bytes(), 'Embedded proxy differs from release DLL')
        need(resource(ROOT / configuration['preparer'], 102) == (generated / 'proxy-hashes.txt').read_bytes(), 'Embedded recognition registry mismatch')
        need(resource(ROOT / configuration['preparer'], 103) == (ROOT / configuration['configuration']).read_bytes(), 'Embedded settings mismatch')
        for entry in preparer_entries(configuration): verify_preparer(ROOT / entry['source'])
    if tests:
        for command in configuration.get('test_commands', []): execute(command, environment)
    artifacts = {path: digest((ROOT / path).read_bytes()) for path in configuration['artifacts']}
    if configuration.get('preparer'):
        artifacts['build/generated/proxy-hashes.txt'] = digest((generated / 'proxy-hashes.txt').read_bytes())
    write_json(ROOT / 'build/release-identity.json', {'version': release, 'source_revision': git('rev-parse', 'HEAD'), 'sources': inputs(), 'artifacts': artifacts, 'synthetic_tests_passed': tests})
    print(json.dumps({'built': configuration['project'], 'version': release, 'artifacts': artifacts, 'synthetic_tests_passed': tests}, indent=2))


def check_links(files):
    import posixpath
    for name, data in files.items():
        if not name.endswith('.md'): continue
        for link in re.findall(r'\]\(([^)#]+)(?:#[^)]*)?\)', data.decode('utf-8')):
            if '://' in link or link.startswith('#'): continue
            target = posixpath.normpath(posixpath.join(posixpath.dirname(name), link))
            need(target in files, f'Packaged link target missing: {name} -> {link}')


def package(configuration):
    need(not git('status', '--porcelain'), 'Package only clean committed source')
    need(git('branch', '--show-current') == 'production', 'Package only production')
    identity = json.loads((ROOT / 'build/release-identity.json').read_text())
    need(identity['version'] == version() and identity['source_revision'] == git('rev-parse', 'HEAD'), 'Rebuild current committed version')
    need(identity['sources'] == inputs(), 'Build source set changed')
    need(identity['synthetic_tests_passed'], 'Run build-release.ps1 -Tests before packaging')
    for path, expected in identity['artifacts'].items():
        need(digest((ROOT / path).read_bytes()) == expected, f'Build artifact changed: {path}')
    payload = {}
    for entry in configuration['package_files']:
        name, path = entry['target'], entry['source']
        need(not PurePosixPath(name).is_absolute() and '..' not in PurePosixPath(name).parts and '\\' not in name, 'Unsafe ZIP member')
        need(name not in payload, 'Duplicate ZIP member')
        payload[name] = (ROOT / path).read_bytes()
    for name, expected in configuration['machines'].items(): need(machine(payload[name]) == expected, f'Wrong machine: {name}')
    if configuration.get('preparer'):
        need(resource(ROOT / configuration['preparer'], 101) == (ROOT / configuration['proxy']).read_bytes(), 'Embedded proxy mismatch')
        need(resource(ROOT / configuration['preparer'], 102) == (ROOT / 'build/generated/proxy-hashes.txt').read_bytes(), 'Embedded registry mismatch')
        need(resource(ROOT / configuration['preparer'], 103) == (ROOT / configuration['configuration']).read_bytes(), 'Embedded settings mismatch')
    manifest = {'project': configuration['project'], 'version': version(), 'source_revision': identity['source_revision'], 'architectures': configuration['architectures'], 'files': {p: {'sha256': digest(d), 'bytes': len(d)} for p, d in payload.items()}}
    payload['manifest.json'] = (json.dumps(manifest, indent=2) + '\n').encode('utf-8')
    payload['SHA256SUMS.txt'] = ''.join(f'{digest(d)}  {p}\n' for p, d in sorted(payload.items())).encode('ascii')
    check_links(payload)
    name = configuration['project'] + '-' + version()
    directory, archive = ROOT / 'dist' / name, ROOT / 'dist' / (name + '.zip')
    need(not directory.exists() and not archive.exists() and not archive.with_suffix('.zip.sha256').exists(), 'Preserve existing release output; choose a new version')
    directory.mkdir(parents=True)
    for path, data in payload.items():
        target = directory / path; target.parent.mkdir(parents=True, exist_ok=True); target.write_bytes(data)
    with zipfile.ZipFile(archive, 'x', compression=zipfile.ZIP_DEFLATED) as output:
        for path, data in sorted(payload.items()):
            entry = zipfile.ZipInfo(path, (2026, 1, 1, 0, 0, 0)); entry.compress_type = zipfile.ZIP_DEFLATED
            output.writestr(entry, data)
    with zipfile.ZipFile(archive) as output:
        need(sorted(output.namelist()) == sorted(payload), 'ZIP allowlist mismatch')
        for path, data in payload.items(): need(output.read(path) == data, f'ZIP byte mismatch: {path}')
        for entry in preparer_entries(configuration):
            with tempfile.TemporaryDirectory(prefix='packaged-preparer-') as folder:
                preparer = Path(folder) / Path(entry['target']).name
                preparer.write_bytes(output.read(entry['target']))
                verify_preparer(preparer)
    checksum = digest(archive.read_bytes())
    archive.with_suffix('.zip.sha256').write_text(f'{checksum}  {archive.name}\n', encoding='ascii')
    print(json.dumps({'zip': str(archive), 'sha256': checksum, 'files_verified': len(payload)}, indent=2))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('action', choices=['build', 'package'])
    parser.add_argument('--tests', action='store_true'); parser.add_argument('--visual-studio-directory')
    args = parser.parse_args()
    configuration = json.loads((ROOT / 'release-config.json').read_text())
    try:
        if args.action == 'build': build(configuration, args.tests, args.visual_studio_directory)
        else: package(configuration)
        return 0
    except (RuntimeError, OSError, subprocess.CalledProcessError) as error:
        print(str(error), file=sys.stderr); return 1


if __name__ == '__main__': sys.exit(main())
