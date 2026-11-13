#!/usr/bin/env python3
"""Seeds для рекомпиляции JurassicPark100.exe: начала функций, на которые нет прямых call/jmp.

1. Таблицы инициализации CRT (__xi_a..__xi_z, __xc_a..__xc_z, __xp/__xt): код вида
   push/mov/cmp/lea с двумя адресами в секции данных на расстоянии <= 32 байт (начало и конец
   таблицы для _initterm/_initterm_e); все элементы таблицы — 0 или адреса в исполняемой секции.
   Из-за них падала v3: "dispatch: no function at target" на 00efcf7c.
2. Указатели на код из секций данных (vtable, таблицы колбэков и обработчиков): выровненные dword,
   указывающие в исполняемую секцию на правдоподобное начало функции (--no-data отключает).
3. Списки --merge (файл или папка с *.txt/*.log): hex-адреса по одному на строку (# — комментарий)
   и строки лога "MISSING-FUNC xxxxxxxx".

    python3 tools/jp100_crt_seeds.py JurassicPark100.exe build/seeds/jp100.txt --merge seeds --expect 00efcf7c
"""
import argparse
import os
import re
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from pefile_min import PE  # noqa: E402

try:
    import capstone
    _CS = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32)
except Exception:   # без capstone п. 2 работает только по байтовым признакам
    _CS = None

# инструкции, которых не бывает в начале обычной функции (признак того, что это данные, а не код)
BAD = {'in', 'out', 'insb', 'insw', 'insd', 'outsb', 'outsw', 'outsd', 'hlt', 'cli', 'sti', 'iret', 'iretd',
       'into', 'int', 'int1', 'lds', 'les', 'arpl', 'bound', 'aaa', 'aas', 'daa', 'das', 'aam', 'aad', 'salc',
       'retf', 'lcall', 'ljmp', 'sldt', 'lgdt', 'lidt', 'lldt', 'ltr', 'loopne', 'loope'}
HEXTOK = re.compile(r'(?:0x)?[0-9a-fA-F]{1,8}$')
MISS = re.compile(r'MISSING-FUNC\s+(?:0x)?([0-9a-fA-F]+)')
OPS = [re.compile(rb'(?=[\x68\x3d\xb8-\xbf](....))', re.S),                       # push/cmp eax/mov r32, imm32
       re.compile(rb'(?=\x81[\xf8-\xff](....))', re.S),                            # cmp r32, imm32
       re.compile(rb'(?=\x8d[\x05\x0d\x15\x1d\x25\x2d\x35\x3d](....))', re.S)]       # lea r32, [disp32]


def load_list(path):
    out = []
    with open(path, encoding='utf-8', errors='replace') as f:
        for line in f:
            m = MISS.search(line)
            if m:
                out.append(int(m.group(1), 16))
                continue
            toks = line.split('#', 1)[0].replace(',', ' ').split()
            if toks and all(HEXTOK.match(t) for t in toks):
                out.extend(int(t, 16) for t in toks)
    return out


