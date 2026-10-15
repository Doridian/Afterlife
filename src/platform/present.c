/* SPDX-License-Identifier: AGPL-3.0-or-later */
/*
 * Port-only framebuffer layout and UI scale arithmetic. See present.h.
 */
#include "platform/present.h"

/* (no libm: floor and round by hand) */
static int ifloor(double v)
{
    int i = (int)v;
    return i - (v < i);
}

PresentRect present_layout(int out_w, int out_h, int fb_w, int fb_h)
{
    PresentRect r = {0, 0, fb_w, fb_h};
    if (out_w <= 0 || out_h <= 0 || fb_w <= 0 || fb_h <= 0)
        return r;
    if (out_w >= fb_w && out_h >= fb_h) {
        int kx = out_w / fb_w, ky = out_h / fb_h;
        int k = kx < ky ? kx : ky;
        r.w = fb_w * k;
        r.h = fb_h * k;
    } else {
        double sx = (double)out_w / fb_w, sy = (double)out_h / fb_h;
        double s = sx < sy ? sx : sy;
        r.w = (int)(fb_w * s + 0.5);
        r.h = (int)(fb_h * s + 0.5);
        if (r.w < 1)
            r.w = 1;
        if (r.h < 1)
            r.h = 1;
    }
    r.x = (out_w - r.w) / 2;
    r.y = (out_h - r.h) / 2;
    return r;
}

void present_to_fb(PresentRect dst, int fb_w, int fb_h, float px, float py, int *fx, int *fy)
{
    double x = dst.w > 0 ? ((double)px - dst.x) * fb_w / dst.w : px;
    double y = dst.h > 0 ? ((double)py - dst.y) * fb_h / dst.h : py;
    *fx = ifloor(x);
    *fy = ifloor(y);
}

void present_from_fb(PresentRect dst, int fb_w, int fb_h, int fx, int fy, float *px, float *py)
{
    *px = (float)(dst.x + (fx + 0.5) * (fb_w > 0 ? (double)dst.w / fb_w : 1.0));
    *py = (float)(dst.y + (fy + 0.5) * (fb_h > 0 ? (double)dst.h / fb_h : 1.0));
}

int present_auto_scale(float density)
{
    if (!(density > 0.0f))
        return 1;
    int pct = (int)(density * 100.0f + 0.5f);
    int k = (pct + 60) / 100;
    return k < 1 ? 1 : k > PRESENT_SCALE_MAX ? PRESENT_SCALE_MAX : k;
}

int present_clamp_scale(int scale, int desktop_w, int desktop_h)
{
    if (scale < 1)
        scale = 1;
    if (scale > PRESENT_SCALE_MAX)
        scale = PRESENT_SCALE_MAX;
    while (scale > 1 && desktop_w > 0 && desktop_h > 0 &&
           (desktop_w / scale < PRESENT_MIN_W || desktop_h / scale < PRESENT_MIN_H))
        scale--;
    return scale;
}
