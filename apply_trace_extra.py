#!/usr/bin/env python3
# Запуск из me1androd/:  python3 apply_trace_extra.py
# Правит runtime/hle_d3d9.c по подстрокам (не зависит от номеров строк).
import sys
p = 'runtime/hle_d3d9.c'; s = open(p, encoding='utf-8').read()
if 'GetViewport -> ' in s: sys.exit('уже применено')
old = '''d->ds ? d->ds->g : 0);
}'''
a = s.index('static void tr_z(Dev* d)')
b = s.index(old, a)
fn = s[a:b + len(old)]
new_fn = fn.replace('rt0=%08x %ux%u ds=%08x\\n",', 'rt0=%08x %ux%u ds=%08x"\n             " vp=%u,%u %ux%u scissor=%u[%u,%u,%u,%u] blend=%u atest=%u\\n",')
new_fn = new_fn.replace('d->ds ? d->ds->g : 0);\n}', 'd->ds ? d->ds->g : 0,\n             d->vp[0], d->vp[1], d->vp[2], d->vp[3],\n             d->rs[174], d->scissor[0], d->scissor[1], d->scissor[2], d->scissor[3], d->rs[27], d->rs[15]);\n}')
assert new_fn != fn
s = s.replace(fn, new_fn)
g_old = 'memcpy(M + A(1), d->vp, 24); return OK_; }'
assert s.count(g_old) == 1
s = s.replace(g_old, 'memcpy(M + A(1), d->vp, 24);\n    if (g_trace > 1) tr_line("[trace] GetViewport -> %u,%u %ux%u\\n", d->vp[0], d->vp[1], d->vp[2], d->vp[3]);\n    return OK_; }')
open(p, 'w', encoding='utf-8').write(s); print('ok')
