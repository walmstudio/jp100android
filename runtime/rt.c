/* ME1 recomp runtime core: arena, module loader, dispatch, faults. */
#define _GNU_SOURCE
#include "rt.h"
#include "hle.h"
#include "cpu_profile.h"
#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <time.h>
#include <strings.h>

uint8_t* M;
CPU g_cpu;                      /* main thread */
_Thread_local CPU* rt_tcpu;     /* guest threads (NULL on the main thread -> g_cpu) */
_Thread_local RtEhFrame* rt_eh_top;
_Thread_local int rt_eh_depth, rt_hle_depth;
/* misaligned LOCK operations: global spinlock (aligned ones use host atomics directly) */
static volatile int g_lk_spin;
void rt_lk_slow_lock(void) { while (__atomic_exchange_n(&g_lk_spin, 1, __ATOMIC_ACQUIRE)) while (__atomic_load_n(&g_lk_spin, __ATOMIC_RELAXED)) RT_PAUSE(); }
void rt_lk_slow_unlock(void) { __atomic_store_n(&g_lk_spin, 0, __ATOMIC_RELEASE); }
#define MAX_MODULES 32
static Module* g_mods[MAX_MODULES]; static int g_nmods;

#define TEB_VA     0x7FFDE000u
#define PEB_VA     0x7FFDF000u
#define STACK_TOP  0x00400000u   /* 3 MB main-thread guest stack (MassEffect.exe reserves 3 MB) */
#define STACK_SIZE 0x00300000u

void rt_init(void) {
    M = mmap(NULL, 1ull << 32, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE, -1, 0);
    if (M == MAP_FAILED) { perror("arena"); exit(1); }
    memset(&g_cpu, 0, sizeof g_cpu);
    g_cpu.esp = STACK_TOP - 16;
    g_cpu.fs_base = TEB_VA;
    rt_fninit(&g_cpu); g_cpu.fcw = 0x27F;      /* MSVC CRT default: 53-bit precision, round to nearest */
    g_cpu.mxcsr = 0x1F80;
    /* Initialize CPU profiling for Stage 8 optimization */
    cpu_prof_init();
    for (int i = 0; i < CPU_PROF_COUNT; i++) {
        const char* name = NULL;
        switch (i) {
            case CPU_PROF_FSIN: name = "rt_fsin"; break;
            case CPU_PROF_FCOS: name = "rt_fcos"; break;
            case CPU_PROF_FSINCOS: name = "rt_fsincos"; break;
            case CPU_PROF_FPTAN: name = "rt_fptan"; break;
            case CPU_PROF_FPATAN: name = "rt_fpatan"; break;
            case CPU_PROF_F2XM1: name = "rt_f2xm1"; break;
            case CPU_PROF_FYL2X: name = "rt_fyl2x"; break;
            case CPU_PROF_FYL2XP1: name = "rt_fyl2xp1"; break;
            case CPU_PROF_FRNDINT: name = "rt_fist"; break;
            case CPU_PROF_FSCALE: name = "rt_fscale"; break;
            case CPU_PROF_FXTRACT: name = "rt_fxtract"; break;
            case CPU_PROF_FPREM: name = "rt_fprem"; break;
            case CPU_PROF_FPREM1: name = "rt_fprem1"; break;
            case CPU_PROF_FXAM: name = "rt_fxam"; break;
            case CPU_PROF_FSQRT: name = "rt_fsqrt"; break;
            case CPU_PROF_FCHS: name = "rt_fchs"; break;
            case CPU_PROF_FABS: name = "rt_fabs"; break;
            case CPU_PROF_FIST16: name = "rt_fist16"; break;
            case CPU_PROF_FIST32: name = "rt_fist32"; break;
            case CPU_PROF_FIST64: name = "rt_fist64"; break;
        }
        if (name) cpu_prof_register(name);
    }
    wr32(TEB_VA + 0x00, 0xFFFFFFFFu);          /* SEH chain end */
    wr32(TEB_VA + 0x04, STACK_TOP);            /* stack base */
    wr32(TEB_VA + 0x08, STACK_TOP - STACK_SIZE);
    wr32(TEB_VA + 0x18, TEB_VA);               /* self */
    wr32(TEB_VA + 0x20, 0x1000);               /* pid */
    wr32(TEB_VA + 0x24, 0x1004);               /* tid */
    wr32(TEB_VA + 0x2C, TEB_VA + 0xE10);       /* TLS slot array */
    wr32(TEB_VA + 0x30, PEB_VA);
    hle_init();
}

