/* JurassicPark100.exe (Telltale, 2011) extras:
 *  - msvcrt.dll for FMOD Ex (fmodex.dll imports the system MSVCRT; the exe itself does not)
 *  - the part of d3dx9_41.dll the exe imports that the d3dx9_31 HLE does not cover (hle_jp_d3dx.inc)
 *    (other d3dx9_41 names fall back to d3dx9_31 in hle_resolve_impl)
 * msvcrt functions are cdecl: nargs 0, the caller pops the arguments. */
#include "rt.h"
#include "hle.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <stdint.h>
#include <time.h>
#include <sys/stat.h>

static double j_argd(CPU* c, int off) { double d; memcpy(&d, M + c->esp + 4 + off, 8); return d; }
static uint32_t g_errno_va, g_iob;
static uint32_t errno_va(void) { if (!g_errno_va) g_errno_va = ghalloc(4, 1); return g_errno_va; }
static void set_errno(int e) { wr32(errno_va(), (uint32_t)e); }
/* call guest code / a thunk and keep our stack pointer (cdecl callees leave their arguments) */
static uint32_t gcall(CPU* c, uint32_t f, int n, const uint32_t* a) { uint32_t sp = c->esp; uint32_t r = rt_call(c, f, n, a); c->esp = sp; return r; }

/* ---- heap ---- */
static void jp_malloc(CPU* c) { uint32_t n = ARG(0); RET(ghalloc(n ? n : 1, 0)); }
static void jp_calloc(CPU* c) {
    uint64_t n = (uint64_t)ARG(0) * ARG(1);
    if (n > 0x7FFFFFFFu) { set_errno(12); RET(0); return; }
    RET(ghalloc(n ? (uint32_t)n : 1, 1));
}
static void jp_free(CPU* c) { if (ARG(0)) ghfree(ARG(0)); }
static void jp_realloc(CPU* c) {
    uint32_t p = ARG(0), n = ARG(1);
    if (!p) { RET(ghalloc(n ? n : 1, 0)); return; }
    if (!n) { ghfree(p); RET(0); return; }
    uint32_t q = ghalloc(n, 0);
    if (!q) { RET(0); return; }
    uint32_t o = ghsize(p);
    memcpy(M + q, M + p, o < n ? o : n);
    ghfree(p);
    RET(q);
}

/* ---- CRT startup ---- */
static void jp_initterm(CPU* c) {
    for (uint32_t p = ARG(0), e = ARG(1); p < e; p += 4) { uint32_t f = rd32(p); if (f) gcall(c, f, 0, NULL); }
}
static void jp_onexit(CPU* c) { RET(ARG(0)); }
static void jp_dllonexit(CPU* c) { RET(ARG(0)); }
static void jp_cxxframe(CPU* c) { RET(1); }   /* ExceptionContinueSearch */
static void jp_purecall(CPU* c) { fprintf(stderr, "[jp] msvcrt: _purecall (pure virtual call)\n"); RET(0); }
static void jp_errno(CPU* c) { RET(errno_va()); }
static void jp_nop(CPU* c) { RET(0); }

