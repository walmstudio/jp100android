/* hle_jp_win_a.inc - JP100 v3, part a: helpers + kernel32.
 * The v2 run died in CRT startup on KERNEL32!HeapSetInformation. v3 resolves every remaining Win32 import
 * of JurassicPark100.exe and fmodex.dll (except WLDAP32). tools/jp_win/gen_jp_win.py concatenates parts a/b/c
 * with generated logged stubs into runtime/hle_jp_win.inc, which hle_jp.c includes after hle_jp_d3dx.inc. */
#include <ctype.h>
#include <unistd.h>
#include <pthread.h>

static inline uint16_t jw_r16(uint32_t a) { uint16_t v; memcpy(&v, M + a, 2); return v; }
static inline void jw_w16(uint32_t a, uint16_t v) { memcpy(M + a, &v, 2); }
static uint32_t jw_wlen(uint32_t p) { uint32_t n = 0; if (p) while (jw_r16(p + 2 * n)) n++; return n; }
static void jw_w2a(uint32_t p, char* o, size_t cap) {
    size_t i = 0;
    if (p) for (; i + 1 < cap; i++) { uint16_t ch = jw_r16(p + 2 * i); if (!ch) break; o[i] = ch < 0x100 ? (char)ch : '?'; }
    o[i] = 0;
}
static void jw_a2w(const char* s, uint32_t dst, uint32_t n) { for (uint32_t i = 0; i < n; i++) jw_w16(dst + 2 * i, (uint8_t)s[i]); jw_w16(dst + 2 * n, 0); }
/* call another HLE'd import by name / a guest function (the calling handler must be HLE_NOLOCK) */
static uint32_t jw_call(CPU* c, const char* dll, const char* name, int n, const uint32_t* a) {
    uint32_t sp = c->esp; uint32_t r = hle_call(dll, name, n, a); c->esp = sp; return r;
}
static uint32_t jw_gcall(CPU* c, uint32_t f, int n, const uint32_t* a) { uint32_t sp = c->esp; uint32_t r = rt_call(c, f, n, a); c->esp = sp; return r; }
static void jw_once(volatile int* f, const char* what) { if (!*f) { *f = 1; fprintf(stderr, "[hle] v3 %s\n", what); } }

