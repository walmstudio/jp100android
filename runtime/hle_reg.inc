/* hle_reg.inc - persistent Win32 registry (included by hle_win32.c).
 * JP100 writes its own state into HKCU\Software\Telltale Games (JPScenario "Unlocked Episode 1", Launcher,
 * JPGraphics, JPSound, JPScore*, ...). The old stubs dropped every write, so after the first launch the menu
 * read "not found" back and kept "Start episode" disabled (Wine: same exe, real registry -> enabled).
 *
 * Store: in memory, saved after every change to $ME1_REG_FILE or <game>/registry.reg (REGEDIT5 text, UTF-8).
 * Optional import on start (before registry.reg, so saved state wins): <game>/import.reg and $ME1_REG_IMPORT
 * ("a.reg;b.reg"). Accepts regedit exports (UTF-16 or UTF-8) and Wine user.reg/system.reg. Only keys under
 * $ME1_REG_IMPORT_FILTER (default "Software\Telltale Games", "*" = all) are imported.
 * Trace: ME1_TRACE_REG=0 off, 1 (default) writes + misses (first 400 lines), 2 everything. */
#include <errno.h>
#include <sys/stat.h>

typedef struct { char name[160]; uint32_t type, len; uint8_t* data; } RegVal;
typedef struct { char path[320]; RegVal* v; int nv, cap, deleted; } RegKey;
static RegKey* g_rk; static int g_nrk, g_caprk;
#define REG_MAXH 1024
static struct { uint32_t h; int key; } g_rh[REG_MAXH];
static uint32_t g_rh_next = 0x5A000100u;
static int g_reg_loaded, g_reg_tr = -1, g_reg_lines, g_reg_dirty_in_load;
static char g_reg_file[1100];

static int reg_tr(int lvl) {
    if (g_reg_tr < 0) { const char* e = getenv("ME1_TRACE_REG"); g_reg_tr = e ? atoi(e) : 1; }
    if (g_reg_tr < lvl) return 0;
    if (g_reg_tr < 2 && g_reg_lines >= 400) return 0;
    g_reg_lines++; return 1;
}
static const char* reg_root_name(uint32_t h) {
    switch (h) {
    case 0x80000000u: return "HKCR"; case 0x80000001u: return "HKCU"; case 0x80000002u: return "HKLM";
    case 0x80000003u: return "HKU"; case 0x80000005u: return "HKCC"; default: return NULL;
    }
}
static const char* reg_long_root(const char* s, size_t n) {
    static const char* const m[][2] = { { "HKEY_CLASSES_ROOT", "HKCR" }, { "HKEY_CURRENT_USER", "HKCU" },
        { "HKEY_LOCAL_MACHINE", "HKLM" }, { "HKEY_USERS", "HKU" }, { "HKEY_CURRENT_CONFIG", "HKCC" },
        { "HKCR", "HKCR" }, { "HKCU", "HKCU" }, { "HKLM", "HKLM" }, { "HKU", "HKU" }, { "HKCC", "HKCC" } };
    for (unsigned i = 0; i < sizeof m / sizeof m[0]; i++) if (strlen(m[i][0]) == n && !strncasecmp(s, m[i][0], n)) return m[i][1];
    return NULL;
}
/* canonical path: single backslashes, no leading/trailing ones */
static void reg_join(char* out, size_t cap, const char* base, const char* sub) {
    size_t o = 0; out[0] = 0;
    const char* parts[2] = { base, sub };
    for (int p = 0; p < 2; p++) {
        const char* s = parts[p]; if (!s) continue;
        for (; *s && o + 1 < cap; s++) {
            if (*s == '\\' || *s == '/') { if (o && out[o - 1] != '\\') out[o++] = '\\'; continue; }
            if (o && p == 1 && s == sub && out[o - 1] != '\\') out[o++] = '\\';
            out[o++] = *s;
        }
        if (o && out[o - 1] != '\\' && p == 0 && sub && *sub && o + 1 < cap) out[o++] = '\\';
    }
    while (o && out[o - 1] == '\\') o--;
    out[o] = 0;
}
static int reg_find(const char* path) {
    for (int i = 0; i < g_nrk; i++) if (!g_rk[i].deleted && !strcasecmp(g_rk[i].path, path)) return i;
    return -1;
}
static int reg_make(const char* path) {
    int k = reg_find(path); if (k >= 0) return k;
    const char* bs = strrchr(path, '\\');
    if (bs) { char par[320]; size_t n = (size_t)(bs - path); if (n >= sizeof par) n = sizeof par - 1; memcpy(par, path, n); par[n] = 0; reg_make(par); }
    if (g_nrk == g_caprk) { g_caprk = g_caprk ? 2 * g_caprk : 64; g_rk = realloc(g_rk, sizeof(RegKey) * g_caprk); }
    RegKey* r = &g_rk[g_nrk]; memset(r, 0, sizeof *r); snprintf(r->path, sizeof r->path, "%s", path);
    return g_nrk++;
}
static RegVal* reg_val(int k, const char* name) {
    RegKey* r = &g_rk[k];
    for (int i = 0; i < r->nv; i++) if (!strcasecmp(r->v[i].name, name)) return &r->v[i];
    return NULL;
}
static void reg_put(int k, const char* name, uint32_t type, const void* data, uint32_t len) {
    RegVal* v = reg_val(k, name); RegKey* r = &g_rk[k];
    if (!v) {
        if (r->nv == r->cap) { r->cap = r->cap ? 2 * r->cap : 8; r->v = realloc(r->v, sizeof(RegVal) * r->cap); }
        v = &r->v[r->nv++]; memset(v, 0, sizeof *v); snprintf(v->name, sizeof v->name, "%s", name);
    }
    free(v->data); v->data = malloc(len + 2); if (len) memcpy(v->data, data, len); v->data[len] = v->data[len + 1] = 0;
    v->type = type; v->len = len;
}
static int reg_del_val(int k, const char* name) {
    RegKey* r = &g_rk[k];
    for (int i = 0; i < r->nv; i++) if (!strcasecmp(r->v[i].name, name)) { free(r->v[i].data); r->v[i] = r->v[--r->nv]; return 1; }
    return 0;
}
static int reg_is_str(uint32_t t) { return t == 1 || t == 2 || t == 7; }

