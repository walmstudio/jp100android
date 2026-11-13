#!/usr/bin/env python3
"""Temporary debug hook for the generated game C: logs the caller chain when SetMatrix (jp_8e84e0) is given the
"hint icon at the screen corner" matrix (4th row = -17.0236 17 16.9908 17).
  python3 hook_m4.py out/jp100/code_052.c          # insert (keeps code_052.c.orig)
  python3 hook_m4.py out/jp100/code_052.c --undo   # restore
After inserting, rebuild the game as usual and grep the run log for "[dbg] SetMatrix"."""
import sys, os, shutil, re
path = sys.argv[1]; undo = '--undo' in sys.argv
orig = path + '.orig'
if undo:
    if not os.path.exists(orig): sys.exit('no backup: ' + orig)
    shutil.copyfile(orig, path); print('restored', path); sys.exit(0)
src = open(path, encoding='utf-8').read()
if 'JP100-DBG' in src: sys.exit('hook already present')
HOOK = r'''  { /* JP100-DBG: who passes the corner matrix to SetMatrix(index, ptr) */
    static unsigned hn_; uint32_t m_ = rd32(c->esp + 8); union { uint32_t u; float f; } a_, b_, d_;
    a_.u = rd32(m_ + 0x30); b_.u = rd32(m_ + 0x34); d_.u = rd32(m_ + 0x3c);
    if (a_.f > -17.03f && a_.f < -17.02f && b_.f > 16.99f && b_.f < 17.01f && d_.f > 16.99f && d_.f < 17.01f) {
      hn_++;
      if (hn_ <= 12 || hn_ % 500 == 0) {
        fprintf(stderr, "[dbg] SetMatrix idx=%u ptr=%08x ret=%08x this=%08x hit#%u stack:", rd32(c->esp + 4), m_, rd32(c->esp), c->ecx, hn_);
        for (uint32_t i_ = 0; i_ < 128; i_++) { uint32_t w_ = rd32(c->esp + 4 * i_); if (w_ >= 0x401000u && w_ < 0x1000000u) fprintf(stderr, " +%u:%08x", i_ * 4, w_); }
        fprintf(stderr, "\n");
        for (uint32_t r_ = 0; r_ < 16; r_++) { union { uint32_t u; float f; } v_; v_.u = rd32(m_ + 4 * r_); fprintf(stderr, "%s%g", r_ ? " " : "[dbg]   m=", v_.f); }
        fprintf(stderr, "\n");
      }
    }
  }
'''
m = re.search(r'^void jp_8e84e0\(CPU\* c\) \{\n', src, re.M)
if not m: sys.exit('jp_8e84e0 not found in ' + path)
l = src.index('L_8e84e0:\n', m.end())
l_end = l + len('L_8e84e0:\n')
out = src[:l_end] + HOOK + src[l_end:]
if '#include <stdio.h>' not in out: out = '#include <stdio.h>\n' + out
if not os.path.exists(orig): shutil.copyfile(path, orig)
open(path, 'w', encoding='utf-8').write(out)
print('hook inserted into', path, '(backup', orig + ')')
