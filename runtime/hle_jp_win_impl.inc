/* hle_jp_win_impl.inc - JP100 v3: Win32 imports of JurassicPark100.exe / fmodex.dll that had no HLE.
 * Included by hle_exe.c before its table. Real implementations for calls whose results matter
 * (CRT startup, locale/time formatting, path helpers, SLists, BSTR/COM memory, sockets helpers);
 * hle_jp_win_gen.inc adds logged failure stubs (correct stdcall argument counts) and JPW_DEFS. */
#include <strings.h>

static inline uint16_t jw_r16(uint32_t a) { uint16_t v; memcpy(&v, M + a, 2); return v; }
static inline void jw_w16(uint32_t a, uint16_t v) { memcpy(M + a, &v, 2); }
static uint32_t jw_wlen(uint32_t p) { uint32_t n = 0; if (p) while (jw_r16(p + 2 * n)) n++; return n; }
static void jw_w2a(uint32_t p, char* o, size_t cap) {
    size_t i = 0;
    if (p) for (; i + 1 < cap; i++) { uint16_t ch = jw_r16(p + 2 * i); if (!ch) break; o[i] = ch < 0x100 ? (char)ch : '?'; }
    o[i] = 0;
}
static void jw_a2w(const char* s, uint32_t dst, uint32_t n) { for (uint32_t i = 0; i < n; i++) jw_w16(dst + 2 * i, (uint8_t)s[i]); jw_w16(dst + 2 * n, 0); }
/* call another HLE'd import by name / a guest function; the caller must be HLE_NOLOCK */
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

