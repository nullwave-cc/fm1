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
#ifdef NW_HD
    if (!hd_quiet)
        hd_blend1(x, y, c, a);           /* (a shape with no 4x drawing of its own: its pixels, 4x as large) */
#endif
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
#ifdef NW_HD
    hd_rrect(x, y, w, h, clamp(r, 0, 4), c);
    hd_quiet++;
#endif
    r = clamp(r, 0, 4);
    for (j = 0; j < h; j++) {
        int32_t k = j < r ? IN[r][j] : h - 1 - j < r ? IN[r][h - 1 - j] : 0;
        cv_rect(x + k, y + j, w - 2 * k, 1, c);
    }
#ifdef NW_HD
    hd_quiet--;
#endif
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
#ifdef NW_HD
    hd_circle(cx2, cy2, r, t, c);
    hd_quiet++;
#endif
    for (y = y0; y <= y1; y++)
        for (x = x0; x <= x1; x++) {
            float dx = (float)(2 * x + 1 - cx2) * .5f, dy = (float)(2 * y + 1 - cy2) * .5f;
            float d = fm_sqrtf(dx * dx + dy * dy), a = r + .5f - d;
            if (t > 0 && d < r - t + .5f)
                a = d - (r - t) + .5f;
            if (a > 0)
                px_blend(x, y, c, a >= 1 ? 15u : (uint32_t)(a * 15.f + .5f));
        }
#ifdef NW_HD
    hd_quiet--;
#endif
}

/* a knob: the ring (art.h) around (cx, cy), lit in colour c over 270 degrees * v / 255 */
static void knob(int32_t cx, int32_t cy, uint32_t v, uint16_t c)
{
    int32_t x, y, o = RING_N / 2;
#ifdef NW_HD
    hd_knob(cx, cy, v, c, TC(PANEL), (float)RING_N / 2.0f - 1.0f);
    hd_quiet++;
#endif
    for (y = 0; y < RING_N; y++)
        for (x = 0; x < RING_N; x++) {
            uint32_t i = (uint32_t)(y * RING_N + x);
            px_blend(cx - o + x, cy - o + y, v && RING_POS[i] <= v ? c : TC(PANEL), RING_COV[i]);
        }
#ifdef NW_HD
    hd_quiet--;
#endif
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
#ifdef PLAT_HOST
    if (getenv("NW_BATTERY"))            /* (the host: a battery state to draw, 4 on USB power, for the pictures) */
        b = atoi(getenv("NW_BATTERY"));
#endif
#ifdef APP_BATTERY_RAW
    if (b >= 0 && b < 4 && plat_battery_charging() == 2)
        b = 5;
#endif
    return b;
}

/* the battery as a percentage (SETTINGS SYSTEM): from the reading plat_battery's bars come from (the converter's counts,
 * the bars' steps at 531 / 561 / 591, full ~620), through a Li-ion cell's curve (flat in the middle, steep at both
 * ends), smoothed over ~20 s and shown in steps of 5 that move only when the estimate has gone 3 past them, so it
 * never jumps. It is an estimate: no fuel gauge, the curve not measured on an FM-1, the cell's voltage sagging under
 * load: within ~15 %. On USB (5 V present) the reading is the charger's: USB or CHARGING instead */