static int fcmp(const void* k, const void* e) {
    uint32_t a = *(const uint32_t*)k, b = ((const FuncEntry*)e)->va;
    return a < b ? -1 : a > b;
}

static uint32_t g_expect_crc;
void rt_expect_image_crc(uint32_t crc) { g_expect_crc = crc; }
uint32_t rt_crc32(const uint8_t* p, size_t n) {
    static uint32_t T[256];
    if (!T[1]) for (uint32_t i = 0; i < 256; i++) { uint32_t c = i; for (int k = 0; k < 8; k++) c = c & 1 ? 0xEDB88320u ^ (c >> 1) : c >> 1; T[i] = c; }
    uint32_t c = 0xFFFFFFFFu;
    while (n--) c = T[(c ^ *p++) & 0xFF] ^ (c >> 8);
    return c ^ 0xFFFFFFFFu;
}
static uint32_t le32(const uint8_t* p) { return p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24; }
static uint16_t le16(const uint8_t* p) { return p[0] | p[1] << 8; }
/* Maps an original PE file (DLL/EXE from the game's Binaries folder) the way the Windows loader does:
 * headers + raw section data at their RVAs, base relocations when the build uses another base.
 * Returns 0 if the file is not a PE32 or does not match the recompiled module. */
static int map_pe(Module* m, const uint8_t* d, size_t n, const char* path) {
    if (n < 0x40 || d[0] != 'M' || d[1] != 'Z') return 0;
    uint32_t pe = le32(d + 0x3C);
    if (pe > n - 0xF8 || memcmp(d + pe, "PE\0\0", 4) || le16(d + pe + 24) != 0x10B) { fprintf(stderr, "[rt] %s: not a PE32 file\n", path); return 0; }
    unsigned nsec = le16(d + pe + 6), optsz = le16(d + pe + 20);
    const uint8_t* o = d + pe + 24;
    uint32_t ibase = le32(o + 28), isize = le32(o + 56), hsize = le32(o + 60), ndirs = le32(o + 92);
    if (isize != m->size) { fprintf(stderr, "[rt] %s: SizeOfImage %x, this build expects %x (different game version?)\n", path, isize, m->size); return 0; }
    uint8_t* img = M + m->base;
    memset(img, 0, m->size);
    memcpy(img, d, hsize < n && hsize < m->size ? hsize : 0x400);
    const uint8_t* sh = o + optsz;
    if ((size_t)(sh - d) + 40u * nsec > n) return 0;
    for (unsigned i = 0; i < nsec; i++) {
        const uint8_t* s = sh + 40 * i;
        uint32_t va = le32(s + 12), rs = le32(s + 16), rp = le32(s + 20);
        if (va >= m->size || rp >= n) continue;
        if (rs > m->size - va) rs = m->size - va;
        if (rs > n - rp) rs = (uint32_t)(n - rp);
        memcpy(img + va, d + rp, rs);
    }
    if (ibase != m->base && ndirs > 5) {     /* recompiled with --base: apply IMAGE_REL_BASED_HIGHLOW fixups */
        uint32_t rva = le32(o + 96 + 40), sz = le32(o + 96 + 44), delta = m->base - ibase, nfix = 0;
        for (uint32_t p = rva; p + 8 <= rva + sz && p + 8 <= m->size; ) {
            uint32_t page = le32(img + p), bsz = le32(img + p + 4);
            if (bsz < 8) break;
            for (uint32_t k = 8; k + 2 <= bsz; k += 2) {
                uint16_t e = le16(img + p + k);
                if (e >> 12 == 3 && page + (e & 0xFFF) + 4 <= m->size) {
                    uint8_t* q = img + page + (e & 0xFFF); uint32_t v = le32(q) + delta;
                    q[0] = v; q[1] = v >> 8; q[2] = v >> 16; q[3] = v >> 24; nfix++;
                }
            }
            p += bsz;
        }
        fprintf(stderr, "[rt] %s: rebased %08x -> %08x (%u fixups)\n", path, ibase, m->base, nfix);
    }
    return 1;
}

