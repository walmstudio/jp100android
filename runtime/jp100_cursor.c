#include <stdio.h>
/* jp100_cursor.c — Jurassic Park 100 cursor/input subsystem
 * Isolated from ME1's hle_exe.c cursor logic.
 * Single source of truth for window↔frame coordinate mapping (letterbox-aware).
 * Absolute mouse semantics for menus and QTE (DOWN -> held drag -> UP).
 *
 * The module owns the mapping and the mode switch; the Win32 message / DirectInput
 * plumbing stays in hle_exe.c behind the public hle_ui_* API (hle.h). This layer
 * is compiled into both the Android libme1game.so and the JP100 Linux build, so it
 * must not include hle_exe.c internals. */

#include "jp100_cursor.h"
#include "hle.h"           /* hle_ui_pointer, hle_ui_mouse_rel, hle_ui_mouse_button, hle_ui_cursor, ... */
#include <pthread.h>

/* ---- Letterbox view (single source of truth; fed by the presenter) ---- */
static pthread_mutex_t g_view_mx = PTHREAD_MUTEX_INITIALIZER;
static int g_ww = 0, g_wh = 0;      /* window size (Android surface) */
static int g_fw = 0, g_fh = 0;      /* frame buffer (back buffer) size */
static int g_vx = 0, g_vy = 0;      /* view origin in window pixels */
static int g_vw = 0, g_vh = 0;      /* view size in window pixels */

/* ---- Cursor state (frame/client coordinates, as hle_ui_cursor reports them) ---- */
static unsigned g_touch_buttons;   /* accepted touch presses, owned by the UI thread */

/* ---- Input mode ---- */
static jp100_input_mode_t g_mode = JP100_INPUT_MODE_MENU;

/* Sub-pixel remainders for relative motion (mickeys) */
static float g_rem_x, g_rem_y;

/* ---- Helpers ---- */

/* Window pixel -> frame/client pixel. Returns 1 if inside the picture.
 * press: a finger *down* outside the letterbox bars is rejected (the black bars must not
 * click anything beyond the image edge); move/up is clamped to the edge so a drag that
 * leaves the picture keeps working instead of teleporting the pointer. */
static int win_to_frame(int wx, int wy, int press, int* fx, int* fy, int* fw, int* fh) {
    pthread_mutex_lock(&g_view_mx);
    int vx = g_vx, vy = g_vy, vw = g_vw, vh = g_vh, fw_ = g_fw, fh_ = g_fh;
    pthread_mutex_unlock(&g_view_mx);

    if (vw <= 0 || vh <= 0 || fw_ <= 0 || fh_ <= 0) return 0;

    int px = wx - vx;
    int py = wy - vy;

    if (press && (px < 0 || py < 0 || px >= vw || py >= vh)) return 0;

    if (px < 0) px = 0;
    if (py < 0) py = 0;
    if (px >= vw) px = vw - 1;
    if (py >= vh) py = vh - 1;

    *fx = (int)((int64_t)px * fw_ / vw);
    *fy = (int)((int64_t)py * fh_ / vh);
    if (*fx >= fw_) *fx = fw_ - 1;
    if (*fy >= fh_) *fy = fh_ - 1;
    *fw = fw_;
    *fh = fh_;
    return 1;
}

/* Frame/client pixel -> window pixel (the exact inverse of win_to_frame up to one pixel
 * of truncation): where the CursorView arrow is drawn for a game cursor at (fx, fy). */
static void frame_to_win(int fx, int fy, int* wx, int* wy) {
    pthread_mutex_lock(&g_view_mx);
    int vx = g_vx, vy = g_vy, vw = g_vw, vh = g_vh, fw_ = g_fw, fh_ = g_fh;
    pthread_mutex_unlock(&g_view_mx);

    if (vw <= 0 || vh <= 0 || fw_ <= 0 || fh_ <= 0) { *wx = fx; *wy = fy; return; }

    int x = vx + (int)(((int64_t)fx * vw + fw_ / 2) / fw_);
    int y = vy + (int)(((int64_t)fy * vh + fh_ / 2) / fh_);
    if (x < 0) x = 0;
    if (y < 0) y = 0;
    *wx = x;
    *wy = y;
}

/* ---- Public API ---- */

void jp100_cursor_init(void) {
    g_touch_buttons = 0;
    g_rem_x = g_rem_y = 0.0f;
    g_mode = JP100_INPUT_MODE_MENU;
}

void jp100_cursor_set_mode(jp100_input_mode_t mode) {
    g_mode = mode;
}

jp100_input_mode_t jp100_cursor_get_mode(void) {
    return g_mode;
}

