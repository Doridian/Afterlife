#include "platform.h"
#include "present.h"

#include <SDL3/SDL.h>
#include <stdlib.h>
#include <string.h>

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#endif

static SDL_Window *g_window;
static SDL_Renderer *g_renderer;
static SDL_Texture *g_texture;
static SDL_Surface *g_indexed;
static SDL_Palette *g_palette;
static SDL_AudioStream *g_audio;
static char g_data_dir[1024] = ".";
static int g_ui_scale = 1;     /* port enhancement, in effect: see plat_set_ui_scale */
static int g_ui_scale_req = 1; /* requested (automatic resolved), 1..4 */
static PresentRect g_dst;      /* where the framebuffer goes, output pixels */
static bool g_managed;         /* port window: the framebuffer follows the window */
static PlatWindowState g_wstate = {0, 0, PLAT_WINDOW_POS_DEFAULT, PLAT_WINDOW_POS_DEFAULT, false, false};
static SDL_Scancode g_swallow_sc; /* the fullscreen shortcut's key: its key up is dropped too */
static bool g_swallow_alt;        /* Alt+Enter: drop Alt's key up (it would open the menu bar) */
static bool g_alt_held, g_ctrl_held; /* from the key events, for the shortcut */
static char g_dropped[1024];         /* plat_take_dropped_file */

/* How the framebuffer is put into the window (present_layout). The window
 * is sized in pixels (SDL_WINDOW_HIGH_PIXEL_DENSITY, plat_video_set_mode),
 * so normally it maps 1:1, or k:1 with UI scale k. When the window is at
 * least as large as the framebuffer it is scaled by a whole factor and
 * centred (crisp); only a smaller window (the user shrank it) scales down
 * with letterboxing. Mouse positions go back through present_to_fb. */
static void update_presentation(void)
{
    if (!g_renderer || !g_indexed)
        return;
    int ow = 0, oh = 0;
    SDL_GetRenderOutputSize(g_renderer, &ow, &oh); /* pixels */
    g_dst = present_layout(ow, oh, g_indexed->w, g_indexed->h);
}

/* The desktop's pixel density (1.5 for 150 % scaling), 1 if unknown. */
static float desktop_density(void)
{
    const SDL_DisplayMode *mode = SDL_GetDesktopDisplayMode(SDL_GetPrimaryDisplay());
    return mode && mode->pixel_density > 0 ? mode->pixel_density : 1.0f;
}

/* The desktop in real pixels (unscaled). */
static void desktop_pixels(int *width, int *height)
{
    /* SDL's mode size is in points; GetClientRect(GetDesktopWindow()) of a
     * DPI-unaware 1996 program is the whole screen in pixels. */
    const SDL_DisplayMode *mode = SDL_GetDesktopDisplayMode(SDL_GetPrimaryDisplay());
    float d = desktop_density();
    *width = mode ? (int)SDL_lroundf((float)mode->w * d) : 0;
    *height = mode ? (int)SDL_lroundf((float)mode->h * d) : 0;
}

void plat_set_ui_scale(int scale)
{
    if (scale <= 0)
        scale = present_auto_scale(desktop_density());
    /* clamped to 1..4 here; to the desktop (plat_video_desktop_size) and,
     * with the port window, to the window (plat_video_resize) where used */
    g_ui_scale_req = present_clamp_scale(scale, 0, 0);
    if (!g_managed) {
        int dw = 0, dh = 0;
        desktop_pixels(&dw, &dh);
        g_ui_scale = present_clamp_scale(g_ui_scale_req, dw, dh);
    }
}

int plat_ui_scale(void) { return g_ui_scale; }
int plat_ui_scale_requested(void) { return g_ui_scale_req; }

bool plat_init(const char *title, int width, int height)
{
    /* A close request is WM_CLOSE (the game asks); only signals and the
     * session ending are SDL_EVENT_QUIT. */
    SDL_SetHint(SDL_HINT_QUIT_ON_LAST_WINDOW_CLOSE, "0");
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO)) {
        SDL_Log("SDL_Init: %s", SDL_GetError());
        return false;
    }
    /* Hidden until plat_video_set_mode, like the original's 1x1 window. */
    SDL_WindowFlags flags = SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIDDEN | SDL_WINDOW_HIGH_PIXEL_DENSITY;
#ifdef __EMSCRIPTEN__
    /* WebGL from the start: SDL would otherwise recreate the window for the
     * renderer, and destroying it leaves an inline 1x1 CSS size on the
     * canvas (with devicePixelRatio != 1), which hides the page's sizing. */
    flags |= SDL_WINDOW_OPENGL;
#endif
    if (!SDL_CreateWindowAndRenderer(title ? title : "", width, height, flags, &g_window, &g_renderer)) {
        SDL_Log("SDL_CreateWindowAndRenderer: %s", SDL_GetError());
        return false;
    }
    SDL_SetRenderVSync(g_renderer, 1);

    g_indexed = SDL_CreateSurface(width, height, SDL_PIXELFORMAT_INDEX8);
    g_palette = SDL_CreateSurfacePalette(g_indexed);
    g_texture = SDL_CreateTexture(g_renderer, SDL_PIXELFORMAT_XRGB8888, SDL_TEXTUREACCESS_STREAMING, width, height);
    SDL_SetTextureScaleMode(g_texture, SDL_SCALEMODE_NEAREST);
    update_presentation();
    /* WM_CHAR comes from SDL's text input events (see plat_poll_event). */
    SDL_StartTextInput(g_window);
    return g_indexed && g_palette && g_texture;
}

void plat_video_set_title(const char *title)
{
    if (g_window)
        SDL_SetWindowTitle(g_window, title ? title : "");
}

void plat_shutdown(void)
{
    plat_audio_close();
    SDL_DestroyTexture(g_texture);
    SDL_DestroySurface(g_indexed);
    SDL_DestroyRenderer(g_renderer);
    SDL_DestroyWindow(g_window);
    SDL_Quit();
}