static int32_t batt_pct(void)
{
    static const int16_t RAW[] = {500, 520, 531, 546, 561, 576, 591, 606, 620};   /* the curve's points: raw counts */
    static const int8_t PCT[] = {0, 5, 10, 25, 40, 58, 75, 90, 100};               /* their percentages */
    static float est = -1.0f;
    static int32_t shown = -1;
    int32_t raw = plat_battery_raw(), i = 0;
    float pc;
    int b = plat_battery();
    if (b == 4 || raw <= 0)
        return -1;
#ifdef APP_BATTERY_RAW
    if (plat_battery_charging() > 0)
        return -1;
#endif
    while (i < 7 && raw > RAW[i + 1])
        i++;
    pc = raw <= RAW[0] ? 0.0f : raw >= RAW[8] ? 100.0f
       : (float)PCT[i] + (float)(PCT[i + 1] - PCT[i]) * (float)(raw - RAW[i]) / (float)(RAW[i + 1] - RAW[i]);
    est = est < 0.0f ? pc : est + (pc - est) * 0.002f;   /* (a frame's share: ~20 s at 25 frames a second) */
    if (shown < 0 || est > (float)shown + 5.0f + 3.0f || est < (float)shown - 3.0f)
        shown = (int32_t)(est / 5.0f + 0.5f) * 5;
    return shown;
}
static void draw_battery(int b)
{
    static const char BOLT[11][8] = {"....##.", "...##..", "..##...", ".##....", "######.", ".######",
                                     "....##.", "...##..", "..##...", ".##....", "##....."};
    int32_t i, j, bars = b == 4 ? 3 : b == 5 ? 2 : b;
    uint16_t c = b == 4 ? TC(FG) : bars >= 3 ? TC(BAT3) : bars == 2 ? TC(BAT2) : TC(BAT1);
    if (b < 0)
        return;
    rrect_line(211, 6, 20, 11, 2, b == 4 ? TC(FG) : TC(SUB), TC(BG));
    cv_rect(231, 9, 2, 5, b == 4 ? TC(FG) : TC(SUB));
    if (b == 4) {                            /* on USB: a plug in the battery, no words */
        cv_rect(214, 9, 3, 1, TC(FG));       /* (its two pins) */
        cv_rect(214, 13, 3, 1, TC(FG));
        rrect(217, 8, 6, 7, 1, TC(FG));      /* (its body) */
        cv_rect(223, 11, 4, 1, TC(FG));      /* (its lead) */
        return;
    }
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

/* -------------------------------------------------------------- the logo --- */
/* the logotypes repository's FM-1 splash (art.h): the ring, the disc, the slash, "null", "wave"; drawn in the
 * theme's colours (dark: the ring #3A3A3A, the disc and "null" white; light: the ring #C8C8C8, they black; the slash
 * and "wave" red in both), a shape at a time, faded (alpha 0..16), moved (dx), the ring swept in from the top
 * clockwise (sweep 0..4096 of a turn, 4096 all of it) */
static const struct { int16_t x, y, w, h; const uint8_t *rle; uint16_t n; } LOGO[5] = {
    {LOGO_RING_X, LOGO_RING_Y, LOGO_RING_W, LOGO_RING_H, LOGO_RING, sizeof LOGO_RING},
    {LOGO_DISC_X, LOGO_DISC_Y, LOGO_DISC_W, LOGO_DISC_H, LOGO_DISC, sizeof LOGO_DISC},
    {LOGO_SLASH_X, LOGO_SLASH_Y, LOGO_SLASH_W, LOGO_SLASH_H, LOGO_SLASH, sizeof LOGO_SLASH},
    {LOGO_NULL_X, LOGO_NULL_Y, LOGO_NULL_W, LOGO_NULL_H, LOGO_NULL, sizeof LOGO_NULL},
    {LOGO_WAVE_X, LOGO_WAVE_Y, LOGO_WAVE_W, LOGO_WAVE_H, LOGO_WAVE, sizeof LOGO_WAVE},
};
static uint16_t logo_colour(uint32_t part, uint32_t light)
{
    static const uint32_t C[2][5] = {{0x3A3A3A, 0xFFFFFF, 0xE5202E, 0xFFFFFF, 0xE5202E},
                                     {0xC8C8C8, 0x000000, 0xE5202E, 0x000000, 0xE5202E}};
    return rgb565(C[light & 1u][part % 5u]);
}
static uint32_t turn_of(int32_t dx, int32_t dy)   /* (dx, dy)'s angle from straight up, clockwise, 0..4095 */
{
    float x = (float)dx, y = (float)-dy, ax = x < 0 ? -x : x, ay = y < 0 ? -y : y, a, t;
    if (ax < 1e-6f && ay < 1e-6f)
        return 0;
    t = ax < ay ? ax / ay : ay / ax;      /* atan on 0..1, a short polynomial, then the octant */
    a = t * (0.7853982f + 0.2730f * (1.0f - t));
    if (ax > ay)
        a = 1.5707963f - a;               /* a: from straight up towards the side, 0..pi/2 */
    if (x < 0 && y >= 0)
        a = 6.2831853f - a;
    else if (x < 0)
        a = 3.1415927f + a;
    else if (y < 0)
        a = 3.1415927f - a;
    return (uint32_t)(a / 6.2831853f * 4096.0f) & 4095u;
}
#ifdef NW_HD
/* the HD render: the shape from the svg at 4x (tools/gen_hd_font.py), its sweep and its fade as below */
static void hd_logo_part(uint32_t part, uint16_t c, uint32_t alpha, int32_t dx, uint32_t sweep, int32_t cx, int32_t cy)
{
    int32_t i, j, X0 = HD_LOGO[part].x + dx * HDS, Y0 = HD_LOGO[part].y + cv_oy * HDS;
    for (j = 0; j < HD_LOGO[part].h; j++)
        for (i = 0; i < HD_LOGO[part].w; i++) {
            uint32_t a = HD_LOGO[part].a[j * HD_LOGO[part].w + i];
            if (!a || (sweep < 4096u && turn_of(HD_LOGO[part].x + i - cx * HDS, HD_LOGO[part].y + j - cy * HDS) > sweep))
                continue;
            hd_put(X0 + i, Y0 + j, c, a * alpha / 16u);
        }
}
#endif
static void logo_part(uint32_t part, uint32_t light, uint32_t alpha, int32_t dx, uint32_t sweep)
{
    const uint8_t *r = LOGO[part].rle;
    uint32_t i = 0, n = 0, w = (uint32_t)LOGO[part].w, all = w * (uint32_t)LOGO[part].h;
    uint16_t c = logo_colour(part, light);
    int32_t cx = LOGO[0].x + LOGO[0].w / 2, cy = LOGO[0].y + LOGO[0].h / 2;
    if (!alpha)
        return;
#ifdef NW_HD
    hd_logo_part(part, c, alpha, dx, sweep, cx, cy);
    hd_quiet++;
#endif
    while (n < all && i < LOGO[part].n) {
        uint32_t b = r[i++], lv = b >> 4, run = (b & 15u) + 1u;
        if (!lv) {
            n += run;
            continue;
        }
        while (run-- && n < all) {
            int32_t x = LOGO[part].x + (int32_t)(n % w), y = LOGO[part].y + (int32_t)(n / w);
            n++;
            if (sweep < 4096u && turn_of(x - cx, y - cy) > sweep)
                continue;
            px_blend(x + dx, y, c, lv * alpha / 16u);
        }
    }
#ifdef NW_HD
    hd_quiet--;
#endif
}

/* the boot animation at time t (ms from the start): the ring draws itself in from the top, the disc comes up inside
 * it, then the slash, then "null" from the left and the red "wave" from the right, the version under them; the ring
 * is always there before anything else, as the logo's rule asks. Drawn whole (two bands), in the theme */
#define LOGO_MS 1300u
static uint32_t ramp(uint32_t t, uint32_t t0, uint32_t len)   /* 0..16 between t0 and t0 + len */
{
    return t <= t0 ? 0u : t >= t0 + len ? 16u : (t - t0) * 16u / len;
}
static const char *logo_cap, *logo_cap2;   /* the words under the logo instead of the version (0: the version) */
static uint32_t logo_ring = 4096u;       /* the ring drawn this far round (of 4096): a factory reset's progress */
static void logo_frame(uint32_t t, uint32_t light)
{
    uint32_t b;
    uint16_t bg = light ? rgb565(0xFFFFFF) : rgb565(0x000000);
    for (b = 0; b < 2u; b++) {           /* rows 0..143, 144..239 */
        uint32_t h = b ? 96u : 144u, sw = t >= 420u ? 4096u : t * 4096u / 420u;
        uint32_t an = ramp(t, 700u, 320u), aw = ramp(t, 820u, 320u);
        cv_begin(240, h, bg);
        cv_oy = -(int32_t)(144u * b);
        cv_y0 = 0;
        cv_y1 = (int32_t)h;
        logo_part(0, light, 16u, 0, sw < logo_ring ? sw : logo_ring);
        logo_part(1, light, ramp(t, 300u, 260u), 0, 4096u);
        logo_part(2, light, ramp(t, 520u, 220u), 0, 4096u);
        logo_part(3, light, an, -(int32_t)((16u - an) * 10u / 16u), 4096u);
        logo_part(4, light, aw, (int32_t)((16u - aw) * 10u / 16u), 4096u);
        if (t >= 1000u) {                /* the version, red as the logo files have it; or the install's words */
            const char *c1 = logo_cap ? logo_cap : fresh_build ? "INSTALLED" : APP_VERSION;
            const char *c2 = logo_cap ? logo_cap2 : fresh_build ? APP_VERSION : 0;
            int32_t w = text_w(&FONT_XS, c1);
            cv_text((240 - w) / 2, c2 ? 200 : 214, &FONT_XS, c1, rgb565(0xE5202E));
            if (c2) {
                w = text_w(&FONT_XS, c2);
                cv_text((240 - w) / 2, 218, &FONT_XS, c2, light ? rgb565(0x000000) : rgb565(0xFFFFFF));
            }
        }
        cv_oy = 0;
        cv_blit(0, 144u * b);
    }
}

/* an install begins: the whole logo, INSTALLING and what comes next under it (app_update_begin) */
static void update_screen(void)
{
    logo_cap = "INSTALLING";
    logo_cap2 = "THE FM-1 RESTARTS INTO ITS UPDATER";
    logo_frame(LOGO_MS, prefs.display == 1u);
    logo_cap = logo_cap2 = 0;
}

/* a factory reset under way: the ring fills as nullwave's storage is erased, the words under it */
static void factory_screen(uint32_t permille)
{
    static char pc[24];
    uint32_t k = 0, v = permille / 10u;
    const char *w = "ERASING ";
    while (*w)
        pc[k++] = *w++;
    if (v >= 100u)
        pc[k++] = '1';
    if (v >= 10u)
        pc[k++] = (char)('0' + v / 10u % 10u);
    pc[k++] = (char)('0' + v % 10u);
    pc[k++] = '%';
    pc[k] = 0;
    logo_cap = "FACTORY RESET";
    logo_cap2 = pc;
    logo_ring = 1u + permille * 4095u / 1000u;
    logo_frame(LOGO_MS, prefs.display == 1u);
    logo_cap = logo_cap2 = 0;
    logo_ring = 4096u;
}

#ifdef APP_SPLASH
/* the boot screen: the animation's first frame (the ring starting), in the theme the preferences hold if they are
 * there yet; app_frame plays the rest */
void app_splash(void)
{
    prefs_t pf;
    uint32_t light = plat_store_load(OBJ_SOUND, &pf, sizeof pf) == (int)sizeof pf && pf.magic == PREFS_MAGIC
                     && pf.display == 1u;
    lcd_fill(0, 0, 240, 240, light ? rgb565(0xFFFFFF) : C_BLACK);
    logo_frame(0, light);
}
#endif