/* ---- UTF-8 <-> UTF-16 ---- */
static size_t reg_u8_put(char* o, unsigned cp) {
    if (cp < 0x80) { o[0] = (char)cp; return 1; }
    if (cp < 0x800) { o[0] = (char)(0xC0 | cp >> 6); o[1] = (char)(0x80 | (cp & 63)); return 2; }
    if (cp < 0x10000) { o[0] = (char)(0xE0 | cp >> 12); o[1] = (char)(0x80 | ((cp >> 6) & 63)); o[2] = (char)(0x80 | (cp & 63)); return 3; }
    o[0] = (char)(0xF0 | cp >> 18); o[1] = (char)(0x80 | ((cp >> 12) & 63)); o[2] = (char)(0x80 | ((cp >> 6) & 63)); o[3] = (char)(0x80 | (cp & 63)); return 4;
}
/* n16 code units -> malloc'd UTF-8 (NULs kept), *olen bytes */
static char* reg_16to8(const uint16_t* s, size_t n16, uint32_t* olen) {
    char* o = malloc(4 * n16 + 4); size_t k = 0;
    for (size_t i = 0; i < n16; i++) {
        unsigned cp = s[i];
        if (cp >= 0xD800 && cp < 0xDC00 && i + 1 < n16 && s[i + 1] >= 0xDC00 && s[i + 1] < 0xE000) { cp = 0x10000 + ((cp - 0xD800) << 10) + (s[i + 1] - 0xDC00); i++; }
        k += reg_u8_put(o + k, cp);
    }
    o[k] = 0; *olen = (uint32_t)k; return o;
}
static uint16_t* reg_8to16(const uint8_t* s, size_t n, size_t* on16) {
    uint16_t* o = malloc(2 * n + 4); size_t k = 0;
    for (size_t i = 0; i < n;) {
        unsigned c0 = s[i], cp, need;
        if (c0 < 0x80) { cp = c0; need = 0; } else if ((c0 & 0xE0) == 0xC0) { cp = c0 & 31; need = 1; }
        else if ((c0 & 0xF0) == 0xE0) { cp = c0 & 15; need = 2; } else if ((c0 & 0xF8) == 0xF0) { cp = c0 & 7; need = 3; }
        else { o[k++] = (uint16_t)c0; i++; continue; }   /* not UTF-8 (ANSI byte): keep as Latin-1 */
        if (i + need >= n + (need ? 0 : 1) && need) { o[k++] = (uint16_t)c0; i++; continue; }
        int bad = 0; for (unsigned j = 1; j <= need; j++) { if (i + j >= n || (s[i + j] & 0xC0) != 0x80) { bad = 1; break; } cp = cp << 6 | (s[i + j] & 63); }
        if (bad) { o[k++] = (uint16_t)c0; i++; continue; }
        i += need + 1;
        if (cp >= 0x10000) { cp -= 0x10000; o[k++] = (uint16_t)(0xD800 + (cp >> 10)); o[k++] = (uint16_t)(0xDC00 + (cp & 1023)); } else o[k++] = (uint16_t)cp;
    }
    o[k] = 0; *on16 = k; return o;
}
static void reg_gw2u(uint32_t p, char* o, size_t cap) {   /* guest UTF-16 string -> UTF-8 */
    size_t n = 0; if (p) while (n < 4096 && rd16(p + 2 * n)) n++;
    uint16_t* t = malloc(2 * n + 2); for (size_t i = 0; i < n; i++) t[i] = rd16(p + 2 * i);
    uint32_t l; char* u = reg_16to8(t, n, &l); snprintf(o, cap, "%s", u); free(u); free(t);
}