uint8_t *plat_video_framebuffer(void) { return g_indexed->pixels; }
int plat_video_width(void) { return g_indexed->w; }
int plat_video_pitch(void) { return g_indexed->pitch; }
int plat_video_height(void) { return g_indexed->h; }

void plat_video_set_palette(const PlatColor *colors, int first, int count)
{
    SDL_Color tmp[256];
    for (int i = 0; i < count; i++)
        tmp[i] = (SDL_Color){colors[i].r, colors[i].g, colors[i].b, 255};
    SDL_SetPaletteColors(g_palette, tmp, first, count);
}

/* (Re)creates the framebuffer, its palette and the texture at w x h. */
static bool alloc_framebuffer(int width, int height)
{
    if (g_indexed && g_indexed->w == width && g_indexed->h == height)
        return true;
    SDL_Surface *ind = SDL_CreateSurface(width, height, SDL_PIXELFORMAT_INDEX8);
    SDL_Palette *pal = ind ? SDL_CreateSurfacePalette(ind) : NULL;
    SDL_Texture *tex =
        SDL_CreateTexture(g_renderer, SDL_PIXELFORMAT_XRGB8888, SDL_TEXTUREACCESS_STREAMING, width, height);
    if (!ind || !pal || !tex) {
        SDL_Log("framebuffer %dx%d: %s", width, height, SDL_GetError());
        SDL_DestroySurface(ind);
        SDL_DestroyTexture(tex);
        return false;
    }
    SDL_SetTextureScaleMode(tex, SDL_SCALEMODE_NEAREST);
    if (g_palette)
        SDL_SetPaletteColors(pal, g_palette->colors, 0, g_palette->ncolors);
    SDL_DestroySurface(g_indexed);
    SDL_DestroyTexture(g_texture);
    g_indexed = ind;
    g_palette = pal;
    g_texture = tex;
    return true;
}

bool plat_video_set_mode(int width, int height, bool fullscreen)
{
    if (!g_window || width <= 0 || height <= 0)
        return false;
    if (!alloc_framebuffer(width, height))
        return false;
    if (g_managed) {
        /* The port window keeps the geometry plat_window_open gave it; the
         * caller asked for the size it wants (plat_video_target_size). */
        int pw = 0, ph = 0;
        SDL_GetWindowSizeInPixels(g_window, &pw, &ph);
        present_logical_size(pw, ph, g_ui_scale_req, NULL, NULL, &g_ui_scale);
        if (fullscreen && !g_wstate.fullscreen)
            plat_window_set_fullscreen(true);
        update_presentation();
        return true;
    }
    /* The original makes a popup window covering the desktop when the
     * resolution fills it (0x4cd73a), else a framed window of that client
     * size. A desktop-sized framed window doesn't fit next to the title bar
     * and panels, so the window manager shrank it and the picture came out
     * letterboxed; the port uses a borderless fullscreen window instead.
     * Sizes are in pixels: the window size is divided by the density (and
     * multiplied by the UI scale, an enhancement). */
    int dw = 0, dh = 0;
    plat_video_desktop_size(&dw, &dh);
    bool cover = fullscreen || (dw > 0 && width >= dw && height >= dh);
    if (!cover) {
        SDL_SetWindowFullscreen(g_window, false);
        float d = desktop_density();
        float k = (float)g_ui_scale;
        SDL_SetWindowSize(g_window, (int)SDL_ceilf((float)width * k / d), (int)SDL_ceilf((float)height * k / d));
    } else {
        SDL_SetWindowFullscreenMode(g_window, NULL); /* the desktop's mode */
        SDL_SetWindowFullscreen(g_window, true);
    }
    SDL_ShowWindow(g_window);
    SDL_SyncWindow(g_window);
    update_presentation();
    return true;
}

/* --- the port window ------------------------------------------------------- */

/* The primary display's usable area (without panels), in desktop units. */
static SDL_Rect usable_bounds(void)
{
    SDL_Rect r = {0, 0, 0, 0};
    SDL_DisplayID d = SDL_GetPrimaryDisplay();
    if (!SDL_GetDisplayUsableBounds(d, &r) || r.w <= 0 || r.h <= 0)
        SDL_GetDisplayBounds(d, &r);
    if (r.w <= 0 || r.h <= 0)
        r = (SDL_Rect){0, 0, 1024, 768};
    return r;
}

/* Whether (x, y) lies on some display (a saved position after a monitor
 * was unplugged may not). */
static bool point_on_a_display(int x, int y)
{
    int n = 0;
    SDL_DisplayID *ids = SDL_GetDisplays(&n);
    bool on = false;
    for (int i = 0; ids && i < n && !on; i++) {
        SDL_Rect r;
        if (SDL_GetDisplayBounds(ids[i], &r))
            on = x >= r.x && y >= r.y && x < r.x + r.w && y < r.y + r.h;
    }
    SDL_free(ids);
    return on;
}

