# Allow ordinary filesystem writes only below the disposable smoke directory.
import ctypes as c
import os, platform, stat, sys
from pathlib import Path

assert platform.machine() == 'x86_64'
libc = c.CDLL(None, use_errno=True)
libc.syscall.restype = c.c_long

def checked(result):
    if result < 0:
        error = c.get_errno()
        raise OSError(error, os.strerror(error))
    return result

class PathRule(c.Structure):
    _pack_ = 1
    _fields_ = [('access', c.c_uint64), ('parent_fd', c.c_int32)]

abi = checked(libc.syscall(444, 0, 0, 1))
assert abi >= 3
rights = (1 << 1) | sum(1 << bit for bit in range(4, 15))
attr = c.c_uint64(rights)
rs = checked(libc.syscall(444, c.byref(attr), c.sizeof(attr), 0))

def rule(path, access):
    fd = os.open(path, os.O_PATH | os.O_CLOEXEC)
    try:
        checked(libc.syscall(445, rs, 1, c.byref(PathRule(access, fd)), 0))
    finally:
        os.close(fd)

private = Path(sys.argv[1]).resolve(strict=True)
rule(private, rights)
# Graphics discovery may open DRM character nodes O_RDWR; no other /dev writes.
for node in (Path('/dev/null'), *Path('/dev/dri').glob('renderD*'), *Path('/dev/dri').glob('card*')):
    if stat.S_ISCHR(node.stat().st_mode):
        rule(node, 1 << 1)
checked(libc.prctl(38, 1, 0, 0, 0))  # PR_SET_NO_NEW_PRIVS
checked(libc.syscall(446, rs, 0))
os.close(rs)
print(f'Landlock ABI {abi}: file writes admitted under {private} (and character nodes for DRM/null)', flush=True)
os.execvp(sys.argv[2], sys.argv[2:])
