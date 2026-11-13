/* Jurassic Park: The Game (Telltale 2011), episode 100 (JurassicPark100.exe): recompiled exe + FMOD Ex + Bink 1.8,
 * Win32/D3D9/msvcrt through HLE.  argv: <game dir> <docs dir> [image dir] [-- game args]
 * -DME1_LIB: built into libme1game.so (Android): main() becomes me1_exe_main(), called by game_jni.c */
#include "rt.h"
#include "hle.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <signal.h>
#include <unistd.h>
#include <pthread.h>
#include <dirent.h>
#define MODDECL(p) extern const FuncEntry p##_funcs[]; extern const unsigned p##_nfuncs; \
    extern const ImportEntry p##_imports[]; extern const unsigned p##_nimports; \
    extern const ExportEntry p##_exports[]; extern const uint32_t p##_base, p##_entry, p##_image_size, p##_image_crc;
MODDECL(fmod) MODDECL(bk) MODDECL(jp)
#define MODINIT(mod, p, nm) do { (mod)->name = nm; (mod)->base = p##_base; (mod)->size = p##_image_size; (mod)->entry = p##_entry; \
    (mod)->funcs = p##_funcs; (mod)->nfuncs = p##_nfuncs; (mod)->imports = p##_imports; (mod)->nimports = p##_nimports; \
    (mod)->exports = p##_exports; rt_expect_image_crc(p##_image_crc); } while (0)
static Module fm, bk, exe;
static const char* g_game;
static int file_ok(const char* p) { FILE* f = fopen(p, "rb"); if (f) fclose(f); return f != NULL; }
/* <dir>/<fn>, also with another letter case (Android storage is case-sensitive) */
static int find_ci(const char* dir, const char* fn, char* out, size_t n) {
    snprintf(out, n, "%s/%s", dir, fn);
    if (file_ok(out)) return 1;
    DIR* d = opendir(dir); if (!d) return 0;
    struct dirent* e; int ok = 0;
    while ((e = readdir(d))) if (!strcasecmp(e->d_name, fn)) { snprintf(out, n, "%s/%s", dir, e->d_name); ok = file_ok(out); break; }
    closedir(d);
    return ok;
}
static void image_path(char* img, size_t n, const char* dir, const char* sub, const char* file) {
    snprintf(img, n, "%s/%s/image.bin", dir, sub);
    if (file_ok(img)) return;
    if (!find_ci(g_game, file, img, n)) snprintf(img, n, "%s/%s", g_game, file);
}
static void load(Module* m, const char* dir, const char* sub, const char* file) {
    char img[1024], win[300]; image_path(img, sizeof img, dir, sub, file);
    snprintf(win, sizeof win, "C:\\JPTelltale\\%s", file);
    fprintf(stderr, "[jp] %s <- %s\n", m->name, img);
    hle_register_module(m, win);
    rt_load_module(m, img);
}
static int try_load(Module* m, const char* dir, const char* sub, const char* file) {
    char img[1024], win[300]; image_path(img, sizeof img, dir, sub, file);
    snprintf(win, sizeof win, "C:\\JPTelltale\\%s", file);
    if (!rt_try_load_module(m, img)) { fprintf(stderr, "[jp] %s: %s missing or not the expected build: not loaded\n", m->name, img); return 0; }
    hle_register_module(m, win);
    return 1;
}
static uint32_t entry_ret;
static void* run_entry(void* p) { (void)p; hle_cpu_place(2); entry_ret = rt_call(&g_cpu, exe.entry, 0, NULL); return NULL; }
static void on_term(int sig) {
    fprintf(stderr, "\n[jp] signal %d: stopping\n", sig);
    hle_hb_now("(signal)");
    hle_report(); fflush(stdout); fflush(stderr); _exit(128 + sig);
}
#ifdef ME1_LIB
__attribute__((visibility("default"))) void me1_exe_on_signal(int sig) { on_term(sig); }
__attribute__((visibility("default"))) int me1_exe_main(int argc, char** argv) {
#else
int main(int argc, char** argv) {
    signal(SIGTERM, on_term); signal(SIGINT, on_term);
#endif
    if (argc < 3) { fprintf(stderr, "usage: %s <game dir> <docs dir> [image dir] [-- game args]\n", argv[0]); return 2; }
    const char* imgdir = argc > 3 && strcmp(argv[3], "--") && strcmp(argv[3], "-") ? argv[3] : "out";
    static char cmd[2048] = "\"C:\\JPTelltale\\JurassicPark100.exe\"";
    for (int i = 3; i < argc; i++) if (!strcmp(argv[i], "--")) {
        for (int j = i + 1; j < argc; j++) { strncat(cmd, " ", sizeof cmd - strlen(cmd) - 1); strncat(cmd, argv[j], sizeof cmd - strlen(cmd) - 1); }
        break;
    }
    g_game = argv[1];
    fprintf(stderr, "[jp] Jurassic Park: The Game (ep. 100) port\n[jp] game %s\n[jp] docs %s\n[jp] images %s\n", argv[1], argv[2], imgdir);
    /* The SDL frontend already defaults to 2, but Android / direct library entry did not.
     * Respect an explicit user override; do not silently replace diagnostic settings. */
    if (!getenv("ME1_FLOAT_EMU")) setenv("ME1_FLOAT_EMU", "2", 0);
    rt_init();
    hle_fs_init(argv[1], argv[2], NULL);
    hle_set_cmdline(cmd);
    hle_exe_base = jp_base;
    hle_exe_path = "C:\\JPTelltale\\JurassicPark100.exe";
    MODINIT(&fm, fmod, "fmodex.dll"); load(&fm, imgdir, "fmodex", "fmodex.dll");
    hle_jp_fix_data_imports(&fm);
    int have_bink = 0;
    const char* eb = getenv("ME1_BINK");
    if (!eb || strcmp(eb, "0")) { MODINIT(&bk, bk, "binkw32.dll"); have_bink = try_load(&bk, imgdir, "bink", "binkw32.dll"); }
    MODINIT(&exe, jp, "JurassicPark100.exe"); load(&exe, imgdir, "jp100", "JurassicPark100.exe");
    uint32_t r = hle_dll_attach(&fm); fprintf(stderr, "[jp] fmodex.dll at %08x, DllMain = %u\n", fm.base, r);
    if (have_bink) { r = hle_dll_attach(&bk); fprintf(stderr, "[jp] binkw32.dll at %08x, DllMain = %u\n", bk.base, r); }
    fprintf(stderr, "[jp] command line: %s\n[jp] entry %08x\n", cmd, exe.entry);
    hle_hb_start();
    hle_watch_start();
    pthread_attr_t a; pthread_attr_init(&a); pthread_attr_setstacksize(&a, 256u << 20);
    pthread_t th; pthread_create(&th, &a, run_entry, NULL); pthread_join(th, NULL); r = entry_ret;
    fprintf(stderr, "[jp] entry returned %u\n", r);
    hle_report();
    return (int)r;
}
