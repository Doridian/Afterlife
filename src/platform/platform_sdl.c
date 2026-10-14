#include "platform.h"

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

bool plat_init(const char *title, int width, int height)
{
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO)) {
        SDL_Log("SDL_Init: %s", SDL_GetError());
        return false;
    }
    if (!SDL_CreateWindowAndRenderer(title, width, height, SDL_WINDOW_RESIZABLE, &g_window, &g_renderer)) {
        SDL_Log("SDL_CreateWindowAndRenderer: %s", SDL_GetError());
        return false;
    }
    SDL_SetRenderLogicalPresentation(g_renderer, width, height, SDL_LOGICAL_PRESENTATION_LETTERBOX);
    SDL_SetRenderVSync(g_renderer, 1);

    g_indexed = SDL_CreateSurface(width, height, SDL_PIXELFORMAT_INDEX8);
    g_palette = SDL_CreateSurfacePalette(g_indexed);
    g_texture = SDL_CreateTexture(g_renderer, SDL_PIXELFORMAT_XRGB8888, SDL_TEXTUREACCESS_STREAMING, width, height);
    SDL_SetTextureScaleMode(g_texture, SDL_SCALEMODE_NEAREST);
    return g_indexed && g_palette && g_texture;
}

void plat_shutdown(void)
{
    if (g_audio)
        SDL_DestroyAudioStream(g_audio);
    SDL_DestroyTexture(g_texture);
    SDL_DestroySurface(g_indexed);
    SDL_DestroyRenderer(g_renderer);
    SDL_DestroyWindow(g_window);
    SDL_Quit();
}

uint8_t *plat_video_framebuffer(void) { return g_indexed->pixels; }
int plat_video_width(void) { return g_indexed->w; }
int plat_video_height(void) { return g_indexed->h; }

void plat_video_set_palette(const PlatColor *colors, int first, int count)
{
    SDL_Color tmp[256];
    for (int i = 0; i < count; i++)
        tmp[i] = (SDL_Color){colors[i].r, colors[i].g, colors[i].b, 255};
    SDL_SetPaletteColors(g_palette, tmp, first, count);
}

bool plat_video_set_mode(int width, int height, bool fullscreen)
{
    if (!g_window || width <= 0 || height <= 0)
        return false;
    if (!g_indexed || g_indexed->w != width || g_indexed->h != height) {
        SDL_Surface *ind = SDL_CreateSurface(width, height, SDL_PIXELFORMAT_INDEX8);
        SDL_Palette *pal = ind ? SDL_CreateSurfacePalette(ind) : NULL;
        SDL_Texture *tex =
            SDL_CreateTexture(g_renderer, SDL_PIXELFORMAT_XRGB8888, SDL_TEXTUREACCESS_STREAMING, width, height);
        if (!ind || !pal || !tex) {
            SDL_Log("plat_video_set_mode: %s", SDL_GetError());
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
        SDL_SetRenderLogicalPresentation(g_renderer, width, height, SDL_LOGICAL_PRESENTATION_LETTERBOX);
    }
    SDL_SetWindowFullscreen(g_window, fullscreen);
    if (!fullscreen)
        SDL_SetWindowSize(g_window, width, height);
    return true;
}

void plat_video_desktop_size(int *width, int *height)
{
    const SDL_DisplayMode *mode = SDL_GetDesktopDisplayMode(SDL_GetPrimaryDisplay());
    *width = mode ? mode->w : 0;
    *height = mode ? mode->h : 0;
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

void plat_cursor_show(bool show)
{
    if (show)
        SDL_ShowCursor();
    else
        SDL_HideCursor();
}

void plat_mouse_warp(int x, int y)
{
    float wx = 0, wy = 0;
    if (g_renderer && SDL_RenderCoordinatesToWindow(g_renderer, (float)x, (float)y, &wx, &wy))
        SDL_WarpMouseInWindow(g_window, wx, wy);
}

void plat_video_present(void)
{
    SDL_Surface *dst;
    if (SDL_LockTextureToSurface(g_texture, NULL, &dst)) {
        SDL_BlitSurface(g_indexed, NULL, dst, NULL);
        SDL_UnlockTexture(g_texture);
    }
    SDL_RenderClear(g_renderer);
    SDL_RenderTexture(g_renderer, g_texture, NULL, NULL);
    SDL_RenderPresent(g_renderer);
}

bool plat_poll_event(PlatEvent *ev)
{
    SDL_Event e;
    while (SDL_PollEvent(&e)) {
        memset(ev, 0, sizeof(*ev));
        switch (e.type) {
        case SDL_EVENT_QUIT:
            ev->type = PLAT_EV_QUIT;
            return true;
        case SDL_EVENT_KEY_DOWN:
        case SDL_EVENT_KEY_UP:
            ev->type = e.type == SDL_EVENT_KEY_DOWN ? PLAT_EV_KEY_DOWN : PLAT_EV_KEY_UP;
            ev->key = (int)e.key.key;
            return true;
        case SDL_EVENT_MOUSE_MOTION:
        case SDL_EVENT_MOUSE_BUTTON_DOWN:
        case SDL_EVENT_MOUSE_BUTTON_UP:
            SDL_ConvertEventToRenderCoordinates(g_renderer, &e);
            if (e.type == SDL_EVENT_MOUSE_MOTION) {
                ev->type = PLAT_EV_MOUSE_MOVE;
                ev->x = (int)e.motion.x;
                ev->y = (int)e.motion.y;
            } else {
                ev->type = e.type == SDL_EVENT_MOUSE_BUTTON_DOWN ? PLAT_EV_MOUSE_DOWN : PLAT_EV_MOUSE_UP;
                ev->button = e.button.button;
                ev->x = (int)e.button.x;
                ev->y = (int)e.button.y;
            }
            return true;
        default:
            break;
        }
    }
    return false;
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

FILE *plat_fopen_data(const char *name)
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
            return NULL;
        memcpy(part, p, n);
        part[n] = '\0';
        p += n;
        char next[1024];
        if (!find_ci(path, part, next, sizeof(next)))
            return NULL;
        SDL_strlcpy(path, next, sizeof(path));
    }
    return fopen(path, "rb");
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