void plat_window_open(const PlatWindowState *saved, int pref_w, int pref_h)
{
    if (!g_window)
        return;
    g_managed = true;
#ifdef __EMSCRIPTEN__
    /* The page's CSS sizes the canvas (web/shell.html) and the browser
     * window resizes it; SDL_SetWindowSize would fight that. Fullscreen
     * needs a user gesture, so it isn't restored either. */
    (void)saved, (void)pref_w, (void)pref_h;
    SDL_GetWindowSize(g_window, &g_wstate.w, &g_wstate.h);
    g_wstate.x = g_wstate.y = PLAT_WINDOW_POS_DEFAULT;
    g_wstate.fullscreen = g_wstate.maximized = false;
    SDL_ShowWindow(g_window);
    return;
#endif
    SDL_Rect ub = usable_bounds();
    float d = desktop_density();
    int k = g_ui_scale_req;
    /* the game's minimum, 640x480 pixels */
    int min_w = (int)SDL_ceilf((float)PRESENT_MIN_W / d), min_h = (int)SDL_ceilf((float)PRESENT_MIN_H / d);
    int ww = 0, wh = 0;
    if (saved && saved->w > 0 && saved->h > 0) {
        ww = saved->w < ub.w ? saved->w : ub.w;
        wh = saved->h < ub.h ? saved->h : ub.h;
    } else if (pref_w > 0 && pref_h > 0) {
        /* INI Resolution smaller than the desktop: that size, if it fits
         * with room for the frame */
        int pw = (int)SDL_ceilf((float)pref_w * (float)k / d), ph = (int)SDL_ceilf((float)pref_h * (float)k / d);
        if (pw <= ub.w * 95 / 100 && ph <= ub.h * 90 / 100)
            ww = pw, wh = ph;
    }
    if (ww <= 0 || wh <= 0) {
        ww = ub.w * 4 / 5;
        wh = ub.h * 4 / 5;
    }
    if (ww < min_w)
        ww = min_w;
    if (wh < min_h)
        wh = min_h;
    SDL_SetWindowMinimumSize(g_window, min_w, min_h);
    SDL_SetWindowResizable(g_window, true);
    SDL_SetWindowFullscreen(g_window, false);
    SDL_SetWindowSize(g_window, ww, wh);
    bool have_pos = saved && saved->x != PLAT_WINDOW_POS_DEFAULT && saved->y != PLAT_WINDOW_POS_DEFAULT &&
                    point_on_a_display(saved->x + 20, saved->y + 20);
    if (have_pos)
        SDL_SetWindowPosition(g_window, saved->x, saved->y);
    else
        SDL_SetWindowPosition(g_window, (int)SDL_WINDOWPOS_CENTERED_DISPLAY(SDL_GetPrimaryDisplay()),
                              (int)SDL_WINDOWPOS_CENTERED_DISPLAY(SDL_GetPrimaryDisplay()));
    g_wstate.w = ww;
    g_wstate.h = wh;
    g_wstate.x = have_pos ? saved->x : PLAT_WINDOW_POS_DEFAULT;
    g_wstate.y = have_pos ? saved->y : PLAT_WINDOW_POS_DEFAULT;
    g_wstate.fullscreen = g_wstate.maximized = false;
    SDL_ShowWindow(g_window);
    if (saved && saved->maximized) {
        SDL_MaximizeWindow(g_window);
        g_wstate.maximized = true;
    }
    if (saved && saved->fullscreen)
        plat_window_set_fullscreen(true);
    SDL_SyncWindow(g_window);
}

bool plat_window_managed(void) { return g_managed; }

void plat_window_get_state(PlatWindowState *out) { *out = g_wstate; }

void plat_window_set_fullscreen(bool on)
{
    if (!g_window)
        return;
    if (on)
        SDL_SetWindowFullscreenMode(g_window, NULL); /* borderless, the desktop's mode */
    SDL_SetWindowFullscreen(g_window, on);
    g_wstate.fullscreen = on;
    SDL_SyncWindow(g_window);
}

bool plat_window_fullscreen(void) { return g_wstate.fullscreen; }

void plat_window_resize(int w, int h)
{
    if (!g_window || w <= 0 || h <= 0)
        return;
#ifdef __EMSCRIPTEN__
    return; /* the browser window sizes the canvas (plat_window_open) */
#endif
    if (g_wstate.fullscreen)
        plat_window_set_fullscreen(false);
    if (g_wstate.maximized) {
        SDL_RestoreWindow(g_window);
        g_wstate.maximized = false;
    }
    SDL_SetWindowSize(g_window, w, h);
    SDL_SyncWindow(g_window);
    g_wstate.w = w;
    g_wstate.h = h;
}

void plat_window_pixel_size(int *w, int *h)
{
    *w = *h = 0;
    if (g_window)
        SDL_GetWindowSizeInPixels(g_window, w, h);
}

bool plat_video_target_size(int *w, int *h)
{
    int pw = 0, ph = 0, k = 1;
    plat_window_pixel_size(&pw, &ph);
    present_logical_size(pw, ph, g_ui_scale_req, w, h, &k);
    return !g_indexed || g_indexed->w != *w || g_indexed->h != *h || k != g_ui_scale;
}

bool plat_video_resize(int w, int h)
{
    if (!g_window || w <= 0 || h <= 0 || !alloc_framebuffer(w, h))
        return false;
    int pw = 0, ph = 0;
    plat_window_pixel_size(&pw, &ph);
    present_logical_size(pw, ph, g_ui_scale_req, NULL, NULL, &g_ui_scale);
    update_presentation();
    return true;
}

void plat_video_desktop_size(int *width, int *height)
{
    /* With UI scale k (an enhancement) the game sees a desktop k times
     * smaller, so Resolution=MAX makes a framebuffer that is scaled back up
     * k times. */
    desktop_pixels(width, height);
    int k = present_clamp_scale(g_ui_scale_req, *width, *height);
    *width /= k;
    *height /= k;
}

struct PlatCursor {
    SDL_Cursor *cursor;
};

PlatCursor *plat_cursor_create(const uint8_t *rgba, int w, int h, int hot_x, int hot_y)
{
    SDL_Surface *s = SDL_CreateSurfaceFrom(w, h, SDL_PIXELFORMAT_RGBA32, (void *)rgba, w * 4);
    SDL_Cursor *c = s ? SDL_CreateColorCursor(s, hot_x, hot_y) : NULL;
    SDL_DestroySurface(s);
    PlatCursor *pc = c ? malloc(sizeof(*pc)) : NULL;
    if (!pc) {
        if (c)
            SDL_DestroyCursor(c);
        return NULL;
    }
    pc->cursor = c;
    return pc;
}

