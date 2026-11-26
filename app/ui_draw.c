/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 nullwave contributors */
/* nullwave's drawing (in app.c's unit, after gfx.c): the theme's colours, rounded boxes, circles, the knobs' rings,
 * text by its baseline, the battery, the boot splash. */
/* ----------------------------------------------------------------- theme --- */
/* nullwave.cc's colours, dark and light (GLO DISPLAY); 0xRRGGBB */
enum { T_BG, T_FG, T_SUB, T_DIM, T_LINE, T_PANEL, T_MUTE, T_REC, T_BAT3, T_BAT2, T_BAT1, T_BOLT, T_N };
static const uint32_t THEME_RGB[2][T_N] = {
    {0x000000, 0xFFFFFF, 0x8A8A8A, 0x555555, 0x333333, 0x222222, 0x8A8A8A, 0xE5202E, 0x22C55E, 0xF59E0B, 0xE5202E,
     0xFACC15},
    {0xFFFFFF, 0x000000, 0x5C5C5C, 0xB0B0B0, 0xD0D0D0, 0xE8E8E8, 0x767676, 0xE5202E, 0x16A34A, 0xD97706, 0xE5202E,
     0xEAB308},
};
static const uint32_t CH_RGB[2][NCH] = {   /* red, ember, amber, rose, steel, sage */
    {0xE5202E, 0xF07A40, 0xD9A25F, 0xDE708C, 0x7E9CB8, 0x8FAE8B},
    {0xCE1826, 0xCE5822, 0xA87028, 0xBA4268, 0x3E6286, 0x4E764C},
};
static struct {
    uint16_t c[T_N];
    uint16_t ch[NCH], ch_mute[NCH], ch_fill[NCH], ch_line[NCH];   /* a channel's, muted, its row's fill and line */
} th;
#define TC(i) th.c[T_##i]

static uint16_t rgb565(uint32_t c) { return RGB(c >> 16 & 0xFFu, c >> 8 & 0xFFu, c & 0xFFu); }
static uint32_t rgb_mix(uint32_t a, uint32_t b, uint32_t pct)   /* pct percent of the way from a to b */
{
    uint32_t r = 0, s;
    for (s = 0; s < 24u; s += 8u)
        r |= (((a >> s & 0xFFu) * (100u - pct) + (b >> s & 0xFFu) * pct + 50u) / 100u) << s;
    return r;
}

static void theme_set(uint32_t light)
{
    uint32_t i, bg = THEME_RGB[light][T_BG];
    for (i = 0; i < T_N; i++)
        th.c[i] = rgb565(THEME_RGB[light][i]);
    for (i = 0; i < NCH; i++) {
        uint32_t c = CH_RGB[light][i];
        th.ch[i] = rgb565(c);
        th.ch_mute[i] = rgb565(rgb_mix(c, bg, 60));
        th.ch_fill[i] = rgb565(rgb_mix(bg, c, 13));
        th.ch_line[i] = rgb565(rgb_mix(bg, c, 75));
    }
}

/* ------------------------------------------------------------------- draw --- */
/* a pixel of colour c over the canvas at coverage a (0..15) */
static void px_blend(int32_t x, int32_t y, uint16_t c, uint32_t a)
{
    uint16_t *p;
    uint32_t bg;
    y += cv_oy;
    if (!a || (uint32_t)x >= cv_w || y < cv_y0 || y >= cv_y1)
        return;
    p = &cv_px[(uint32_t)y * cv_w + (uint32_t)x];
    if (a >= 15u) {
        *p = swap16(c);
        return;
    }
    bg = swap16(*p);
    *p = swap16((((c >> 11) * a + (bg >> 11) * (15u - a)) / 15u) << 11
                | ((((c >> 5) & 63u) * a + ((bg >> 5) & 63u) * (15u - a)) / 15u) << 5
                | (((c & 31u) * a + (bg & 31u) * (15u - a)) / 15u));
}