static int load_module(Module* m, const char* image_path, int soft);
void rt_load_module(Module* m, const char* image_path) { load_module(m, image_path, 0); }
int rt_try_load_module(Module* m, const char* image_path) { return load_module(m, image_path, 1); }
static int load_module(Module* m, const char* image_path, int soft) {
    FILE* f = fopen(image_path, "rb");
    if (!f) { perror(image_path); if (soft) { g_expect_crc = 0; return 0; } exit(1); }
    fseek(f, 0, SEEK_END); long fl = ftell(f); fseek(f, 0, SEEK_SET);
    if (fl <= 0) { fprintf(stderr, "empty image %s\n", image_path); fclose(f); if (soft) { g_expect_crc = 0; return 0; } exit(1); }
    uint8_t* buf = malloc((size_t)fl);
    size_t n = buf ? fread(buf, 1, (size_t)fl, f) : 0; fclose(f);
    if (n != (size_t)fl) { fprintf(stderr, "read error %s\n", image_path); exit(1); }
    if (n >= 2 && buf[0] == 'M' && buf[1] == 'Z' && n != m->size) {      /* original PE file */
        if (!map_pe(m, buf, n, image_path)) { free(buf); if (soft) { g_expect_crc = 0; return 0; } exit(1); }
    } else {                                                             /* image.bin written by recomp.py */
        if (n != m->size) { fprintf(stderr, "short image %s\n", image_path); free(buf); if (soft) { g_expect_crc = 0; return 0; } exit(1); }
        memcpy(M + m->base, buf, n);
    }
    free(buf);
    if (g_expect_crc) {
        uint32_t crc = rt_crc32(M + m->base, m->size);
        if (crc != g_expect_crc) {
            fprintf(stderr, "[rt] %s: image CRC %08x, this build was recompiled from %08x - the file is a different version "
                            "(the recompiled code would not match its data)\n", image_path, crc, g_expect_crc);
            g_expect_crc = 0;
            if (soft) { memset(M + m->base, 0, m->size); return 0; }
            exit(1);
        }
        g_expect_crc = 0;
    }
    hle_vmem_exclude(m->base, m->size);   /* fixed-base images (JurassicPark100.exe at 0x00400000) leave the VirtualAlloc arena */
    unsigned nguest = 0, nmiss = 0;
    for (unsigned i = 0; i < m->nimports; i++) {
        const ImportEntry* im = &m->imports[i];
        Module* dm = rt_module_by_name(im->dll);       /* statically linked guest DLL (msvcr80, msvcp80, ...) */
        uint32_t v = dm ? rt_find_export(dm, im->name) : 0;
        if (v && rt_import_prefer_hle && rt_import_prefer_hle(m, im->dll, im->name)) dm = NULL, v = 0;
        if (dm && !v) fprintf(stderr, "[rt] %s: import %s!%s not exported -> HLE\n", m->name, im->dll, im->name);
        if (v) nguest++;
        else if (!hle_resolve_impl(im->dll, im->name)) {   /* would fault when called: list them (ME1_LIST_MISSING=1 prints all) */
            static int all = -1; if (all < 0) all = getenv("ME1_LIST_MISSING") != NULL;
            if (all) fprintf(stderr, "[rt] %s: no HLE for %s!%s\n", m->name, im->dll, im->name);
            nmiss++;
        }
        wr32(im->slot, v ? v : hle_resolve(im->dll, im->name));
    }
    g_mods[g_nmods++] = m;
    fprintf(stderr, "[rt] loaded %s at %08x (%u funcs, %u imports, %u from guest DLLs, %u without HLE)\n", m->name, m->base, m->nfuncs, m->nimports, nguest, nmiss);
    return 1;
}