/* ---- save / load ---- */
static const char* reg_root_long(const char* shortp, size_t* n) {
    static const char* const m[][2] = { { "HKCR", "HKEY_CLASSES_ROOT" }, { "HKCU", "HKEY_CURRENT_USER" }, { "HKLM", "HKEY_LOCAL_MACHINE" },
        { "HKU", "HKEY_USERS" }, { "HKCC", "HKEY_CURRENT_CONFIG" } };
    for (unsigned i = 0; i < 5; i++) { size_t l = strlen(m[i][0]); if (!strncasecmp(shortp, m[i][0], l) && (shortp[l] == 0 || shortp[l] == '\\')) { *n = l; return m[i][1]; } }
    *n = 0; return "";
}
static void reg_put_str(FILE* f, const char* s) {
    fputc('"', f);
    for (; *s; s++) { if (*s == '\\' || *s == '"') fputc('\\', f); fputc(*s, f); }
    fputc('"', f);
}
static void reg_save(void) {
    if (!g_reg_file[0] || g_reg_dirty_in_load) return;
    char tmp[1200]; snprintf(tmp, sizeof tmp, "%s.tmp", g_reg_file);
    FILE* f = fopen(tmp, "wb");
    if (!f) { static int warned; if (!warned++) fprintf(stderr, "[reg] cannot write %s: %s (registry stays in memory)\n", tmp, strerror(errno)); return; }
    fputs("Windows Registry Editor Version 5.00\r\n", f);
    for (int i = 0; i < g_nrk; i++) {
        RegKey* r = &g_rk[i]; if (r->deleted || !strchr(r->path, '\\')) continue;
        size_t n; const char* lr = reg_root_long(r->path, &n);
        fprintf(f, "\r\n[%s%s]\r\n", lr, r->path + n);
        for (int j = 0; j < r->nv; j++) {
            RegVal* v = &r->v[j];
            if (v->name[0]) reg_put_str(f, v->name); else fputc('@', f);
            fputc('=', f);
            if (v->type == 1 && strlen((char*)v->data) + 1 >= v->len && !strpbrk((char*)v->data, "\r\n")) reg_put_str(f, (char*)v->data);
            else if (v->type == 4 && v->len == 4) { uint32_t d; memcpy(&d, v->data, 4); fprintf(f, "dword:%08x", d); }
            else {
                const uint8_t* b = v->data; uint32_t len = v->len; uint16_t* w = NULL;
                if (reg_is_str(v->type)) { size_t n16; w = reg_8to16(v->data, v->len, &n16); b = (const uint8_t*)w; len = (uint32_t)(2 * n16); }
                if (v->type == 3) fputs("hex:", f); else fprintf(f, "hex(%x):", v->type);
                for (uint32_t q = 0; q < len; q++) fprintf(f, q ? ",%02x" : "%02x", b[q]);
                free(w);
            }
            fputs("\r\n", f);
        }
    }
    fputs("\r\n", f);
    if (fclose(f) == 0) rename(tmp, g_reg_file);
}
/* unescape a quoted .reg string starting after the opening quote; returns pointer past the closing quote */
static const char* reg_unq(const char* s, char* o, size_t cap, size_t* olen, int wine) {
    size_t k = 0;
    while (*s && *s != '"') {
        char tmp[4]; size_t tl = 1; tmp[0] = *s;
        if (*s == '\\' && s[1]) {
            s++;
            if (wine && *s == 'x') {
                unsigned cp = 0; int d = 0; s++;
                while (d < 4 && isxdigit((unsigned char)*s)) { cp = cp * 16 + (unsigned)(isdigit((unsigned char)*s) ? *s - '0' : (tolower((unsigned char)*s) - 'a' + 10)); s++; d++; }
                tl = reg_u8_put(tmp, cp); s--;
            } else if (wine && *s == 'n') tmp[0] = '\n'; else if (wine && *s == 't') tmp[0] = '\t';
            else if (wine && *s == 'r') tmp[0] = '\r'; else if (wine && *s == '0') tmp[0] = 0;
            else tmp[0] = *s;
        }
        for (size_t j = 0; j < tl && k + 1 < cap; j++) o[k++] = tmp[j];
        s++;
    }
    o[k] = 0; if (olen) *olen = k;
    return *s == '"' ? s + 1 : s;
}
static int reg_filter_ok(const char* path, const char* filt) {
    if (!filt || !strcmp(filt, "*")) return 1;
    const char* p = strchr(path, '\\'); p = p ? p + 1 : path;
    size_t n = strlen(filt);
    return !strncasecmp(p, filt, n) && (p[n] == 0 || p[n] == '\\');
}
static char* reg_read_text(const char* fn, size_t* len) {
    FILE* f = fopen(fn, "rb"); if (!f) return NULL;
    fseek(f, 0, SEEK_END); long n = ftell(f); fseek(f, 0, SEEK_SET);
    if (n < 0) { fclose(f); return NULL; }
    uint8_t* b = malloc((size_t)n + 2); size_t got = fread(b, 1, (size_t)n, f); fclose(f); b[got] = b[got + 1] = 0;
    if (got >= 2 && b[0] == 0xFF && b[1] == 0xFE) {
        size_t n16 = (got - 2) / 2; uint16_t* w = malloc(2 * n16 + 2); memcpy(w, b + 2, 2 * n16);
        uint32_t l; char* u = reg_16to8(w, n16, &l); free(w); free(b); *len = l; return u;
    }
    if (got >= 3 && b[0] == 0xEF && b[1] == 0xBB && b[2] == 0xBF) { memmove(b, b + 3, got - 2); got -= 3; }
    *len = got; return (char*)b;
}
/* returns number of values imported; filt == NULL: own file (everything) */
static int reg_import(const char* fn, const char* filt) {
    size_t len; char* t = reg_read_text(fn, &len); if (!t) return -1;
    int wine = !strncmp(t, "WINE REGISTRY", 13), nv = 0, cur = -1, skip = 1;
    const char* wroot = NULL;
    if (wine) {
        const char* rel = strstr(t, ";; All keys relative to ");
        if (rel) {
            rel += 24;
            if (!strncasecmp(rel, "\\\\Machine", 9)) wroot = "HKLM";
            else if (!strncasecmp(rel, "\\\\User\\\\.Default", 16)) wroot = "HKU\\.DEFAULT";
            else if (!strncasecmp(rel, "\\\\User\\\\", 8)) wroot = "HKCU";
        }
        if (!wroot) wroot = "HKCU";
    }
    char* line = t; size_t cap = 1 << 16; char* acc = malloc(cap);
    while (line && *line) {
        char* nl = strchr(line, '\n'); if (nl) *nl = 0;
        size_t l = strlen(line); while (l && (line[l - 1] == '\r' || line[l - 1] == ' ' || line[l - 1] == '\t')) line[--l] = 0;
        /* hex continuation: "...,\" + next line */
        size_t al = 0; acc[0] = 0;
        for (;;) {
            char* s = line; while (*s == ' ' || *s == '\t') s++;
            size_t sl = strlen(s);
            int cont = sl && s[sl - 1] == '\\' && strstr(acc[0] ? acc : s, "=hex") != NULL;
            if (cont) sl--;
            if (al + sl + 1 > cap) { cap = 2 * (al + sl + 1); acc = realloc(acc, cap); }
            memcpy(acc + al, s, sl); al += sl; acc[al] = 0;
            if (!cont || !nl) break;
            line = nl + 1; nl = strchr(line, '\n'); if (nl) *nl = 0;
            l = strlen(line); while (l && (line[l - 1] == '\r' || line[l - 1] == ' ' || line[l - 1] == '\t')) line[--l] = 0;
        }
        line = nl ? nl + 1 : NULL;
        char* s = acc;
        if (!*s || *s == ';' || *s == '#') continue;
        if (*s == '[') {
            char* e = strrchr(s, ']'); if (!e) { cur = -1; continue; }
            *e = 0; s++;
            int del = *s == '-'; if (del) s++;
            char raw[640], path[320]; size_t o = 0;
            if (wine) {   /* \\ -> \, \xHHHH */
                for (; *s && o + 4 < sizeof raw; s++) {
                    if (*s == '\\' && s[1] == '\\') { raw[o++] = '\\'; s++; }
                    else if (*s == '\\' && s[1] == 'x') { unsigned cp = 0; int d = 0; s += 2; while (d < 4 && isxdigit((unsigned char)*s)) { cp = cp * 16 + (unsigned)(isdigit((unsigned char)*s) ? *s - '0' : (tolower((unsigned char)*s) - 'a' + 10)); s++; d++; } s--; o += reg_u8_put(raw + o, cp); }
                    else raw[o++] = *s;
                }
                raw[o] = 0; reg_join(path, sizeof path, wroot, raw);
            } else {
                const char* bs = strchr(s, '\\'); size_t rn = bs ? (size_t)(bs - s) : strlen(s);
                const char* r = reg_long_root(s, rn); if (!r) { cur = -1; skip = 1; continue; }
                reg_join(path, sizeof path, r, bs ? bs + 1 : "");
            }
            skip = !reg_filter_ok(path, filt);
            if (skip) { cur = -1; continue; }
            if (del) { int k = reg_find(path); size_t pl = strlen(path); for (int i = 0; k >= 0 && i < g_nrk; i++) if (!strncasecmp(g_rk[i].path, path, pl) && (g_rk[i].path[pl] == 0 || g_rk[i].path[pl] == '\\')) g_rk[i].deleted = 1; cur = -1; continue; }
            cur = reg_make(path); continue;
        }
        if (cur < 0 || skip) continue;
        char name[160]; const char* p = s;
        if (*p == '@') { name[0] = 0; p++; }
        else if (*p == '"') p = reg_unq(p + 1, name, sizeof name, NULL, wine);
        else continue;
        while (*p == ' ') p++;
        if (*p != '=') continue;
        p++; while (*p == ' ') p++;
        if (*p == '-') { reg_del_val(cur, name); continue; }
        if (*p == '"' || !strncmp(p, "str(", 4)) {
            uint32_t type = 1;
            if (*p == 's') { type = (uint32_t)strtoul(p + 4, NULL, 16); p = strchr(p, '"'); if (!p) continue; }
            size_t vl; char* val = malloc(strlen(p) + 8); reg_unq(p + 1, val, strlen(p) + 8, &vl, wine);
            reg_put(cur, name, type, val, (uint32_t)vl + 1); free(val); nv++; continue;
        }
        if (!strncmp(p, "dword:", 6)) { uint32_t d = (uint32_t)strtoul(p + 6, NULL, 16); reg_put(cur, name, 4, &d, 4); nv++; continue; }
        if (!strncmp(p, "hex", 3)) {
            uint32_t type = 3; p += 3;
            if (*p == '(') { type = (uint32_t)strtoul(p + 1, NULL, 16); p = strchr(p, ')'); if (!p) continue; p++; }
            if (*p != ':') continue;
            p++;
            uint8_t* b = malloc(strlen(p) / 2 + 4); uint32_t n = 0;
            while (*p) { while (*p == ',' || *p == ' ' || *p == '\\') p++; if (!isxdigit((unsigned char)*p)) break; b[n++] = (uint8_t)strtoul(p, (char**)&p, 16); }
            if (reg_is_str(type)) { uint32_t ul; char* u = reg_16to8((const uint16_t*)b, n / 2, &ul); reg_put(cur, name, type, u, ul ? ul : 0); if (ul && u[ul - 1]) { /* keep NUL */ } free(u); }
            else reg_put(cur, name, type, b, n);
            free(b); nv++; continue;
        }
    }
    free(acc); free(t);
    return nv;
}
static void reg_load(void) {
    if (g_reg_loaded) return;
    g_reg_loaded = 1;
    const char* game = hle_fs_game_dir();
    const char* e = getenv("ME1_REG_FILE");
    if (e && *e) snprintf(g_reg_file, sizeof g_reg_file, "%s", e); else snprintf(g_reg_file, sizeof g_reg_file, "%s/registry.reg", game);
    /* PhysXLoader (ME1): HKLM\Software\Ageia Technologies\PhysXCore Path */
    int ag = reg_make("HKLM\\Software\\Ageia Technologies");
    reg_put(ag, "PhysXCore Path", 1, "C:\\MassEffect\\Binaries\\", 26);
    g_reg_dirty_in_load = 1;
    const char* filt = getenv("ME1_REG_IMPORT_FILTER"); if (!filt || !*filt) filt = "Software\\Telltale Games";
    char imp[1100]; snprintf(imp, sizeof imp, "%s/import.reg", game);
    int n = reg_import(imp, filt);
    if (n >= 0) fprintf(stderr, "[reg] imported %d values from %s (filter \"%s\")\n", n, imp, filt);
    if ((e = getenv("ME1_REG_IMPORT")) && *e) {
        char list[4096]; snprintf(list, sizeof list, "%s", e);
        for (char* tok = strtok(list, ";"); tok; tok = strtok(NULL, ";")) {
            n = reg_import(tok, filt);
            fprintf(stderr, n >= 0 ? "[reg] imported %d values from %s (filter \"%s\")\n" : "[reg] %.0d%s: cannot read (%s)\n", n < 0 ? 0 : n, tok, filt);
        }
    }
    n = reg_import(g_reg_file, NULL);
    fprintf(stderr, n >= 0 ? "[reg] loaded %d values from %s\n" : "[reg] %.0dno %s yet - it will be created on the first write\n", n < 0 ? 0 : n, g_reg_file);
    g_reg_dirty_in_load = 0;
}

