/* hle_jp_win_b.inc - JP100 v3, part b: more kernel32, advapi32, oleaut32/ole32, shlwapi */
static void jw_GetVolumeInformationW(CPU* c) {
    static const char lab[] = "JP100";
    uint32_t nb = ARG(1), nn = ARG(2), ser = ARG(3), mcl = ARG(4), fsf = ARG(5), fs = ARG(6), fsn = ARG(7);
    if (nb && nn >= sizeof lab) jw_a2w(lab, nb, sizeof lab - 1);
    if (ser) wr32(ser, 0x1A2B3C4D);
    if (mcl) wr32(mcl, 255);
    if (fsf) wr32(fsf, 0x0007);
    if (fs && fsn >= 5) jw_a2w("NTFS", fs, 4);
    RET(1);
}
static void jw_GetSystemDirectoryW(CPU* c) {
    static const char d[] = "C:\\WINDOWS\\system32"; uint32_t n = sizeof d - 1;
    if (!ARG(0) || ARG(1) <= n) { RET(n + 1); return; }
    jw_a2w(d, ARG(0), n); RET(n);
}
static void jw_SystemTimeToTzSpecificLocalTime(CPU* c) {   /* the TIME_ZONE_INFORMATION arg is ignored: host local zone */
    uint32_t in = ARG(1), out = ARG(2); if (!in || !out) { hle_set_last_error(c, 87); RET(0); return; }
    struct tm u; memset(&u, 0, sizeof u);
    u.tm_year = jw_r16(in) - 1900; u.tm_mon = jw_r16(in + 2) - 1; u.tm_mday = jw_r16(in + 6);
    u.tm_hour = jw_r16(in + 8); u.tm_min = jw_r16(in + 10); u.tm_sec = jw_r16(in + 12);
    time_t t = timegm(&u); struct tm l; localtime_r(&t, &l);
    jw_w16(out, (uint16_t)(l.tm_year + 1900)); jw_w16(out + 2, (uint16_t)(l.tm_mon + 1)); jw_w16(out + 4, (uint16_t)l.tm_wday);
    jw_w16(out + 6, (uint16_t)l.tm_mday); jw_w16(out + 8, (uint16_t)l.tm_hour); jw_w16(out + 10, (uint16_t)l.tm_min);
    jw_w16(out + 12, (uint16_t)l.tm_sec); jw_w16(out + 14, jw_r16(in + 14)); RET(1);
}
static void jw_GlobalSize(CPU* c) { RET(ARG(0) ? ghsize(ARG(0)) : 0); }
static void jw_GetProfileStringW(CPU* c) {   /* win.ini is empty: copy the default */
    uint32_t def = ARG(2), out = ARG(3), cap = ARG(4);
    if (!out || !cap) { RET(0); return; }
    uint32_t n = def ? jw_wlen(def) : 0; if (n > cap - 1) n = cap - 1;
    for (uint32_t i = 0; i < n; i++) jw_w16(out + 2 * i, jw_r16(def + 2 * i));
    jw_w16(out + 2 * n, 0); RET(n);
}
static void jw_GetThreadTimes(CPU* c) {
    struct timespec ts; clock_gettime(CLOCK_THREAD_CPUTIME_ID, &ts);
    uint64_t user = (uint64_t)ts.tv_sec * 10000000ull + (uint64_t)ts.tv_nsec / 100, cr = 116444736000000000ull, z = 0;
    if (ARG(1)) memcpy(M + ARG(1), &cr, 8);
    if (ARG(2)) memcpy(M + ARG(2), &z, 8);
    if (ARG(3)) memcpy(M + ARG(3), &z, 8);
    if (ARG(4)) memcpy(M + ARG(4), &user, 8);
    RET(1);
}
static void jw_VirtualProtect(CPU* c) { if (ARG(3)) wr32(ARG(3), 0x40); RET(1); }   /* guest memory is always RWX */
static void jw_FindFirstFileExA(CPU* c) {   /* FindExInfoStandard + FindExSearchNameMatch == FindFirstFileA */
    uint32_t a[2] = { ARG(0), ARG(2) };
    uint32_t r = jw_call(c, "kernel32.dll", "FindFirstFileA", 2, a); RET(r);
}
static pthread_mutex_t jw_slist_mu = PTHREAD_MUTEX_INITIALIZER;
static void jw_InterlockedPushEntrySList(CPU* c) {   /* SLIST_HEADER = { Next, u16 Depth, u16 Sequence } */
    uint32_t h = ARG(0), e = ARG(1);
    pthread_mutex_lock(&jw_slist_mu);
    uint32_t old = rd32(h); wr32(e, old); wr32(h, e);
    jw_w16(h + 4, (uint16_t)(jw_r16(h + 4) + 1)); jw_w16(h + 6, (uint16_t)(jw_r16(h + 6) + 1));
    pthread_mutex_unlock(&jw_slist_mu); RET(old);
}
static void jw_InterlockedPopEntrySList(CPU* c) {
    uint32_t h = ARG(0);
    pthread_mutex_lock(&jw_slist_mu);
    uint32_t e = rd32(h);
    if (e) { wr32(h, rd32(e)); jw_w16(h + 4, (uint16_t)(jw_r16(h + 4) - 1)); jw_w16(h + 6, (uint16_t)(jw_r16(h + 6) + 1)); }
    pthread_mutex_unlock(&jw_slist_mu); RET(e);
}
static void jw_CreateWaitableTimerA(CPU* c) {   /* modelled as an event: CreateEventA(sa, manualReset, FALSE, NULL) */
    uint32_t a[4] = { ARG(0), ARG(1), 0, 0 };
    uint32_t r = jw_call(c, "kernel32.dll", "CreateEventA", 4, a); RET(r);
}
static void jw_SetWaitableTimer(CPU* c) {   /* short relative one-shot timers sleep, everything else fires at once */
    static volatile int once; uint32_t dp = ARG(1); int64_t due = 0; if (dp) memcpy(&due, M + dp, 8);
    int64_t ms = due < 0 ? -due / 10000 : 0;
    if (ms > 0 && ms <= 50 && !ARG(2)) usleep((useconds_t)ms * 1000);
    jw_once(&once, "SetWaitableTimer fires synchronously");
    uint32_t a[1] = { ARG(0) }; jw_call(c, "kernel32.dll", "SetEvent", 1, a); RET(1);
}