/* ---- memory / strings ---- */
static void jp_memset(CPU* c) { memset(M + ARG(0), (int)(ARG(1) & 0xFF), ARG(2)); RET(ARG(0)); }
static void jp_memcpy(CPU* c) { memmove(M + ARG(0), M + ARG(1), ARG(2)); RET(ARG(0)); }
static void jp_memcmp(CPU* c) { int r = memcmp(M + ARG(0), M + ARG(1), ARG(2)); RET(r < 0 ? -1 : r > 0); }
static void jp_memchr(CPU* c) {
    uint32_t p = ARG(0); const uint8_t* r = memchr(M + p, (int)(ARG(1) & 0xFF), ARG(2));
    RET(r ? p + (uint32_t)(r - (M + p)) : 0);
}
static void jp_strlen(CPU* c) { RET(strlen(GSTR(ARG(0)))); }
static void jp_strcpy(CPU* c) { uint32_t d = ARG(0); const char* s = GSTR(ARG(1)); memmove(M + d, s, strlen(s) + 1); RET(d); }
static void jp_strcat(CPU* c) {
    uint32_t d = ARG(0); const char* s = GSTR(ARG(1)); size_t l = strlen(GSTR(d));
    memmove(M + d + l, s, strlen(s) + 1); RET(d);
}
static void jp_strcmp(CPU* c) { int r = strcmp(GSTR(ARG(0)), GSTR(ARG(1))); RET(r < 0 ? -1 : r > 0); }
static void jp_strncpy(CPU* c) { uint32_t d = ARG(0); strncpy((char*)M + d, GSTR(ARG(1)), ARG(2)); RET(d); }

/* ---- math: doubles on the stack, results in ST(0) ---- */
#define F1(nm) static void jp_##nm(CPU* c) { fpush(c, nm(j_argd(c, 0))); }
F1(sin) F1(cos) F1(tan) F1(fabs) F1(sqrt) F1(log) F1(log10) F1(exp) F1(atan) F1(floor) F1(acos)
static void jp_pow(CPU* c) { fpush(c, pow(j_argd(c, 0), j_argd(c, 8))); }
static void jp_atan2(CPU* c) { fpush(c, atan2(j_argd(c, 0), j_argd(c, 8))); }
static void jp_ldexp(CPU* c) { fpush(c, ldexp(j_argd(c, 0), (int)rd32(c->esp + 12))); }
static void jp_ftol(CPU* c) {   /* ST(0) -> edx:eax, truncated, popped */
    double v = c->st[c->ftop & 7];
    int64_t r = (v != v || v >= 9223372036854775808.0 || v < -9223372036854775808.0) ? INT64_MIN : (int64_t)v;
    fpop(c);
    c->eax = (uint32_t)r; c->edx = (uint32_t)((uint64_t)r >> 32);
}