/* ---------------- kernel32 ---------------- */
static void jw_GlobalMemoryStatusEx(CPU* c) {
    uint32_t p = ARG(0); if (!p) { hle_set_last_error(c, 87); RET(0); return; }
    uint64_t v[7] = { 4ull << 30, 2ull << 30, 8ull << 30, 6ull << 30, 0x7FFE0000ull, 0x60000000ull, 0 };
    wr32(p + 4, 50); memcpy(M + p + 8, v, sizeof v); RET(1);
}
static void jw_lstrcmpA(CPU* c) {
    const char* a = ARG(0) ? GSTR(ARG(0)) : ""; const char* b = ARG(1) ? GSTR(ARG(1)) : "";
    int r = strcmp(a, b); RET(r < 0 ? -1 : r > 0);
}
static void jw_lstrcmpW(CPU* c) {
    uint32_t a = ARG(0), b = ARG(1); int r = 0;
    for (uint32_t i = 0;; i++) {
        uint16_t x = a ? jw_r16(a + 2 * i) : 0, y = b ? jw_r16(b + 2 * i) : 0;
        if (x != y) { r = x < y ? -1 : 1; break; }
        if (!x) break;
    }
    RET(r);
}
static void jw_MulDiv(CPU* c) {
    int64_t a = (int32_t)ARG(0), b = (int32_t)ARG(1), d = (int32_t)ARG(2), r;
    if (!d) { RET(-1); return; }
    if (d < 0) { a = -a; d = -d; }
    int64_t p = a * b;
    r = p >= 0 ? (p + d / 2) / d : (p - d / 2) / d;
    RET((r > 2147483647LL || r < -2147483647LL) ? -1 : (int32_t)r);
}
static void jw_lstrcpynA(CPU* c) {
    uint32_t d = ARG(0), s = ARG(1); int32_t n = (int32_t)ARG(2);
    if (d && n > 0) { int32_t i = 0; if (s) for (; i < n - 1 && M[s + i]; i++) M[d + i] = M[s + i]; M[d + i] = 0; }
    RET(d);
}
static void jw_LoadLibraryExA(CPU* c) {
    uint32_t a[1] = { ARG(0) };
    if (ARG(2) & 0x62) { hle_set_last_error(c, 2); RET(0); return; }   /* as-datafile / image-resource loads: not supported */
    uint32_t r = jw_call(c, "kernel32.dll", "LoadLibraryA", 1, a); RET(r);
}
static void jw_LoadLibraryExW(CPU* c) {
    uint32_t a[1] = { ARG(0) };
    if (ARG(2) & 0x62) { hle_set_last_error(c, 2); RET(0); return; }
    uint32_t r = jw_call(c, "kernel32.dll", "LoadLibraryW", 1, a); RET(r);
}
static uint32_t jw_fold(uint32_t ch) {
    if (ch >= 'A' && ch <= 'Z') return ch + 32;
    if (ch >= 0xC0 && ch <= 0xDE && ch != 0xD7) return ch + 32;
    if (ch >= 0x410 && ch <= 0x42F) return ch + 32;
    if (ch >= 0x400 && ch <= 0x40F) return ch + 80;
    return ch;
}
static void jw_CompareStringW(CPU* c) {
    uint32_t fl = ARG(1), s1 = ARG(2), s2 = ARG(4); int32_t n1 = (int32_t)ARG(3), n2 = (int32_t)ARG(5);
    if (!s1 || !s2) { hle_set_last_error(c, 87); RET(0); return; }
    if (n1 < 0) n1 = (int32_t)jw_wlen(s1);
    if (n2 < 0) n2 = (int32_t)jw_wlen(s2);
    int ic = (fl & 0x11) != 0, r = 0;
    for (int32_t i = 0; !r && (i < n1 || i < n2); i++) {
        if (i >= n1) { r = -1; break; }
        if (i >= n2) { r = 1; break; }
        uint32_t x = jw_r16(s1 + 2 * i), y = jw_r16(s2 + 2 * i);
        if (ic) { x = jw_fold(x); y = jw_fold(y); }
        if (x != y) r = x < y ? -1 : 1;
    }
    RET(r < 0 ? 1 : r > 0 ? 3 : 2);
}