static void jw_GetVolumeInformationW(CPU* c) {
    uint32_t vn = ARG(1), vns = ARG(2), ser = ARG(3), mcl = ARG(4), fsf = ARG(5), fsn = ARG(6), fsns = ARG(7);
    if (vn && vns) jw_w16(vn, 0);
    if (ser) wr32(ser, 0x4A503130);
    if (mcl) wr32(mcl, 255);
    if (fsf) wr32(fsf, 7);
    if (fsn && fsns >= 5) jw_a2w("NTFS", fsn, 4);
    RET(1);
}
static void jw_GetSystemDirectoryW(CPU* c) {
    static const char d[] = "C:\\Windows\\system32"; uint32_t n = sizeof d - 1, b = ARG(0), sz = ARG(1);
    if (b && sz > n) { jw_a2w(d, b, n); RET(n); } else RET(n + 1);
}
static void jw_SystemTimeToTzSpecificLocalTime(CPU* c) {
    uint32_t u = ARG(1), l = ARG(2); if (!u || !l) { hle_set_last_error(c, 87); RET(0); return; }
    struct tm tm; memset(&tm, 0, sizeof tm);
    tm.tm_year = jw_r16(u) - 1900; tm.tm_mon = jw_r16(u + 2) - 1; tm.tm_mday = jw_r16(u + 6);
    tm.tm_hour = jw_r16(u + 8); tm.tm_min = jw_r16(u + 10); tm.tm_sec = jw_r16(u + 12);
    uint16_t ms = jw_r16(u + 14);
    time_t t = timegm(&tm); struct tm lt; localtime_r(&t, &lt);
    jw_w16(l, (uint16_t)(lt.tm_year + 1900)); jw_w16(l + 2, (uint16_t)(lt.tm_mon + 1)); jw_w16(l + 4, (uint16_t)lt.tm_wday); jw_w16(l + 6, (uint16_t)lt.tm_mday);
    jw_w16(l + 8, (uint16_t)lt.tm_hour); jw_w16(l + 10, (uint16_t)lt.tm_min); jw_w16(l + 12, (uint16_t)lt.tm_sec); jw_w16(l + 14, ms);
    RET(1);
}
static void jw_GlobalSize(CPU* c) { RET(ARG(0) ? ghsize(ARG(0)) : 0); }   /* GlobalAlloc hands out ghalloc blocks */
static void jw_GetProfileStringW(CPU* c) {
    uint32_t def = ARG(2), out = ARG(3), sz = ARG(4), n = def ? jw_wlen(def) : 0;
    if (!out || !sz) { RET(0); return; }
    if (n > sz - 1) n = sz - 1;
    for (uint32_t i = 0; i < n; i++) jw_w16(out + 2 * i, jw_r16(def + 2 * i));
    jw_w16(out + 2 * n, 0); RET(n);
}
static void jw_GetThreadTimes(CPU* c) {
    for (int i = 1; i <= 4; i++) if (ARG(i)) { wr32(ARG(i), 0); wr32(ARG(i) + 4, 0); }
    if (ARG(4) && ARG(0) == 0xFFFFFFFEu) {
        struct timespec ts; clock_gettime(CLOCK_THREAD_CPUTIME_ID, &ts);
        uint64_t v = (uint64_t)ts.tv_sec * 10000000ull + (uint64_t)ts.tv_nsec / 100;
        wr32(ARG(4), (uint32_t)v); wr32(ARG(4) + 4, (uint32_t)(v >> 32));
    }
    RET(1);
}
static void jw_VirtualProtect(CPU* c) { if (ARG(3)) wr32(ARG(3), 0x40); RET(1); }   /* recompiled code never runs from guest pages */
static void jw_FindFirstFileExA(CPU* c) {
    uint32_t a[2] = { ARG(0), ARG(2) };
    if (ARG(1) > 1) { hle_set_last_error(c, 87); RET(0xFFFFFFFFu); return; }
    uint32_t r = jw_call(c, "kernel32.dll", "FindFirstFileA", 2, a); RET(r);
}
static pthread_mutex_t jw_slist_mx = PTHREAD_MUTEX_INITIALIZER;
static void jw_InterlockedPushEntrySList(CPU* c) {
    uint32_t h = ARG(0), e = ARG(1);
    pthread_mutex_lock(&jw_slist_mx);
    uint32_t first = rd32(h); wr32(e, first); wr32(h, e);
    jw_w16(h + 4, (uint16_t)(jw_r16(h + 4) + 1)); jw_w16(h + 6, (uint16_t)(jw_r16(h + 6) + 1));
    pthread_mutex_unlock(&jw_slist_mx);
    RET(first);
}
static void jw_InterlockedPopEntrySList(CPU* c) {
    uint32_t h = ARG(0);
    pthread_mutex_lock(&jw_slist_mx);
    uint32_t first = rd32(h);
    if (first) { wr32(h, rd32(first)); jw_w16(h + 4, (uint16_t)(jw_r16(h + 4) - 1)); jw_w16(h + 6, (uint16_t)(jw_r16(h + 6) + 1)); }
    pthread_mutex_unlock(&jw_slist_mx);
    RET(first);
}
/* waitable timers (fmodex): a manual-reset, already signaled event; SetWaitableTimer itself sleeps the due time */
static void jw_CreateWaitableTimerA(CPU* c) {
    static volatile int once; jw_once(&once, "CreateWaitableTimerA -> signaled event, SetWaitableTimer sleeps");
    uint32_t a[4] = { ARG(0), 1, 1, ARG(2) };
    uint32_t r = jw_call(c, "kernel32.dll", "CreateEventA", 4, a); RET(r);
}
static void jw_SetWaitableTimer(CPU* c) {
    uint32_t p = ARG(1); int64_t due = 0;
    if (p) due = (int64_t)(((uint64_t)rd32(p + 4) << 32) | rd32(p));
    if (due < 0) { int64_t us = -due / 10; if (us > 1000000) us = 1000000; if (us > 0) usleep((useconds_t)us); }
    RET(1);
}

/* ---------------- advapi32 ---------------- */
static void jw_ReportEventA(CPU* c) {
    uint32_t n = ARG(5) & 0xFFFF, s = ARG(7);
    fprintf(stderr, "[hle] ReportEventA type=%u id=%08x:", ARG(1) & 0xFFFF, ARG(3));
    for (uint32_t i = 0; i < n && i < 8 && s; i++) { uint32_t p = rd32(s + 4 * i); fprintf(stderr, " \"%.300s\"", p ? GSTR(p) : ""); }
    fprintf(stderr, "\n"); RET(1);
}
static void jw_RegQueryInfoKeyW(CPU* c) {   /* an empty key */
    static const int outs[] = { 2, 4, 5, 6, 7, 8, 9, 10 };
    if (ARG(1) && ARG(2) && rd32(ARG(2))) jw_w16(ARG(1), 0);
    for (unsigned i = 0; i < sizeof outs / sizeof outs[0]; i++) if (ARG(outs[i])) wr32(ARG(outs[i]), 0);
    if (ARG(11)) { wr32(ARG(11), 0); wr32(ARG(11) + 4, 0); }
    RET(0);
}

