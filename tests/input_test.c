/*
 * tests/input_test: feeds synthetic SDL events through the platform layer
 * and main_wnd_proc, and checks what reaches the keyboard queue and the
 * mouse module; then checks the emulated 16 ms timer. Runs headless
 * (SDL_VIDEO_DRIVER=dummy is set if nothing else is). No game data needed.
 */
#include <SDL3/SDL.h>
#include <stdio.h>
#include <stdlib.h>

#include "game/input.h"
#include "game/startup.h"
#include "game/timer.h"
#include "gfx/mouse.h"
#include "gfx/surface.h"
#include "platform/platform.h"

static int g_fail;
#define CHECK(c)                                                                                                   \
    do {                                                                                                           \
        if (!(c)) {                                                                                                \
            fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #c);                                          \
            g_fail = 1;                                                                                            \
        }                                                                                                          \
    } while (0)

static void push_key(SDL_EventType type, SDL_Keycode key, SDL_Scancode sc, SDL_Keymod mod)
{
    SDL_Event e;
    SDL_zero(e); /* not "= {0}": that zeroes only the union's first member (GCC 15+) */
    e.type = type;
    e.key.key = key;
    e.key.scancode = sc;
    e.key.mod = mod;
    e.key.down = type == SDL_EVENT_KEY_DOWN;
    SDL_PushEvent(&e);
}

static void push_text(const char *t)
{
    SDL_Event e;
    SDL_zero(e);
    e.type = SDL_EVENT_TEXT_INPUT;
    e.text.text = SDL_strdup(t); /* leaks a few bytes; test only */
    SDL_PushEvent(&e);
}

static void drain(void)
{
    while (sys_dispatch_message())
        ;
}

static int g_calls[TM_SLOTS];
static void on_tick(int slot, void *user)
{
    (void)user;
    g_calls[slot]++;
}

int main(void)
{
    SDL_setenv_unsafe("SDL_VIDEO_DRIVER", "dummy", 0);
    if (!plat_init("input_test", 1, 1) || !gfx_init(320, 200, false)) {
        fprintf(stderr, "init failed\n");
        return 1;
    }
    kb_initialize();
    gfx_mouse_init();
    drain();

    /* 'a' down: WM_KEYDOWN 'A', then the text 'a' -> WM_CHAR, then up. */
    push_key(SDL_EVENT_KEY_DOWN, SDLK_A, SDL_SCANCODE_A, SDL_KMOD_NONE);
    push_text("a\xc3\xa9"); /* a, e-acute (0xe9 in cp1252) */
    push_key(SDL_EVENT_KEY_UP, SDLK_A, SDL_SCANCODE_A, SDL_KMOD_NONE);
    /* Enter: key down + control char 13. Ctrl+C: char 3. */
    push_key(SDL_EVENT_KEY_DOWN, SDLK_RETURN, SDL_SCANCODE_RETURN, SDL_KMOD_NONE);
    push_key(SDL_EVENT_KEY_DOWN, SDLK_C, SDL_SCANCODE_C, SDL_KMOD_LCTRL);
    push_key(SDL_EVENT_KEY_DOWN, SDLK_F5, SDL_SCANCODE_F5, SDL_KMOD_NONE);
    push_key(SDL_EVENT_KEY_DOWN, SDLK_LSHIFT, SDL_SCANCODE_LSHIFT, SDL_KMOD_NONE);
    drain();

    static const struct { int type, vk, ch; } want[] = {
        {KB_EV_DOWN, 'A', 'A'}, {KB_EV_CHAR, 0, 'a'},  {KB_EV_CHAR, 0, 0xe9}, {KB_EV_UP, 'A', 'A'},
        {KB_EV_DOWN, 0x0d, 0x0d}, {KB_EV_CHAR, 0, 13}, {KB_EV_DOWN, 'C', 'C'}, {KB_EV_CHAR, 0, 3},
        {KB_EV_DOWN, 0x74, 0x74}, {KB_EV_DOWN, 0x10, 0x10},
    };
    CHECK(kb_pending() == (int)(sizeof(want) / sizeof(want[0])));
    for (size_t i = 0; i < sizeof(want) / sizeof(want[0]); i++) {
        const KbEvent *k = kb_get_event();
        CHECK(k != NULL);
        if (!k)
            break;
        if (k->type != want[i].type || k->vk != want[i].vk || k->ch != want[i].ch) {
            fprintf(stderr, "FAIL event %zu: got {%d, 0x%x, 0x%x}\n", i, (int)k->type, (unsigned)k->vk, k->ch);
            g_fail = 1;
        }
    }
    CHECK(kb_get_event() == NULL);

    /* Queue limit: 32. */
    for (int i = 0; i < 40; i++)
        kb_post(KB_EV_CHAR, 0, (uint8_t)i);
    CHECK(kb_pending() == 32);
    kb_queue_reset();

    /* Mouse: button transitions are queued with the position. */
    gfx_mouse_queue_reset();
    SDL_Event m;
    SDL_zero(m);
    m.type = SDL_EVENT_MOUSE_BUTTON_DOWN;
    m.button.button = SDL_BUTTON_LEFT;
    m.button.down = true;
    m.button.x = 10;
    m.button.y = 20;
    SDL_PushEvent(&m);
    m.type = SDL_EVENT_MOUSE_BUTTON_UP;
    m.button.down = false;
    SDL_PushEvent(&m);
    drain();
    int x, y;
    unsigned b = gfx_mouse_get_state(&x, &y);
    printf("mouse 1: buttons %u at %d,%d\n", b, x, y);
    CHECK(b == GFX_MOUSE_LEFT);
    b = gfx_mouse_get_state(&x, &y);
    CHECK(b == 0);

    /* Close button -> WM_CLOSE. */
    SDL_Event c;
    SDL_zero(c);
    c.type = SDL_EVENT_WINDOW_CLOSE_REQUESTED;
    SDL_PushEvent(&c);
    drain();
    CHECK(sys_quit_requested());

    /* Timer: a 60 Hz service (period 16 ms) and a 50 ms one-shot. */
    tm_initialize();
    int s1 = tm_add_service(on_tick, 60);
    int s2 = tm_add(on_tick, 50, TM_FLAG_ONESHOT, NULL);
    CHECK(s1 == 1 && s2 == 2);
    uint32_t t0 = tm_get_time();
    tm_wait(320);
    uint32_t wall = tm_get_time() - t0;
    printf("timer: %u ms wall, elapsed %u, service %d calls, one-shot %d\n", (unsigned)wall,
           (unsigned)tm_get_elapsed(), g_calls[s1], g_calls[s2]);
    CHECK(g_calls[s1] == 20);
    CHECK(g_calls[s2] == 1);
    CHECK(wall >= 300);
    tm_remove_service(s1);
    tm_shutdown();

    gfx_shutdown();
    plat_shutdown();
    puts(g_fail ? "input_test: FAILED" : "input_test: ok");
    return g_fail;
}