/* ---- handles ---- */
static int reg_key_of(uint32_t h) {
    const char* r = reg_root_name(h);
    if (r) return reg_make(r);
    for (int i = 0; i < REG_MAXH; i++) if (g_rh[i].h == h) return g_rk[g_rh[i].key].deleted ? -1 : g_rh[i].key;
    return -1;
}
static uint32_t reg_new_handle(int k) {
    for (int i = 0; i < REG_MAXH; i++) if (!g_rh[i].h) { g_rh[i].h = g_rh_next++; g_rh[i].key = k; return g_rh[i].h; }
    /* table full (leaking caller): recycle the oldest slot */
    static int rr; int i = rr++ % REG_MAXH; g_rh[i].h = g_rh_next++; g_rh[i].key = k; return g_rh[i].h;
}

/* ---- API cores (names already UTF-8) ---- */
static uint32_t reg_open_core(uint32_t h, const char* sub, int create, uint32_t phk, uint32_t pdisp, const char* fn) {
    reg_load();
    int b = reg_key_of(h);
    if (b < 0) { if (phk) wr32(phk, 0); if (reg_tr(1)) fprintf(stderr, "[reg] %s(%08x, \"%s\") -> invalid handle\n", fn, h, sub); return 6; }
    char path[320]; reg_join(path, sizeof path, g_rk[b].path, sub);
    int k = reg_find(path), created = 0;
    if (k < 0 && create) { k = reg_make(path); created = 1; reg_save(); }
    if (k < 0) { if (phk) wr32(phk, 0); if (reg_tr(1)) fprintf(stderr, "[reg] %s(\"%s\") -> not found\n", fn, path); return 2; }
    if (phk) wr32(phk, reg_new_handle(k));
    if (pdisp) wr32(pdisp, created ? 1 : 2);
    if (reg_tr(created ? 1 : 2)) fprintf(stderr, "[reg] %s(\"%s\") -> %s\n", fn, path, created ? "created" : "ok");
    return 0;
}
static uint32_t reg_set_core(uint32_t h, const char* name, uint32_t type, uint32_t data, uint32_t cb, int wide) {
    reg_load();
    int k = reg_key_of(h); if (k < 0) return 6;
    if (reg_is_str(type) && wide) {
        size_t n16 = cb / 2; uint16_t* w = malloc(2 * n16 + 2); for (size_t i = 0; i < n16; i++) w[i] = data ? rd16(data + 2 * i) : 0;
        uint32_t l; char* u = reg_16to8(w, n16, &l); free(w);
        if (!l || u[l - 1]) u[l++] = 0;   /* reg_16to8 leaves room */
        reg_put(k, name, type, u, l); free(u);
    } else if (reg_is_str(type)) {
        uint8_t* t = malloc(cb + 1); if (cb && data) memcpy(t, M + data, cb); uint32_t l = cb;
        if (!l || t[l - 1]) t[l++] = 0;
        reg_put(k, name, type, t, l); free(t);
    } else reg_put(k, name, type, data ? M + data : (const uint8_t*)"", data ? cb : 0);
    RegVal* v = reg_val(k, name);
    if (reg_tr(1)) {
        if (reg_is_str(type)) fprintf(stderr, "[reg] set %s \"%s\" = \"%.80s\"\n", g_rk[k].path, name, (char*)v->data);
        else if (type == 4 && cb == 4) fprintf(stderr, "[reg] set %s \"%s\" = dword:%08x\n", g_rk[k].path, name, rd32(data));
        else fprintf(stderr, "[reg] set %s \"%s\" = type %u, %u bytes\n", g_rk[k].path, name, type, cb);
    }
    reg_save();
    return 0;
}
/* copy value v to guest (type, data, pcb); wide: string types as UTF-16 */
static uint32_t reg_out(RegVal* v, uint32_t ptype, uint32_t pdata, uint32_t pcb, int wide) {
    const uint8_t* b = v->data; uint32_t need = v->len; uint16_t* w = NULL;
    if (wide && reg_is_str(v->type)) { size_t n16; w = reg_8to16(v->data, v->len, &n16); b = (const uint8_t*)w; need = (uint32_t)(2 * n16); }
    if (ptype) wr32(ptype, v->type);
    uint32_t r = 0;
    if (pdata) { if (!pcb || rd32(pcb) < need) r = 234; else memcpy(M + pdata, b, need); }
    if (pcb) wr32(pcb, need);
    free(w); return r;
}
static uint32_t reg_query_core(uint32_t h, const char* name, uint32_t ptype, uint32_t pdata, uint32_t pcb, int wide) {
    reg_load();
    int k = reg_key_of(h); if (k < 0) return 6;
    RegVal* v = reg_val(k, name);
    if (!v) { if (reg_tr(2)) fprintf(stderr, "[reg] query %s \"%s\" -> not found\n", g_rk[k].path, name); return 2; }
    uint32_t r = reg_out(v, ptype, pdata, pcb, wide);
    if (reg_tr(2)) fprintf(stderr, "[reg] query %s \"%s\" -> %s\n", g_rk[k].path, name, r ? "more data" : reg_is_str(v->type) ? (char*)v->data : "ok");
    return r;
}
static int reg_child(int k, uint32_t idx) {   /* idx-th immediate subkey of k */
    size_t pl = strlen(g_rk[k].path); uint32_t n = 0;
    for (int i = 0; i < g_nrk; i++) {
        const char* p = g_rk[i].path;
        if (g_rk[i].deleted || strncasecmp(p, g_rk[k].path, pl) || p[pl] != '\\' || strchr(p + pl + 1, '\\')) continue;
        if (n++ == idx) return i;
    }
    return -1;
}
static void reg_out_name(const char* s, uint32_t buf, uint32_t cch_in, int wide) {
    if (wide) { size_t n16; uint16_t* w = reg_8to16((const uint8_t*)s, strlen(s), &n16); for (size_t i = 0; i <= n16 && i < cch_in; i++) wr16(buf + 2 * i, i < n16 ? w[i] : 0); free(w); }
    else { size_t n = strlen(s); if (n + 1 <= cch_in) memcpy(M + buf, s, n + 1); }
}
static size_t reg_name_len(const char* s, int wide) { if (!wide) return strlen(s); size_t n16; free(reg_8to16((const uint8_t*)s, strlen(s), &n16)); return n16; }