/* ---- printf-style formatting of guest varargs (MSVC conventions: I64, %S, ...) ---- */
typedef struct { char* p; size_t cap, len; } FO;
static void fo_put(FO* o, const char* s, size_t n) {
    for (size_t i = 0; i < n; i++) { if (o->len + 1 < o->cap) o->p[o->len] = s[i]; o->len++; }
}
static void fo_pad(FO* o, int n) { while (n-- > 0) fo_put(o, " ", 1); }
typedef struct { uint32_t ap; int wide; } GVa;
static uint32_t va32(GVa* v) { uint32_t x = rd32(v->ap); v->ap += 4; return x; }
static uint64_t va64(GVa* v) { uint64_t x; memcpy(&x, M + v->ap, 8); v->ap += 8; return x; }
static double vadbl(GVa* v) { double d; memcpy(&d, M + v->ap, 8); v->ap += 8; return d; }
static int u8enc(char* o, uint32_t cp) {
    if (cp < 0x80) { o[0] = (char)cp; return 1; }
    if (cp < 0x800) { o[0] = (char)(0xC0 | cp >> 6); o[1] = (char)(0x80 | (cp & 0x3F)); return 2; }
    o[0] = (char)(0xE0 | cp >> 12); o[1] = (char)(0x80 | ((cp >> 6) & 0x3F)); o[2] = (char)(0x80 | (cp & 0x3F)); return 3;
}
static size_t gfmt(char* out, size_t cap, const char* f, GVa* va) {
    FO o = { out, cap, 0 };
    char* wbuf = NULL;
    while (*f) {
        if (*f != '%') { const char* q = strchr(f, '%'); size_t n = q ? (size_t)(q - f) : strlen(f); fo_put(&o, f, n); f += n; continue; }
        const char* st = f++;
        if (*f == '%') { fo_put(&o, "%", 1); f++; continue; }
        char fl[8]; int nf = 0;
        while (*f && strchr("-+ #0", *f)) { if (nf < 6) fl[nf++] = *f; f++; }
        int width = 0, prec = -1;
        if (*f == '*') { int w = (int)va32(va); f++; if (w < 0) { if (nf < 6) fl[nf++] = '-'; w = -w; } width = w; }
        else while (*f >= '0' && *f <= '9') width = width * 10 + (*f++ - '0');
        if (*f == '.') {
            f++; prec = 0;
            if (*f == '*') { prec = (int)va32(va); f++; }
            else while (*f >= '0' && *f <= '9') prec = prec * 10 + (*f++ - '0');
        }
        fl[nf] = 0;
        int sz = 0, wm = 0;
        for (;;) {
            if (f[0] == 'I' && f[1] == '6' && f[2] == '4') { sz = 2; f += 3; }
            else if (f[0] == 'I' && f[1] == '3' && f[2] == '2') f += 3;
            else if (f[0] == 'I') f++;
            else if (f[0] == 'l' && f[1] == 'l') { sz = 2; f += 2; }
            else if (f[0] == 'l' || f[0] == 'w') { wm = 1; f++; }
            else if (f[0] == 'h' && f[1] == 'h') { sz = -2; f += 2; }
            else if (f[0] == 'h') { sz = -1; wm = -1; f++; }
            else if (f[0] == 'q' || f[0] == 'j') { sz = 2; f++; }
            else if (f[0] == 'L' || f[0] == 'z' || f[0] == 't') f++;
            else break;
        }
        char cv = *f;
        if (!cv) { fo_put(&o, st, strlen(st)); break; }
        f++;
        char spec[24], tmp[512]; int n;
        switch (cv) {
        case 'd': case 'i': case 'u': case 'x': case 'X': case 'o': {
            long long v;
            if (sz == 2) v = (long long)va64(va);
            else {
                uint32_t x = va32(va);
                if (cv == 'd' || cv == 'i') v = sz == -1 ? (short)x : sz == -2 ? (signed char)x : (int32_t)x;
                else v = sz == -1 ? (long long)(uint16_t)x : sz == -2 ? (long long)(uint8_t)x : (long long)x;
            }
            snprintf(spec, sizeof spec, "%%%s*.*ll%c", fl, cv);
            n = snprintf(tmp, sizeof tmp, spec, width, prec, v);
            fo_put(&o, tmp, n < 0 ? 0 : n < (int)sizeof tmp ? (size_t)n : sizeof tmp - 1);
            break;
        }
        case 'e': case 'E': case 'f': case 'F': case 'g': case 'G': case 'a': case 'A': {
            double d = vadbl(va);
            snprintf(spec, sizeof spec, "%%%s*.*%c", fl, cv);
            n = snprintf(tmp, sizeof tmp, spec, width, prec, d);
            fo_put(&o, tmp, n < 0 ? 0 : n < (int)sizeof tmp ? (size_t)n : sizeof tmp - 1);
            break;
        }
        case 'c': case 'C': {
            uint32_t ch = va32(va);
            int wide = cv == 'C' ? !va->wide : va->wide;
            if (wm == 1) wide = 1; else if (wm == -1) wide = 0;
            char cb[4]; int cl;
            if (wide) cl = u8enc(cb, ch & 0xFFFF); else { cb[0] = (char)ch; cl = 1; }
            if (!strchr(fl, '-')) fo_pad(&o, width - 1);
            fo_put(&o, cb, (size_t)cl);
            if (strchr(fl, '-')) fo_pad(&o, width - 1);
            break;
        }
        case 's': case 'S': {
            uint32_t p = va32(va);
            int wide = cv == 'S' ? !va->wide : va->wide;
            if (wm == 1) wide = 1; else if (wm == -1) wide = 0;
            const char* s;
            if (!p) s = "(null)";
            else if (wide) { if (!wbuf) wbuf = malloc(8192); hle_wstr_utf8(p, wbuf, 8192); s = wbuf; }
            else s = GSTR(p);
            size_t l = strlen(s);
            if (prec >= 0 && (size_t)prec < l) l = (size_t)prec;
            int pad = width > (int)l ? width - (int)l : 0;
            if (!strchr(fl, '-')) fo_pad(&o, pad);
            fo_put(&o, s, l);
            if (strchr(fl, '-')) fo_pad(&o, pad);
            break;
        }
        case 'p': n = snprintf(tmp, sizeof tmp, "%08X", va32(va)); fo_put(&o, tmp, (size_t)n); break;
        case 'n': { uint32_t p = va32(va); if (p) wr32(p, (uint32_t)o.len); break; }
        default: fo_put(&o, st, (size_t)(f - st)); break;
        }
    }
    if (o.cap) o.p[o.len < o.cap ? o.len : o.cap - 1] = 0;
    free(wbuf);
    return o.len;
}
#define FMT_CAP 65536
static void jp_sprintf(CPU* c) {
    char* b = malloc(FMT_CAP); GVa va = { c->esp + 12, 0 };
    size_t n = gfmt(b, FMT_CAP, GSTR(ARG(1)), &va);
    if (n >= FMT_CAP) n = FMT_CAP - 1;
    memcpy(M + ARG(0), b, n + 1);
    free(b);
    RET(n);
}
static void jp_printf(CPU* c) {
    char* b = malloc(FMT_CAP); GVa va = { c->esp + 8, 0 };
    size_t n = gfmt(b, FMT_CAP, GSTR(ARG(0)), &va);
    fprintf(stderr, "[fmod] %s", b);
    free(b);
    RET(n);
}
static void jp_snwprintf(CPU* c) {   /* _snwprintf(wchar_t* buf, size_t count, const wchar_t* fmt, ...) */
    uint32_t dst = ARG(0), cnt = ARG(1);
    char* fmt = malloc(8192); hle_wstr_utf8(ARG(2), fmt, 8192);
    char* b = malloc(FMT_CAP); GVa va = { c->esp + 16, 1 };
    gfmt(b, FMT_CAP, fmt, &va);
    uint32_t k = 0; const unsigned char* s = (const unsigned char*)b;
    while (*s) {
        uint32_t cp = *s++;
        if (cp >= 0xC0 && cp < 0xE0 && (s[0] & 0xC0) == 0x80) { cp = ((cp & 0x1F) << 6) | (s[0] & 0x3F); s++; }
        else if (cp >= 0xE0 && cp < 0xF0 && (s[0] & 0xC0) == 0x80 && (s[1] & 0xC0) == 0x80) { cp = ((cp & 0x0F) << 12) | ((s[0] & 0x3Fu) << 6) | (s[1] & 0x3F); s += 2; }
        else if (cp >= 0xF0 && (s[0] & 0xC0) == 0x80 && (s[1] & 0xC0) == 0x80 && (s[2] & 0xC0) == 0x80) {
            cp = ((cp & 0x07) << 18) | ((s[0] & 0x3Fu) << 12) | ((s[1] & 0x3Fu) << 6) | (s[2] & 0x3F); s += 3;
        }
        if (cp >= 0x10000) {
            cp -= 0x10000;
            if (k < cnt) wr16(dst + 2 * k, (uint16_t)(0xD800 | (cp >> 10)));
            k++;
            if (k < cnt) wr16(dst + 2 * k, (uint16_t)(0xDC00 | (cp & 0x3FF)));
            k++;
        } else { if (k < cnt) wr16(dst + 2 * k, (uint16_t)cp); k++; }
    }
    free(fmt); free(b);
    if (k < cnt) { wr16(dst + 2 * k, 0); RET(k); }
    else if (k == cnt) RET(k);
    else RET(0xFFFFFFFFu);
}
/* sscanf: rewrite the MSVC format for the host (long is 32-bit there, I64, %p) and pass host pointers */
static void jp_sscanf(CPU* c) {
    const char* s = GSTR(ARG(0)); const char* f = GSTR(ARG(1));
    char nf[1024]; size_t o = 0;
    while (*f && o < sizeof nf - 8) {
        if (*f != '%') { nf[o++] = *f++; continue; }
        nf[o++] = *f++;
        if (*f == '%') { nf[o++] = *f++; continue; }
        if (*f == '*') nf[o++] = *f++;
        while (*f >= '0' && *f <= '9' && o < sizeof nf - 8) nf[o++] = *f++;
        int L = 0, H = 0;
        for (;;) {
            if (f[0] == 'I' && f[1] == '6' && f[2] == '4') { L = 2; f += 3; }
            else if (f[0] == 'I' && f[1] == '3' && f[2] == '2') f += 3;
            else if (f[0] == 'l' && f[1] == 'l') { L = 2; f += 2; }
            else if (f[0] == 'l' || f[0] == 'L' || f[0] == 'w') { if (L < 1) L = 1; f++; }
            else if (f[0] == 'h' && f[1] == 'h') { H = 2; f += 2; }
            else if (f[0] == 'h') { H = 1; f++; }
            else break;
        }
        char cv = *f;
        if (!cv) break;
        f++;
        if (cv == '[') {
            nf[o++] = '[';
            if (*f == '^') nf[o++] = *f++;
            if (*f == ']') nf[o++] = *f++;
            while (*f && *f != ']' && o < sizeof nf - 4) nf[o++] = *f++;
            if (*f == ']') nf[o++] = *f++;
            continue;
        }
        if (cv == 'p') cv = 'x';
        if (L == 2) { nf[o++] = 'l'; nf[o++] = 'l'; }
        else if (L == 1 && strchr("eEfgGaA", cv)) nf[o++] = 'l';
        else if (H == 1) nf[o++] = 'h';
        else if (H == 2) { nf[o++] = 'h'; nf[o++] = 'h'; }
        nf[o++] = cv;
    }
    nf[o] = 0;
    void* p[16];
    for (int i = 0; i < 16; i++) p[i] = M + rd32(c->esp + 12 + 4 * i);
    RET(sscanf(s, nf, p[0], p[1], p[2], p[3], p[4], p[5], p[6], p[7], p[8], p[9], p[10], p[11], p[12], p[13], p[14], p[15]));
}
static void jp_itoa(CPU* c) {
    int32_t v = (int32_t)ARG(0); uint32_t buf = ARG(1); int radix = (int)ARG(2);
    char t[40]; int n = 0, neg = 0; uint32_t u;
    if (radix < 2 || radix > 36) radix = 10;
    if (radix == 10 && v < 0) { neg = 1; u = (uint32_t)(-(int64_t)v); } else u = (uint32_t)v;
    do { int d = (int)(u % (uint32_t)radix); t[n++] = (char)(d < 10 ? '0' + d : 'a' + d - 10); u /= (uint32_t)radix; } while (u);
    char* o = (char*)M + buf; int k = 0;
    if (neg) o[k++] = '-';
    while (n) o[k++] = t[--n];
    o[k] = 0;
    RET(buf);
}
static void jp_atoi(CPU* c) { RET((uint32_t)atoi(GSTR(ARG(0)))); }
static void jp_abs(CPU* c) { int32_t v = (int32_t)ARG(0); RET((uint32_t)(v < 0 ? -v : v)); }
static void jp_time(CPU* c) { uint32_t t = (uint32_t)time(NULL); if (ARG(0)) wr32(ARG(0), t); RET(t); }
static void jp_getenv(CPU* c) { RET(0); }
static void jp_wcstoul(CPU* c) {
    uint32_t p = ARG(0), endp = ARG(1); int base = (int)ARG(2);
    char t[128]; int n = 0;
    for (; n < 127; n++) { uint16_t ch = rd16(p + 2 * (uint32_t)n); if (!ch || ch > 0x7F) break; t[n] = (char)ch; }
    t[n] = 0;
    char* e; unsigned long long v = strtoull(t, &e, base);
    if (endp) wr32(endp, p + 2 * (uint32_t)(e - t));
    RET((uint32_t)(v > 0xFFFFFFFFull ? 0xFFFFFFFFu : v));
}
static void jp_wcstombs(CPU* c) {
    uint32_t d = ARG(0), s = ARG(1), n = ARG(2), k = 0;
    for (;;) {
        uint16_t ch = rd16(s + 2 * k);
        if (!ch) { if (d && k < n) M[d + k] = 0; break; }
        if (d) { if (k >= n) break; M[d + k] = (uint8_t)(ch < 0x80 ? ch : '?'); }
        k++;
    }
    RET(k);
}
static void jp_fullpath(CPU* c) {
    uint32_t ab = ARG(0), rel = ARG(1), max = ARG(2);
    if (!ab) { max = 260; ab = ghalloc(max, 1); }
    uint32_t a[4] = { rel, max, ab, 0 };
    uint32_t r = hle_call("kernel32.dll", "GetFullPathNameA", 4, a);
    RET(r && r < max ? ab : 0);
}
static void jp_stat(CPU* c) {   /* struct _stat (32-bit time_t): mode@6 nlink@8 size@20 atime@24 mtime@28 ctime@32 */
    uint32_t path = ARG(0), buf = ARG(1), tmp = c->esp - 1536;
    uint32_t a[4] = { path, 1024, tmp, 0 };
    uint32_t r = hle_call("kernel32.dll", "GetFullPathNameA", 4, a);
    char host[1200]; struct stat st;
    if (!r || r >= 1024 || !hle_fs_host_path(GSTR(tmp), host, sizeof host) || stat(host, &st)) { set_errno(2); RET(0xFFFFFFFFu); return; }
    memset(M + buf, 0, 36);
    wr16(buf + 6, S_ISDIR(st.st_mode) ? 0x41FF : 0x81B6);
    wr16(buf + 8, 1);
    wr32(buf + 20, (uint32_t)st.st_size);
    wr32(buf + 24, (uint32_t)st.st_atime); wr32(buf + 28, (uint32_t)st.st_mtime); wr32(buf + 32, (uint32_t)st.st_ctime);
    RET(0);
}

