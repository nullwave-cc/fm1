/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 nullwave contributors */
/* PLAY (in ui.c's unit): the selected channel's sound, drawn as its wave. Nothing figurative: the channel's own output
 * (the graph view's scope, already kept: no work per sample), a smooth line with a soft glow and the last frames
 * fading behind it, still on a rising zero crossing, as tall as it is loud; in the theme's ink on its background,
 * greys for the trails, red only for the newest note (a point on the line where its pitch falls). The modulation
 * shows: the LFO moves the line up and down in its own shape and rate, the pitch's moves lift it.
 *
 * It draws the 240 x 112 between PLAY's header line and the names under it, in two bands of 72 (anim_step once a frame
 * first: the frame's sound, then anim_draw for each band), at most 25 times a second while something sounds (12 at
 * rest). State: the wave's last four frames (4 x 97 bytes). */

#define AN_W 240
#define AN_H (PLAY_ANIM - 32)            /* (the chord's name under it: its own 32) */
#define AN_NP 97                         /* a line's points, 2.5 px apart */
#define AN_NH 4                          /* the frames kept */

static uint16_t mix565(uint16_t a, uint16_t b, uint32_t t)   /* a to b, t 0..255 */
{
    uint32_t r = ((a >> 11) * (255u - t) + (b >> 11) * t) / 255u;
    uint32_t g = (((a >> 5) & 63u) * (255u - t) + ((b >> 5) & 63u) * t) / 255u;
    uint32_t bl = ((a & 31u) * (255u - t) + (b & 31u) * t) / 255u;
    return (uint16_t)(r << 11 | g << 5 | bl);
}

static struct {
    uint8_t ch1;                         /* the channel drawn + 1 (0: none yet) */
    int8_t hist[AN_NH][AN_NP];           /* the wave's last frames, newest first, -127..127 */
    uint32_t hist_t;                     /* when the newest went in */
    float gain;                          /* the wave's scale, following its loudness */
    float lvl, lx, pit;                  /* the frame's: how loud, the LFO (-1..1), the pitch's move */
    float t;                             /* seconds */
    int32_t newest_x;                    /* the newest note's place (its pitch), -1 none */
} an;

static int an_rows(float y0, float y1)   /* does y0..y1 touch the band being drawn */
{
    return y1 >= (float)(-cv_oy - 3) && y0 <= (float)(-cv_oy + cv_y1 + 3);
}

static void an_dot(float x, float y, float r, uint16_t c)
{
    if (x > -4.0f && x < AN_W + 4.0f && an_rows(y - r, y + r))
        circle((int32_t)(2.0f * x), (int32_t)(2.0f * y), r, 0.0f, c);
}

static float an_lfo(uint32_t ch)         /* the LFO now, in its own shape, -1..1 by its depth (0: it reaches nothing) */
{
    const sound_t *so = synth_sound(ch);
    float dep, ph = synth_lfo_now(ch, &dep), lx;
    uint32_t k;
    int used = so->lfo_pit || so->lfo_flt || so->lfo_shp || so->lfo_amp;
    for (k = 0; k < SYN_MODS; k++)
        used |= synth_mod(ch)->m[k].src == MS_LFO && synth_mod(ch)->m[k].amt;
    if (!used)
        return 0.0f;
    switch (so->lfo_wave) {
    case 1: lx = ph < 0.5f ? ph * 4.0f - 1.0f : 3.0f - ph * 4.0f; break;
    case 2: lx = ph * 2.0f - 1.0f; break;
    case 3: lx = ph < 0.5f ? 1.0f : -1.0f; break;
    case 4: case 5: lx = fm_sin_turns(ph); break;   /* (S&H, RND: drawn as a smooth swing; jumps read as noise) */
    default: lx = fm_sin_turns(ph); break;
    }
    return lx * dep;
}