uint32_t rt_find_export(Module* m, const char* name) {
    for (const ExportEntry* e = m->exports; e->name; e++) if (!strcmp(e->name, name)) return e->va;
    return 0;
}

Module* rt_module_at(uint32_t va) {
    for (int i = 0; i < g_nmods; i++) if (va >= g_mods[i]->base && va - g_mods[i]->base < g_mods[i]->size) return g_mods[i];
    return NULL;
}
int (*rt_import_prefer_hle)(const Module* importer, const char* dll, const char* name);
Module* rt_module_by_name(const char* dll) {
    for (int i = 0; i < g_nmods; i++) if (!strcasecmp(g_mods[i]->name, dll)) return g_mods[i];
    /* d3dx9_31.dll links against the system msvcrt.dll: msvcr80 exports the same functions with the same ABI */
    if (!strcasecmp(dll, "msvcrt.dll")) return rt_module_by_name("msvcr80.dll");
    return NULL;
}
/* v0.10.13 (R3): direct-mapped cache of indirect-call targets (vtable calls, call [eax+N]) in front of the
   module scan + bsearch. Entries point into the static per-module function tables, which are never freed (no
   module unloading), so a stale entry is impossible; a slot is valid only when its va equals the target. */
#define DCACHE_BITS 16
static const FuncEntry* g_dcache[1u << DCACHE_BITS];
static const FuncEntry* lookup(uint32_t t) {
    uint32_t h = ((t >> 2) ^ (t >> (2 + DCACHE_BITS))) & ((1u << DCACHE_BITS) - 1);
    const FuncEntry* e = __atomic_load_n(&g_dcache[h], __ATOMIC_RELAXED);
    if (e && e->va == t) return e;
    Module* m = rt_module_at(t);
    e = m ? bsearch(&t, m->funcs, m->nfuncs, sizeof(FuncEntry), fcmp) : NULL;
    if (e) __atomic_store_n(&g_dcache[h], e, __ATOMIC_RELAXED);
    return e;
}

/* v6: ME1_ESP_TRACE=1 - ESP balance of every indirect/import call that goes through dispatch_call (call reg,
 * call [mem], call [IAT], jmp reg/mem). Per-thread ring of the last 256 calls, dumped by rt_esp_dump() on a
 * VC debug report / RTC failure and on an unhandled exception. A call site that cleans the stack itself
 * (add esp,N / pop ecx right after the call = cdecl) must see delta 4; anything else is logged as MISMATCH. */
