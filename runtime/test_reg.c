/* test_reg: persistent registry (hle_reg.inc). Host only, no game files.
 * JP100 writes HKCU\Software\Telltale Games\JPScenario "Unlocked Episode 1"="1" and reads it back to enable
 * "Start episode"; the old stubs dropped the write. */
#include "rt.h"
#include "hle.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <unistd.h>

static int g_fail, g_ok;
#define CHECK(c, ...) do { if (c) g_ok++; else { g_fail++; fprintf(stderr, "FAIL %d: ", __LINE__); fprintf(stderr, __VA_ARGS__); fputc('\n', stderr); } } while (0)
static uint32_t api(const char* name, int n, ...) {
    uint32_t a[16]; va_list ap; va_start(ap, n); for (int i = 0; i < n; i++) a[i] = va_arg(ap, uint32_t); va_end(ap);
    return hle_call("advapi32.dll", name, n, a);
}
static uint32_t gs(const char* s) { uint32_t p = ghalloc(strlen(s) + 1, 1); memcpy(M + p, s, strlen(s) + 1); return p; }

int main(void) {
    rt_init();
    char dir[] = "/tmp/test_reg_XXXXXX"; if (!mkdtemp(dir)) return 2;
    char fn[256]; snprintf(fn, sizeof fn, "%s/import.reg", dir);
    FILE* f = fopen(fn, "wb");   /* Wine user.reg excerpt (from the user's prefix) + a key outside the filter */
    fputs("WINE REGISTRY Version 2\n;; All keys relative to \\\\User\\\\S-1-5-21-1-2-3-1000\n\n#arch=win32\n\n"
          "[Software\\\\Telltale Games\\\\JPGraphics] 1794221286\n#time=1dd7037cdce049c\n\"Aftereffects\"=\"true\"\n\"RenderQuality\"=\"9\"\n\n"
          "[Software\\\\Telltale Games\\\\Launcher] 1794221279\n\"password\"=\"3712053306\"\n\"Bin\"=hex:01,02,\\\n  03\n\"D\"=dword:0000002a\n\n"
          "[Software\\\\Wine\\\\Debug] 1793488226\n\"x\"=\"y\"\n", f);
    fclose(f);
    hle_fs_init(dir, NULL, NULL);
    uint32_t ph = ghalloc(4, 1), pt = ghalloc(4, 1), pcb = ghalloc(4, 1), buf = ghalloc(256, 1), disp = ghalloc(4, 1);
    const uint32_t HKCU = 0x80000001u;

    CHECK(api("RegOpenKeyExA", 5, HKCU, gs("Software\\Telltale Games\\JPScenario\\"), 0, 0x20019, ph) == 2, "JPScenario absent before first run");
    CHECK(api("RegCreateKeyExA", 9, HKCU, gs("Software\\Telltale Games\\JPScenario"), 0, 0, 0, 0xF003F, 0, ph, disp) == 0, "create");
    CHECK(rd32(disp) == 1, "REG_CREATED_NEW_KEY");
    uint32_t h = rd32(ph);
    CHECK(api("RegSetValueExA", 6, h, gs("Unlocked Episode 1"), 0, 1, gs("1"), 2) == 0, "set");
    CHECK(api("RegCloseKey", 1, h) == 0, "close");
    CHECK(api("RegOpenKeyExA", 5, HKCU, gs("Software\\Telltale Games\\JPScenario\\"), 0, 0x20019, ph) == 0, "open after create (trailing backslash)");
    h = rd32(ph);
    wr32(pcb, 256); memset(M + buf, 0, 256);
    CHECK(api("RegQueryValueExA", 6, h, gs("unlocked episode 1"), 0, pt, buf, pcb) == 0, "query (case-insensitive name)");
    CHECK(rd32(pt) == 1 && rd32(pcb) == 2 && !strcmp((char*)M + buf, "1"), "value \"1\" type %u cb %u", rd32(pt), rd32(pcb));
    wr32(pcb, 1);
    CHECK(api("RegQueryValueExA", 6, h, gs("Unlocked Episode 1"), 0, pt, buf, pcb) == 234 && rd32(pcb) == 2, "small buffer -> ERROR_MORE_DATA");
    CHECK(api("RegQueryValueExA", 6, h, gs("Unlocked Episode 2"), 0, pt, buf, pcb) == 2, "missing value");
    uint32_t d = 7, pd = ghalloc(4, 1); wr32(pd, d);
    CHECK(api("RegSetValueExA", 6, h, gs("Score"), 0, 4, pd, 4) == 0, "dword set");

    /* import (filtered) */
    CHECK(api("RegOpenKeyExA", 5, HKCU, gs("Software\\Telltale Games\\JPGraphics"), 0, 0x20019, ph) == 0, "imported JPGraphics");
    uint32_t hg = rd32(ph); wr32(pcb, 256);
    CHECK(api("RegQueryValueExA", 6, hg, gs("RenderQuality"), 0, pt, buf, pcb) == 0 && !strcmp((char*)M + buf, "9"), "RenderQuality=9");
    CHECK(api("RegOpenKeyExA", 5, HKCU, gs("Software\\Telltale Games\\Launcher"), 0, 0x20019, ph) == 0, "Launcher");
    uint32_t hl = rd32(ph); wr32(pcb, 256);
    CHECK(api("RegQueryValueExA", 6, hl, gs("Bin"), 0, pt, buf, pcb) == 0 && rd32(pt) == 3 && rd32(pcb) == 3 && M[buf + 2] == 3, "hex continuation");
    wr32(pcb, 4);
    CHECK(api("RegQueryValueExA", 6, hl, gs("D"), 0, pt, buf, pcb) == 0 && rd32(buf) == 42, "dword import");
    CHECK(api("RegOpenKeyExA", 5, HKCU, gs("Software\\Wine\\Debug"), 0, 0x20019, ph) == 2, "filter: Wine keys not imported");

    /* W side */
    uint32_t wk = hle_wstr("Software\\Telltale Games\\JPScenario"), wn = hle_wstr("Unlocked Episode 1");
    CHECK(api("RegOpenKeyExW", 5, HKCU, wk, 0, 0x20019, ph) == 0, "RegOpenKeyExW");
    uint32_t hw = rd32(ph); wr32(pcb, 256);
    CHECK(api("RegQueryValueExW", 6, hw, wn, 0, pt, buf, pcb) == 0 && rd32(pcb) == 4 && rd16(buf) == '1' && rd16(buf + 2) == 0, "W query -> UTF-16 (cb %u)", rd32(pcb));
    uint32_t wv = hle_wstr("\xd0\xa0\xd0\xb5\xd0\xba\xd1\x81");   /* "Рекс" */
    CHECK(api("RegSetValueExW", 6, hw, hle_wstr("Name"), 0, 1, wv, 10) == 0, "W set");

    /* enum */
    uint32_t pc = ghalloc(4, 1); wr32(pc, 64);
    CHECK(api("RegOpenKeyExA", 5, HKCU, gs("Software\\Telltale Games"), 0, 0x20019, ph) == 0, "open parent");
    uint32_t hp = rd32(ph), nk = 0;
    for (;; nk++) { wr32(pc, 64); if (api("RegEnumKeyExA", 8, hp, nk, buf, pc, 0, 0, 0, 0)) break; }
    CHECK(nk == 3, "3 subkeys (JPGraphics, Launcher, JPScenario), got %u", nk);
    uint32_t nv = 0; for (;; nv++) { wr32(pc, 64); wr32(pcb, 256); if (api("RegEnumValueA", 8, h, nv, buf, pc, 0, pt, ghalloc(256, 1), pcb)) break; }
    CHECK(nv == 3, "JPScenario has 3 values, got %u", nv);

    /* persistence: reload from registry.reg */
    char rf[300]; snprintf(rf, sizeof rf, "%s/registry.reg", dir);
    CHECK(access(rf, R_OK) == 0, "registry.reg written");
    unlink(fn);   /* import gone: values must come from registry.reg */
    hle_reg_reset();
    CHECK(api("RegOpenKeyExA", 5, HKCU, gs("Software\\Telltale Games\\JPScenario"), 0, 0x20019, ph) == 0, "reopen after reload");
    h = rd32(ph); wr32(pcb, 256);
    CHECK(api("RegQueryValueExA", 6, h, gs("Unlocked Episode 1"), 0, pt, buf, pcb) == 0 && !strcmp((char*)M + buf, "1"), "persisted Unlocked Episode 1");
    wr32(pcb, 4);
    CHECK(api("RegQueryValueExA", 6, h, gs("Score"), 0, pt, buf, pcb) == 0 && rd32(pt) == 4 && rd32(buf) == 7, "persisted dword");
    wr32(pcb, 256);
    CHECK(api("RegQueryValueExW", 6, h, hle_wstr("Name"), 0, pt, buf, pcb) == 0 && rd32(pcb) == 10 && rd16(buf) == 0x420, "persisted Cyrillic");
    CHECK(api("RegOpenKeyExA", 5, HKCU, gs("Software\\Telltale Games\\JPGraphics"), 0, 0x20019, ph) == 0, "imported key persisted");
    CHECK(api("RegDeleteValueA", 2, h, gs("Score")) == 0, "delete value");
    CHECK(api("RegOpenKeyExA", 5, 0x80000002u, gs("Software\\Ageia Technologies"), 0, 0x20019, ph) == 0, "HKLM Ageia (ME1) kept");
    CHECK(api("RegCloseKey", 1, 0x12345678u) == 6, "bad handle");
    printf("test_reg: %d ok, %d failed (%s)\n", g_ok, g_fail, dir);
    return g_fail != 0;
}