void plat_cursor_destroy(PlatCursor *cursor)
{
    if (cursor) {
        if (SDL_GetCursor() == cursor->cursor)
            SDL_SetCursor(SDL_GetDefaultCursor());
        SDL_DestroyCursor(cursor->cursor);
        free(cursor);
    }
}

void plat_cursor_set(PlatCursor *cursor)
{
    SDL_SetCursor(cursor ? cursor->cursor : SDL_GetDefaultCursor());
}

bool plat_cursor_show(bool show)
{
    bool was = SDL_CursorVisible();
    if (show)
        SDL_ShowCursor();
    else
        SDL_HideCursor();
    return was;
}

void plat_video_fb_to_window(int x, int y, float *wx, float *wy)
{
    float px = (float)x, py = (float)y;
    *wx = px;
    *wy = py;
    if (!g_indexed)
        return;
    present_from_fb(g_dst, g_indexed->w, g_indexed->h, x, y, &px, &py);
    if (!g_renderer || !SDL_RenderCoordinatesToWindow(g_renderer, px, py, wx, wy)) {
        *wx = px;
        *wy = py;
    }
}

void plat_mouse_warp(int x, int y)
{
    if (!g_window || !g_indexed)
        return;
    float wx, wy;
    plat_video_fb_to_window(x, y, &wx, &wy);
    SDL_WarpMouseInWindow(g_window, wx, wy);
}

static void render_frame(void)
{
    SDL_Surface *dst;
    if (SDL_LockTextureToSurface(g_texture, NULL, &dst)) {
        SDL_BlitSurface(g_indexed, NULL, dst, NULL);
        SDL_UnlockTexture(g_texture);
    }
    SDL_RenderClear(g_renderer);
    SDL_FRect out = {(float)g_dst.x, (float)g_dst.y, (float)g_dst.w, (float)g_dst.h};
    SDL_RenderTexture(g_renderer, g_texture, NULL, &out);
}

void plat_video_present(void)
{
    render_frame();
    SDL_RenderPresent(g_renderer);
}

bool plat_video_screenshot(const char *path)
{
    if (!g_renderer || !g_indexed)
        return false;
    render_frame();
    SDL_Surface *s = SDL_RenderReadPixels(g_renderer, NULL);
    bool ok = s && SDL_SaveBMP(s, path);
    SDL_DestroySurface(s);
    SDL_RenderPresent(g_renderer);
    return ok;
}

/* --- input events ---------------------------------------------------------
 * One SDL event can stand for several window messages (a key down that
 * TranslateMessage turns into WM_KEYDOWN + WM_CHAR, or a text event with
 * several characters), so translated events go through a small FIFO. */

#define PLAT_EVQ_LEN 64
static PlatEvent g_evq[PLAT_EVQ_LEN];
static int g_evq_head, g_evq_tail;

static void evq_push(const PlatEvent *ev)
{
    int next = (g_evq_tail + 1) % PLAT_EVQ_LEN;
    if (next == g_evq_head)
        return; /* full: drop, like a full Windows message queue */
    g_evq[g_evq_tail] = *ev;
    g_evq_tail = next;
}

static bool evq_pop(PlatEvent *ev)
{
    if (g_evq_head == g_evq_tail)
        return false;
    *ev = g_evq[g_evq_head];
    g_evq_head = (g_evq_head + 1) % PLAT_EVQ_LEN;
    return true;
}

static void push_simple(PlatEventType type)
{
    PlatEvent ev = {0};
    ev.type = type;
    evq_push(&ev);
}

static void push_char(uint8_t ch)
{
    PlatEvent ev = {0};
    ev.type = PLAT_EV_CHAR;
    ev.ch = ch;
    evq_push(&ev);
}

/* SDL key -> Windows virtual-key code. Letters and digits follow the
 * keyboard layout (as VK_A.. do); everything else goes by position. */