/* ---------------- oleaut32 / ole32 ---------------- */
static void jw_SysAllocStringLen(CPU* c) {   /* same layout as oa_SysAllocString: 4-byte byte length, then UTF-16 + NUL */
    uint32_t s = ARG(0), n = ARG(1);
    if (n > 0x3FFFFFF0u) { RET(0); return; }
    uint32_t p = ghalloc(4 + 2 * n + 2, 0); if (!p) { RET(0); return; }
    wr32(p, 2 * n);
    if (s) memmove(M + p + 4, M + s, 2 * (size_t)n); else memset(M + p + 4, 0, 2 * (size_t)n);
    jw_w16(p + 4 + 2 * n, 0); RET(p + 4);
}
static void jw_SysStringLen(CPU* c) { RET(ARG(0) ? rd32(ARG(0) - 4) / 2 : 0); }
static void jw_VariantInit(CPU* c) { if (ARG(0)) jw_w16(ARG(0), 0); RET(0); }
static void jw_VariantClear(CPU* c) {
    uint32_t v = ARG(0); if (!v) { RET(0); return; }
    if (jw_r16(v) == 8 && rd32(v + 8)) ghfree(rd32(v + 8) - 4);   /* VT_BSTR */
    jw_w16(v, 0); RET(0);
}
static void jw_VarUI4FromStr(CPU* c) {
    uint32_t s = ARG(0), out = ARG(3), i = 0, any = 0; uint64_t v = 0;
    if (!s || !out) { RET(0x80070057u); return; }
    while (jw_r16(s + 2 * i) == ' ') i++;
    for (uint16_t ch; (ch = jw_r16(s + 2 * i)) >= '0' && ch <= '9'; i++) { v = v * 10 + (uint64_t)(ch - '0'); any = 1; if (v > 0xFFFFFFFFull) { RET(0x8002000Au); return; } }
    while (jw_r16(s + 2 * i) == ' ') i++;
    if (!any || jw_r16(s + 2 * i)) { RET(0x80020005u); return; }
    wr32(out, (uint32_t)v); RET(0);
}
static int jw_hex(int ch) { return ch >= '0' && ch <= '9' ? ch - '0' : ch >= 'a' && ch <= 'f' ? ch - 'a' + 10 : ch >= 'A' && ch <= 'F' ? ch - 'A' + 10 : -1; }
static void jw_CLSIDFromString(CPU* c) {
    static const int pos[16] = { 1, 3, 5, 7, 10, 12, 15, 17, 20, 22, 25, 27, 29, 31, 33, 35 };
    uint32_t s = ARG(0), g = ARG(1); uint8_t b[16]; char t[48];
    if (!g) { RET(0x80070057u); return; }
    if (!s) { memset(M + g, 0, 16); RET(0); return; }
    jw_w2a(s, t, sizeof t);
    if (strlen(t) != 38 || t[0] != '{' || t[9] != '-' || t[14] != '-' || t[19] != '-' || t[24] != '-' || t[37] != '}') { RET(0x800401F3u); return; }
    for (int i = 0; i < 16; i++) { int h = jw_hex((uint8_t)t[pos[i]]), l = jw_hex((uint8_t)t[pos[i] + 1]); if (h < 0 || l < 0) { RET(0x800401F3u); return; } b[i] = (uint8_t)(h << 4 | l); }
    uint8_t o[16] = { b[3], b[2], b[1], b[0], b[5], b[4], b[7], b[6], b[8], b[9], b[10], b[11], b[12], b[13], b[14], b[15] };
    memcpy(M + g, o, 16); RET(0);
}
static void jw_CoTaskMemAlloc(CPU* c) { RET(ghalloc(ARG(0) ? ARG(0) : 1, 0)); }
static void jw_CoTaskMemFree(CPU* c) { if (ARG(0)) ghfree(ARG(0)); RET(0); }
static void jw_CoTaskMemRealloc(CPU* c) {
    uint32_t p = ARG(0), n = ARG(1);
    if (!p) { RET(ghalloc(n ? n : 1, 0)); return; }
    if (!n) { ghfree(p); RET(0); return; }
    uint32_t q = ghalloc(n, 0); if (!q) { RET(0); return; }
    uint32_t old = ghsize(p); memcpy(M + q, M + p, old < n ? old : n); ghfree(p); RET(q);
}
static void jw_PropVariantClear(CPU* c) { if (ARG(0)) jw_w16(ARG(0), 0); RET(0); }

