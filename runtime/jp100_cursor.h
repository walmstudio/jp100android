/* jp100_cursor.h — Jurassic Park 100 cursor/input subsystem
 * Isolated from ME1's hle_exe.c cursor logic.
 * Single source of truth for window↔frame coordinate mapping (letterbox-aware).
 * Hybrid input: menu = mouse emulation, gameplay = native touch/QTE. */

#ifndef JP100_CURSOR_H
#define JP100_CURSOR_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Input mode for JP100 */
typedef enum {
    JP100_INPUT_MODE_MENU,      /* Front-end menus: mouse emulation (cursor visible, click = left button) */
    JP100_INPUT_MODE_GAMEPLAY,  /* In-game: native touch/QTE (cursor hidden, gestures) */
    JP100_INPUT_MODE_QTE        /* Quick-time events: special handling */
} jp100_input_mode_t;

/* Cursor state returned to Java (CursorView) */
typedef struct {
    int visible;          /* 1 = shown, 0 = hidden */
    int x, y;             /* Window pixels (for drawing the arrow) */
    int w, h;             /* Window size */
} jp100_cursor_state_t;

/* Initialize the cursor subsystem */
void jp100_cursor_init(void);

/* Set current input mode (called when game transitions menu↔gameplay) */
void jp100_cursor_set_mode(jp100_input_mode_t mode);

/* Get current input mode */
jp100_input_mode_t jp100_cursor_get_mode(void);

/* Process a touch event (from JNI nativeTouch)
 * action: 0=down, 1=up, 2=move, 3=second finger down (right), 4=second finger up
 * 5=cancel all accepted touch presses (coordinates ignored)
 * x,y: window pixels (surface coordinates)
 * Returns 1 if event was consumed, 0 if ignored (e.g., press in letterbox bar) */
int jp100_cursor_touch(int action, float x, float y);

/* Process relative mouse motion (from JNI nativeMouseRel, on-screen look area)
 * dx,dy: window pixels, accumulated into mickeys */
void jp100_cursor_mouse_rel(float dx, float dy);

/* Process mouse button (from JNI nativeMouseButton)
 * button: 0=left, 1=right, 2=middle; down: 1=press, 0=release */
void jp100_cursor_mouse_button(int button, int down);

/* Get cursor state for CursorView (called from JNI nativeCursor)
 * Fills state with window-pixel position and visibility.
 * Returns 1 if valid, 0 if no window/view yet. */
int jp100_cursor_get_state(jp100_cursor_state_t* state);

/* Internal: copy the exact presenter viewport (not a second reconstruction of it).
 * ww,wh: Android surface size; fw,fh: back buffer; vx,vy,vw,vh: displayed image rect. */
void jp100_cursor_set_view(int ww, int wh, int fw, int fh, int vx, int vy, int vw, int vh);

/* Internal: notify that the game's cursor visibility changed (ShowCursor) */
void jp100_cursor_notify_visibility(int visible);

/* Internal: notify that the game set a cursor sprite (SetCursor) */
void jp100_cursor_notify_sprite(void* cursor_handle);

/* Shutdown */
void jp100_cursor_shutdown(void);

#ifdef __cplusplus
}
#endif

#endif /* JP100_CURSOR_H */
