/* d3dx9_41 subset for JurassicPark100.exe (included by hle_jp.c) */
static uint32_t com(CPU* c, uint32_t obj, int slot, int n, const uint32_t* rest) {   /* obj->vtbl[slot](obj, rest...) */
    uint32_t a[16]; a[0] = obj;
    for (int i = 0; i < n; i++) a[i + 1] = rest[i];
    return gcall(c, rd32(rd32(obj) + 4 * (uint32_t)slot), n + 1, a);
}
#define FCC(a, b, c_, d) ((uint32_t)(a) | (uint32_t)(b) << 8 | (uint32_t)(c_) << 16 | (uint32_t)(d) << 24)
static uint32_t fmt_block(uint32_t f) {   /* bytes per 4x4 block, 0 = not block-compressed */
    if (f == FCC('D', 'X', 'T', '1') || f == FCC('A', 'T', 'I', '1')) return 8;
    if (f == FCC('D', 'X', 'T', '2') || f == FCC('D', 'X', 'T', '3') || f == FCC('D', 'X', 'T', '4') || f == FCC('D', 'X', 'T', '5') ||
        f == FCC('A', 'T', 'I', '2')) return 16;
    return 0;
}
static uint32_t fmt_bits(uint32_t f) {
    switch (f) {
    case 20: return 24;
    case 23: case 24: case 25: case 26: case 51: case 60: case 81: case 111: return 16;
    case 28: case 50: return 8;
    case 113: case 115: return 64;
    case 116: return 128;
    default: return 32;
    }
}
static uint32_t lvl_rows(uint32_t f, uint32_t h) { if (!h) h = 1; return fmt_block(f) ? (h + 3) / 4 : h; }
static uint32_t lvl_pitch(uint32_t f, uint32_t w) { if (!w) w = 1; uint32_t b = fmt_block(f); return b ? (w + 3) / 4 * b : (w * fmt_bits(f) + 7) / 8; }
static uint32_t full_mips(uint32_t w, uint32_t h) { uint32_t m = w > h ? w : h, n = 1; while (m > 1) { m >>= 1; n++; } return n; }
static void fix_req(uint32_t* w, uint32_t* h, uint32_t* lv, uint32_t* fmt) {
    if (!*w || *w == 0xFFFFFFFFu) *w = *h && *h != 0xFFFFFFFFu ? *h : 256;
    if (!*h || *h == 0xFFFFFFFFu) *h = *w;
    uint32_t full = full_mips(*w, *h);
    if (!*lv || *lv > full) *lv = full;
    if (!*fmt || *fmt == 0xFFFFFFFFu) *fmt = 21;   /* A8R8G8B8 */
    if (*fmt == 20) *fmt = 22;                      /* R8G8B8 -> X8R8G8B8 */
}
static void x_CheckTextureRequirements(CPU* c) {   /* (dev, pW, pH, pLevels, usage, pFormat, pool) */
    uint32_t pw = ARG(1), ph = ARG(2), pl = ARG(3), pf = ARG(5);
    uint32_t w = pw ? rd32(pw) : 0, h = ph ? rd32(ph) : 0, lv = pl ? rd32(pl) : 0, f = pf ? rd32(pf) : 0;
    fix_req(&w, &h, &lv, &f);
    if (pw) wr32(pw, w);
    if (ph) wr32(ph, h);
    if (pl) wr32(pl, lv);
    if (pf) wr32(pf, f);
    RET(0);
}
static void x_CreateTexture(CPU* c) {   /* (dev, w, h, levels, usage, format, pool, ppTex) */
    uint32_t dev = ARG(0), w = ARG(1), h = ARG(2), lv = ARG(3), us = ARG(4), f = ARG(5), pool = ARG(6), pp = ARG(7);
    fix_req(&w, &h, &lv, &f);
    uint32_t a[8] = { w, h, lv, us, f, pool, pp, 0 };
    RET(com(c, dev, 23, 8, a));
}
static void x_GetShaderVersion(CPU* c) { RET(ARG(0) ? rd32(ARG(0)) : 0); }
static int g_font_log, g_save_log, g_tex_log;
static void x_CreateFontA(CPU* c) {
    if (!g_font_log++) fprintf(stderr, "[jp] D3DXCreateFontA(%s): not supported (E_FAIL)\n", ARG(10) ? GSTR(ARG(10)) : "");
    if (ARG(11)) wr32(ARG(11), 0);
    RET(0x80004005u);
}
static void x_SaveTextureToFileInMemory(CPU* c) {
    if (!g_save_log++) fprintf(stderr, "[jp] D3DXSaveTextureToFileInMemory: not supported (E_FAIL)\n");
    if (ARG(0)) wr32(ARG(0), 0);
    RET(0x80004005u);
}
static int surf_copy(CPU* c, uint32_t dst, uint32_t src) {   /* same format: lock both, copy rows */
    uint32_t tmp = c->esp - 512;
    uint32_t a1[1] = { tmp }, a2[1] = { tmp + 32 };
    if (com(c, src, 12, 1, a1) || com(c, dst, 12, 1, a2)) return -1;
    uint32_t sf = rd32(tmp), sh = rd32(tmp + 28), df = rd32(tmp + 32), dh = rd32(tmp + 60);
    if (sf != df) return -1;
    uint32_t l1[3] = { tmp + 64, 0, 0x10 }, l2[3] = { tmp + 72, 0, 0 };
    if (com(c, src, 13, 3, l1)) return -1;
    if (com(c, dst, 13, 3, l2)) { com(c, src, 14, 0, NULL); return -1; }
    uint32_t sp = rd32(tmp + 64), sb = rd32(tmp + 68), dp = rd32(tmp + 72), db = rd32(tmp + 76);
    uint32_t rows = lvl_rows(sf, sh < dh ? sh : dh), pitch = sp < dp ? sp : dp;
    if (sb && db) for (uint32_t y = 0; y < rows; y++) memmove(M + db + y * dp, M + sb + y * sp, pitch);
    com(c, dst, 14, 0, NULL);
    com(c, src, 14, 0, NULL);
    return 0;
}
static void x_LoadSurfaceFromSurface(CPU* c) {   /* (dst, dstPal, dstRect, src, srcPal, srcRect, filter, colorkey) */
    uint32_t dst = ARG(0), drect = ARG(2), src = ARG(3), srect = ARG(5), tmp = c->esp - 512, hr = 1;
    wr32(tmp, 0);
    uint32_t g[1] = { tmp };
    if (!com(c, dst, 3, 1, g) && rd32(tmp)) {   /* GetDevice -> StretchRect(linear) */
        uint32_t dev = rd32(tmp);
        uint32_t s[5] = { src, srect, dst, drect, 2 };
        hr = com(c, dev, 34, 5, s);
        com(c, dev, 2, 0, NULL);
    }
    if (hr) surf_copy(c, dst, src);
    RET(0);
}
static int tex_fill(CPU* c, uint32_t tex, uint32_t lvl, const uint8_t* src, uint32_t sp, uint32_t rows, uint32_t w, int expand) {
    uint32_t tmp = c->esp - 512;
    uint32_t a[4] = { lvl, tmp, 0, 0 };
    wr32(tmp + 4, 0);
    if (com(c, tex, 19, 4, a)) return -1;
    uint32_t dp = rd32(tmp), db = rd32(tmp + 4);
    uint32_t u[1] = { lvl };
    if (!db) { com(c, tex, 20, 1, u); return -1; }
    for (uint32_t y = 0; y < rows; y++) {
        uint8_t* d = M + db + y * dp; const uint8_t* s = src + (size_t)y * sp;
        if (expand) for (uint32_t x = 0; x < w; x++) { d[4 * x] = s[3 * x]; d[4 * x + 1] = s[3 * x + 1]; d[4 * x + 2] = s[3 * x + 2]; d[4 * x + 3] = 0xFF; }
        else memcpy(d, s, sp < dp ? sp : dp);
    }
    com(c, tex, 20, 1, u);
    return 0;
}
static int fill_all(CPU* c, uint32_t tex, const uint8_t* d, uint32_t off, uint32_t size, uint32_t fmt, uint32_t w, uint32_t h, uint32_t lv, int expand) {
    for (uint32_t l = 0; l < lv; l++) {
        uint32_t sp = lvl_pitch(fmt, w), rows = lvl_rows(fmt, h);
        if ((uint64_t)off + (uint64_t)sp * rows > size) break;   /* truncated file: keep what we have */
        if (tex_fill(c, tex, l, d + off, sp, rows, w, expand)) return -1;
        off += sp * rows; w = w > 1 ? w >> 1 : 1; h = h > 1 ? h >> 1 : 1;
    }
    return 0;
}
static void tex_err(const char* why, uint32_t v) { if (g_tex_log++ < 8) fprintf(stderr, "[jp] D3DXCreateTextureFromFileInMemoryEx: %s (%08x)\n", why, v); }
/* (dev, data, size, w, h, levels, usage, format, pool, filter, mipfilter, colorkey, pInfo, pPalette, ppTex): DDS only,
 * the file's size and format are kept, levels = min(file, request) */
