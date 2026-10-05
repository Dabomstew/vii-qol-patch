"""Read-only PE resources and proxy registry checks for installer fixtures."""
import ctypes
from ctypes import wintypes
import re




def embedded_resource(executable, number):
    kernel32 = ctypes.WinDLL('kernel32', use_last_error=True)
    kernel32.LoadLibraryExW.argtypes = [wintypes.LPCWSTR, wintypes.HANDLE, wintypes.DWORD]
    kernel32.LoadLibraryExW.restype = wintypes.HMODULE
    kernel32.FindResourceW.argtypes = [wintypes.HMODULE, ctypes.c_void_p, ctypes.c_void_p]
    kernel32.FindResourceW.restype = wintypes.HANDLE
    kernel32.SizeofResource.argtypes = [wintypes.HMODULE, wintypes.HANDLE]
    kernel32.SizeofResource.restype = wintypes.DWORD
    kernel32.LoadResource.argtypes = [wintypes.HMODULE, wintypes.HANDLE]
    kernel32.LoadResource.restype = wintypes.HANDLE
    kernel32.LockResource.argtypes = [wintypes.HANDLE]
    kernel32.LockResource.restype = ctypes.c_void_p
    kernel32.FreeLibrary.argtypes = [wintypes.HMODULE]
    kernel32.FreeLibrary.restype = wintypes.BOOL
    handle = kernel32.LoadLibraryExW(str(executable), None, 0x00000002)
    if not handle:
        raise OSError(ctypes.get_last_error(), 'Cannot open Prepare Game resources')
    try:
        resource = kernel32.FindResourceW(handle, ctypes.c_void_p(number), ctypes.c_void_p(10))
        size = kernel32.SizeofResource(handle, resource)
        pointer = kernel32.LockResource(kernel32.LoadResource(handle, resource))
        if not resource or not pointer or not size:
            raise ValueError(f'Prepare Game does not contain resource {number}')
        return ctypes.string_at(pointer, size)
    finally:
        kernel32.FreeLibrary(handle)


def embedded_proxy(executable):
    return embedded_resource(executable, 101)


def proxy_hashes(data):
    hashes = set()
    for line in data.decode('ascii').splitlines():
        if not line or line.startswith('#'):
            continue
        match = re.fullmatch(r'([0-9a-f]{64}) \| (.*\S.*)', line)
        if not match or match[1] in hashes:
            raise ValueError('Malformed or duplicate known-proxy registry entry')
        hashes.add(match[1])
    if not hashes:
        raise ValueError('Known-proxy registry is empty')
    return hashes