static int sdl_to_vk(SDL_Keycode key, SDL_Scancode sc)
{
    if (key >= 'a' && key <= 'z')
        return (int)(key - 'a' + 'A');
    if (key >= '0' && key <= '9')
        return (int)key;
    switch (key) {
    case ';': return PLAT_VK_OEM_1;
    case '=': return PLAT_VK_OEM_PLUS;
    case ',': return PLAT_VK_OEM_COMMA;
    case '-': return PLAT_VK_OEM_MINUS;
    case '.': return PLAT_VK_OEM_PERIOD;
    case '/': return PLAT_VK_OEM_2;
    case '`': return PLAT_VK_OEM_3;
    case '[': return PLAT_VK_OEM_4;
    case '\\': return PLAT_VK_OEM_5;
    case ']': return PLAT_VK_OEM_6;
    case '\'': return PLAT_VK_OEM_7;
    default: break;
    }
    if (sc >= SDL_SCANCODE_A && sc <= SDL_SCANCODE_Z)
        return 'A' + (int)(sc - SDL_SCANCODE_A);
    if (sc >= SDL_SCANCODE_1 && sc <= SDL_SCANCODE_9)
        return '1' + (int)(sc - SDL_SCANCODE_1);
    if (sc >= SDL_SCANCODE_F1 && sc <= SDL_SCANCODE_F12)
        return PLAT_VK_F1 + (int)(sc - SDL_SCANCODE_F1);
    if (sc >= SDL_SCANCODE_KP_1 && sc <= SDL_SCANCODE_KP_9)
        return PLAT_VK_NUMPAD0 + 1 + (int)(sc - SDL_SCANCODE_KP_1);
    switch (sc) {
    case SDL_SCANCODE_0: return '0';
    case SDL_SCANCODE_BACKSPACE: return PLAT_VK_BACK;
    case SDL_SCANCODE_TAB: return PLAT_VK_TAB;
    case SDL_SCANCODE_RETURN:
    case SDL_SCANCODE_KP_ENTER: return PLAT_VK_RETURN;
    case SDL_SCANCODE_LSHIFT:
    case SDL_SCANCODE_RSHIFT: return PLAT_VK_SHIFT;
    case SDL_SCANCODE_LCTRL:
    case SDL_SCANCODE_RCTRL: return PLAT_VK_CONTROL;
    case SDL_SCANCODE_LALT:
    case SDL_SCANCODE_RALT: return PLAT_VK_MENU;
    case SDL_SCANCODE_PAUSE: return PLAT_VK_PAUSE;
    case SDL_SCANCODE_CAPSLOCK: return PLAT_VK_CAPITAL;
    case SDL_SCANCODE_ESCAPE: return PLAT_VK_ESCAPE;
    case SDL_SCANCODE_SPACE: return PLAT_VK_SPACE;
    case SDL_SCANCODE_PAGEUP: return PLAT_VK_PRIOR;
    case SDL_SCANCODE_PAGEDOWN: return PLAT_VK_NEXT;
    case SDL_SCANCODE_END: return PLAT_VK_END;
    case SDL_SCANCODE_HOME: return PLAT_VK_HOME;
    case SDL_SCANCODE_LEFT: return PLAT_VK_LEFT;
    case SDL_SCANCODE_UP: return PLAT_VK_UP;
    case SDL_SCANCODE_RIGHT: return PLAT_VK_RIGHT;
    case SDL_SCANCODE_DOWN: return PLAT_VK_DOWN;
    case SDL_SCANCODE_PRINTSCREEN: return PLAT_VK_SNAPSHOT;
    case SDL_SCANCODE_INSERT: return PLAT_VK_INSERT;
    case SDL_SCANCODE_DELETE: return PLAT_VK_DELETE;
    case SDL_SCANCODE_KP_0: return PLAT_VK_NUMPAD0;
    case SDL_SCANCODE_KP_MULTIPLY: return PLAT_VK_MULTIPLY;
    case SDL_SCANCODE_KP_PLUS: return PLAT_VK_ADD;
    case SDL_SCANCODE_KP_MINUS: return PLAT_VK_SUBTRACT;
    case SDL_SCANCODE_KP_PERIOD: return PLAT_VK_DECIMAL;
    case SDL_SCANCODE_KP_DIVIDE: return PLAT_VK_DIVIDE;
    case SDL_SCANCODE_NUMLOCKCLEAR: return PLAT_VK_NUMLOCK;
    case SDL_SCANCODE_SCROLLLOCK: return PLAT_VK_SCROLL;
    case SDL_SCANCODE_SEMICOLON: return PLAT_VK_OEM_1;
    case SDL_SCANCODE_EQUALS: return PLAT_VK_OEM_PLUS;
    case SDL_SCANCODE_COMMA: return PLAT_VK_OEM_COMMA;
    case SDL_SCANCODE_MINUS: return PLAT_VK_OEM_MINUS;
    case SDL_SCANCODE_PERIOD: return PLAT_VK_OEM_PERIOD;
    case SDL_SCANCODE_SLASH: return PLAT_VK_OEM_2;
    case SDL_SCANCODE_GRAVE: return PLAT_VK_OEM_3;
    case SDL_SCANCODE_LEFTBRACKET: return PLAT_VK_OEM_4;
    case SDL_SCANCODE_BACKSLASH: return PLAT_VK_OEM_5;
    case SDL_SCANCODE_RIGHTBRACKET: return PLAT_VK_OEM_6;
    case SDL_SCANCODE_APOSTROPHE: return PLAT_VK_OEM_7;
    default: return 0;
    }
}

/* The control characters TranslateMessage makes from a WM_KEYDOWN (SDL's
 * text input only reports printable text). 0 = none. */
static int control_char(int vk, SDL_Keymod mod)
{
    bool ctrl = (mod & SDL_KMOD_CTRL) != 0, alt = (mod & SDL_KMOD_ALT) != 0;
    if (alt && !ctrl)
        return 0; /* WM_SYSKEYDOWN -> WM_SYSCHAR, which the game ignores */
    switch (vk) {
    case PLAT_VK_BACK: return ctrl ? 0x7f : 0x08;
    case PLAT_VK_TAB: return ctrl ? 0 : 0x09;
    case PLAT_VK_RETURN: return ctrl ? 0x0a : 0x0d;
    case PLAT_VK_ESCAPE: return 0x1b;
    default: break;
    }
    if (ctrl && !alt) {
        if (vk >= 'A' && vk <= 'Z')
            return vk - 'A' + 1;
        if (vk == PLAT_VK_OEM_4)
            return 0x1b;
        if (vk == PLAT_VK_OEM_5)
            return 0x1c;
        if (vk == PLAT_VK_OEM_6)
            return 0x1d;
    }
    return 0;
}

/* SDL_EVENT_TEXT_INPUT (UTF-8) -> WM_CHAR per character, in Windows-1252
 * (code points the game's fonts can't have are dropped). */
static void push_text(const char *text, SDL_Keymod mod)
{
    if ((mod & SDL_KMOD_ALT) && !(mod & SDL_KMOD_CTRL))
        return;
    static const uint16_t k_cp1252[32] = {
        0x20ac, 0, 0x201a, 0x0192, 0x201e, 0x2026, 0x2020, 0x2021, 0x02c6, 0x2030, 0x0160, 0x2039, 0x0152, 0, 0x017d, 0,
        0, 0x2018, 0x2019, 0x201c, 0x201d, 0x2022, 0x2013, 0x2014, 0x02dc, 0x2122, 0x0161, 0x203a, 0x0153, 0, 0x017e,
        0x0178};
    const char *p = text;
    while (p && *p) {
        Uint32 cp = SDL_StepUTF8(&p, NULL);
        if (cp == 0)
            break;
        if ((cp >= 0x20 && cp < 0x7f) || (cp >= 0xa0 && cp <= 0xff)) {
            push_char((uint8_t)cp);
            continue;
        }
        for (int i = 0; i < 32; i++)
            if (k_cp1252[i] && k_cp1252[i] == cp)
                push_char((uint8_t)(0x80 + i));
    }
}