/* ---------------- shlwapi ---------------- */
static void jw_PathRemoveFileSpecA(CPU* c) {
    uint32_t p = ARG(0); if (!p) { RET(0); return; }
    char* s = (char*)(M + p); char* last = NULL;
    for (char* q = s; *q; q++) if (*q == '\\' || *q == '/') last = q;
    if (!last) { if (*s) { *s = 0; RET(1); } else RET(0); return; }
    if (last == s || (last == s + 2 && s[1] == ':')) { if (last[1]) { last[1] = 0; RET(1); } else RET(0); return; }
    *last = 0; RET(1);
}
static void jw_PathRemoveFileSpecW(CPU* c) {
    uint32_t p = ARG(0); if (!p) { RET(0); return; }
    uint32_t n = jw_wlen(p); int64_t last = -1;
    for (uint32_t i = 0; i < n; i++) { uint16_t ch = jw_r16(p + 2 * i); if (ch == '\\' || ch == '/') last = i; }
    if (last < 0) { if (n) { jw_w16(p, 0); RET(1); } else RET(0); return; }
    if (last == 0 || (last == 2 && jw_r16(p + 2) == ':')) { if ((uint32_t)last + 1 < n) { jw_w16(p + 2 * (uint32_t)(last + 1), 0); RET(1); } else RET(0); return; }
    jw_w16(p + 2 * (uint32_t)last, 0); RET(1);
}
static void jw_PathCombineW(CPU* c) {
    uint32_t d = ARG(0), a = ARG(1), b = ARG(2), n = 0; uint16_t buf[520];
    if (!d || (!a && !b)) { RET(0); return; }
    uint32_t lb = b ? jw_wlen(b) : 0, la = a ? jw_wlen(a) : 0;
    int abs_b = lb && (jw_r16(b) == '\\' || jw_r16(b) == '/' || (lb > 1 && jw_r16(b + 2) == ':'));
    if (!abs_b && a) for (uint32_t i = 0; i < la && n < 519; i++) buf[n++] = jw_r16(a + 2 * i);
    if (lb) {
        if (n && buf[n - 1] != '\\' && buf[n - 1] != '/') buf[n++] = '\\';
        for (uint32_t i = 0; i < lb && n < 519; i++) buf[n++] = jw_r16(b + 2 * i);
    }
    if (n >= 260) { jw_w16(d, 0); RET(0); return; }
    for (uint32_t i = 0; i < n; i++) jw_w16(d + 2 * i, buf[i]);
    jw_w16(d + 2 * n, 0); RET(d);
}
static void jw_PathIsDirectoryA(CPU* c) {
    uint32_t a[1] = { ARG(0) }; if (!a[0]) { RET(0); return; }
    uint32_t r = jw_call(c, "kernel32.dll", "GetFileAttributesA", 1, a);
    RET(r != 0xFFFFFFFFu && (r & 0x10) ? 0x10 : 0);
}

/* ---------------- user32 ---------------- */
static void jw_CharLowerBuffA(CPU* c) { uint32_t p = ARG(0), n = ARG(1); if (p) for (uint32_t i = 0; i < n; i++) M[p + i] = (uint8_t)tolower(M[p + i]); RET(n); }
static void jw_SendMessageA(CPU* c) { uint32_t a[4] = { ARG(0), ARG(1), ARG(2), ARG(3) }; uint32_t r = jw_call(c, "user32.dll", "SendMessageW", 4, a); RET(r); }
static char jw_atoms[64][64]; static int jw_natoms; static pthread_mutex_t jw_atom_mx = PTHREAD_MUTEX_INITIALIZER;
static uint32_t jw_atom(const char* s) {
    pthread_mutex_lock(&jw_atom_mx);
    int i; for (i = 0; i < jw_natoms; i++) if (!strcasecmp(jw_atoms[i], s)) break;
    if (i == jw_natoms && jw_natoms < 64) { snprintf(jw_atoms[i], sizeof jw_atoms[i], "%s", s); jw_natoms++; }
    pthread_mutex_unlock(&jw_atom_mx);
    return i < 64 ? 0xC100u + (uint32_t)i : 0;
}
static void jw_RegisterWindowMessageA(CPU* c) { RET(ARG(0) ? jw_atom(GSTR(ARG(0))) : 0); }
static void j