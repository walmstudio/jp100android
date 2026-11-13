/* test_hledll: LoadLibraryA/GetModuleHandle/GetProcAddress for HLE-only DLLs (no guest image).
 * JP100 probes d3d9.dll with LoadLibraryA before Direct3DCreate9; it used to get NULL -> "Please install Direct3D".
 * Host only, no game files. */
#include "rt.h"
#include "hle.h"
#include <stdio.h>
#include <string.h>

static int g_fail, g_ok;
#define CHECK(c, ...) do { if (c) g_ok++; else { g_fail++; fprintf(stderr, "FAIL %d: ", __LINE__); fprintf(stderr, __VA_ARGS__); fputc('\n', stderr); } } while (0)

int main(void) {
    uint32_t h = hle_load_library("d3d9.dll");
    CHECK(h != 0, "LoadLibraryA(d3d9.dll) = %08x", h);
    CHECK(hle_load_library("D3D9.DLL") == h, "case-insensitive, same handle");
    CHECK(hle_load_library("C:\\Windows\\System32\\d3d9.dll") == h, "full path");
    CHECK(hle_module_handle("d3d9") == h, "GetModuleHandle(d3d9) without extension");
    uint32_t t = hle_get_proc(h, "Direct3DCreate9", 0);
    CHECK(t && t == hle_resolve_impl("d3d9.dll", "Direct3DCreate9"), "GetProcAddress(Direct3DCreate9) = %08x", t);
    CHECK(hle_get_proc(h, "NoSuchExport", 0) == 0, "unknown export -> 0");
    CHECK(strstr(hle_module_path(h), "d3d9.dll") != NULL, "GetModuleFileName -> %s", hle_module_path(h));
    uint32_t di = hle_load_library("dinput8.dll");
    CHECK(di && di != h, "dinput8.dll distinct handle %08x", di);
    CHECK(hle_load_library("definitely_missing.dll") == 0, "missing DLL still NULL");
    CHECK(hle_module_handle("kernel32.dll") == 0x7C800000u, "kernel32 unchanged");
    printf("test_hledll: %d ok, %d failed\n", g_ok, g_fail);
    return g_fail != 0;
}