/* ---- exports ---- */
static void a_RegOpenKeyExA(CPU* c) { RET(reg_open_core(ARG(0), ARG(1) ? GSTR(ARG(1)) : "", 0, ARG(4), 0, "RegOpenKeyExA")); }
static void a_RegOpenKeyA(CPU* c) { RET(reg_open_core(ARG(0), ARG(1) ? GSTR(ARG(1)) : "", 0, ARG(2), 0, "RegOpenKeyA")); }
static void a_RegCreateKeyExA(CPU* c) { RET(reg_open_core(ARG(0), ARG(1) ? GSTR(ARG(1)) : "", 1, ARG(7), ARG(8), "RegCreateKeyExA")); }
static void a_RegCreateKeyA(CPU* c) { RET(reg_open_core(ARG(0), ARG(1) ? GSTR(ARG(1)) : "", 1, ARG(2), 0, "RegCreateKeyA")); }
static void a_RegOpenKeyExW(CPU* c) { char k[640]; reg_gw2u(ARG(1), k, sizeof k); RET(reg_open_core(ARG(0), k, 0, ARG(4), 0, "RegOpenKeyExW")); }
static void a_RegCreateKeyExW(CPU* c) { char k[640]; reg_gw2u(ARG(1), k, sizeof k); RET(reg_open_core(ARG(0), k, 1, ARG(7), ARG(8), "RegCreateKeyExW")); }
static void a_RegCloseKey(CPU* c) {
    uint32_t h = ARG(0);
    if (reg_root_name(h)) { RET(0); return; }
    for (int i = 0; i < REG_MAXH; i++) if (g_rh[i].h == h) { g_rh[i].h = 0; RET(0); return; }
    RET(6);
}
static void a_RegFlushKey(CPU* c) { reg_load(); reg_save(); RET(0); }
static void a_RegSetValueExA(CPU* c) { RET(reg_set_core(ARG(0), ARG(1) ? GSTR(ARG(1)) : "", ARG(3), ARG(4), ARG(5), 0)); }
static void a_RegSetValueExW(CPU* c) { char n[320]; reg_gw2u(ARG(1), n, sizeof n); RET(reg_set_core(ARG(0), n, ARG(3), ARG(4), ARG(5), 1)); }
static void a_RegQueryValueExA(CPU* c) { RET(reg_query_core(ARG(0), ARG(1) ? GSTR(ARG(1)) : "", ARG(3), ARG(4), ARG(5), 0)); }
static void a_RegQueryValueExW(CPU* c) { char n[320]; reg_gw2u(ARG(1), n, sizeof n); RET(reg_query_core(ARG(0), n, ARG(3), ARG(4), ARG(5), 1)); }
static uint32_t reg_delval(uint32_t h, const char* name) {
    reg_load(); int k = reg_key_of(h); if (k < 0) return 6;
    if (!reg_del_val(k, name)) return 2;
    if (reg_tr(1)) fprintf(stderr, "[reg] delete %s \"%s\"\n", g_rk[k].path, name);
    reg_save(); return 0;
}
static void a_RegDeleteValueA(CPU* c) { RET(reg_delval(ARG(0), ARG(1) ? GSTR(ARG(1)) : "")); }
static void a_RegDeleteValueW(CPU* c) { char n[320]; reg_gw2u(ARG(1), n, sizeof n); RET(reg_delval(ARG(0), n)); }
static uint32_t reg_delkey(uint32_t h, const char* sub) {
    reg_load(); int b = reg_key_of(h); if (b < 0) return 6;
    char path[320]; reg_join(path, sizeof path, g_rk[b].path, sub);
    int k = reg_find(path); if (k < 0) return 2;
    if (reg_child(k, 0) >= 0) return 5;   /* RegDeleteKey: no subkeys allowed */
    g_rk[k].deleted = 1;
    if (reg_tr(1)) fprintf(stderr, "[reg] delete key %s\n", path);
    reg_save(); return 0;
}
static void a_RegDeleteKeyA(CPU* c) { RET(reg_delkey(ARG(0), ARG(1) ? GSTR(ARG(1)) : "")); }
static void a_RegDeleteKeyW(CPU* c) { char n[640]; reg_gw2u(ARG(1), n, sizeof n); RET(reg_delkey(ARG(0), n)); }
/* RegEnumValue(h, idx, name, pcchName, reserved, type, data, pcbData) */
static uint32_t reg_enumval(CPU* c, int wide) {
    reg_load(); int k = reg_key_of(ARG(0)); if (k < 0) return 6;
    uint32_t idx = ARG(1); if (idx >= (uint32_t)g_rk[k].nv) return 259;
    RegVal* v = &g_rk[k].v[idx];
    size_t nl = reg_name_len(v->name, wide); uint32_t cch = ARG(3) ? rd32(ARG(3)) : 0;
    if (nl + 1 > cch) return 234;
    reg_out_name(v->name, ARG(2), cch, wide); wr32(ARG(3), (uint32_t)nl);
    return reg_out(v, ARG(5), ARG(6), ARG(7), wide);
}
static void a_RegEnumValueA(CPU* c) { RET(reg_enumval(c, 0)); }
static void a_RegEnumValueW(CPU* c) { RET(reg_enumval(c, 1)); }
/* RegEnumKeyEx(h, idx, name, pcchName, reserved, class, pcchClass, ftime) / RegEnumKey(h, idx, name, cchName) */
static uint32_t reg_enumkey(CPU* c, int wide, int ex) {
    reg_load(); int k = reg_key_of(ARG(0)); if (k < 0) return 6;
    int ch = reg_child(k, ARG(1)); if (ch < 0) return 259;
    const char* nm = strrchr(g_rk[ch].path, '\\') + 1;
    size_t nl = reg_name_len(nm, wide); uint32_t cch = ex ? (ARG(3) ? rd32(ARG(3)) : 0) : ARG(3);
    if (nl + 1 > cch) return 234;
    reg_out_name(nm, ARG(2), cch, wide);
    if (ex) { wr32(ARG(3), (uint32_t)nl); if (ARG(6)) wr32(ARG(6), 0); if (ARG(5) && ARG(6)) {} if (ARG(7)) { wr32(ARG(7), 0); wr32(ARG(7) + 4, 0); } }
    return 0;
}
static void a_RegEnumKeyExA(CPU* c) { RET(reg_enumkey(c, 0, 1)); }
static void a_RegEnumKeyExW(CPU* c) { RET(reg_enumkey(c, 1, 1)); }
static void a_RegEnumKeyA(CPU* c) { RET(reg_enumkey(c, 0, 0)); }
/* RegQueryInfoKey(h, class, pcchClass, reserved, pcSubKeys, pcbMaxSubKeyLen, pcbMaxClassLen, pcValues,
 *                 pcbMaxValueNameLen, pcbMaxValueLen, pcbSecurityDescriptor, lpftLastWriteTime) */