static void translate(SDL_Event *e)
{
    PlatEvent ev = {0};
    switch (e->type) {
    case SDL_EVENT_QUIT:
#ifdef __APPLE__
        /* macOS: Cmd+Q / the app menu's Quit (and logout, which SDL holds
         * off until the app quits) are how a user closes an app there, so
         * they ask like the close button does. */
        push_simple(PLAT_EV_CLOSE);
#else
        push_simple(PLAT_EV_QUIT);
#endif
        break;
    case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
        push_simple(PLAT_EV_CLOSE);
        break;
    case SDL_EVENT_WINDOW_FOCUS_GAINED:
    case SDL_EVENT_WINDOW_FOCUS_LOST:
        g_alt_held = g_ctrl_held = false;
        ev.type = PLAT_EV_ACTIVATE;
        ev.active = e->type == SDL_EVENT_WINDOW_FOCUS_GAINED;
        evq_push(&ev);
        break;
    case SDL_EVENT_WINDOW_MINIMIZED:
        push_simple(PLAT_EV_MINIMIZE);
        break;
    case SDL_EVENT_WINDOW_RESTORED:
        g_wstate.maximized = (SDL_GetWindowFlags(g_window) & SDL_WINDOW_MAXIMIZED) != 0;
        push_simple(PLAT_EV_RESTORE);
        break;
    case SDL_EVENT_WINDOW_EXPOSED:
        push_simple(PLAT_EV_EXPOSE);
        break;
    case SDL_EVENT_DROP_FILE:
        if (e->drop.data) {
            if (g_dropped[0] && strcmp(g_dropped, e->drop.data) != 0)
                plat_release_dropped_file(g_dropped); /* superseded */
            SDL_strlcpy(g_dropped, e->drop.data, sizeof(g_dropped));
        }
        break;
    case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED:
        update_presentation();
        if (g_managed)
            push_simple(PLAT_EV_RESIZE);
        push_simple(PLAT_EV_EXPOSE);
        break;
    case SDL_EVENT_WINDOW_RESIZED:
        /* the windowed geometry the port remembers (ALIFEPORT.INI) */
        if (g_managed && !(SDL_GetWindowFlags(g_window) & (SDL_WINDOW_FULLSCREEN | SDL_WINDOW_MAXIMIZED))) {
            g_wstate.w = e->window.data1;
            g_wstate.h = e->window.data2;
        }
        break;
    case SDL_EVENT_WINDOW_MOVED:
        if (g_managed && !(SDL_GetWindowFlags(g_window) & (SDL_WINDOW_FULLSCREEN | SDL_WINDOW_MAXIMIZED))) {
            g_wstate.x = e->window.data1;
            g_wstate.y = e->window.data2;
        }
        break;
    case SDL_EVENT_WINDOW_MAXIMIZED:
        g_wstate.maximized = true;
        break;
    case SDL_EVENT_WINDOW_ENTER_FULLSCREEN:
        g_wstate.fullscreen = true;
        break;
    case SDL_EVENT_WINDOW_LEAVE_FULLSCREEN:
        g_wstate.fullscreen = false;
        break;
    case SDL_EVENT_KEY_DOWN:
    case SDL_EVENT_KEY_UP:
        /* Port: Alt+Enter and F11 toggle fullscreen (the original uses
         * F1-F10 only). Neither the key nor its release reaches the game,
         * nor Alt's release after Alt+Enter (it would open the menu bar). */
        if (e->key.scancode == SDL_SCANCODE_LALT || e->key.scancode == SDL_SCANCODE_RALT)
            g_alt_held = e->type == SDL_EVENT_KEY_DOWN;
        else if (e->key.scancode == SDL_SCANCODE_LCTRL || e->key.scancode == SDL_SCANCODE_RCTRL)
            g_ctrl_held = e->type == SDL_EVENT_KEY_DOWN;
        if (g_managed && e->type == SDL_EVENT_KEY_DOWN) {
            bool enter = e->key.scancode == SDL_SCANCODE_RETURN || e->key.scancode == SDL_SCANCODE_KP_ENTER;
            /* Alt from the key events seen (and SDL's keyboard state):
             * synthetic events can carry any modifier bits */
            SDL_Keymod held = SDL_GetModState();
            bool alt = (g_alt_held || (held & SDL_KMOD_ALT)) && !(g_ctrl_held || (held & SDL_KMOD_CTRL));
            bool toggle = e->key.scancode == SDL_SCANCODE_F11 || (enter && alt);
#ifdef __APPLE__
            /* and the macOS shortcut, Ctrl+Cmd+F (F11 shows the desktop there) */
            if (e->key.scancode == SDL_SCANCODE_F && (held & SDL_KMOD_GUI) && (held & SDL_KMOD_CTRL))
                toggle = true;
#endif
            if (toggle) {
                if (!e->key.repeat)
                    plat_window_set_fullscreen(!g_wstate.fullscreen);
                g_swallow_sc = e->key.scancode;
                if (enter)
                    g_swallow_alt = true;
                break;
            }
        } else if (g_managed && e->type == SDL_EVENT_KEY_UP) {
            if (g_swallow_sc && e->key.scancode == g_swallow_sc) {
                g_swallow_sc = SDL_SCANCODE_UNKNOWN;
                break;
            }
            if (g_swallow_alt && (e->key.scancode == SDL_SCANCODE_LALT || e->key.scancode == SDL_SCANCODE_RALT)) {
                g_swallow_alt = false;
                break;
            }
        }
        ev.vk = sdl_to_vk(e->key.key, e->key.scancode);
        if (!ev.vk)
            break;
        ev.type = e->type == SDL_EVENT_KEY_DOWN ? PLAT_EV_KEY_DOWN : PLAT_EV_KEY_UP;
        evq_push(&ev);
        if (e->type == SDL_EVENT_KEY_DOWN) {
            int ch = control_char(ev.vk, e->key.mod);
            if (ch)
                push_char((uint8_t)ch);
        }
        break;
    case SDL_EVENT_TEXT_INPUT:
        push_text(e->text.text, SDL_GetModState());
        break;
    case SDL_EVENT_MOUSE_MOTION:
    case SDL_EVENT_MOUSE_BUTTON_DOWN:
    case SDL_EVENT_MOUSE_BUTTON_UP:
        /* window points -> output pixels -> framebuffer */
        SDL_ConvertEventToRenderCoordinates(g_renderer, e);
        if (e->type == SDL_EVENT_MOUSE_MOTION) {
            ev.type = PLAT_EV_MOUSE_MOVE;
            present_to_fb(g_dst, g_indexed->w, g_indexed->h, e->motion.x, e->motion.y, &ev.x, &ev.y);
        } else {
            ev.type = e->type == SDL_EVENT_MOUSE_BUTTON_DOWN ? PLAT_EV_MOUSE_DOWN : PLAT_EV_MOUSE_UP;
            ev.button = e->button.button;
            present_to_fb(g_dst, g_indexed->w, g_indexed->h, e->button.x, e->button.y, &ev.x, &ev.y);
        }
        evq_push(&ev);
        break;
    default:
        break;
    }
}

