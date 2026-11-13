#!/usr/bin/env python3
"""Проверка, что DLL подходит игре: все функции, которые exe импортирует из неё, в DLL есть.

    python3 tools/jp100_check_dll.py JurassicPark100.exe binkw32.dll [ИМЯ_DLL_В_ИМПОРТАХ]

Код возврата 0 — DLL подходит, 1 — каких-то экспортов не хватает (печатаются первые 8).
"""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from pefile_min import PE  # noqa: E402

if len(sys.argv) < 3:
    sys.exit(__doc__)
exe, dll = PE(sys.argv[1]), PE(sys.argv[2])
name = os.path.basename(sys.argv[3] if len(sys.argv) > 3 else sys.argv[2]).lower()
need = sorted({nm for _, d, nm in exe.imports if d.lower() == name})
have = set(dll.exports) | {'#%d' % o for o in dll.export_ords.values()}
miss = [n for n in need if n not in have]
print('%s: игра импортирует %d, в DLL экспортов %d, не хватает %d%s' % (
    name, len(need), len(dll.exports), len(miss), (': ' + ', '.join(miss[:8])) if miss else ''))
sys.exit(1 if miss else 0)
