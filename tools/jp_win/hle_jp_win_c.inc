/* hle_jp_win_c.inc - JP100 v3, part c: user32, ws2_32 */
static void jw_CharLowerBuffA(CPU* c) {
    uint32_t p = ARG(0), n = ARG(1);
    for (uint32_t i = 0; p && i < n; i++) { uint8_t ch = M[p + i]; if ((ch >= 'A' && ch <= 'Z') || (ch >= 0xC0 && ch <= 0xDE && ch != 0xD7)) M[p + i] = ch + 32; }
    RET(n);
}
static void jw_SendMessageA(CPU* c) {
    uint32_t a[4] = { ARG(0), ARG(1), ARG(2), ARG(3) };
    uint32_t r = jw_call(c, "user32.dll", "SendMessageW", 4, a); RET(r);
}
static pthread_mutex_t jw_atom_mu = PTHREAD_MUTEX_INITIALIZER;
static char jw_atoms[64][64]; static int jw_natoms;
static uint32_t jw_atom(const char* s) {   /* RegisterWindowMessage / RegisterClipboardFormat ids 0xC000.. */
    pthread_mutex_lock(&jw_atom_mu); int i;
    for (i = 0; i < jw_natoms; i++) if (!strcasecmp(jw_atoms[i], s)) break;
    if (i == jw_natoms && jw_natoms < 64) snprintf(jw_atoms[jw_natoms++], 64, "%s", s);
    pthread_mutex_unlock(&jw_atom_mu); return 0xC000u + (uint32_t)i;
}
static void jw_RegisterWindowMessageA(CPU* c) { RET(ARG(0) ? jw_atom(GSTR(ARG(0))) : 0); }
static void jw_RegisterClipboardFormatW(CPU* c) { char b[64]; if (!ARG(0)) { RET(0); return; } jw_w2a(ARG(0), b, sizeof b); RET(jw_atom(b)); }
static void jw_wsprintfA(CPU* c) {   /* cdecl varargs: %d %i %u %x %X %c %s %S with flags/width/precision/l/h */
    uint32_t d = ARG(0), fp = ARG(1); int ai = 2, n = 0; char out[1025];
    if (!d || !fp) { RET(0); return; }
    const char* f = GSTR(fp);
    while (*f && n < 1024) {
        if (*f != '%') { out[n++] = *f++; continue; }
        if (f[1] == '%') { out[n++] = '%'; f += 2; continue; }
        char hs[32]; int hl = 0, wide = 0, nar = 0; hs[hl++] = *f++;
        while (*f && strchr("-#+ .0123456789", *f) && hl < 24) hs[hl++] = *f++;
        while (*f == 'l' || *f == 'h') { if (*f == 'l') wide = 1; else nar = 1; f++; }
        char t = *f; if (!t) break;
        f++;
        uint32_t v = ARG(ai++); char b[1100]; b[0] = 0;
        switch (t) {
        case 'd': case 'i': hs[hl++] = 'd'; hs[hl] = 0; snprintf(b, sizeof b, hs, (int)v); break;
        case 'u': case 'x': case 'X': hs[hl++] = t; hs[hl] = 0; snprintf(b, sizeof b, hs, (unsigned)v); break;
        case 'c': case 'C': hs[hl++] = 'c'; hs[hl] = 0; snprintf(b, sizeof b, hs, (int)(v & 0xFF)); break;
        case 's': case 'S': {
            char s[1024];
            if (wide || (t == 'S' && !nar)) jw_w2a(v, s, sizeof s); else snprintf(s, sizeof s, "%s", v ? GSTR(v) : "(null)");
            hs[hl++] = 's'; hs[hl] = 0; snprintf(b, sizeof b, hs, s); break;
        }
        default: ai--; break;
        }
        for (char* q = b; *q && n < 1024; q++) out[n++] = *q;
    }
    out[n] = 0; memcpy(M + d, out, (size_t)n + 1); RET(n);
}
static void jw_CopyRect(CPU* c) { if (!ARG(0) || !ARG(1)) { RET(0); return; } memmove(M + ARG(0), M + ARG(1), 16); RET(1); }
static void jw_GetTextA0(CPU* c) { if (ARG(1) && (int32_t)ARG(2) > 0) M[ARG(1)] = 0; RET(0); }   /* GetWindowTextA / GetClassNameA */
static void jw_CharNextA(CPU* c) { uint32_t p = ARG(0); RET(p && M[p] ? p + 1 : p); }
static void jw_CharNextExA(CPU* c) { uint32_t p = ARG(1); RET(p && M[p] ? p + 1 : p); }
static void jw_GetKeyboardLayoutList(CPU* c) { if (ARG(0) > 0 && ARG(1)) wr32(ARG(1), 0x04090409); RET(1); }
static int jw_vk2ch(uint32_t vk, int sh) {   /* US layout */
    if (vk >= 'A' && vk <= 'Z') return sh ? (int)vk : (int)vk + 32;
    if (vk >= '0' && vk <= '9') return sh ? ")!@#$%^&*("[vk - '0'] : (int)vk;
    if (vk >= 0x60 && vk <= 0x69) return '0' + (int)(vk - 0x60);
    switch (vk) {
    case 0x20: return ' '; case 0x0D: return '\r'; case 0x08: return 8; case 0x09: return '\t'; case 0x1B: return 27;
    case 0x6A: return '*'; case 0x6B: return '+'; case 0x6D: return '-'; case 0x6E: return '.'; case 0x6F: return '/';
    case 0xBA: return sh ? ':' : ';'; case 0xBB: return sh ? '+' : '='; case 0xBC: return sh ? '<' : ',';
    case 0xBD: return sh ? '_' : '-'; case 0xBE: return sh ? '>' : '.'; case 0xBF: return sh ? '?' : '/';
    case 0xC0: return sh ? '~' : '`'; case 0xDB: return sh ? '{' : '['; case 0xDC: return sh ? '|' : '\\';
    case 0xDD: return sh ? '}' : ']'; case 0xDE: return sh ? '"' : '\'';
    }
    return 0;
}
static int jw_key_ch(uint32_t vk, uint32_t ks) {
    int sh = ks && ((M[ks + 0x10] | M[ks + 0xA0] | M[ks + 0xA1]) & 0x80);
    int ch = jw_vk2ch(vk, sh);
    if (ks && (M[ks + 0x14] & 1) && ch >= 'a' && ch <= 'z') ch -= 32;   /* caps lock */
    return ch;
}
static void jw_ToUnicode(CPU* c) {   /* (vk, scan, keystate, buf, cch, flags) */
    int ch = jw_key_ch(ARG(0), ARG(2));
    if (!ch || (ARG(1) & 0x8000) || !ARG(3) || (int32_t)ARG(4) < 1) { RET(0); return; }
    jw_w16(ARG(3), (uint16_t)ch); if ((int32_t)ARG(4) > 1) jw_w16(ARG(3) + 2, 0);
    RET(1);
}
static void jw_ToAscii(CPU* c) {   /* (vk, scan, keystate, LPWORD, flags) */
    int ch = jw_key_ch(ARG(0), ARG(2));
    if (!ch || (ARG(1) & 0x8000) || !ARG(3)) { RET(0); return; }
    jw_w16(ARG(3), (uint16_t)ch); RET(1);
}
static void jw_GetKeyboardState(CPU* c) {
    uint32_t p = ARG(0); if (!p) { RET(0); return; }
    for (uint32_t vk = 0; vk < 256; vk++) {
        uint32_t a[1] = { vk };
        M[p + vk] = (vk >= 1 && vk < 0xFF && (jw_call(c, "user32.dll", "GetAsyncKeyState", 1, a) & 0x8000)) ? 0x80 : 0;
    }
    RET(1);
}
static void jw_GetLastInputInfo(CPU* c) { if (!ARG(0)) { RET(0); return; } wr32(ARG(0) + 4, jw_call(c, "kernel32.dll", "GetTickCount", 0, NULL)); RET(1); }
typedef struct { uint32_t hwnd; char name[48]; uint32_t val; } JwProp;
static JwProp jw_props[64]; static int jw_nprops;
static void jw_propname(uint32_t p, char* o) { if (p < 0x10000) snprintf(o, 48, "#%u", p); else jw_w2a(p, o, 48); }
static void jw_GetPropW(CPU* c) {
    char n[48]; jw_propname(ARG(1), n); uint32_t r = 0;
    pthread_mutex_lock(&jw_atom_mu);
    for (int i = 0; i < jw_nprops; i++) if (jw_props[i].hwnd == ARG(0) && !strcmp(jw_props[i].name, n)) { r = jw_props[i].val; break; }
    pthread_mutex_unlock(&jw_atom_mu); RET(r);
}
static void jw_SetPropW(CPU* c) {
    char n[48]; jw_propname(ARG(1), n); int i, ok = 1;
    pthread_mutex_lock(&jw_atom_mu);
    for (i = 0; i < jw_nprops; i++) if (jw_props[i].hwnd == ARG(0) && !strcmp(jw_props[i].name, n)) break;
    if (i == jw_nprops) { if (jw_nprops < 64) { jw_props[i].hwnd = ARG(0); snprintf(jw_props[i].name, 48, "%s", n); jw_nprops++; } else ok = 0; }
    if (ok) jw_props[i].val = ARG(2);
    pthread_mutex_unlock(&jw_atom_mu); RET(ok);
}
static void jw_CallWindowProc(CPU* c) {   /* CallWindowProcA/W(prev, hwnd, msg, wp, lp) */
    uint32_t f = ARG(0), a[4] = { ARG(1), ARG(2), ARG(3), ARG(4) };
    if (f < 0x10000) { RET(0); return; }
    uint32_t r = jw_gcall(c, f, 4, a); RET(r);
}
static void jw_MsgWaitForMultipleObjectsEx(CPU* c) {   /* waits at most 10 ms, then reports "input available" */
    uint32_t n = ARG(0), ms = ARG(2), w = (ms == 0xFFFFFFFFu || ms > 10) ? 10 : ms, r;
    if (!n) { uint32_t s[1] = { w }; if (w) jw_call(c, "kernel32.dll", "Sleep", 1, s); r = 0x102; }
    else { uint32_t a[4] = { n, ARG(1), (ARG(4) & 1) ? 1u : 0u, w }; r = jw_call(c, "kernel32.dll", "WaitForMultipleObjects", 4, a); }
    if (r == 0x102 && w != ms) r = n;
    RET(r);
}
static void jw_WaitMessage(CPU* c) { uint32_t s[1] = { 5 }; jw_call(c, "kernel32.dll", "Sleep", 1, s); RET(1); }
static void jw_GetUserObjectInformationW(CPU* c) {   /* the CRT asks for UOI_FLAGS before showing message boxes */
    uint32_t idx = ARG(1), p = ARG(2), len = ARG(3);
    if (idx == 1) {
        if (ARG(4)) wr32(ARG(4), 12);
        if (!p || len < 12) { hle_set_last_error(c, 122); RET(0); return; }
        wr32(p, 0); wr32(p + 4, 0); wr32(p + 8, 1); RET(1); return;   /* WSF_VISIBLE */
    }
    if (idx == 2) {
        static const char nm[] = "WinSta0"; uint32_t need = sizeof nm * 2;
        if (ARG(4)) wr32(ARG(4), need);
        if (!p || len < need) { hle_set_last_error(c, 122); RET(0); return; }
        jw_a2w(nm, p, sizeof nm - 1); RET(1); return;
    }
    hle_set_last_error(c, 87); RET(0);
}
static void jw_GetSysColor(CPU* c) {
    static const uint32_t col[31] = { 0xC8C8C8, 0x000000, 0xD1B499, 0xDBCDBF, 0xF0F0F0, 0xFFFFFF, 0x646464, 0x000000, 0x000000, 0x000000,
        0xB4B4B4, 0xFCF7F4, 0xABABAB, 0xFF9933, 0xFFFFFF, 0xF0F0F0, 0xA0A0A0, 0x6D6D6D, 0x000000, 0x000000, 0xFFFFFF, 0x696969,
        0xE3E3E3, 0x000000, 0xE1FFFF, 0x000000, 0xCC6600, 0xEAD1B9, 0xF2E4D7, 0xFF9933, 0xF0F0F0 };
    RET(ARG(0) < 31 ? col[ARG(0)] : 0);
}
static void jw_GetWindowPlacement(CPU* c) {   /* WINDOWPLACEMENT: 44 bytes, rcNormalPosition at +28 */
    uint32_t p = ARG(1); if (!p) { RET(0); return; }
    uint32_t a[2] = { ARG(0), p + 28 };
    memset(M + p + 4, 0, 40); wr32(p, 44); wr32(p + 8, 1);
    jw_call(c, "user32.dll", "GetClientRect", 2, a); RET(1);
}
static void jw_GetUpdateRect(CPU* c) { if (ARG(1)) memset(M + ARG(1), 0, 16); RET(0); }
static void jw_SetTimer(CPU* c) {
    static volatile int once; static uint32_t next = 0x7100;
    jw_once(&once, "SetTimer: WM_TIMER is not delivered");
    RET(ARG(0) ? (ARG(1) ? ARG(1) : 1) : next++);
}
static void jw_SetWindowTextA(CPU* c) { static int n; if (ARG(1) && n < 5) { n++; fprintf(stderr, "[hle] SetWindowTextA \"%.200s\"\n", GSTR(ARG(1))); } RET(1); }

