/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 nullwave contributors */
/* The HD render (host only, NW_HD: tools/hd_shots.sh): the same UI code drawing at 4x as well, for the pictures of
 * the guide and the website. Each canvas call the UI makes (cv_begin, cv_rect, cv_text, cv_blit) is done twice: at
 * 1x as always, and into a 4x canvas and a 960 x 960 screen. The shapes nullwave draws itself (rrect, circle,
 * aa_line, the knobs' rings) are drawn again at 4x, anti-aliased, and text in the same Barlow faces at four times
 * their size (tools/gen_hd_font.py). "expect hd:NAME 1" in a script writes $NW_HD_DIR/NAME@4x.rgb: 960 x 960,
 * 8-bit RGB, which tools/hd_shots.py turns into PNG and WebP. Never in the firmware: no RAM, no flash, no time there. */
#ifdef NW_HD
#include <stdio.h>
#include <string.h>
#include "build/hd/hd_font.h"

#define HDS 4
static uint16_t hd_cv[240 * HDS * 240 * HDS];   /* the canvas at 4x (plain RGB565, not swapped) */
static uint16_t hd_fb[240 * HDS * 240 * HDS];   /* the screen at 4x */
static uint32_t hd_w, hd_h;
static int hd_quiet;                     /* inside a shape drawn at 4x of its own: its 1x pixels not mirrored */

static void hd_put(int32_t X, int32_t Y, uint16_t c, uint32_t a)   /* a 4x pixel, a 0..255, in canvas rows */
{
    uint16_t *p;
    uint32_t bg, r, g, b;
    if (X < 0 || (uint32_t)X >= hd_w || Y < cv_y0 * HDS || Y >= cv_y1 * HDS || !a)
        return;
    p = &hd_cv[(uint32_t)Y * hd_w + (uint32_t)X];
    if (a >= 255u) {
        *p = c;
        return;
    }
    bg = *p;
    r = ((c >> 11) * a + (bg >> 11) * (255u - a)) / 255u;
    g = (((c >> 5) & 63u) * a + ((bg >> 5) & 63u) * (255u - a)) / 255u;
    b = ((c & 31u) * a + (bg & 31u) * (255u - a)) / 255u;
    *p = (uint16_t)(r << 11 | g << 5 | b);
}

static void hd_begin(uint32_t w, uint32_t h, uint16_t bg)
{
    uint32_t i, n;
    hd_w = w * HDS;
    hd_h = h * HDS;
    if (hd_w * hd_h > sizeof hd_cv / 2)
        hd_h = (uint32_t)(sizeof hd_cv / 2) / hd_w;
    n = hd_w * hd_h;
    for (i = 0; i < n; i++)
        hd_cv[i] = bg;
}

static void hd_fill(int32_t x, int32_t y, int32_t w, int32_t h, uint16_t c)   /* 1x coordinates */
{
    int32_t X, Y, X0 = x * HDS, Y0 = (y + cv_oy) * HDS;
    for (Y = Y0; Y < Y0 + h * HDS; Y++)
        for (X = X0; X < X0 + w * HDS; X++)
            hd_put(X, Y, c, 255u);
}

static void hd_blend1(int32_t x, int32_t y, uint16_t c, uint32_t a15)   /* a 1x pixel's blend, as a 4x block */
{
    int32_t X, Y;
    for (Y = (y + cv_oy) * HDS; Y < (y + cv_oy + 1) * HDS; Y++)
        for (X = x * HDS; X < (x + 1) * HDS; X++)
            hd_put(X, Y, c, a15 * 17u);
}

static void hd_blit(uint32_t x, uint32_t y)
{
    uint32_t j;
    for (j = 0; j < hd_h && (y * HDS + j) < 240u * HDS; j++)
        memcpy(&hd_fb[(y * HDS + j) * 240u * HDS + x * HDS], &hd_cv[j * hd_w],
               (x * HDS + hd_w > 240u * HDS ? 240u * HDS - x * HDS : hd_w) * 2u);
}

static const hd_font_t *hd_face(const felucca_font_t *f)
{
    return f == &FONT_T ? &HD_T : f == &FONT_XS ? &HD_XS : f == &FONT_S ? &HD_S : f == &FONT_B ? &HD_B
         : f == &FONT_M ? &HD_M : &HD_L;
}