typedef struct { uint32_t site, tgt, in, out; } EspRec;
static __thread EspRec g_espring[256];
static __thread unsigned g_espn;
static int g_esptr = -1;
static int esp_trace_on(void) {
    if (g_esptr < 0) {
        const char* s = getenv("ME1_ESP_TRACE");
        g_esptr = (s && *s && *s != '0') ? (*s == '2' ? 2 : 1) : 0;
        if (g_esptr) fprintf(stderr, "[esp] ESP trace on (ME1_ESP_TRACE)\n");
    }
    return g_esptr;
}
const char* hle_thunk_info(uint32_t t, int* nargs);
static const char* esp_tname(uint32_t t, char* b, size_t n) {
    if (!hle_is_thunk(t)) { snprintf(b, n, "guest"); return b; }
    int k = -1; const char* nm = hle_thunk_info(t, &k);
    snprintf(b, n, "HLE %s/%d", nm ? nm : "?", k);
    return b;
}
static int esp_site_cdecl(uint32_t site, uint32_t* clean) {
    if (!rt_module_at(site)) return 0;
    uint8_t a = rd8(site), b = rd8(site + 1);
    if (a == 0x83 && b == 0xC4) { *clean = rd8(site + 2); return 1; }   /* add esp, imm8 */
    if (a == 0x81 && b == 0xC4) { *clean = rd32(site + 2); return 1; }  /* add esp, imm32 */
    if (a == 0x59) { *clean = 4; return 1; }                             /* pop ecx */
    return 0;
}
static void esp_note(CPU* c, uint32_t site, uint32_t t, uint32_t in, uint32_t out) {
    EspRec* r = &g_espring[g_espn++ & 255];
    r->site = site; r->tgt = t; r->in = in; r->out = out;
    uint32_t d = out - in, clean = 0;
    int cd = esp_site_cdecl(site, &clean);
    if ((d & 3) || (int32_t)d < 4 || d > 0x400 || (cd && d != 4 && g_esptr > 1)) {   /* cdecl heuristic: MSVC merges cleanup with locals -> only ME1_ESP_TRACE=2 */
        static int cnt;
        if (__atomic_fetch_add(&cnt, 1, __ATOMIC_RELAXED) < 64) {
            char b[128];
            fprintf(stderr, "[esp] MISMATCH ret %08x -> %08x (%s): esp %08x -> %08x, popped %d%s (thread %u)\n",
                    site, t, esp_tname(t, b, sizeof b), in, out, (int32_t)d,
                    cd ? ", caller cleans itself (cdecl) -> expected 4" : "", rd32(c->fs_base + 0x24));
        }
    }
}
void rt_esp_dump(CPU* c, const char* why) {
    if (g_esptr <= 0) return;
    unsigned tid = rd32(c->fs_base + 0x24);
    unsigned n = g_espn < 256 ? g_espn : 256, show = n < 48 ? n : 48;
    fprintf(stderr, "[esp] --- %s: thread %u esp=%08x ebp=%08x, last %u indirect/import calls (newest last):\n",
            why, tid, c->esp, c->ebp, show);
    for (unsigned k = g_espn - show; k != g_espn; k++) {
        const EspRec* r = &g_espring[k & 255];
        uint32_t d = r->out - r->in, clean = 0; char b[128];
        int cd = esp_site_cdecl(r->site, &clean);
        fprintf(stderr, "[esp]   ret %08x -> %08x %-34s popped %3d%s\n", r->site, r->tgt, esp_tname(r->tgt, b, sizeof b),
                (int32_t)d, cd ? (d == 4 ? "  cdecl ok" : "  cdecl BAD") : "");
    }
    /* EBP chain: return addresses + call targets; for a frame of _RTC_CheckEsp the saved ESP difference */
    uint32_t f = c->ebp;
    for (int i = 0; i < 12 && f && f < 0xFFFFFF00u; i++) {
        uint32_t ret = rd32(f + 4), tgt = 0;
        if (!rt_module_at(ret)) break;
        if (rd8(ret - 5) == 0xE8) tgt = ret + rd32(ret - 4);
        fprintf(stderr, "[esp]   frame ebp=%08x ret %08x (call %08x)\n", f, ret, tgt);
#ifdef JP100
        if (tgt == 0x00fc9b7bu)   /* _RTC_CheckEsp: esp at the cmp = f + 8, the checked function's ebp = [f] */
            fprintf(stderr, "[esp]   RTC ESP check in caller of %08x: ebp=%08x esp=%08x -> esp off by %d bytes\n",
                    ret, rd32(f), f + 8, (int32_t)(rd32(f) - (f + 8)));
#endif
        uint32_t nf = rd32(f);
        if (nf <= f) break;
        f = nf;
    }
}