static int jp100_cursor_touch_impl(int action, float x, float y);
/* diagnostics: the first touch events (not every move) and refused ones go to the log as "[touch] ..." */
int jp100_cursor_touch(int action, float x, float y) {
    static int n_all, n_mv;
    int r = jp100_cursor_touch_impl(action, x, y);
    if (action == 2 ? n_mv < 3 : (n_all < 60 || !r)) {
        if (action == 2) n_mv++; else n_all++;
        fprintf(stderr, "[touch] action %d at %.0f,%.0f -> %s\n", action, (double)x, (double)y, r ? "sent to the game" : "REFUSED (no viewport yet / no matching button down)");
    }
    return r;
}
static int jp100_cursor_touch_impl(int action, float x, float y) {
    if (action == 5) { /* cancel: release even if the surface/viewport disappeared */
        if (g_touch_buttons & 1) hle_ui_mouse_button(2);
        if (g_touch_buttons & 2) hle_ui_mouse_button(4);
        g_touch_buttons = 0;
        return 1;
    }
    if (action < 0 || action > 4) return 0;
    unsigned bit = (action == 3 || action == 4) ? 2u : 1u;
    if ((action == 1 || action == 4) && !(g_touch_buttons & bit)) return 0;

    if ((action == 0 || action == 3) && (g_touch_buttons & bit)) return 1; /* no duplicate DOWN */
    const int press = (action == 0 || action == 3);   /* down events */
    int fx, fy, fw, fh;
    if (!win_to_frame((int)x, (int)y, press, &fx, &fy, &fw, &fh)) {
        if ((action == 1 || action == 4) && (g_touch_buttons & bit)) {
            hle_ui_mouse_button(action == 1 ? 2 : 4);
            g_touch_buttons &= ~bit;
            return 1;
        }
        return 0;
    }

    static const int btn[5] = { 1, 2, 0, 3, 4 };      /* 1=Ldown 2=Lup 0=move 3=Rdown 4=Rup */

    /* The Win32 message + DirectInput path is the proven ME1 one. hle_ui_touch_pointer takes
     * back buffer coordinates (fx,fy of fw x fh) and maps them onto the game's client
     * area proportionally - the same mapping the game's own rendering uses, so a tap
     * lands exactly where the frame is drawn. */
    hle_ui_touch_pointer(fx, fy, fw, fh, btn[action]);

    if (press) g_touch_buttons |= bit;
    else if (action == 1 || action == 4) g_touch_buttons &= ~bit;
    return 1;
}

void jp100_cursor_mouse_rel(float dx, float dy) {
    g_rem_x += dx; g_rem_y += dy;
    int ix = (int)g_rem_x, iy = (int)g_rem_y;
    g_rem_x -= (float)ix; g_rem_y -= (float)iy;
    if (!ix && !iy) return;
    hle_ui_mouse_rel(ix, iy);
}

void jp100_cursor_mouse_button(int button, int down) {
    /* button: 0=left, 1=right, 2=middle  ->  hle_ui_mouse_button: 1/2 left, 3/4 right, 5/6 middle */
    if (button < 0 || button > 2) return;
    hle_ui_mouse_button(button == 0 ? (down ? 1 : 2) : button == 1 ? (down ? 3 : 4) : (down ? 5 : 6));
}

/* Returns 1 if a cursor state is available, 0 if there is no window/view yet.
 * Coordinates are in window (Android surface) pixels for CursorView, computed from the
 * game's client-space cursor via the game render mapping (client -> frame) and then the
 * presenter letterbox (frame -> window). */
int jp100_cursor_get_state(jp100_cursor_state_t* state) {
    int cx, cy, cw, ch;
    int s = hle_ui_cursor(&cx, &cy, &cw, &ch);
    if (s < 0 || cw <= 0 || ch <= 0) return 0;

    pthread_mutex_lock(&g_view_mx);
    int fw = g_fw, fh = g_fh;
    pthread_mutex_unlock(&g_view_mx);
    if (fw <= 0 || fh <= 0) return 0;

    /* client (cw x ch) -> frame (fw x fh), then frame -> window (letterbox) */
    int fx = (int)((int64_t)cx * fw / cw);
    int fy = (int)((int64_t)cy * fh / ch);
    if (fx >= fw) fx = fw - 1;
    if (fy >= fh) fy = fh - 1;

    int wx, wy;
    frame_to_win(fx, fy, &wx, &wy);

    state->visible = s ? 1 : 0;
    state->x = wx;
    state->y = wy;
    pthread_mutex_lock(&g_view_mx);
    state->w = g_ww;
    state->h = g_wh;
    pthread_mutex_unlock(&g_view_mx);
    return 1;
}

void jp100_cursor_set_view(int ww, int wh, int fw, int fh, int vx, int vy, int vw, int vh) {
    pthread_mutex_lock(&g_view_mx);
    g_ww = ww; g_wh = wh; g_fw = fw; g_fh = fh;
    g_vx = vx; g_vy = vy; g_vw = vw; g_vh = vh;

    pthread_mutex_unlock(&g_view_mx);
}

void jp100_cursor_notify_visibility(int visible) {
    (void)visible; /* hle_ui_cursor owns ShowCursor + SetCursor + DirectInput visibility */
}

void jp100_cursor_notify_sprite(void* cursor_handle) {
    (void)cursor_handle;
}

void jp100_cursor_shutdown(void) {
}