static void hd_text(int32_t x, int32_t y, const felucca_font_t *f, const char *s, uint16_t c)
{
    const hd_font_t *h = hd_face(f);
    int32_t X = x * HDS, Y0 = (y + cv_oy) * HDS + ((int32_t)f->h * HDS - (int32_t)h->h) / 2;
    for (; *s; s++) {
        uint32_t ch = (uint8_t)*s, gi, w, bpr, gx, gy;
        if (ch < h->first || ch > h->last)
            ch = ch >= 'a' && ch <= 'z' && 'Z' <= h->last ? ch - 32u : '?';
        gi = ch - h->first;
        w = h->bw[gi];
        bpr = (w + 1u) / 2u;
        for (gy = 0; gy < h->h; gy++)
            for (gx = 0; gx < w; gx++) {
                uint32_t a = h->data[h->off[gi] + gy * bpr + gx / 2u];
                a = (gx & 1u) ? (a & 15u) : (a >> 4);
                if (a)
                    hd_put(X - HD_PAD + (int32_t)gx, Y0 + (int32_t)gy, c, a * 17u);
            }
        X += h->adv[gi];
    }
}

/* the shapes nullwave draws itself, at 4x */
static void hd_circle(int32_t cx2, int32_t cy2, float r, float t, uint16_t c)
{
    float cx = (float)cx2 * HDS * .5f, cy = ((float)cy2 * .5f + (float)cv_oy) * HDS, R = r * HDS, T = t * HDS;
    int32_t X, Y;
    for (Y = (int32_t)(cy - R - 3); Y <= (int32_t)(cy + R + 3); Y++)
        for (X = (int32_t)(cx - R - 3); X <= (int32_t)(cx + R + 3); X++) {
            float dx = (float)X + .5f - cx, dy = (float)Y + .5f - cy, d = sqrtf(dx * dx + dy * dy), a = R + .5f - d;
            if (T > 0 && d < R - T + .5f)
                a = d - (R - T) + .5f;
            if (a > 0)
                hd_put(X, Y, c, a >= 1 ? 255u : (uint32_t)(a * 255.f));
        }
}

static void hd_line(float x0, float y0, float x1, float y1, float w, uint16_t c)   /* 1x coordinates, w half-width */
{
    float X0 = x0 * HDS, Y0 = (y0 + (float)cv_oy) * HDS, X1 = x1 * HDS, Y1 = (y1 + (float)cv_oy) * HDS, W = w * HDS;
    float dx = X1 - X0, dy = Y1 - Y0, l2 = dx * dx + dy * dy;
    int32_t X, Y, xa = (int32_t)fminf(X0, X1) - (int32_t)W - 2, xb = (int32_t)fmaxf(X0, X1) + (int32_t)W + 2;
    int32_t ya = (int32_t)fminf(Y0, Y1) - (int32_t)W - 2, yb = (int32_t)fmaxf(Y0, Y1) + (int32_t)W + 2;
    for (Y = ya; Y <= yb; Y++)
        for (X = xa; X <= xb; X++) {
            float px = (float)X + .5f - X0, py = (float)Y + .5f - Y0, t = l2 > 0 ? (px * dx + py * dy) / l2 : 0, d, a;
            t = t < 0 ? 0 : t > 1 ? 1 : t;
            px -= t * dx;
            py -= t * dy;
            d = sqrtf(px * px + py * py);
            a = W + .5f - d;
            if (a > 0)
                hd_put(X, Y, c, a >= 1 ? 255u : (uint32_t)(a * 255.f));
        }
}

static void hd_rrect(int32_t x, int32_t y, int32_t w, int32_t h, int32_t r, uint16_t c)
{
    float X0 = (float)(x * HDS), Y0 = (float)((y + cv_oy) * HDS), X1 = X0 + (float)(w * HDS), Y1 = Y0 + (float)(h * HDS);
    float R = (float)(r * HDS) * .85f;
    int32_t X, Y;
    for (Y = (int32_t)Y0; Y < (int32_t)Y1; Y++)
        for (X = (int32_t)X0; X < (int32_t)X1; X++) {
            float px = (float)X + .5f, py = (float)Y + .5f;
            float qx = px < X0 + R ? X0 + R - px : px > X1 - R ? px - (X1 - R) : 0;
            float qy = py < Y0 + R ? Y0 + R - py : py > Y1 - R ? py - (Y1 - R) : 0;
            float a = qx > 0 && qy > 0 ? R + .5f - sqrtf(qx * qx + qy * qy) : 1.0f;
            if (a > 0)
                hd_put(X, Y, c, a >= 1 ? 255u : (uint32_t)(a * 255.f));
        }
}