/* ---- stdio on top of the kernel32 HLE: FILE = { magic, HANDLE, eof } in guest memory ---- */
#define JFMAGIC 0x3146504Au
static int is_std(uint32_t f) { return g_iob && f >= g_iob && f < g_iob + 96; }
static int jf_ok(uint32_t f) { return f && !is_std(f) && rd32(f) == JFMAGIC; }
static void jp_fopen(CPU* c) {
    uint32_t name = ARG(0); const char* mode = GSTR(ARG(1));
    uint32_t acc = 0x80000000u, disp = 3;
    if (strchr(mode, 'w')) { acc = 0x40000000u; disp = 2; }
    else if (strchr(mode, 'a')) { acc = 0x40000000u; disp = 4; }
    if (strchr(mode, '+')) acc = 0xC0000000u;
    int app = strchr(mode, 'a') != NULL;
    uint32_t a[7] = { name, acc, 3, 0, disp, 0x80, 0 };
    uint32_t h = hle_call("kernel32.dll", "CreateFileA", 7, a);
    if (h == 0xFFFFFFFFu || !h) { set_errno(2); RET(0); return; }
    if (app) { uint32_t s[4] = { h, 0, 0, 2 }; hle_call("kernel32.dll", "SetFilePointer", 4, s); }
    uint32_t f = ghalloc(32, 1);
    wr32(f, JFMAGIC); wr32(f + 4, h);
    RET(f);
}
static void jp_fread(CPU* c) {
    uint32_t buf = ARG(0), sz = ARG(1), n = ARG(2), f = ARG(3);
    uint64_t want = (uint64_t)sz * n;
    if (!want || want > 0x7FFFFFFFu || !jf_ok(f)) { RET(0); return; }
    uint32_t got_va = c->esp - 256;
    wr32(got_va, 0);
    uint32_t a[5] = { rd32(f + 4), buf, (uint32_t)want, got_va, 0 };
    hle_call("kernel32.dll", "ReadFile", 5, a);
    uint32_t got = rd32(got_va);
    if (got < want) wr32(f + 8, 1);
    RET(got / sz);
}
static void jp_fwrite(CPU* c) {
    uint32_t buf = ARG(0), sz = ARG(1), n = ARG(2), f = ARG(3);
    uint64_t want = (uint64_t)sz * n;
    if (!want || want > 0x7FFFFFFFu) { RET(0); return; }
    if (is_std(f)) { fwrite(M + buf, 1, (size_t)want, stderr); RET(n); return; }
    if (!jf_ok(f)) { RET(0); return; }
    uint32_t got_va = c->esp - 256;
    wr32(got_va, 0);
    uint32_t a[5] = { rd32(f + 4), buf, (uint32_t)want, got_va, 0 };
    hle_call("kernel32.dll", "WriteFile", 5, a);
    RET(rd32(got_va) / sz);
}
static void jp_fseek(CPU* c) {
    uint32_t f = ARG(0), off = ARG(1), org = ARG(2);
    if (!jf_ok(f)) { RET(0xFFFFFFFFu); return; }
    uint32_t a[4] = { rd32(f + 4), off, 0, org };
    uint32_t r = hle_call("kernel32.dll", "SetFilePointer", 4, a);
    if (r == 0xFFFFFFFFu) { RET(0xFFFFFFFFu); return; }
    wr32(f + 8, 0);
    RET(0);
}
static void jp_ftell(CPU* c) {
    uint32_t f = ARG(0);
    if (!jf_ok(f)) { RET(0xFFFFFFFFu); return; }
    uint32_t a[4] = { rd32(f + 4), 0, 0, 1 };
    uint32_t r = hle_call("kernel32.dll", "SetFilePointer", 4, a);
    RET(r);
}
static void jp_fclose(CPU* c) {
    uint32_t f = ARG(0);
    if (!jf_ok(f)) { RET(0xFFFFFFFFu); return; }
    uint32_t a[1] = { rd32(f + 4) };
    hle_call("kernel32.dll", "CloseHandle", 1, a);
    wr32(f, 0); ghfree(f);
    RET(0);
}