static void dispatch_call_raw(CPU* c, uint32_t t);
void dispatch_call(CPU* c, uint32_t t) {
    if (__builtin_expect(g_esptr == 0, 1) || !esp_trace_on()) { dispatch_call_raw(c, t); return; }
    uint32_t in = c->esp, site = rd32(in);
    dispatch_call_raw(c, t);
    esp_note(c, site, t, in, c->esp);
}
/* v6 (JP100): small x86 thunks the game writes into heap memory at run time (ATL/WTL window-procedure thunks: the
 * window gets subclassed with a generated stub that stores "this" and jumps to the real procedure; FlushInstructionCache
 * right before is the tell). They are not in the recompiled image, so interpret the few known shapes and continue at
 * the real target. Returns the final target (unchanged if t is not a known thunk). */
static uint32_t run_dyn_thunk(CPU* c, uint32_t t) {
    for (int k = 0; k < 4 && !hle_is_thunk(t) && !lookup(t); k++) {
        const uint8_t* b = M + t; uint32_t n;
        if (b[0] == 0xC7 && b[1] == 0x44 && b[2] == 0x24 && b[3] == 0x04) {          /* mov dword [esp+4], imm32 */
            wr32(c->esp + 4, rd32(t + 4)); n = t + 8;
        } else if (b[0] == 0xB9) { c->ecx = rd32(t + 1); n = t + 5;                   /* mov ecx, imm32 (thiscall) */
        } else if (b[0] == 0xBA) { c->edx = rd32(t + 1); n = t + 5;                   /* mov edx, imm32 */
        } else if (b[0] == 0x68 && b[5] == 0xC3) { t = rd32(t + 1); goto log;         /* push imm32; ret */
        } else if (b[0] == 0xFF && b[1] == 0x25) { t = rd32(rd32(t + 2)); goto log;   /* jmp [mem] */
        } else return t;
        b = M + n;
        if (b[0] == 0xE9) t = n + 5 + rd32(n + 1);                                    /* jmp rel32 */
        else if (b[0] == 0xB8 && b[5] == 0xFF && b[6] == 0xE0) { c->eax = rd32(n + 1); t = c->eax; }   /* mov eax, imm; jmp eax */
        else if (b[0] == 0x68 && b[5] == 0xC3) t = rd32(n + 1);                        /* push imm; ret */
        else if (b[0] == 0xFF && b[1] == 0x25) t = rd32(rd32(n + 2));                 /* jmp [mem] */
        else { t = n; continue; }                                                     /* next instruction: another thunk step */
    log:;
        static unsigned logged;
        if (logged < 8) { logged++; fprintf(stderr, "[rt] run-time thunk -> %08x (generated code interpreted)\n", (unsigned)t); }
    }
    return t;
}
static void dispatch_call_raw(CPU* c, uint32_t t) {
    if (hle_is_thunk(t)) { hle_invoke(c, t); return; }
    const FuncEntry* e = lookup(t);
    if (!e && rt_module_at(t) == 0) {   /* outside the loaded images: maybe generated code */
        uint32_t t2 = run_dyn_thunk(c, t);
        if (t2 != t) { if (hle_is_thunk(t2)) { hle_invoke(c, t2); return; } t = t2; e = lookup(t); }
    }
    if (!e) {
        /* v4: the address goes to seeds/jp100-extra.txt (build-*-jp100.sh also reads MISSING-FUNC lines from logs there) */
        fprintf(stderr, "[rt] MISSING-FUNC %08x (no recompiled function here: add it to seeds/jp100-extra.txt and rebuild)\n", (unsigned)t);
        fprintf(stderr, "[rt]   bytes at %08x:", (unsigned)t);
        for (int i = 0; i < 16; i++) fprintf(stderr, " %02x", rd8(t + i));
        fprintf(stderr, "\n");
        rt_fault(c, t, "dispatch: no function at target");
    }
    if (e->owner && rt_eh_depth) rt_eh_land(c, e);   /* exception transfer into a live frame (does not return if found) */
    e->fn(c);
}