/* once a frame: the sound now, the wave's newest frame */
static void anim_step(uint32_t ch, uint32_t now)
{
    synth_vview_t vv[SYN_VMAX];
    int16_t b[192];
    uint32_t n, i, k, nv, newest = 0;
    int32_t pk = 0;
    float lvl = 0.0f;
    if (an.ch1 != ch + 1u) {             /* another channel: a clean start */
        memset(&an, 0, sizeof an);
        an.ch1 = (uint8_t)(ch + 1u);
        an.gain = 1.0f / 6000.0f;
    }
    an.t = (float)(now % 600000u) / 1000.0f;
    an.lx = an_lfo(ch);
    an.pit = fm_clampf((float)synth_mod_now(ch, MT_PIT) / 32768.0f + an.lx * (float)synth_sound(ch)->lfo_pit / 63.0f,
                       -1.0f, 1.0f);
    nv = synth_voice_view(ch, vv);       /* the voices: how loud, the newest */
    an.newest_x = -1;
    for (i = 0; i < nv; i++) {
        float x = 20.0f + (float)clamp((int32_t)vv[i].note - 24, 0, 72) * (AN_W - 40.0f) / 72.0f;
        if (synth_is_kit(ch))            /* (a kit: each drum its own place, kick at the left) */
            x = 24.0f + (float)(synth_drum_lane(vv[i].note) & 7) * (AN_W - 48.0f) / 7.0f;
        lvl += (float)vv[i].amp * (float)(40 + vv[i].vel) / (255.0f * 167.0f);
        if (vv[i].start >= newest) {
            newest = vv[i].start;
            an.newest_x = (int32_t)x;
        }
    }
    an.lvl = fm_minf(1.0f, lvl);
    /* the wave: the scope (a rising zero crossing first), as 97 points, its scale following its loudness */
    n = synth_scope(b, nv ? 192u : 0u);   /* (~9 ms: a cycle or two of a middle note) */
    for (i = 0; i < n; i++)
        pk = b[i] > pk ? b[i] : -b[i] > pk ? -b[i] : pk;
    {
        float want = pk > 600 ? 0.9f / (float)pk * fm_minf(1.0f, 0.35f + (float)pk / 9000.0f) : an.gain;
        an.gain += (want - an.gain) * (want < an.gain ? 0.6f : 0.15f);   /* (louder: at once; quieter: slowly) */
    }
    if (now - an.hist_t >= 40u || !an.hist_t) {   /* (a new frame every 40 ms: the trails move at one pace) */
        an.hist_t = now;
        for (k = AN_NH - 1u; k > 0u; k--)   /* (the frames down one: by hand, the device has no memmove) */
            memcpy(an.hist[k], an.hist[k - 1u], sizeof an.hist[0]);
        for (i = 0; i < AN_NP; i++) {
            float v = 0.0f;
            if (n > 1u) {
                float f = (float)i * (float)(n - 1u) / (float)(AN_NP - 1u);
                uint32_t j = (uint32_t)f;
                float fr = f - (float)j;
                v = ((float)b[j] * (1.0f - fr) + (float)b[j + 1u < n ? j + 1u : j] * fr) * an.gain;
            }
            an.hist[0][i] = (int8_t)fm_clampf(v * 127.0f, -127.0f, 127.0f);
        }
        for (k = 0; k < 2u; k++) {       /* (smoothed twice, 1-2-1: a flowing line, not the samples' grit) */
            int32_t prev = an.hist[0][0];
            for (i = 1; i + 1u < AN_NP; i++) {
                int32_t cur = an.hist[0][i];
                an.hist[0][i] = (int8_t)((prev + 2 * cur + an.hist[0][i + 1u]) / 4);
                prev = cur;
            }
        }
    }
}

static float an_y(int8_t v, float x, float amp)   /* a point's height, its line's ends calm (a window over the width) */
{
    float w = fm_sin_turns(x / (AN_W * 2.0f));
    return (float)v / 127.0f * amp * (0.25f + 0.75f * w);
}

static void an_wave(void)                /* WAVE */
{
    float mid = AN_H / 2.0f - 2.0f * an.pit * 6.0f + 4.0f * an.lx, amp = AN_H * 0.42f;
    int32_t f, i;
    for (f = 4; f >= 0; f--) {           /* (the oldest first, faintest: the newest over them; 4: the newest's glow) */
        int32_t fr = f == 4 ? 0 : f;
        uint16_t c = f == 4 ? mix565(TC(BG), TC(SUB), (uint32_t)(40 + 60.0f * an.lvl))
                   : f ? mix565(TC(BG), TC(SUB), (uint32_t)(190 - f * 38)) : TC(FG);
        float hw = f == 4 ? 2.2f + 1.0f * an.lvl : f ? 0.5f : 0.9f + 0.4f * an.lvl, py = 0.0f, px = 0.0f;
        if (f == 4 && an.lvl < 0.02f)
            continue;
        for (i = 0; i < AN_NP; i++) {    /* (the older frames where they were, fainter: a glow of where it has been) */
            float x = (float)i * 2.5f, y = mid - an_y(an.hist[fr][i], x, amp)
                                       + (fr ? 0.0f : 0.9f * fm_sin_turns(an.t * 0.25f + x / 160.0f) * (1.0f - an.lvl));
            if (i && an_rows(fm_minf(py, y) - 2.0f, fm_maxf(py, y) + 2.0f) && x < AN_W)
                aa_line(px, py, x, y, hw, c);
            px = x;
            py = y;
        }
    }
    if (an.newest_x >= 0) {              /* the newest note: a red point on the line where its pitch falls */
        int32_t i0 = an.newest_x * (AN_NP - 1) / AN_W;
        an_dot((float)an.newest_x, mid - an_y(an.hist[0][i0], (float)an.newest_x, amp), 2.2f + 1.2f * an.lvl, TC(REC));
    }
}

static void anim_draw(uint32_t ch, uint32_t now)
{
    (void)ch;
    (void)now;
    an_wave();
}

static uint32_t anim_busy(uint32_t ch) { return synth_voices(ch) != 0u; }