def load_merge(p):
    if os.path.isdir(p):
        r = []
        for n in sorted(os.listdir(p)):
            if n.lower().endswith(('.txt', '.log')):
                r += load_list(os.path.join(p, n))
        return r
    if os.path.isfile(p):
        return load_list(p)
    print('[seeds] нет %s — пропускаю' % p)
    return []


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('pe')
    ap.add_argument('out')
    ap.add_argument('--merge', action='append', default=[], help='файл или папка со списками адресов (можно несколько раз)')
    ap.add_argument('--no-data', action='store_true', help='не искать указатели на код в секциях данных (п. 2)')
    ap.add_argument('--expect', action='append', default=[], help='проверить, что адрес попал в seeds')
    a = ap.parse_args()

    pe = PE(a.pe)
    img, base = pe.img, pe.base
    X, D = [], []
    for s in pe.sections:
        lo, hi = s.va, min(s.va + (s.vs or s.rs), len(img))
        if hi <= lo:
            continue
        if s.fl & 0x20000000:
            X.append((lo, hi, s.name))
        elif s.name.lower() not in ('.rsrc', '.reloc'):
            D.append((lo, hi, s.name))
    if not X:
        sys.exit('[seeds] в %s нет исполняемых секций' % a.pe)
    XV = [(lo + base, hi + base) for lo, hi, _ in X]
    xmin, xmax = min(x[0] for x in XV), max(x[1] for x in XV)
    print('[seeds] %s: база %08x, код %08x-%08x, capstone: %s' % (os.path.basename(a.pe), base, xmin, xmax, 'да' if _CS else 'нет'))

    def in_x(va):
        return any(lo <= va < hi for lo, hi in XV)

    def in_d(va):
        r = va - base
        for lo, hi, n in D:
            if lo <= r < hi:
                return n
        return None

    def u32(va):
        return struct.unpack_from('<I', img, va - base)[0]

    def decodes(va):
        r = va - base
        b = img[r:r + 96]
        if len(b) < 2 or b[0] == 0xCC or b[:2] == b'\0\0':
            return False
        if _CS is None:
            return True
        n = 0
        for ins in _CS.disasm(b, va):
            if ins.mnemonic in BAD:
                return False
            n += 1
            if ins.mnemonic in ('ret', 'retn', 'jmp') or n >= 6:
                return True
        return False

    def looks_like_start(va):
        r = va - base
        if r < 3:
            return False
        prev = img[r - 1]
        if not ((va & 15) == 0 or prev in (0xCC, 0x90, 0xC3) or (img[r - 3] == 0xC2 and prev == 0)):
            return False
        return decodes(va)

    # 1. таблицы CRT
    cand = set()
    for lo, hi, _ in X:
        code = img[lo:hi]
        ops = []
        for rx in OPS:
            for m in rx.finditer(code):
                v = struct.unpack('<I', m.group(1))[0]
                if in_d(v):
                    ops.append((m.start(), v))
        ops.sort()
        for i, (o1, v1) in enumerate(ops):
            j = i + 1
            while j < len(ops) and ops[j][0] - o1 <= 32:
                v2 = ops[j][1]
                j += 1
                t0, t1 = min(v1, v2), max(v1, v2)
                if not (0 < t1 - t0 <= 0x10000) or (t1 - t0) % 4:
                    continue
                n0 = in_d(t0)
                if not n0 or n0 != in_d(t1 - 1):
                    continue
                if all(v == 0 or in_x(v) for v in (u32(x) for x in range(t0, t1, 4))):
                    cand.add((t0, t1))
    keep = [(t0, t1) for t0, t1 in sorted(cand)
            if not any(c0 <= t0 and t1 <= c1 and (c0, c1) != (t0, t1) for c0, c1 in cand)]
    crt, ntab, nsmall = set(), 0, 0
    for t0, t1 in keep:
        nz = [v for v in (u32(x) for x in range(t0, t1, 4)) if v]
        if len(nz) >= 3:
            crt.update(nz)
            ntab += 1
            print('[seeds]   таблица %08x..%08x: %d адресов' % (t0, t1, len(nz)))
        else:
            good = [v for v in nz if decodes(v)]
            if good:
                crt.update(good)
                nsmall += 1
    print('[seeds] таблицы CRT: %d (%d адресов), мелких таблиц: %d' % (ntab, len(crt), nsmall))

    # 2. указатели на код из секций данных
    data = set()
    if not a.no_data:
        seen = set()
        for lo, hi, _ in D:
            lo4 = (lo + 3) & ~3
            end = lo4 + ((hi - lo4) // 4) * 4
            for (v,) in struct.iter_unpack('<I', img[lo4:end]):
                if xmin <= v < xmax and v not in seen:
                    seen.add(v)
                    if in_x(v) and looks_like_start(v):
                        data.add(v)
        print('[seeds] указатели из данных: %d (кандидатов %d)' % (len(data), len(seen)))

    # 3. списки --merge
    ext = set()
    for p in a.merge:
        ext.update(load_merge(p))
    ext_ok = {v for v in ext if in_x(v)}
    final = sorted(v for v in (crt | data | ext_ok) if in_x(v))
    fs = set(final)
    print('[seeds] из --merge: %d (вне кода отброшено %d), новых: %d, всего: %d' % (
        len(ext), len(ext) - len(ext_ok), len(fs - ext_ok), len(final)))
    d = os.path.dirname(os.path.abspath(a.out))
    os.makedirs(d, exist_ok=True)
    with open(a.out, 'w') as f:
        f.write(''.join('%08x\n' % v for v in final))
    for e in a.expect:
        v = int(e, 16)
        print('[seeds] проверка %08x: %s' % (v, 'есть' if v in fs else 'НЕТ'))


if __name__ == '__main__':
    main()