static void hd_knob(int32_t cx, int32_t cy, uint32_t v, uint16_t lit, uint16_t off, float rad)
{
    /* a ring of radius rad (1x) over 270 degrees from the bottom left, lit to v / 255 */
    float CX = (float)cx * HDS, CY = ((float)cy + (float)cv_oy) * HDS, R = rad * HDS, T = 2.2f * HDS;
    int32_t X, Y;
    for (Y = (int32_t)(CY - R - 3); Y <= (int32_t)(CY + R + 3); Y++)
        for (X = (int32_t)(CX - R - 3); X <= (int32_t)(CX + R + 3); X++) {
            float dx = (float)X + .5f - CX, dy = (float)Y + .5f - CY, d = sqrtf(dx * dx + dy * dy);
            float a = fminf(R + .5f - d, d - (R - T) + .5f), ang = atan2f(dx, -dy) * 57.29578f;   /* 0 up, clockwise */
            float pos = ang + 135.0f;    /* 0 .. 270 over the ring's sweep */
            if (pos < 0)
                pos += 360.0f;
            if (a <= 0 || pos > 270.0f)
                continue;
            hd_put(X, Y, v && pos <= 270.0f * (float)v / 255.0f ? lit : off, a >= 1 ? 255u : (uint32_t)(a * 255.f));
        }
}

static void hd_tri(float ax, float ay, float bx, float by, float cx, float cy, uint16_t c)   /* 1x coordinates */
{
    float P[3][2] = {{ax * HDS, (ay + (float)cv_oy) * HDS}, {bx * HDS, (by + (float)cv_oy) * HDS},
                     {cx * HDS, (cy + (float)cv_oy) * HDS}};
    int32_t X, Y, k;
    for (Y = (int32_t)fminf(P[0][1], fminf(P[1][1], P[2][1])) - 1; Y <= (int32_t)fmaxf(P[0][1], fmaxf(P[1][1], P[2][1])) + 1; Y++)
        for (X = (int32_t)fminf(P[0][0], fminf(P[1][0], P[2][0])) - 1; X <= (int32_t)fmaxf(P[0][0], fmaxf(P[1][0], P[2][0])) + 1; X++) {
            float a = 1.0f;
            for (k = 0; k < 3; k++) {    /* the distance inside each edge (clockwise), the least of them the coverage */
                float x0 = P[k][0], y0 = P[k][1], x1 = P[(k + 1) % 3][0], y1 = P[(k + 1) % 3][1];
                float ex = x1 - x0, ey = y1 - y0, l = sqrtf(ex * ex + ey * ey);
                float d = (((float)X + .5f - x0) * ey - ((float)Y + .5f - y0) * ex) / l;
                a = fminf(a, -d + .5f);
            }
            if (a > 0)
                hd_put(X, Y, c, a >= 1 ? 255u : (uint32_t)(a * 255.f));
        }
}

static int hd_save(const char *name)     /* "expect hd:NAME 1" in a script: $NW_HD_DIR/NAME@4x.rgb */
{
    char path[4300];
    FILE *f;
    uint32_t i, n = 240u * HDS * 240u * HDS;
    const char *dir = getenv("NW_HD_DIR");
    snprintf(path, sizeof path, "%s/%s@4x.rgb", dir ? dir : ".", name);
    if (!(f = fopen(path, "wb")))
        return 0;
    for (i = 0; i < n; i++) {
        uint16_t c = hd_fb[i];
        uint8_t rgb[3] = {(uint8_t)((c >> 11) * 255u / 31u), (uint8_t)(((c >> 5) & 63u) * 255u / 63u),
                          (uint8_t)((c & 31u) * 255u / 31u)};
        fwrite(rgb, 1, 3, f);
    }
    fclose(f);
    return 1;
}

/* the canvas calls of ui_draw.c and ui.c, each done at 4x too */
static void hd_cv_begin(uint32_t w, uint32_t h, uint16_t bg) { cv_begin(w, h, bg); hd_begin(w, h, bg); }
static void hd_cv_rect(int32_t x, int32_t y, int32_t w, int32_t h, uint16_t c)
{
    cv_rect(x, y, w, h, c);
    if (!hd_quiet)
        hd_fill(x, y, w, h, c);
}
static void hd_cv_pset(int32_t x, int32_t y, uint16_t c)
{
    cv_pset(x, y, c);
    if (!hd_quiet)
        hd_fill(x, y, 1, 1, c);
}
static int32_t hd_cv_text(int32_t x, int32_t y, const felucca_font_t *f, const char *s, uint16_t c)
{
    if (!hd_quiet)
        hd_text(x, y, f, s, c);
    return cv_text(x, y, f, s, c);
}
static void hd_cv_blit(uint32_t x, uint32_t y) { cv_blit(x, y); hd_blit(x, y); }
#define cv_begin hd_cv_begin
#define cv_rect hd_cv_rect
#define cv_pset hd_cv_pset
#define cv_text hd_cv_text
#define cv_blit hd_cv_blit
#endif