/* ---------------- ws2_32 (no network: everything else fails with WSAENETDOWN) ---------------- */
static void jw_htonl(CPU* c) { RET(__builtin_bswap32(ARG(0))); }
static void jw_ntohs(CPU* c) { RET(__builtin_bswap16((uint16_t)ARG(0))); }
static void jw_WSAHtonl(CPU* c) { if (!ARG(2)) { RET(0xFFFFFFFFu); return; } wr32(ARG(2), __builtin_bswap32(ARG(1))); RET(0); }
static void jw_WSAHtons(CPU* c) { if (!ARG(2)) { RET(0xFFFFFFFFu); return; } jw_w16(ARG(2), __builtin_bswap16((uint16_t)ARG(1))); RET(0); }
static void jw_inet_addr(CPU* c) {
    unsigned a, b, d, e; char x;
    if (!ARG(0) || sscanf(GSTR(ARG(0)), "%u.%u.%u.%u%c", &a, &b, &d, &e, &x) != 4 || a > 255 || b > 255 || d > 255 || e > 255) { RET(0xFFFFFFFFu); return; }
    RET(a | b << 8 | d << 16 | e << 24);
}
static void jw_gethostname(CPU* c) {
    static const char h[] = "jp100-android"; uint32_t p = ARG(0); int32_t n = (int32_t)ARG(1);
    if (!p || n < (int32_t)sizeof h) { hle_set_last_error(c, 10014); RET(0xFFFFFFFFu); return; }
    memcpy(M + p, h, sizeof h); RET(0);
}
static void jw_WSAGetLastError(CPU* c) { RET(rd32(c->fs_base + 0x34)); }
static void jw_WSASetLastError(CPU* c) { hle_set_last_error(c, ARG(0)); RET(0); }
static void jw_FDIsSet(CPU* c) {   /* fd_set = { u_int count; SOCKET arr[64]; } */
    uint32_t s = ARG(0), f = ARG(1); if (!f) { RET(0); return; }
    uint32_t n = rd32(f); if (n > 64) n = 64;
    for (uint32_t i = 0; i < n; i++) if (rd32(f + 4 + 4 * i) == s) { RET(1); return; }
    RET(0);
}
