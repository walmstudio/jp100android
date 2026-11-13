#!/usr/bin/env python3
"""Conservative x86 MSVC RTTI and luaL_Reg seed discovery for PE images.

This is an opt-in aid, not a complete disassembler. Output is an address list
for recomp.py --seeds. Validate coverage dynamically; do not publish seeds or
generated code when derived from a proprietary executable.
"""
import argparse
import re
import struct
from collections import Counter
from pefile_min import PE


def scan(pe):
    data_sections = [s for s in pe.sections if not s.exec and s.name in ('.rdata', '.data', '.data1')]
    img, base = pe.img, pe.base
    iat = {slot for slot, _, _ in pe.imports}

    def section_end(rva):
        s = pe.section_of(rva)
        return min(len(img), s.va + max(s.vs, s.rs)) if s and s in data_sections else 0

    def u32(va):
        rva = va - base
        end = section_end(rva)
        return struct.unpack_from('<I', img, rva)[0] if 0 <= rva and rva + 4 <= end else None

    def string(va, pattern):
        rva = va - base
        end = section_end(rva)
        if rva < 0 or not end: return False
        stop = img.find(b'\0', rva, min(end, rva + 256))
        return stop >= 0 and bool(pattern.fullmatch(img[rva:stop]))

    rtti_name = re.compile(rb'\.\?A[VU][!-~]{1,240}@@')
    lua_name = re.compile(rb'[A-Za-z_][A-Za-z0-9_.:]{0,127}')
    # MSVC TypeDescriptor: two pointers followed by the decorated type name.
    types = set()
    for s in data_sections:
        end = min(len(img), s.va + max(s.vs, s.rs))
        start = s.va
        pos = img.find(b'.?A', start, end)
        while pos != -1:
            td = base + pos - 8
            if (td & 3) == 0 and pos - 8 >= start and string(base + pos, rtti_name):
                types.add(td)
            pos = img.find(b'.?A', pos + 3, end)
    # CompleteObjectLocator: 0, offset, cdOffset, pTypeDescriptor, pClassDescriptor.
    cols = set()
    for s in data_sections:
        end = min(len(img), s.va + max(s.vs, s.rs))
        for rva in range((s.va + 3) & ~3, end - 19, 4):
            sig, off, cd, typ, hierarchy = struct.unpack_from('<5I', img, rva)
            if (sig == 0 and off < 0x100000 and cd < 0x100000 and typ in types
                    and u32(hierarchy) == 0 and u32(hierarchy + 8) is not None
                    and 0 < u32(hierarchy + 8) < 10000 and section_end(hierarchy + 12 - base)):
                cols.add(base + rva)
    seeds, counts = set(), Counter()
    # A vtable starts after a pointer to a validated COL; cap length to avoid runaway scans.
    for s in data_sections:
        end = min(len(img), s.va + max(s.vs, s.rs))
        for rva in range((s.va + 3) & ~3, end - 7, 4):
            if base + rva in iat or struct.unpack_from('<I', img, rva)[0] not in cols: continue
            n = 0
            for j in range(1, min(257, (end - rva) // 4)):
                slot = base + rva + 4*j
                target = struct.unpack_from('<I', img, rva + 4*j)[0]
                if slot in iat or not pe.is_code(target): break
                seeds.add(target); n += 1
            if n: counts['vtable_candidates'] += 1; counts['vtable_entries'] += n
    # luaL_Reg sequences require >=2 named entries and a terminating {NULL,NULL}.
    for s in data_sections:
        rva, end = (s.va + 3) & ~3, min(len(img), s.va + max(s.vs, s.rs))
        while rva + 16 <= end:
            start, targets = rva, []
            while rva + 8 <= end:
                name, fn = struct.unpack_from('<II', img, rva)
                if not name and not fn:
                    if len(targets) >= 2:
                        seeds.update(targets); counts['lua_tables'] += 1; counts['lua_entries'] += len(targets)
                        rva += 8
                    break
                if base + rva in iat or base + rva + 4 in iat or not pe.is_code(fn) or not string(name, lua_name): break
                targets.append(fn); rva += 8
            if rva <= start or len(targets) < 2: rva = start + 4
    counts['type_descriptors'] = len(types); counts['complete_object_locators'] = len(cols)
    counts['unique_seeds'] = len(seeds)
    return seeds, counts


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('pe'); parser.add_argument('output', help='private address list for recomp.py --seeds')
    args = parser.parse_args()
    pe = PE(args.pe)
    seeds, counts = scan(pe)
    with open(args.output, 'w', encoding='ascii') as f:
        for va in sorted(seeds): f.write('%08x\n' % va)
    for key, value in counts.items(): print('%s: %d' % (key, value))

if __name__ == '__main__': main()
