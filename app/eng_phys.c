/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 nullwave contributors */
/* PHYS: physical models, in the engine interface (felucca_engine.h), written for nullwave (the models, the
 * pages and knobs; none of its code; textbook models: Karplus-Strong strings, two-pole modal resonators).
 *   MODEL  MODAL (a struck bar or bell: four modes), STRNG (a plucked string), MEMB (a struck drum head: inharmonic
 *          modes and a falling pitch), SYMP (a string with sympathetic strings tuned to a chord)
 *   STRC   the structure: MODAL the modes' spread (harmonic .. bar .. bell), STRNG the stiffness, MEMB the head's
 *          harmonics, SYMP the chord the sympathetic strings make
 *   BRIT   brightness (the exciter and the string's loss filter)    DAMP  how fast it dies
 *   POS    where it is struck or plucked (SYMP: how much the sympathetic strings ring)
 *   ACC    the accent (the strike's strength)    BOW  a bowing: excitation that goes on (MEMB: the pitch's fall)
 *   EXC    the exciter: an impulse (0) .. noise (127)
 * Three voices a channel. A string's delay line lives in phys_line (2 KiB a voice, in the pool).
 * State: s[0..7] the four resonators (y1, y2) or, for a string, s[0] the line's write index, s[1] its length (Q8),
 * s[2] the loss filter, s[3] the exciter's low-pass; ph[0] the exciter's age (samples), ph[1] the noise, ph[2] the
 * modal exciter's low-pass. Floats kept as their bits. */
#define PHYS_LINE 1024u
static int16_t phys_line[NPART][3][PHYS_LINE] PLAT_POOL;
static int16_t phys_symp[NPART][2][PHYS_LINE] PLAT_POOL;   /* SYMP: two sympathetic strings a channel */
static uint32_t phys_symp_w[NPART][2];
static float phys_symp_lp[NPART][2];

static const char *const N_PHYS_MODEL[] = {"MODAL", "STRNG", "MEMB", "SYMP", 0};
enum { PM_MODAL, PM_STRNG, PM_MEMB, PM_SYMP };

static float phys_fs(void) { return (float)FS; }

static void phys_note_on(track_t *t, voice_t *v)
{
    uint32_t part = (uint32_t)(t - trk) % NPART, k = (uint32_t)(v - t->v) % 3u;
    memset(v->s, 0, sizeof v->s);
    v->ph[0] = 0;
    v->ph[1] = v->age * 2654435761u | 1u;
    v->ph[2] = 0;
    memset(phys_line[part][k], 0, sizeof phys_line[0][0]);
}

static inline float phys_noise(voice_t *v)
{
    int32_t st = (int32_t)v->ph[1];
    uint32_t r = noise32(&st);
    v->ph[1] = (uint32_t)st;
    return (float)((int32_t)(r >> 16) - 32768) / 32768.0f;
}

/* the exciter: a short burst, an impulse or noise (EXC), shaped by BRIT, ACC; BOW keeps a little going */
static inline float phys_exc(voice_t *v, float exc, float brit, float acc, float bow, float *lp)
{
    uint32_t age = v->ph[0]++;
    float burst = age < 120u ? 1.0f - (float)age / 120.0f : 0.0f, x;
    x = (age == 0u ? (1.0f - exc) * 4.0f : 0.0f) + phys_noise(v) * exc * burst;
    x += phys_noise(v) * bow * 0.06f;
    *lp += (x - *lp) * (0.08f + brit * 0.9f);
    return *lp * acc;
}