/* a filled rectangle with corners of radius r (0..4), each corner's pixels left out as a circle would */
static void rrect(int32_t x, int32_t y, int32_t w, int32_t h, int32_t r, uint16_t c)
{
    static const uint8_t IN[5][4] = {{0}, {1}, {1, 0}, {1, 0, 0}, {2, 1, 0, 0}};
    int32_t j;
    r = clamp(r, 0, 4);
    for (j = 0; j < h; j++) {
        int32_t k = j < r ? IN[r][j] : h - 1 - j < r ? IN[r][h - 1 - j] : 0;
        cv_rect(x + k, y + j, w - 2 * k, 1, c);
    }
}

static void rrect_line(int32_t x, int32_t y, int32_t w, int32_t h, int32_t r, uint16_t line, uint16_t fill)
{
    rrect(x, y, w, h, r, line);
    rrect(x + 1, y + 1, w - 2, h - 2, r - 1, fill);
}

/* a circle around (cx, cy) in half pixels: radius r, filled (t 0) or a ring t pixels thick, edges smoothed */
static void circle(int32_t cx2, int32_t cy2, float r, float t, uint16_t c)
{
    int32_t x, y, x0 = (cx2 - (int32_t)(2 * r) - 2) / 2, x1 = (cx2 + (int32_t)(2 * r) + 2) / 2;
    int32_t y0 = (cy2 - (int32_t)(2 * r) - 2) / 2, y1 = (cy2 + (int32_t)(2 * r) + 2) / 2;
    for (y = y0; y <= y1; y++)
        for (x = x0; x <= x1; x++) {
            float dx = (float)(2 * x + 1 - cx2) * .5f, dy = (float)(2 * y + 1 - cy2) * .5f;
            float d = fm_sqrtf(dx * dx + dy * dy), a = r + .5f - d;
            if (t > 0 && d < r - t + .5f)
                a = d - (r - t) + .5f;
            if (a > 0)
                px_blend(x, y, c, a >= 1 ? 15u : (uint32_t)(a * 15.f + .5f));
        }
}

/* a knob: the ring (art.h) around (cx, cy), lit in colour c over 270 degrees * v / 255 */
static void knob(int32_t cx, int32_t cy, uint32_t v, uint16_t c)
{
    int32_t x, y, o = RING_N / 2;
    for (y = 0; y < RING_N; y++)
        for (x = 0; x < RING_N; x++) {
            uint32_t i = (uint32_t)(y * RING_N + x);
            px_blend(cx - o + x, cy - o + y, v && RING_POS[i] <= v ? c : TC(PANEL), RING_COV[i]);
        }
}

/* a face's capitals: their top row and the baseline, from its 'H' */
static void font_caps(const felucca_font_t *f, int32_t *top, int32_t *base)
{
#if defined(FONT_TRIM) || defined(FONT_PACK)
    *top = (int32_t)f->h / 4;
    *base = (int32_t)f->h * 3 / 4;
#else
    uint32_t gi = glyph(f, 'H'), w = f->bw[gi], bpr = (w + 1u) / 2u, gy, i;
    const uint8_t *gd = f->data + f->off[gi];
    *top = -1;
    *base = (int32_t)f->h;
    for (gy = 0; gy < f->h; gy++)
        for (i = 0; i < bpr; i++)
            if (gd[gy * bpr + i]) {
                if (*top < 0)
                    *top = (int32_t)gy;
                *base = (int32_t)gy + 1;
            }
    if (*top < 0)
        *top = 0;
#endif
}
static int32_t at_base(const felucca_font_t *f, int32_t base)   /* the y to draw at for this baseline */
{
    int32_t t, b;
    font_caps(f, &t, &b);
    return base - b;
}
static int32_t at_mid(const felucca_font_t *f, int32_t mid)     /* the y that centres the capitals on mid */
{
    int32_t t, b;
    font_caps(f, &t, &b);
    return mid - (t + b) / 2;
}