/* GetDateFormat / GetTimeFormat (en-US pictures) */
static const char* const jw_mon[12] = { "January", "February", "March", "April", "May", "June", "July", "August", "September", "October", "November", "December" };
static const char* const jw_wd[7] = { "Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday" };
typedef struct { int y, mo, d, h, mi, s; } JwTm;
static int jw_dow(int y, int m, int d) { static const int t[] = { 0, 3, 2, 5, 0, 3, 5, 1, 4, 6, 2, 4 }; if (m < 3) y--; return (y + y / 4 - y / 100 + y / 400 + t[m - 1] + d) % 7; }
static int jw_get_tm(uint32_t st, JwTm* t, int need_date, int need_time) {
    if (!st) {
        time_t now = time(NULL); struct tm lt; localtime_r(&now, &lt);
        t->y = lt.tm_year + 1900; t->mo = lt.tm_mon + 1; t->d = lt.tm_mday; t->h = lt.tm_hour; t->mi = lt.tm_min; t->s = lt.tm_sec; return 1;
    }
    t->y = jw_r16(st); t->mo = jw_r16(st + 2); t->d = jw_r16(st + 6); t->h = jw_r16(st + 8); t->mi = jw_r16(st + 10); t->s = jw_r16(st + 12);
    if (need_date && (t->mo < 1 || t->mo > 12 || t->d < 1 || t->d > 31 || t->y < 1601)) return 0;
    if (need_time && (t->h > 23 || t->mi > 59 || t->s > 59)) return 0;
    return 1;
}
static void jw_put(char* o, int* n, int cap, const char* s) { while (*s) { if (*n < cap - 1) o[*n] = *s; (*n)++; s++; } }
static void jw_num(char* o, int* n, int cap, int v, int w) { char b[16]; snprintf(b, sizeof b, "%0*d", w, v); jw_put(o, n, cap, b); }
static int jw_fmt(const JwTm* t, const char* f, int is_time, uint32_t flags, char* o, int cap) {
    int n = 0;
    while (*f) {
        char ch = *f;
        if (ch == '\'') {
            f++;
            while (*f) {
                if (*f == '\'') { if (f[1] == '\'') { jw_put(o, &n, cap, "'"); f += 2; continue; } f++; break; }
                char s[2] = { *f++, 0 }; jw_put(o, &n, cap, s);
            }
            continue;
        }
        int k = 1; while (f[k] == ch) k++;
        char b[16];
        if (!is_time && ch == 'd') { if (k <= 2) jw_num(o, &n, cap, t->d, k); else { snprintf(b, sizeof b, k == 3 ? "%.3s" : "%s", jw_wd[jw_dow(t->y, t->mo, t->d)]); jw_put(o, &n, cap, b); } }
        else if (!is_time && ch == 'M') { if (k <= 2) jw_num(o, &n, cap, t->mo, k); else { snprintf(b, sizeof b, k == 3 ? "%.3s" : "%s", jw_mon[t->mo - 1]); jw_put(o, &n, cap, b); } }
        else if (!is_time && ch == 'y') { if (k <= 2) jw_num(o, &n, cap, t->y % 100, k); else jw_num(o, &n, cap, t->y, 4); }
        else if (!is_time && ch == 'g') jw_put(o, &n, cap, "A.D.");
        else if (is_time && (ch == 'h' || ch == 'H')) { int h = t->h; if (ch == 'h' && !(flags & 8)) { h %= 12; if (!h) h = 12; } jw_num(o, &n, cap, h, k >= 2 ? 2 : 1); }
        else if (is_time && ch == 'm') jw_num(o, &n, cap, t->mi, k >= 2 ? 2 : 1);
        else if (is_time && ch == 's') jw_num(o, &n, cap, t->s, k >= 2 ? 2 : 1);
        else if (is_time && ch == 't') { if (!(flags & 12)) jw_put(o, &n, cap, k == 1 ? (t->h < 12 ? "A" : "P") : (t->h < 12 ? "AM" : "PM")); }
        else { for (int i = 0; i < k; i++) { char s[2] = { ch, 0 }; jw_put(o, &n, cap, s); } }
        f += k;
    }
    if (cap > 0) o[n < cap ? n : cap - 1] = 0;
    return n;
}
static const char* jw_deffmt(int is_time, uint32_t fl) {
    if (!is_time) return (fl & 2) ? "dddd, MMMM d, yyyy" : (fl & 8) ? "MMMM yyyy" : "M/d/yyyy";
    int h24 = fl & 8, nomark = fl & 4;
    if (fl & 1) return h24 ? "HH" : nomark ? "h" : "h tt";
    if (fl & 2) return h24 ? "HH:mm" : nomark ? "h:mm" : "h:mm tt";
    return h24 ? "HH:mm:ss" : nomark ? "h:mm:ss" : "h:mm:ss tt";
}
static void jw_datetime(CPU* c, int is_time, int wide) {
    uint32_t fl = ARG(1), st = ARG(2), fmt = ARG(3), out = ARG(4); int32_t cap = (int32_t)ARG(5);
    JwTm t; char f[256], buf[512];
    if (!jw_get_tm(st, &t, !is_time, is_time) || cap < 0 || (cap && !out)) { hle_set_last_error(c, 87); RET(0); return; }
    if (fmt) { if (wide) jw_w2a(fmt, f, sizeof f); else snprintf(f, sizeof f, "%s", GSTR(fmt)); }
    else snprintf(f, sizeof f, "%s", jw_deffmt(is_time, fl));
    int n = jw_fmt(&t, f, is_time, fmt ? fl : 0, buf, sizeof buf);
    if (n > (int)sizeof buf - 1) n = (int)sizeof buf - 1;
    if (!cap) { RET(n + 1); return; }
    if (n + 1 > cap) { hle_set_last_error(c, 122); RET(0); return; }
    if (wide) jw_a2w(buf, out, (uint32_t)n); else { memcpy(M + out, buf, (size_t)n); M[out + n] = 0; }
    RET(n + 1);
}
static void jw_GetDateFormatA(CPU* c) { jw_datetime(c, 0, 0); }
static void jw_GetDateFormatW(CPU* c) { jw_datetime(c, 0, 1); }
static void jw_GetTimeFormatA(CPU* c) { jw_datetime(c, 1, 0); }
static void jw_GetTimeFormatW(CPU* c) { jw_datetime(c, 1, 1); }