/* ---- threads, qsort ---- */
static void jp_beginthreadex(CPU* c) {   /* same argument order as CreateThread */
    uint32_t a[6] = { ARG(0), ARG(1), ARG(2), ARG(3), ARG(4), ARG(5) };
    uint32_t r = hle_call("kernel32.dll", "CreateThread", 6, a);
    RET(r);
}
static void jp_endthreadex(CPU* c) { uint32_t a[1] = { ARG(0) }; hle_call("kernel32.dll", "ExitThread", 1, a); }
static void jp_qsort(CPU* c) {   /* insertion sort with in-place swaps: the guest comparator only sees guest pointers */
    uint32_t base = ARG(0), num = ARG(1), w = ARG(2), cmp = ARG(3);
    if (num < 2 || !w) return;
    if (w > 1024) { fprintf(stderr, "[jp] qsort: element size %u not supported\n", w); return; }
    uint8_t t[1024];
    for (uint32_t i = 1; i < num; i++)
        for (uint32_t j = i; j > 0; j--) {
            uint32_t a[2] = { base + (j - 1) * w, base + j * w };
            if ((int32_t)gcall(c, cmp, 2, a) <= 0) break;
            memcpy(t, M + a[0], w); memmove(M + a[0], M + a[1], w); memcpy(M + a[1], t, w);
        }
}