/* ---------------- advapi32 ---------------- */
static void jw_ReportEventA(CPU* c) {   /* (log, type, cat, id, sid, nstr, datasize, strings, data) */
    uint32_t n = ARG(5) & 0xFFFF, s = ARG(7);
    fprintf(stderr, "[hle] ReportEvent type=%u id=0x%x", ARG(1) & 0xFFFF, ARG(3));
    for (uint32_t i = 0; s && i < n && i < 8; i++) { uint32_t p = rd32(s + 4 * i); if (p) fprintf(stderr, " | %.300s", GSTR(p)); }
    fputc('\n', stderr); RET(1);
}
static void jw_RegQueryInfoKeyW(CPU* c) {   /* every key is empty */
    if (ARG(1) && ARG(2)) { if (rd32(ARG(2))) jw_w16(ARG(1), 0); wr32(ARG(2), 0); }
    for (int i = 4; i <= 10; i++) if (ARG(i)) wr32(ARG(i), 0);
    if (ARG(11)) { wr32(ARG(11), 0); wr32(ARG(11) + 4, 0); }
    RET(0);
}

/* ---------------- oleaut32 / ole32 ---------------- */
static void jw_SysAllocStringLen(CPU* c) {   /* layout of oa_SysAllocString: [u32 byte length][WCHARs][0], returns p + 4 */
    uint32_t s = ARG(0), n = ARG(1); if (n > 0x10000000u) { RET(0); return; }
    uint32_t p = ghalloc(4 + 2 * n + 2, 1); if (!p) { RET(0); return; }
    wr32(p, 2 * n); if (s) memcpy(M + p + 4, M + s, 2 * (size_t)n); jw_w16(p + 4 + 2 * n, 0); RET(p + 4);
}
static void jw_SysStringLen(CPU* c) { RET(ARG(0) ? rd32(ARG(0) - 4) / 2 : 0); }
static void jw_VariantInit(CPU* c) { if (ARG(0)) memset(M + ARG(0), 0, 16); RET(0); }
static void jw_VariantClear(CPU* c) {
    static volatile int once; uint32_t v = ARG(0); if (!v) { RET(0x80070057u); return; }
    uint16_t vt = jw_r16(v);
    if (vt == 8) { uint32_t b = rd32(v + 8); if (b) ghfree(b - 4); }                       /* VT_BSTR */
    else if ((vt == 9 || vt == 13) && rd32(v + 8)) jw_once(&once, "VariantClear: interface not released");
    memset(M + v, 0, 16); RET(0);
}
static void jw_VarUI4FromStr(CPU* c) {   /* (OLECHAR*, LCID, flags, ULONG*) */
    char b[64]; jw_w2a(ARG(0), b, sizeof b); char* e = b; while (*e == ' ') e++;
    if (!*e || !ARG(3)) { RET(0x80020005u); return; }
    char* end; unsigned long v = strtoul(e, &end, 10); while (*end == ' ') end++;
    if (*end) { RET(0x80020005u); return; }
    wr32(ARG(3), (uint32_t)v); RET(0);
}
static void jw_CoTaskMemAlloc(CPU* c) { RET(ghalloc(ARG(0) ? ARG(0) : 1, 0)); }
static void jw_CoTaskMemFree(CPU* c) { if (ARG(0)) ghfree(ARG(0)); RET(0); }
static void jw_CoTaskMemRealloc(CPU* c) {
    uint32_t p = ARG(0), n = ARG(1);
    if (!p) { RET(ghalloc(n ? n : 1, 0)); return; }
    if (!n) { ghfree(p); RET(0); return; }
    uint32_t q = ghalloc(n, 0); if (!q) { RET(0); return; }
    uint32_t o = ghsize(p); memcpy(M + q, M + p, o < n ? o : n); ghfree(p); RET(q);
}
static void jw_PropVariantClear(CPU* c) {
    uint32_t v = ARG(0); if (!v) { RET(0); return; }
    uint16_t vt = jw_r16(v); uint32_t b = rd32(v + 8);
    if (vt == 8 && b) ghfree(b - 4);
    else if ((vt == 30 || vt == 31 || vt == 72) && b) ghfree(b);   /* LPSTR / LPWSTR / CLSID*: CoTaskMemAlloc'd */
    memset(M + v, 0, 16); RET(0);
}
static void jw_CLSIDFromString(CPU* c) {   /* "{xxxxxxxx-xxxx-xxxx-xxxx-xxxxxxxxxxxx}" */
    char s[64]; jw_w2a(ARG(0), s, sizeof s); uint32_t out = ARG(1); unsigned d1, d2, d3, b[8];
    if (!out || sscanf(s, "{%8x-%4x-%4x-%2x%2x-%2x%2x%2x%2x%2x%2x}", &d1, &d2, &d3, &b[0], &b[1], &b[2], &b[3], &b[4], &b[5], &b[6], &b[7]) != 11) {
        if (out) memset(M + out, 0, 16);
        RET(0x800401F3u); return;   /* CO_E_CLASSSTRING */
    }
    wr32(out, d1); jw_w16(out + 4, (uint16_t)d2); jw_w16(out + 6, (uint16_t)d3);
    for (int i = 0; i < 8; i++) M[out + 8 + i] = (uint8_t)b[i];
    RET(0);
}