static uint32_t reg_info(CPU* c, int wide) {
    reg_load(); int k = reg_key_of(ARG(0)); if (k < 0) return 6;
    uint32_t nsub = 0, maxsub = 0, maxvn = 0, maxvl = 0;
    for (int ch; (ch = reg_child(k, nsub)) >= 0; nsub++) { size_t l = reg_name_len(strrchr(g_rk[ch].path, '\\') + 1, wide); if (l > maxsub) maxsub = (uint32_t)l; }
    for (int i = 0; i < g_rk[k].nv; i++) {
        RegVal* v = &g_rk[k].v[i]; size_t l = reg_name_len(v->name, wide); if (l > maxvn) maxvn = (uint32_t)l;
        uint32_t dl = v->len; if (wide && reg_is_str(v->type)) dl = (uint32_t)(2 * reg_name_len((char*)v->data, 1) + 2); if (dl > maxvl) maxvl = dl;
    }
    if (ARG(2)) { if (ARG(1) && rd32(ARG(2))) { if (wide) wr16(ARG(1), 0); else wr8(ARG(1), 0); } wr32(ARG(2), 0); }
    if (ARG(4)) wr32(ARG(4), nsub); if (ARG(5)) wr32(ARG(5), maxsub); if (ARG(6)) wr32(ARG(6), 0);
    if (ARG(7)) wr32(ARG(7), (uint32_t)g_rk[k].nv); if (ARG(8)) wr32(ARG(8), maxvn); if (ARG(9)) wr32(ARG(9), maxvl);
    if (ARG(10)) wr32(ARG(10), 0); if (ARG(11)) { wr32(ARG(11), 0); wr32(ARG(11) + 4, 0); }
    return 0;
}
static void a_RegQueryInfoKeyA(CPU* c) { RET(reg_info(c, 0)); }
static void a_RegQueryInfoKeyW(CPU* c) { RET(reg_info(c, 1)); }