bool plat_poll_event(PlatEvent *ev)
{
    SDL_Event e;
    while (!evq_pop(ev)) {
        if (!SDL_PollEvent(&e))
            return false;
        /* Windows has at most one WM_MOUSEMOVE pending (it is made from the
         * current position when the queue is read), so collapse a run of
         * motion events into the last one. */
        if (e.type == SDL_EVENT_MOUSE_MOTION) {
            SDL_Event next;
            while (SDL_PeepEvents(&next, 1, SDL_PEEKEVENT, SDL_EVENT_FIRST, SDL_EVENT_LAST) == 1 &&
                   next.type == SDL_EVENT_MOUSE_MOTION && SDL_PollEvent(&next))
                e = next;
        }
        translate(&e);
    }
    return true;
}

void plat_yield(void)
{
#ifdef __EMSCRIPTEN__
    /* The game keeps its original blocking main loop; ASYNCIFY lets us
     * unwind to the browser here. */
    emscripten_sleep(0);
#endif
}

uint32_t plat_ticks_ms(void) { return (uint32_t)SDL_GetTicks(); }

void plat_delay_ms(uint32_t ms)
{
#ifdef __EMSCRIPTEN__
    emscripten_sleep(ms);
#else
    SDL_Delay(ms);
#endif
}

bool plat_audio_open(int rate)
{
    SDL_AudioSpec spec = {SDL_AUDIO_S16, 2, rate};
    g_audio = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, NULL, NULL);
    if (!g_audio) {
        SDL_Log("audio: %s", SDL_GetError());
        return false;
    }
    SDL_ResumeAudioStreamDevice(g_audio);
    return true;
}

void plat_audio_close(void)
{
    if (g_audio)
        SDL_DestroyAudioStream(g_audio);
    g_audio = NULL;
}

void plat_audio_pause(bool paused)
{
    if (!g_audio)
        return;
    if (paused) {
        SDL_PauseAudioStreamDevice(g_audio);
        SDL_ClearAudioStream(g_audio);
    } else {
        SDL_ResumeAudioStreamDevice(g_audio);
    }
}

void plat_audio_queue(const int16_t *frames, int frame_count)
{
    if (g_audio)
        SDL_PutAudioStreamData(g_audio, frames, frame_count * 4);
}

int plat_audio_queued_frames(void)
{
    return g_audio ? SDL_GetAudioStreamQueued(g_audio) / 4 : 0;
}

void plat_set_data_dir(const char *dir)
{
    SDL_strlcpy(g_data_dir, dir, sizeof(g_data_dir));
}

static bool find_ci(const char *dir, const char *name, char *out, size_t out_len)
{
    int count = 0;
    char **entries = SDL_GlobDirectory(dir, NULL, 0, &count);
    bool found = false;
    for (int i = 0; entries && i < count && !found; i++) {
        if (SDL_strcasecmp(entries[i], name) == 0) {
            SDL_snprintf(out, out_len, "%s/%s", dir, entries[i]);
            found = true;
        }
    }
    SDL_free(entries);
    return found;
}

const char *plat_data_dir(void) { return g_data_dir; }

bool plat_pref_path(const char *name, char *out, size_t out_len)
{
    char *dir = SDL_GetPrefPath("Afterlife", "Afterlife");
    if (!dir)
        return false;
    SDL_snprintf(out, out_len, "%s%s", dir, name);
    SDL_free(dir);
    return true;
}

const char *plat_save_dir(void)
{
#ifdef __EMSCRIPTEN__
    /* the pref folder, which web/pre.js keeps in IndexedDB (written back
     * whenever a file there is closed); the data folder is the package
     * built in, or the files the player picked */
    static char dir[1024];
    if (!dir[0]) {
        char *p = SDL_GetPrefPath("Afterlife", "Afterlife");
        if (!p)
            return NULL;
        SDL_strlcpy(dir, p, sizeof(dir));
        SDL_free(p);
        size_t n = strlen(dir);
        if (n > 1 && dir[n - 1] == '/')
            dir[n - 1] = '\0';
    }
    return dir;
#else
    return NULL;
#endif
}

bool plat_data_path(const char *name, char *out, size_t out_len)
{
    /* Accept DOS paths from the original (e.g. "C:\\ALIFE\\SAVE.SAV") by
     * walking each component case-insensitively under the data dir. */
    char path[1024], part[256];
    SDL_strlcpy(path, g_data_dir, sizeof(path));
    const char *p = name;
    if (p[0] && p[1] == ':')
        p += 2;
    while (*p) {
        while (*p == '\\' || *p == '/')
            p++;
        size_t n = strcspn(p, "\\/");
        if (n == 0)
            break;
        if (n >= sizeof(part))
            return false;
        memcpy(part, p, n);
        part[n] = '\0';
        p += n;
        char next[1024];
        if (!find_ci(path, part, next, sizeof(next)))
            return false;
        SDL_strlcpy(path, next, sizeof(path));
    }
    SDL_strlcpy(out, path, out_len);
    return true;
}

