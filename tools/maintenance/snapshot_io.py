"""Windows 快照交换：具名互斥锁与 FILE_SHARE_DELETE，避免读者阻塞原子替换。"""
import ctypes as C
import hashlib
import msvcrt
import os
import time
from contextlib import contextmanager
from pathlib import Path

_kernel = C.WinDLL('kernel32', use_last_error=True)
_kernel.CreateFileW.argtypes = [C.c_wchar_p, C.c_ulong, C.c_ulong, C.c_void_p, C.c_ulong, C.c_ulong, C.c_void_p]
_kernel.CreateFileW.restype = C.c_void_p
_kernel.CloseHandle.argtypes = [C.c_void_p]
_kernel.CloseHandle.restype = C.c_int
_kernel.CreateMutexW.argtypes = [C.c_void_p, C.c_int, C.c_wchar_p]
_kernel.CreateMutexW.restype = C.c_void_p
_kernel.WaitForSingleObject.argtypes = [C.c_void_p, C.c_ulong]
_kernel.WaitForSingleObject.restype = C.c_ulong
_kernel.ReleaseMutex.argtypes = [C.c_void_p]
_kernel.ReleaseMutex.restype = C.c_int


@contextmanager
def snapshot_lock(path, timeout_ms=2000):
    path = Path(path).resolve()
    name = 'Local\\AstralForgeSnapshot' + hashlib.sha256(str(path).casefold().encode()).hexdigest()
    handle = _kernel.CreateMutexW(None, False, name)
    if not handle: raise C.WinError(C.get_last_error())
    acquired = False
    try:
        result = _kernel.WaitForSingleObject(handle, timeout_ms)
        if result == 0x102: raise TimeoutError('快照锁未在指定时间内取得')
        if result not in (0, 0x80): raise C.WinError(C.get_last_error())
        acquired = True
        yield path
    finally:
        if acquired: _kernel.ReleaseMutex(handle)
        _kernel.CloseHandle(handle)


def publish_snapshot(path, text):
    with snapshot_lock(path) as path:
        temporary = path.with_name(path.name + '.pending')
        temporary.write_text(text, encoding='utf-8')
        # 只容忍外部查看器已经观察到的短暂占用，不吞掉其他错误。
        for attempt in range(11):
            try:
                temporary.replace(path)
                return
            except PermissionError as error:
                if error.winerror not in (5, 32) or attempt == 10: raise
                time.sleep(.02)


def read_snapshot(path):
    with snapshot_lock(path) as path:
        handle = _kernel.CreateFileW(str(path), 0x80000000, 7, None, 3, 0x80, None)
        if handle == C.c_void_p(-1).value: raise C.WinError(C.get_last_error())
        try:
            fd = msvcrt.open_osfhandle(handle, os.O_RDONLY | os.O_BINARY)
        except Exception:
            _kernel.CloseHandle(handle)
            raise
        with os.fdopen(fd, 'r', encoding='utf-8') as stream:
            return stream.read()
