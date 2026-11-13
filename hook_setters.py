#!/usr/bin/env python3
"""Temporary debug hooks (v2) in the generated game C: the four constant setters that copy into the shadow array 0x1517dd0.
Prints "[JPHINT] alive: <name>" on the first call of each setter (proves the hook is in the binary) and, whenever the data being
copied contains the "hint icon at the screen corner" row (-17.0236 17 16.9908 17), the caller chain.
  python3 hook_setters.py out/jp100/code_052.c          # insert (keeps code_052.c.orig)
  python3 hook_setters.py out/jp100/code_052.c --undo
Rebuild, run to the scene with the icons, then:  grep -a "\\[JPHINT\\]" run.log | head -60"""
import sys, os, shutil, re
path = sys.argv[1]; undo = '--undo' in sys.argv
orig = path + '.orig'
if undo:
    if not os.path.exists(orig): sys.exit('no backup: ' + orig)
    shutil.copyfile(orig, path); print('restored', path); sys.exit(0)
src = open(path, encoding='utf-8').read()
if 'JPHINT' in src: sys.exit('hooks already present')
HELPERS = r'''/* JPHINT debug helpers (temporary) */
static int jphint_c3_(uint32_t a) {
    union { uint32_t u; float f; } x, y, w; x.u = rd32(a); y.u = rd32(a + 4); w.u = rd32(a + 12);
    return x.f > -17.03f && x.f < -17.02f && y.f > 16.99f && y.f < 17.01f && w.f > 16.99f && w.f < 17.01f;
}
static void jphint_log_(CPU* c, const char* nm, uint32_t idx, uint32_t cnt, uint32_t ptr, uint32_t off) {
    static unsigned hn_; hn_++;
    if (hn_ > 12 && hn_ % 500) return;
    fprintf(stderr, "[JPHINT] %s idx=%u count=%u ptr=%08x row3-at=+%u ret=%08x this=%08x hit#%u stack:", nm, idx, cnt, ptr, off, rd32(c->esp), c->ecx, hn_);
    for (uint32_t i_ = 0; i_ < 128; i_++) { uint32_t w_ = rd32(c->esp + 4 * i_); if (w_ >= 0x401000u && w_ < 0x1000000u) fprintf(stderr, " +%u:%08x", i_ * 4, w_); }
    fprintf(stderr, "\n");
    for (uint32_t r_ = 0; r_ < 16; r_++) { union { uint32_t u; float f; } v_; v_.u = rd32(ptr + 4 * r_); fprintf(stderr, "%s%g", r_ ? " " : "[JPHINT]   data=", v_.f); }
    fprintf(stderr, "\n");
}
'''
def hook(name, body):
    return ('  { /* JPHINT */ static unsigned k_; if (k_++ == 0) fprintf(stderr, "[JPHINT] alive: %s\\n", "' + name + '");\n' + body + '  }\n')
HOOKS = {
 '8e8350': ('SetFloat4(idx,ptr)',
   '    uint32_t p_ = rd32(c->esp + 8); if (jphint_c3_(p_)) jphint_log_(c, "SetFloat4", rd32(c->esp + 4), 1, p_, 0);\n'),
 '8e84e0': ('SetMatrix(idx,ptr)',
   '    uint32_t p_ = rd32(c->esp + 8); if (jphint_c3_(p_ + 0x30)) jphint_log_(c, "SetMatrix", rd32(c->esp + 4), 1, p_, 48);\n'),
 '8e8600': ('SetMatrixArray(idx,count,ptr)',
   '    uint32_t n_ = rd32(c->esp + 8), p_ = rd32(c->esp + 12); if (n_ > 64) n_ = 64;\n'
   '    for (uint32_t i_ = 0; i_ < n_; i_++) if (jphint_c3_(p_ + 64 * i_ + 0x30)) { jphint_log_(c, "SetMatrixArray", rd32(c->esp + 4), n_, p_ + 64 * i_, 48); break; }\n'),
 '8e8760': ('SetFloat4Array(idx,count,ptr)',
   '    uint32_t n_ = rd32(c->esp + 8), p_ = rd32(c->esp + 12); if (n_ > 256) n_ = 256;\n'
   '    for (uint32_t i_ = 0; i_ < n_; i_++) if (jphint_c3_(p_ + 16 * i_)) { jphint_log_(c, "SetFloat4Array", rd32(c->esp + 4), n_, p_ + 16 * i_, 0); break; }\n'),
}
out = src
for addr, (nm, body) in HOOKS.items():
    m = re.search(r'^void jp_%s\(CPU\* c\) \{\n' % addr, out, re.M)
    if not m: sys.exit('jp_%s not found in %s (is it another code_NNN.c?)' % (addr, path))
    lab = 'L_%s:\n' % addr
    l = out.index(lab, m.end()) + len(lab)
    out = out[:l] + hook(nm, body) + out[l:]
first = re.search(r'^(static )?void jp_[0-9a-f]+', out, re.M)
out = out[:first.start()] + HELPERS + out[first.start():]
if '#include <stdio.h>' not in out: out = '#include <stdio.h>\n' + out
if not os.path.exists(orig): shutil.copyfile(path, orig)
open(path, 'w', encoding='utf-8').write(out)
print('hooks inserted into', path, '(backup', orig + ')')