FILE *plat_fopen_data(const char *name)
{
    char path[1024];
    return plat_data_path(name, path, sizeof(path)) ? fopen(path, "rb") : NULL;
}

/* --- file dialogs (port: SDL_ShowOpenFileDialog / SDL_ShowSaveFileDialog,
 * asynchronous; the game polls from its modal loop) --- */

static int g_dialog_state; /* 0 idle, 1 pending, 2 chosen, 3 cancelled / failed */
static char g_dialog_path[1024];

#ifndef __EMSCRIPTEN__
static void SDLCALL dialog_done(void *user, const char *const *files, int filter)
{
    (void)user, (void)filter;
    if (files && files[0]) {
        SDL_strlcpy(g_dialog_path, files[0], sizeof(g_dialog_path));
        g_dialog_state = 2;
    } else {
        g_dialog_state = 3;
    }
}
#endif

bool plat_take_dropped_file(char *out, size_t out_len)
{
    if (!g_dropped[0])
        return false;
    SDL_strlcpy(out, g_dropped, out_len);
    g_dropped[0] = '\0';
    return true;
}

void plat_release_dropped_file(const char *path)
{
#ifdef __EMSCRIPTEN__
    /* SDL's copy, /tmp/filedrop/N/NAME, and its folder N */
    static const char k_dir[] = "/tmp/filedrop/";
    if (!path || strncmp(path, k_dir, sizeof(k_dir) - 1) != 0)
        return;
    remove(path);
    char dir[1024];
    SDL_strlcpy(dir, path, sizeof(dir));
    char *slash = strrchr(dir, '/');
    if (slash && slash > dir + sizeof(k_dir) - 1) {
        *slash = '\0';
        remove(dir);
    }
#else
    (void)path; /* the user's own file */
#endif
}

#ifdef __EMSCRIPTEN__
/* An <a download> with the file's bytes as a Blob. */
EM_JS(int, web_download, (const char *path, const char *name), {
    try {
        var data = FS.readFile(UTF8ToString(path));
        var url = URL.createObjectURL(new Blob([data], { type: 'application/octet-stream' }));
        var a = document.createElement('a');
        a.href = url;
        a.download = UTF8ToString(name);
        a.style.display = 'none';
        document.body.appendChild(a);
        a.click();
        a.remove();
        setTimeout(function () { URL.revokeObjectURL(url); }, 60000);
        return 1;
    } catch (e) {
        console.warn('Afterlife: download failed', e);
        return 0;
    }
});
#endif

bool plat_can_download(void)
{
#ifdef __EMSCRIPTEN__
    return true;
#else
    return false;
#endif
}

bool plat_download_file(const char *path, const char *name)
{
#ifdef __EMSCRIPTEN__
    return web_download(path, name) != 0;
#else
    (void)path, (void)name;
    return false;
#endif
}

bool plat_file_dialog_begin(bool save, const char *default_location, const char *filter_name, const char *pattern)
{
#ifdef __EMSCRIPTEN__
    (void)save, (void)default_location, (void)filter_name, (void)pattern;
    return false; /* no file dialogs in the browser */
#else
    if (g_dialog_state == 1 || !g_window)
        return false;
    const char *driver = SDL_GetCurrentVideoDriver();
    if (!driver || SDL_strcmp(driver, "dummy") == 0 || SDL_strcmp(driver, "offscreen") == 0)
        return false; /* headless: no dialogs */
    static SDL_DialogFileFilter filter;
    static char name[64], pat[64];
    SDL_strlcpy(name, filter_name ? filter_name : "Files", sizeof(name));
    SDL_strlcpy(pat, pattern ? pattern : "*", sizeof(pat));
    filter.name = name;
    filter.pattern = pat;
    g_dialog_state = 1;
    g_dialog_path[0] = '\0';
    if (save)
        SDL_ShowSaveFileDialog(dialog_done, NULL, g_window, &filter, 1, default_location);
    else
        SDL_ShowOpenFileDialog(dialog_done, NULL, g_window, &filter, 1, default_location, false);
    return true;
#endif
}

int plat_file_dialog_poll(char *out, size_t out_len)
{
    switch (g_dialog_state) {
    case 1:
        return 0;
    case 2:
        SDL_strlcpy(out, g_dialog_path, out_len);
        g_dialog_state = 0;
        return 1;
    default:
        g_dialog_state = 0;
        return -1;
    }
}

/* PlatFile is a thin wrapper over the CRT stream; keeping it opaque leaves
 * room for a backend without stdio (e.g. reading straight from the CD image). */
struct PlatFile {
    FILE *f;
};

PlatFile *plat_file_open(const char *name)
{
    FILE *f = plat_fopen_data(name);
    PlatFile *pf = f ? malloc(sizeof(*pf)) : NULL;
    if (!pf) {
        if (f)
            fclose(f);
        return NULL;
    }
    pf->f = f;
    return pf;
}

void plat_file_close(PlatFile *f)
{
    if (f) {
        fclose(f->f);
        free(f);
    }
}

size_t plat_file_read(PlatFile *f, void *buf, size_t size, size_t count)
{
    return fread(buf, size, count, f->f);
}

int plat_file_seek(PlatFile *f, long offset, int whence)
{
    static const int k_whence[] = {SEEK_SET, SEEK_CUR, SEEK_END};
    if (whence < 0 || whence > 2)
        return -1;
    return fseek(f->f, offset, k_whence[whence]) == 0 ? 0 : -1;
}

long plat_file_tell(PlatFile *f)
{
    return ftell(f->f);
}