static void phys_render(track_t *t, voice_t *v, int32_t *out, uint32_t n, const vmod_t *m)
{
    uint32_t part = (uint32_t)(t - trk) % NPART, vk = (uint32_t)(v - t->v) % 3u, i, k;
    int32_t model = clamp(t->p[P_E0], 0, 3);
    float strc = (float)clamp(t->p[P_E1], 0, 127) / 127.0f, brit = (float)clamp(t->p[P_E2], 0, 127) / 127.0f;
    float damp = (float)clamp(t->p[P_E3], 0, 127) / 127.0f, pos = (float)clamp(t->p[P_E4], 1, 126) / 127.0f;
    float acc = 0.4f + (float)clamp(t->p[P_E5], 0, 127) / 127.0f * 1.2f, bow = (float)clamp(t->p[P_E6], 0, 127) / 127.0f;
    float exc = (float)clamp(t->p[P_E7], 0, 127) / 127.0f;
    float f0 = (float)m->inc * phys_fs() / 4294967296.0f, *st = (float *)v->s;
    brit = fm_clampf(brit + (float)(m->cutoff >> 8) / 160.0f, 0.0f, 1.0f);
    if (model == PM_STRNG || model == PM_SYMP) {   /* a string: Karplus-Strong, a one-zero loss filter */
        int16_t *line = phys_line[part][vk];
        float len = fm_clampf(phys_fs() / fm_maxf(f0, 20.0f), 4.0f, (float)PHYS_LINE - 2.0f);
        float fb = 0.990f + damp * 0.0098f - (1.0f - brit) * 0.004f, loss = 0.25f + brit * 0.5f, xlp = 0.0f;
        uint32_t w = (uint32_t)v->s[0] % PHYS_LINE;
        float sl;
        memcpy(&sl, &v->s[2], sizeof sl);
        memcpy(&xlp, &v->s[3], sizeof xlp);
        for (i = 0; i < n; i++) {
            float rp = (float)w - len, frac, a, b, y, x;
            uint32_t r0;
            if (rp < 0.0f)
                rp += (float)PHYS_LINE;
            r0 = (uint32_t)rp;
            frac = rp - (float)r0;
            a = (float)line[r0 % PHYS_LINE];
            b = (float)line[(r0 + 1u) % PHYS_LINE];
            y = (a + (b - a) * frac) / 16384.0f;
            sl = y * loss + sl * (1.0f - loss);          /* the loss filter: BRIT bright, low dull */
            if (strc > 0.01f)                            /* stiffness: a little of the next sample's partner */
                sl = sl * (1.0f - strc * 0.3f) + y * strc * 0.3f;
            x = phys_exc(v, exc, brit, acc, bow, &xlp);
            if (v->ph[0] < (uint32_t)(len * pos) + 2u)   /* the pluck position: a comb on the excitation */
                x *= 1.0f;
            else if (v->ph[0] < (uint32_t)len + 2u)
                x *= -0.5f;
            line[w] = (int16_t)fm_clampf((sl * fb + x * 0.5f) * 16384.0f, -32767.0f, 32767.0f);
            w = (w + 1u) % PHYS_LINE;
            if (model == PM_SYMP && vk == 0u) {          /* the sympathetic strings: fed by this one, a chord above */
                static const float CHORD[8][2] = {{2.0f, 3.0f}, {1.5f, 2.0f}, {1.25f, 1.5f}, {1.2f, 1.5f},
                                                  {1.3333f, 2.0f}, {1.5f, 2.25f}, {2.0f, 4.0f}, {1.122f, 1.5f}};
                uint32_t c = (uint32_t)(strc * 7.99f), j;
                for (j = 0; j < 2u; j++) {
                    int16_t *sy = phys_symp[part][j];
                    float sl2 = fm_clampf(len / CHORD[c][j], 4.0f, (float)PHYS_LINE - 2.0f), rp2;
                    uint32_t ww = phys_symp_w[part][j] % PHYS_LINE, q0;
                    float yy;
                    rp2 = (float)ww - sl2;
                    if (rp2 < 0.0f)
                        rp2 += (float)PHYS_LINE;
                    q0 = (uint32_t)rp2;
                    yy = (float)sy[q0 % PHYS_LINE] / 16384.0f;
                    phys_symp_lp[part][j] = yy * 0.5f + phys_symp_lp[part][j] * 0.5f;
                    sy[ww] = (int16_t)fm_clampf((phys_symp_lp[part][j] * 0.996f + y * 0.02f * pos) * 16384.0f,
                                                -32767.0f, 32767.0f);
                    phys_symp_w[part][j] = (ww + 1u) % PHYS_LINE;
                    y += yy * pos * 0.7f;
                    if (bow > 0.0f)                      /* BUZZ: the bridge's rattle, a soft fold */
                        y = y + bow * 0.3f * (y > 0.3f ? y - 0.3f : y < -0.3f ? y + 0.3f : 0.0f);
                }
            }
            out[i] += voice_amp((int32_t)fm_clampf(y * 30000.0f, -32767.0f, 32767.0f), m, i);
        }
        v->s[0] = (int32_t)w;
        memcpy(&v->s[2], &sl, sizeof sl);
        memcpy(&v->s[3], &xlp, sizeof xlp);
        return;
    }
    {   /* MODAL, MEMB: four two-pole resonators; s[0..7] as floats: y1, y2 for each */
        static const float BAR[4] = {1.0f, 2.756f, 5.404f, 8.933f};      /* a free bar's modes */
        static const float BELL[4] = {1.0f, 2.0f, 2.4f, 3.0f};
        static const float HEAD[4] = {1.0f, 1.594f, 2.136f, 2.296f};     /* a membrane's (Bessel) modes */
        float ratio[4], a1[4], a2[4], amp[4], fall = 1.0f;
        float xlp = 0.0f;
        if (model == PM_MEMB) {                          /* BOW: how far the pitch falls after the hit */
            float age = (float)v->ph[0] / phys_fs();
            fall = 1.0f + bow * 0.6f * fm_expf(-age * 18.0f);
        }
        for (k = 0; k < 4u; k++) {
            float r, w, dk;
            if (model == PM_MEMB)
                ratio[k] = 1.0f + (HEAD[k] - 1.0f) * (0.6f + strc * 0.8f);
            else if (strc < 0.5f)
                ratio[k] = (float)(k + 1u) + (BAR[k] - (float)(k + 1u)) * strc * 2.0f;
            else
                ratio[k] = BAR[k] + (BELL[k] - BAR[k]) * (strc - 0.5f) * 2.0f;
            w = 2.0f * 3.14159265f * fm_minf(f0 * ratio[k] * fall, phys_fs() * 0.45f) / phys_fs();
            dk = (0.6f + damp * 6.0f) / (1.0f + (float)k * (1.2f - brit));   /* seconds-ish: the higher die sooner */
            r = fm_expf(-1.0f / (dk * phys_fs() * 0.05f));
            a1[k] = 2.0f * r * fm_cosf(w);
            a2[k] = -r * r;
            amp[k] = fm_sin_pi(pos * (float)(k + 1u)) / (1.0f + (float)k * 0.5f);   /* the strike position */
        }
        memcpy(&xlp, &v->ph[2], sizeof xlp);   /* (the exciter's low-pass, float bits) */
        for (i = 0; i < n; i++) {
            float x = phys_exc(v, exc, brit, acc, model == PM_MEMB ? 0.0f : bow, &xlp) * 0.02f, y = 0.0f;
            for (k = 0; k < 4u; k++) {
                float y0 = a1[k] * st[2 * k] + a2[k] * st[2 * k + 1] + x * amp[k];
                if (y0 > 50.0f || y0 < -50.0f)
                    y0 = 0.0f;
                st[2 * k + 1] = st[2 * k];
                st[2 * k] = y0;
                y += y0;
            }
            out[i] += voice_amp((int32_t)fm_clampf(y * 15000.0f, -32767.0f, 32767.0f), m, i);
        }
        memcpy(&v->ph[2], &xlp, sizeof xlp);
    }
}