static void x_CreateTextureFromFileInMemoryEx(CPU* c) {
    uint32_t dev = ARG(0), data = ARG(1), size = ARG(2), rq = ARG(5), usage = ARG(6), pool = ARG(8), info = ARG(12), pp = ARG(14);
    const uint8_t* d = M + data;
    if (pp) wr32(pp, 0);
    if (size < 128 || memcmp(d, "DDS ", 4)) { tex_err("not a DDS file", size >= 4 ? rd32(data) : 0); RET(0x88760B59u); return; }
    uint32_t h = rd32(data + 12), w = rd32(data + 16), mips = rd32(data + 28), pff = rd32(data + 80), cc = rd32(data + 84);
    uint32_t bits = rd32(data + 88), rm = rd32(data + 92), am = rd32(data + 104), caps2 = rd32(data + 112), fmt = 0;
    if (pff & 4) {
        if (cc == FCC('D', 'X', '1', '0')) { tex_err("DX10 header not supported", cc); RET(0x88760B59u); return; }
        fmt = cc;
    } else if (pff & 0x40) {
        if (bits == 32) fmt = rm == 0xFF ? (pff & 1 ? 32 : 33) : (pff & 1 && am ? 21 : 22);
        else if (bits == 24) fmt = 20;
        else if (bits == 16) fmt = rm == 0xF800 ? 23 : rm == 0x7C00 ? (pff & 1 ? 25 : 24) : rm == 0x0F00 ? 26 : 23;
    } else if (pff & 0x20000) fmt = bits == 16 ? (pff & 1 ? 51 : 81) : 50;
    else if (pff & 2) fmt = 28;
    if (!fmt || !w || !h) { tex_err("unsupported pixel format", pff); RET(0x88760B59u); return; }
    if (!mips) mips = 1;
    uint32_t lv = mips;
    if (rq && rq != 0xFFFFFFFFu && rq != 0xFFFFFFFDu && rq < lv) lv = rq;
    int expand = fmt == 20;
    uint32_t cfmt = expand ? 22 : fmt;
    uint32_t a[8] = { w, h, lv, usage, cfmt, pool, pp, 0 };
    uint32_t hr = com(c, dev, 23, 8, a);
    if (hr || !pp || !rd32(pp)) { tex_err("CreateTexture failed", hr); RET(hr ? hr : 0x88760B59u); return; }
    uint32_t tex = rd32(pp);
    if (fill_all(c, tex, d, 128, size, fmt, w, h, lv, expand)) {   /* not lockable: SYSTEMMEM copy + UpdateTexture */
        uint32_t ps = c->esp - 600;
        wr32(ps, 0);
        uint32_t b[8] = { w, h, lv, 0, cfmt, 2, ps, 0 };
        if (!com(c, dev, 23, 8, b) && rd32(ps)) {
            uint32_t sys = rd32(ps);
            if (!fill_all(c, sys, d, 128, size, fmt, w, h, lv, expand)) { uint32_t u[2] = { sys, tex }; com(c, dev, 31, 2, u); }
            else tex_err("LockRect failed", 0);
            com(c, sys, 2, 0, NULL);
        } else tex_err("cannot fill the texture", pool);
    }
    if (info) {
        wr32(info, w); wr32(info + 4, h); wr32(info + 8, 1); wr32(info + 12, mips);
        wr32(info + 16, fmt); wr32(info + 20, caps2 & 0x200 ? 5 : 3); wr32(info + 24, 4);
    }
    RET(0);
}