#define HLE_REG_DEFS \
    { "advapi32.dll", "RegOpenKeyExA", 5, a_RegOpenKeyExA }, { "advapi32.dll", "RegOpenKeyA", 3, a_RegOpenKeyA }, \
    { "advapi32.dll", "RegCreateKeyExA", 9, a_RegCreateKeyExA }, { "advapi32.dll", "RegCreateKeyA", 3, a_RegCreateKeyA }, \
    { "advapi32.dll", "RegOpenKeyExW", 5, a_RegOpenKeyExW }, { "advapi32.dll", "RegCreateKeyExW", 9, a_RegCreateKeyExW }, \
    { "advapi32.dll", "RegCloseKey", 1, a_RegCloseKey }, { "advapi32.dll", "RegFlushKey", 1, a_RegFlushKey }, \
    { "advapi32.dll", "RegSetValueExA", 6, a_RegSetValueExA }, { "advapi32.dll", "RegSetValueExW", 6, a_RegSetValueExW }, \
    { "advapi32.dll", "RegQueryValueExA", 6, a_RegQueryValueExA }, { "advapi32.dll", "RegQueryValueExW", 6, a_RegQueryValueExW }, \
    { "advapi32.dll", "RegDeleteValueA", 2, a_RegDeleteValueA }, { "advapi32.dll", "RegDeleteValueW", 2, a_RegDeleteValueW }, \
    { "advapi32.dll", "RegDeleteKeyA", 2, a_RegDeleteKeyA }, { "advapi32.dll", "RegDeleteKeyW", 2, a_RegDeleteKeyW }, \
    { "advapi32.dll", "RegEnumValueA", 8, a_RegEnumValueA }, { "advapi32.dll", "RegEnumValueW", 8, a_RegEnumValueW }, \
    { "advapi32.dll", "RegEnumKeyExA", 8, a_RegEnumKeyExA }, { "advapi32.dll", "RegEnumKeyExW", 8, a_RegEnumKeyExW }, \
    { "advapi32.dll", "RegEnumKeyA", 4, a_RegEnumKeyA }, \
    { "advapi32.dll", "RegQueryInfoKeyA", 12, a_RegQueryInfoKeyA }, { "advapi32.dll", "RegQueryInfoKeyW", 12, a_RegQueryInfoKeyW },
/* tests: forget everything in memory (next call reloads import.reg + registry.reg) */
void hle_reg_reset(void) {
    for (int i = 0; i < g_nrk; i++) { for (int j = 0; j < g_rk[i].nv; j++) free(g_rk[i].v[j].data); free(g_rk[i].v); }
    g_nrk = 0; g_reg_loaded = 0; memset(g_rh, 0, sizeof g_rh);
}