static const preset_t PHYS_PRESETS[] = {
    /*                      MODEL STRC BRIT DAMP POS ACC BOW EXC */
    {.name = "BELL TREE", .e = {0, 120, 110, 90, 40, 70, 0, 30}, .env = {0, 90, 127, 90}},
    {.name = "MARIMBA", .e = {0, 64, 70, 40, 30, 80, 0, 10}, .env = {0, 70, 127, 50}},
    {.name = "PLUCK", .e = {1, 10, 90, 70, 20, 80, 0, 60}, .env = {0, 80, 127, 60}},
    {.name = "BOWED METAL", .e = {0, 100, 90, 110, 50, 40, 90, 110}, .env = {60, 80, 110, 90}},
    {.name = "KALIMBA", .e = {0, 30, 80, 50, 15, 90, 0, 5}, .env = {0, 70, 127, 50}},
    {.name = "HAND DRUM", .e = {2, 50, 60, 30, 30, 100, 20, 40}, .env = {0, 60, 127, 30}},
    {.name = "TOMS", .e = {2, 30, 50, 50, 40, 110, 60, 30}, .env = {0, 60, 127, 40}},
    {.name = "DRONE STRING", .e = {3, 20, 80, 120, 70, 60, 50, 80}, .env = {40, 90, 120, 80}},
    {.name = "HARP", .e = {1, 30, 75, 100, 30, 70, 0, 20}, .env = {0, 90, 127, 80}},
};

static const engine_t ENG_PHYS = {
    .name = "PHYS",
    .page_title = {"MODL", "EXCT"},
    .edit = {{"MODEL", F_ENUM, 0, 3, 0, N_PHYS_MODEL, 0}, {"STRC", F_PCT, 0, 127, 64, 0, 0},
             {"BRIT", F_PCT, 0, 127, 80, 0, 0}, {"DAMP", F_PCT, 0, 127, 64, 0, 0},
             {"POS", F_PCT, 1, 126, 30, 0, 0}, {"ACC", F_PCT, 0, 127, 80, 0, 0},
             {"BOW", F_PCT, 0, 127, 0, 0, 0}, {"EXC", F_PCT, 0, 127, 30, 0, 0}},
    .presets = PHYS_PRESETS,
    .npresets = NELEM(PHYS_PRESETS),
    .knob = {P_E1, P_E2, P_E3, P_E4},
    .poly = 3,
    .note_on = phys_note_on,
    .render = phys_render,
};
