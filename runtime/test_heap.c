/* v1.2.19: guest heap (hle_win32.c ghalloc/ghfree) - repeated "scene loads" must not grow the heap, blocks must not
 * overlap, sizes/zeroing must hold. Built like the other runtime tests: cc ... $(RT) runtime/test_heap.c */
#include "hle.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int fails;
#define CHECK(c, m) do { if (!(c)) { fprintf(stderr, "FAIL: %s\n", m); fails++; } } while (0)

static uint32_t rnd(void) { static uint32_t s = 12345; s = s * 1103515245u + 12345u; return s >> 8; }

int main(void) {
    rt_init();
    /* basic */
    uint32_t a = ghalloc(100, 1), b = ghalloc(5000, 0);
    CHECK(a && b && a != b, "alloc");
    CHECK(ghsize(a) == 100 && ghsize(b) == 5000, "ghsize");
    for (int i = 0; i < 100; i++) CHECK(M[a + i] == 0, "zeroed");
    ghfree(a); ghfree(b);

    /* scene loads: many small objects + a few big resources, all freed, 20 times */
    enum { N = 4000 };
    static uint32_t p[N], sz[N];
    uint64_t top1 = 0, fr, t; uint32_t nf;
    for (int load = 0; load < 20; load++) {
        for (int i = 0; i < N; i++) {
            sz[i] = (i % 97 == 0) ? (1u << 20) + rnd() % (3u << 20) : 16 + rnd() % 2000;   /* ~41 blocks of 1-4 MB */
            p[i] = ghalloc(sz[i], 0);
            CHECK(p[i], "alloc in load");
            if (p[i]) { M[p[i]] = (uint8_t)i; M[p[i] + sz[i] - 1] = (uint8_t)(i >> 3); }
        }
        for (int i = 0; i < N; i++) if (p[i]) CHECK(M[p[i]] == (uint8_t)i && M[p[i] + sz[i] - 1] == (uint8_t)(i >> 3), "no overlap");
        for (int i = 0; i < N; i++) { int j = rnd() % N; uint32_t x = p[i]; p[i] = p[j]; p[j] = x; x = sz[i]; sz[i] = sz[j]; sz[j] = x; }
        for (int i = 0; i < N; i++) ghfree(p[i]);
        ghalloc_stats(&t, &fr, &nf);
        if (load == 1) top1 = t;
        if (load == 0 || load == 1 || load == 19) printf("load %2d: heap top %llu MB, free %llu MB in %u blocks\n", load,
                                                        (unsigned long long)(t >> 20), (unsigned long long)(fr >> 20), nf);
    }
    ghalloc_stats(&t, &fr, &nf);
    CHECK(t <= top1 + (top1 >> 2), "heap top stays flat across loads (old allocator grew every load)");
    CHECK(fr == 0 && nf == 0, "everything freed: free blocks merged back into the heap end");

    /* a block kept alive across loads (level data) must not stop reuse of the space around it */
    uint32_t keep = ghalloc(1000, 0);
    for (int load = 0; load < 10; load++) {
        for (int i = 0; i < 64; i++) p[i] = ghalloc(2u << 20, 0);
        for (int i = 0; i < 64; i++) ghfree(p[i]);
    }
    ghalloc_stats(&t, &fr, &nf);
    CHECK(t < (140u << 20), "big blocks reused around a live block");
    /* double free is ignored, not a corrupted bin */
    uint32_t d = ghalloc(64, 0); ghfree(d); ghfree(d);
    uint32_t e1 = ghalloc(64, 0), e2 = ghalloc(64, 0);
    CHECK(e1 != e2, "double free does not hand the same block out twice");
    ghfree(e1); ghfree(e2); ghfree(keep);
    printf("%s (%d failures)\n", fails ? "FAILED" : "OK", fails);
    return fails != 0;
}