/* v5: indirect jmp (switch table, jmp reg/mem): an unknown target also names the jmp instruction */
void dispatch_jmp_from(CPU* c, uint32_t t, uint32_t site) {
    if (!hle_is_thunk(t) && !lookup(t))
        fprintf(stderr, "[rt] MISSING-FUNC %08x <- jmp at %08x\n", (unsigned)t, (unsigned)site);
    dispatch_call(c, t);
}

void call_import(CPU* c, uint32_t slot) { dispatch_call(c, rd32(slot)); }

#define RET_SENTINEL 0xFFFFFFF0u
uint32_t rt_call(CPU* c, uint32_t va, int nargs, const uint32_t* args) {
    uint32_t sp = c->esp;
    for (int i = nargs - 1; i >= 0; i--) push32(c, args[i]);
    push32(c, RET_SENTINEL);
    dispatch_call(c, va);
    uint32_t used = sp - c->esp;   /* 0 for stdcall, 4*nargs for cdecl */
    if (used != 0 && used != 4u * nargs)
        fprintf(stderr, "[rt] warning: stack imbalance after %08x: %d bytes\n", va, (int)used);
    c->esp = sp;
    return c->eax;
}

void rt_fault(CPU* c, uint32_t eip, const char* why) {
    fprintf(stderr, "[rt] FAULT at %08x: %s\n  eax=%08x ecx=%08x edx=%08x ebx=%08x esp=%08x ebp=%08x esi=%08x edi=%08x\n",
            eip, why, c->eax, c->ecx, c->edx, c->ebx, c->esp, c->ebp, c->esi, c->edi);
    fprintf(stderr, "  stack:");
    for (int i = 0; i < 8; i++) fprintf(stderr, " %08x", rd32(c->esp + 4 * i));
    fprintf(stderr, "\n  code addresses on the stack (likely return addresses):");
    for (int i = 0, n = 0; i < 2048 && n < 32; i++) {
        uint32_t a = c->esp + 4 * i, v = rd32(a); Module* m = rt_module_at(v);
        if (m && v - m->base >= 0x1000) { fprintf(stderr, "%s%s+%x", n % 6 ? " " : "\n    ", m->name, v - m->base); n++; }
    }
    fprintf(stderr, "\n");
    void hle_hb_now(const char*); hle_hb_now("(fault)");
    void hle_report(void); hle_report();
    fflush(NULL); _Exit(3);   /* v3: no atexit handlers / static destructors while guest threads run */
}
void UNIMPL(CPU* c, uint32_t eip, const char* what) {
    char b[256]; snprintf(b, sizeof b, "unimplemented instruction: %s", what); rt_fault(c, eip, b);
}
void rt_cpuid(CPU* c) {
    uint32_t leaf = c->eax;
    if (leaf == 0) { c->eax = 1; c->ebx = 0x756e6547; c->edx = 0x49656e69; c->ecx = 0x6c65746e; }   /* GenuineIntel */
    else if (leaf == 1) { c->eax = 0x00000F29; c->ebx = 0; c->ecx = 0; c->edx = (1u<<0) | (1u<<4) | (1u<<8) | (1u<<15) | (1u<<23) | (1u<<24) | (1u<<25) | (1u<<26); } /* FPU TSC CX8 CMOV MMX FXSR SSE SSE2 */
    else { c->eax = c->ebx = c->ecx = c->edx = 0; }
}
void rt_rdtsc(CPU* c) {
    struct timespec ts; clock_gettime(CLOCK_MONOTONIC, &ts);
    uint64_t t = (uint64_t)ts.tv_sec * 2000000000ull + (uint64_t)ts.tv_nsec * 2;
    c->eax = (uint32_t)t; c->edx = (uint32_t)(t >> 32);
}
