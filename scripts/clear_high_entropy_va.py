#!/usr/bin/env python3
"""Clear the HIGH_ENTROPY_VA flag (0x20 in DllCharacteristics) in PE32+ files.

The runtime reserves guest address space at 0x1000000000-0xfc00000000. With high-entropy ASLR Windows may
place DLLs, heaps or stacks anywhere in 128 TiB, which now and then makes that reservation fail (error 487).
Usage: clear_high_entropy_va.py FILE_OR_DIR... (directories: every *.dll and *.exe in them). Idempotent."""
import sys
from pathlib import Path


def clear(path):
    with open(path, 'rb') as f:
        head = f.read(0x400)
    if head[:2] != b'MZ':
        return False
    pe = int.from_bytes(head[0x3c:0x40], 'little')
    if pe + 0x60 > len(head) or head[pe:pe + 4] != b'PE\0\0':
        return False
    opt = pe + 24
    if int.from_bytes(head[opt:opt + 2], 'little') != 0x20b:  # PE32+
        return False
    field = opt + 70
    flags = int.from_bytes(head[field:field + 2], 'little')
    if not flags & 0x20:
        return False
    with open(path, 'r+b') as f:
        f.seek(field)
        f.write((flags & ~0x20).to_bytes(2, 'little'))
    return True


def main(argv):
    if not argv:
        print(__doc__); return 1
    for arg in argv:
        p = Path(arg)
        files = sorted(list(p.glob('*.dll')) + list(p.glob('*.exe'))) if p.is_dir() else [p]
        for f in files:
            if clear(f):
                print(f'cleared HIGH_ENTROPY_VA: {f.name}')
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