/* text cut to w pixels */
static int32_t text_fit(int32_t x, int32_t y, const felucca_font_t *f, const char *s, uint16_t c, int32_t w)
{
    char t[32];
    uint32_t n = 0;
    while (s[n] && n < sizeof t - 1u) {
        t[n] = s[n];
        t[n + 1] = 0;
        if (text_w(f, t) > w)
            break;
        n++;
    }
    t[n] = 0;
    return cv_text(x, y, f, t, c);
}

static uint32_t sig_add(uint32_t h, uint32_t v) { return (h ^ v) * 16777619u; }
static uint32_t sig_str(uint32_t h, const char *s)
{
    while (*s)
        h = sig_add(h, (uint8_t)*s++);
    return sig_add(h, 0);
}

/* the battery: bars by charge, a bolt while charging, USB in place of a charge when a computer powers the FM-1 */
static int battery_state(void)
{
    int b = plat_battery();
#ifdef APP_BATTERY_RAW
    if (b >= 0 && b < 4 && plat_battery_charging() == 2)
        b = 5;
#endif
    return b;
}

static void draw_battery(int b)
{
    static const char BOLT[11][8] = {"....##.", "...##..", "..##...", ".##....", "######.", ".######",
                                     "....##.", "...##..", "..##...", ".##....", "##....."};
    int32_t i, j, bars = b == 4 ? 3 : b == 5 ? 2 : b;
    uint16_t c = b == 4 ? TC(FG) : bars >= 3 ? TC(BAT3) : bars == 2 ? TC(BAT2) : TC(BAT1);
    if (b < 0)
        return;
    if (b == 4)
        cv_text(207 - text_w(&FONT_XS, "USB"), at_mid(&FONT_XS, 11), &FONT_XS, "USB", TC(SUB));
    rrect_line(211, 6, 20, 11, 2, b == 4 ? TC(FG) : TC(SUB), TC(BG));
    cv_rect(231, 9, 2, 5, b == 4 ? TC(FG) : TC(SUB));
    for (i = 0; i < bars; i++)
        cv_rect(213 + 6 * i, 8, 5, 7, c);
    if (b == 5) {                            /* the bolt, ringed in the background's colour */
        for (j = 0; j < 11; j++)
            for (i = 0; i < 7; i++)
                if (BOLT[j][i] == '#') {
                    cv_rect(217 + i - 1, 6 + j, 3, 1, TC(BG));
                    if (j)
                        cv_rect(217 + i, 6 + j - 1, 1, 1, TC(BG));
                    if (j < 10)
                        cv_rect(217 + i, 6 + j + 1, 1, 1, TC(BG));
                }
        for (j = 0; j < 11; j++)
            for (i = 0; i < 7; i++)
                if (BOLT[j][i] == '#')
                    cv_rect(217 + i, 6 + j, 1, 1, TC(BOLT));
    }
}

#ifdef APP_SPLASH
/* the boot screen: the nullwave mark and wordmark (art.h, from the logotypes), the version under them */
void app_splash(void)
{
    uint32_t i = 0, n = 0, w;
    lcd_fill(0, 0, 240, 240, C_BLACK);
    cv_begin(SPLASH_W, SPLASH_H, C_BLACK);
    while (n < SPLASH_W * SPLASH_H && i < sizeof SPLASH_RLE) {
        uint32_t b = SPLASH_RLE[i++], r = (b & 15u) + 1u;
        uint16_t c = swap16(SPLASH_PAL[b >> 4]);
        while (r-- && n < SPLASH_W * SPLASH_H)
            cv_px[n++] = c;
    }
    cv_blit(SPLASH_X, SPLASH_Y);
    w = (uint32_t)text_w(&FONT_XS, APP_VERSION);
    cv_begin(240, FONT_XS.h, C_BLACK);
    cv_text((int32_t)(240u - w) / 2, 0, &FONT_XS, APP_VERSION, APP_SPLASH_RGB);
    cv_blit(0, 214);
}
#endif

