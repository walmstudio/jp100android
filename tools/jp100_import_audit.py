#!/usr/bin/env python3
"""Probe normal PE imports against an hle_import_probe executable.

An HLE registration may itself be a stub. This does not test behavior or delay
imports. A guest DLL export is considered available only by exact name.
"""
import argparse
import collections
import subprocess
from pefile_min import PE

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('image'); p.add_argument('probe')
p.add_argument('--guest', action='append', default=[], metavar='PE', help='guest DLL with exports')
a = p.parse_args()
image = PE(a.image)
guests = {}
for name in a.guest:
    import os
    guests[os.path.basename(name).lower()] = PE(name).exports
entries = [(dll, name) for _, dll, name in image.imports]
raw = ''.join('%s %s\n' % item for item in entries)
out = subprocess.run([a.probe], input=raw, text=True, capture_output=True, check=True).stdout.splitlines()
if len(out) != len(entries): raise RuntimeError('probe returned an unexpected number of records')
counts = collections.Counter(); missing = []
for (dll, name), line in zip(entries, out):
    d, n, present = line.split('\t')
    if (d, n) != (dll, name): raise RuntimeError('probe record mismatch')
    if name in guests.get(dll.lower(), {}): category = 'guest export available'
    elif present == '1': category = 'HLE registered (may be stub)'
    else: category = 'unresolved'
    counts[dll, category] += 1
    if category == 'unresolved': missing.append((dll, name))
for (dll, category), count in sorted(counts.items()): print('%-16s %-30s %4d' % (dll, category, count))
print('\nUnresolved normal imports:')
for dll, name in missing: print('%s!%s' % (dll, name))