/* ---------------- shlwapi ---------------- */
static void jw_PathRemoveFileSpecA(CPU* c) {
    uint32_t p = ARG(0); if (!p) { RET(0); return; }
    char* s = (char*)(M + p); char* last = NULL;
    for (char* q = s; *q; q++) if (*q == '\\' || *q == '/') last = q;
    if (!last) { int had = *s != 0; *s = 0; RET(had); return; }
    if (last == s || (last == s + 2 && s[1] == ':')) last++;   /* keep the root */
    int had = *last != 0; *last = 0; RET(had);
}
static void jw_PathRemoveFileSpecW(CPU* c) {
    uint32_t p = ARG(0); if (!p) { RET(0); return; }
    int32_t last = -1; uint32_t n = jw_wlen(p);
    for (uint32_t i = 0; i < n; i++) { uint16_t ch = jw_r16(p + 2 * i); if (ch == '\\' || ch == '/') last = (int32_t)i; }
    if (last < 0) { jw_w16(p, 0); RET(n != 0); return; }
    if (last == 0 || (last == 2 && jw_r16(p + 2) == ':')) last++;
    int had = (uint32_t)last < n; jw_w16(p + 2 * last, 0); RET(had);
}
static void jw_PathCombineW(CPU* c) {   /* builds into a local buffer: dst may alias src */
    uint32_t d = ARG(0), a = ARG(1), b = ARG(2); uint16_t r[300]; uint32_t n = 0;
    if (!d) { RET(0); return; }
    uint32_t la = a ? jw_wlen(a) : 0, lb = b ? jw_wlen(b) : 0;
    uint16_t b0 = lb ? jw_r16(b) : 0, b1 = lb > 1 ? jw_r16(b + 2) : 0;
    int abs = b0 == '\\' || b0 == '/' || b1 == ':';
    if (!abs) for (uint32_t i = 0; i < la && n < 299; i++) r[n++] = jw_r16(a + 2 * i);
    if (!abs && n && lb && r[n - 1] != '\\' && r[n - 1] != '/' && n < 299) r[n++] = '\\';
    for (uint32_t i = 0; i < lb && n < 299; i++) r[n++] = jw_r16(b + 2 * i);
    if (n >= 260) { jw_w16(d, 0); RET(0); return; }
    for (uint32_t i = 0; i < n; i++) jw_w16(d + 2 * i, r[i]);
    jw_w16(d + 2 * n, 0); RET(d);
}
static void jw_PathIsDirectoryA(CPU* c) {
    uint32_t a[1] = { ARG(0) }; uint32_t r = ARG(0) ? jw_call(c, "kernel32.dll", "GetFileAttributesA", 1, a) : 0xFFFFFFFFu;
    RET(r != 0xFFFFFFFFu && (r & 0x10) ? 0x10 : 0);
}