#include "hle_jp_d3dx.inc"
#include "hle_jp_win.inc"   /* v3: the remaining Win32 imports */

/* ---- table ---- */
#define C(n, f) { "msvcrt.dll", #n, 0, f, 0 },
#define CN(n, f) { "msvcrt.dll", #n, 0, f, HLE_NOLOCK },
#define X(n, a) { "d3dx9_41.dll", "D3DX" #n, a, x_##n, HLE_NOLOCK },
const HleDef hle_jp_defs[] = {
    JPW_DEFS
    C(malloc, jp_malloc) C(calloc, jp_calloc) C(realloc, jp_realloc) C(free, jp_free)
    C(fopen, jp_fopen) C(fclose, jp_fclose) C(_fullpath, jp_fullpath)
    CN(memchr, jp_memchr) CN(memset, jp_memset) CN(memcpy, jp_memcpy) CN(memmove, jp_memcpy) CN(memcmp, jp_memcmp)
    CN(strlen, jp_strlen) CN(strcpy, jp_strcpy) CN(strcat, jp_strcat) CN(strcmp, jp_strcmp) CN(strncpy, jp_strncpy)
    CN(_errno, jp_errno) CN(__dllonexit, jp_dllonexit) CN(_onexit, jp_onexit) CN(_initterm, jp_initterm)
    CN(__CxxFrameHandler, jp_cxxframe) CN(_purecall, jp_purecall) CN(_adjust_fdiv, jp_nop) CN(_iob, jp_nop)
    CN(sprintf, jp_sprintf) CN(printf, jp_printf) CN(_snwprintf, jp_snwprintf) CN(sscanf, jp_sscanf)
    CN(_itoa, jp_itoa) CN(atoi, jp_atoi) CN(abs, jp_abs) CN(wcstoul, jp_wcstoul) CN(wcstombs, jp_wcstombs)
    CN(sin, jp_sin) CN(cos, jp_cos) CN(tan, jp_tan) CN(pow, jp_pow) CN(fabs, jp_fabs) CN(sqrt, jp_sqrt)
    CN(log, jp_log) CN(log10, jp_log10) CN(exp, jp_exp) CN(atan, jp_atan) CN(atan2, jp_atan2) CN(floor, jp_floor)
    CN(acos, jp_acos) CN(ldexp, jp_ldexp) CN(_ftol, jp_ftol)
    CN(fread, jp_fread) CN(fwrite, jp_fwrite) CN(fseek, jp_fseek) CN(ftell, jp_ftell) CN(_stat, jp_stat)
    CN(time, jp_time) CN(getenv, jp_getenv)
    CN(_beginthreadex, jp_beginthreadex) CN(_endthreadex, jp_endthreadex) CN(qsort, jp_qsort)
    X(CreateTexture, 8) X(LoadSurfaceFromSurface, 8) X(SaveTextureToFileInMemory, 4) X(CreateTextureFromFileInMemoryEx, 15)
    X(CreateFontA, 12) X(GetShaderVersion, 1) X(CheckTextureRequirements, 7)
};
const unsigned hle_jp_ndefs = sizeof hle_jp_defs / sizeof hle_jp_defs[0];

/* fmodex.dll data imports from msvcrt: give them real guest data instead of thunks */
void hle_jp_fix_data_imports(Module* m) {
    errno_va();
    for (unsigned i = 0; i < m->nimports; i++) {
        const ImportEntry* im = &m->imports[i];
        if (!im->dll || !im->name || strcasecmp(im->dll, "msvcrt.dll")) continue;
        if (!strcmp(im->name, "_adjust_fdiv")) wr32(im->slot, ghalloc(4, 1));
        else if (!strcmp(im->name, "_iob")) { if (!g_iob) g_iob = ghalloc(96, 1); wr32(im->slot, g_iob); }
    }
}
