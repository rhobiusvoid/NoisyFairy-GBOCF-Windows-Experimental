#!/usr/bin/env python3
"""Fails when a tracked file is larger than the limit, unless it is listed in ALLOWED.
Keeps binaries (exes, zips, big images) out of the git history."""
import subprocess
import sys
from pathlib import Path

LIMIT = 3 * 1024 * 1024
ROOT = Path(__file__).resolve().parent.parent
ALLOWED_PREFIXES = ('gpu/third_party/', 'docs/images/sample.png', 'gpu/assets/overlay.png')
BANNED_SUFFIXES = ('.exe', '.dll', '.zip', '.7z', '.pkg', '.prx', '.sprx')

bad = []
for name in subprocess.check_output(['git', 'ls-files'], cwd=ROOT, text=True).splitlines():
    path = ROOT / name
    if not path.is_file():
        continue
    if name.lower().endswith(BANNED_SUFFIXES):
        bad.append(f'{name}: binary file types are not allowed in git')
    elif path.stat().st_size > LIMIT and not name.startswith(ALLOWED_PREFIXES):
        bad.append(f'{name}: {path.stat().st_size / 1048576:.1f} MiB exceeds {LIMIT >> 20} MiB')
if bad:
    sys.exit('\n'.join(bad))
print('repository size check passed')
