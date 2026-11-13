/* test_vmem: the guest VirtualAlloc arena (hle_vmem.c) next to module images mapped at a fixed base.
 * JurassicPark100.exe has no relocations, so it must sit at its ImageBase: [0x00400000, 0x01582878) (SizeOfImage
 * 0x01182878), across the start of the first free range at 0x00800000. The loader cuts every mapped image out of the
 * arena (hle_vmem_exclude, called from rt.c load_module); this checks that, and that ME1's layout is unchanged.
 * Host only, no game files:  make test-vmem */
#define _GNU_SOURCE
#include "hle.h"
#include <stdio.h>
#include <sys/mman.h>

uint8_t* M;   /* hle_vmem.c touches guest memory through M only when it releases pages */
void hle_watch_release(uint32_t a, uint32_t n, const char* what) { (void)a; (void)n; (void)what; }

#define VM_COMMIT   0x1000u
#define VM_RESERVE  0x2000u
#define VM_RELEASE  0x8000u
#define VM_TOP_DOWN 0x100000u

static int fails;
#define CHECK(c, ...) do { if (!(c)) { fails++; printf("FAIL line %d: ", __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)
static int overlaps(uint32_t a, uint32_t n, uint32_t lo, uint32_t hi) { return a < hi && a + n > lo; }

int main(void) {
    M = mmap(NULL, 1ull << 32, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE, -1, 0);
    if (M == MAP_FAILED) { perror("mmap"); return 2; }
    uint32_t a, b, base, size;

    /* 1. ME1: every image lies in the DLL window 0x10000000-0x12400000 (MassEffect.exe rebased to 0x10900000) */
    CHECK(hle_vmem_exclude(0x10900000u, 0x01A00000u) == 0, "image inside the DLL window");
    a = hle_vmem_alloc(0x12400000u, 0x10000u, VM_RESERVE);
    CHECK(a == 0x12400000u, "second range must still start at 12400000, got %08x", a);
    CHECK(hle_vmem_free(a, 0, VM_RELEASE), "release %08x", a);

    /* 2. JurassicPark100.exe at its fixed ImageBase */
    CHECK(hle_vmem_exclude(0x00400000u, 0x01182878u) == 0, "exclude JurassicPark100.exe");
    a = hle_vmem_alloc(0, 0x10000u, VM_COMMIT | VM_RESERVE);
    CHECK(a == 0x01590000u, "first bottom-up block %08x, want 01590000 (image end 01582878 rounded to 64 KiB)", a);
    CHECK(hle_vmem_alloc(0x00800000u, 0x10000u, VM_RESERVE) == 0, "MEM_RESERVE inside the image must fail");
    CHECK(hle_vmem_alloc(0x01580000u, 0x10000u, VM_RESERVE) == 0, "MEM_RESERVE over the image tail must fail");
    CHECK(hle_vmem_alloc(0x00410000u, 0x1000u, VM_COMMIT) == 0, "MEM_COMMIT inside the image must fail");
    CHECK(!hle_vmem_query(0x00800000u, &base, &size), "image pages are not a VirtualAlloc reservation");
    b = hle_vmem_alloc(0, 0x10000u, VM_COMMIT | VM_RESERVE | VM_TOP_DOWN);
    CHECK(b == 0x77FF0000u, "top-down block %08x, want 77ff0000", b);

    /* 3. many blocks across all ranges, then release them: none touches the image, freed space does not grow into it */
    static uint32_t blk[512], len[512];
    int n = 0;
    for (; n < 512; n++) {
        len[n] = 0x100000u + (uint32_t)(n % 7) * 0x10000u;
        if (!(blk[n] = hle_vmem_alloc(0, len[n], VM_COMMIT | VM_RESERVE))) break;
        CHECK(!overlaps(blk[n], len[n], 0x00400000u, 0x01590000u), "block %08x+%x overlaps the image", blk[n], len[n]);
        CHECK(!overlaps(blk[n], len[n], 0x10000000u, 0x12400000u) && !overlaps(blk[n], len[n], 0x20000000u, 0x30000000u),
              "block %08x+%x in the DLL window or the HLE heap", blk[n], len[n]);
    }
    CHECK(n == 512, "only %d of 512 blocks", n);
    for (int i = 0; i < n; i++) CHECK(hle_vmem_free(blk[i], 0, VM_RELEASE), "release %08x", blk[i]);
    CHECK(hle_vmem_free(a, 0, VM_RELEASE), "release %08x", a);
    a = hle_vmem_alloc(0, 0x10000u, VM_COMMIT | VM_RESERVE);
    CHECK(a == 0x01590000u, "after the releases the lowest block is %08x, want 01590000", a);
    CHECK(hle_vmem_free(a, 0, VM_RELEASE), "release %08x", a);

    /* 4. an image over a gap: the tail of the first range and the head of the second */
    CHECK(hle_vmem_exclude(0x0FF00000u, 0x02600000u) == 0, "exclude 0ff00000-12500000");
    CHECK(hle_vmem_alloc(0x0FF00000u, 0x10000u, VM_RESERVE) == 0, "reserve at 0ff00000 must fail");
    CHECK(hle_vmem_alloc(0x12400000u, 0x10000u, VM_RESERVE) == 0, "reserve at 12400000 must fail");
    CHECK(hle_vmem_alloc(0x0FEF0000u, 0x20000u, VM_RESERVE) == 0, "reserve across the image start must fail");
    a = hle_vmem_alloc(0x0FEF0000u, 0x10000u, VM_RESERVE);
    CHECK(a == 0x0FEF0000u, "block right below the image: %08x", a);
    b = hle_vmem_alloc(0x12500000u, 0x10000u, VM_RESERVE);
    CHECK(b == 0x12500000u, "block right above the image: %08x", b);
    CHECK(hle_vmem_free(a, 0, VM_RELEASE) && hle_vmem_free(b, 0, VM_RELEASE), "release");

    /* 5. a small image inside a range splits it */
    CHECK(hle_vmem_exclude(0x30100000u, 0x8000u) == 0, "exclude 30100000-30108000");
    CHECK(hle_vmem_alloc(0x30108000u, 0x1000u, VM_RESERVE) == 0, "reserve inside the 64 KiB-rounded image must fail");
    a = hle_vmem_alloc(0x300F0000u, 0x10000u, VM_RESERVE);
    b = hle_vmem_alloc(0x30110000u, 0x10000u, VM_RESERVE);
    CHECK(a == 0x300F0000u && b == 0x30110000u, "neighbours %08x %08x", a, b);
    CHECK(hle_vmem_free(a, 0, VM_RELEASE) && hle_vmem_free(b, 0, VM_RELEASE), "release");

    /* 6. an image mapped over a live reservation is reported; size 0 excludes nothing */
    a = hle_vmem_alloc(0x40000000u, 0x10000u, VM_COMMIT | VM_RESERVE);
    CHECK(a == 0x40000000u, "block %08x", a);
    CHECK(hle_vmem_exclude(0x40008000u, 0x1000u) == -1, "overlap with a live reservation must be reported");
    CHECK(hle_vmem_free(a, 0, VM_RELEASE), "release");
    CHECK(hle_vmem_exclude(0x50000000u, 0) == 0 && hle_vmem_alloc(0x50000000u, 0x10000u, VM_RESERVE) == 0x50000000u,
          "size 0 must not exclude anything");

    printf("test_vmem: %s\n", fails ? "FAILED" : "OK");
    return fails != 0;
}
