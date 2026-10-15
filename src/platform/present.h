/* SPDX-License-Identifier: AGPL-3.0-or-later */
/*
 * Port-only: how the 8-bit framebuffer is laid out in the window, and the
 * UI scale (an enhancement, see docs/enhancements.md). Pure arithmetic, no
 * SDL, so tests can check it; platform_sdl.c uses it to present the
 * framebuffer and to map mouse positions back.
 *
 * With UI scale k the game is told the desktop is (desktop pixels / k), so
 * Resolution=MAX gives a framebuffer k times smaller than the screen, and
 * the presentation scales it back up by the largest whole factor that
 * fits (crisp nearest-neighbour pixels).
 */
#ifndef AFTERLIFE_PLATFORM_PRESENT_H
#define AFTERLIFE_PLATFORM_PRESENT_H

/* The smallest framebuffer the game is laid out for (the title's art). */
#define PRESENT_MIN_W 640
#define PRESENT_MIN_H 480
#define PRESENT_SCALE_MAX 4

/* Where the framebuffer goes in the output, in output pixels. */
typedef struct {
    int x, y, w, h;
} PresentRect;

/* An fb_w x fb_h framebuffer in an out_w x out_h output: scaled by the
 * largest whole factor that fits and centred; when the output is smaller
 * than the framebuffer, scaled down to fit (letterboxed). */
PresentRect present_layout(int out_w, int out_h, int fb_w, int fb_h);

/* Output pixel (px, py) -> framebuffer pixel (may lie outside it). */
void present_to_fb(PresentRect dst, int fb_w, int fb_h, float px, float py, int *fx, int *fy);
/* Framebuffer pixel -> output position of its centre. */
void present_from_fb(PresentRect dst, int fb_w, int fb_h, int fx, int fy, float *px, float *py);

/* The automatic UI scale for a desktop pixel density (1.5 at 150 %):
 * 1 below 140 %, 2 from 140 % to below 240 %, then 3, ... */
int present_auto_scale(float density);
/* Clamps a UI scale to 1..PRESENT_SCALE_MAX and so that the desktop
 * divided by it is still at least PRESENT_MIN_W x PRESENT_MIN_H. */
int present_clamp_scale(int scale, int desktop_w, int desktop_h);

/* Port window (docs/rendering.md): the game's logical desktop for a window
 * of out_w x out_h pixels at UI scale `scale` (1..4): the effective scale
 * (`scale` lowered until out / scale is at least PRESENT_MIN_W x
 * PRESENT_MIN_H) and the framebuffer, out / effective scale, never smaller
 * than PRESENT_MIN_W x PRESENT_MIN_H (a smaller window letterboxes it).
 * Any pointer may be NULL. */
void present_logical_size(int out_w, int out_h, int scale, int *fb_w, int *fb_h, int *eff_scale);

#endif
