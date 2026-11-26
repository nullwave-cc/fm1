/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 nullwave contributors */
/* SAMPLE, GRAIN and SLICE: three engines on the built-in samples (samples.h: CC0 recordings of a piano, a flute and a
 * tenor saxophone, and a drum break made by tools/gen_samples.py), in the engine interface (felucca_engine.h). Written
 * for nullwave. A set that is not there (the
 * three user slots, for samples loaded later) plays a plain sine at the note's pitch.
 *
 * SAMPLE: SET (PIANO, FLUTE, SAX, USR1..3), TUNE, BITS, LOOP; TONE page: CUT, DRV. The zone recorded nearest the note,
 *   read at the note's rate (linear interpolation), looped where it was recorded to loop (LOOP ON).
 * GRAIN: SRC (the sets and BREAK), POS, SIZE, DENS; SPRY page: PTCH, SPRD, RAND, TONE. Up to four grains a voice, each
 *   a piece of the source under a window, started DENS times a second around POS (SPRD) at the note's pitch (RAND).
 * SLICE: SRC (BREAK, PIANO, USR1..3) cut into SLCS equal slices, one a key from F3 (MIDI 36 up); TUNE, LEN (how much
 *   of the slice plays); PLAY page: RVRS, TONE, DRV, CRSH.
 *
 * State (SAMPLE, SLICE): ph[0] the position (samples), ph[1] its fraction (Q16), s[0] the zone, s[1] the step (Q16),
 * s[2] the tone's low-pass, s[3] the end, s[4] the held sample (BITS / CRSH), s[5] the direction, s[6] the sine's
 * phase (no sample). GRAIN keeps its grains in grain_st. */
#include "samples.h"

static int16_t smp_mu[256];              /* mu-law -> Q15 */
static void smp_init(void)
{
    uint32_t i;
    if (smp_mu[255])
        return;
    for (i = 0; i < 256u; i++) {
        float y = ((float)i / 127.5f) - 1.0f, a = y < 0 ? -y : y;
        float lin = (fm_powf(256.0f, a) - 1.0f) / 255.0f;
        smp_mu[i] = (int16_t)((y < 0 ? -lin : lin) * 32767.0f);
    }
}
static inline int32_t smp_at(uint32_t i) { return smp_mu[SMP_DATA[i]]; }

static int smp_zone(uint32_t set, uint32_t note)   /* the set's zone recorded nearest the note, -1 no set */
{
    uint32_t z;
    int best = -1, bd = 999;
    for (z = 0; z < SMP_NZONES; z++) {
        int d;
        if (SMP_ZONE[z].set != set)
            continue;
        d = (int)note - (int)SMP_ZONE[z].root;
        d = d < 0 ? -d : d;
        if (d < bd) {
            bd = d;
            best = (int)z;
        }
    }
    return best;
}

/* the step (Q16) that plays a zone recorded at root as note, tuned by st semitones */
static uint32_t smp_step(uint32_t root, int32_t note16, int32_t st)
{
    float r = fm_exp2f(((float)(note16 - (int32_t)root * 16) / 16.0f + (float)st) / 12.0f);
    return (uint32_t)(r * (float)SMP_RATE / (float)FS * 65536.0f);
}

static const char *const N_SMP_SET[] = {"PIANO", "FLUTE", "SAX", "USR1", "USR2", "USR3", 0};
static const char *const N_GRAIN_SRC[] = {"PIANO", "FLUTE", "SAX", "BREAK", "USR1", "USR2", "USR3", 0};
static const char *const N_SLICE_SRC[] = {"BREAK", "PIANO", "USR1", "USR2", "USR3", 0};
static int set_of(const char *name)      /* a set's number in samples.h, -1 not built in */
{
    uint32_t i, k;
    for (i = 0; i < SMP_NSETS; i++) {
        for (k = 0; name[k] && name[k] == SMP_SET_NAME[i][k]; k++)
            ;
        if (!name[k] && !SMP_SET_NAME[i][k])
            return (int)i;
    }
    return -1;
}

/* a voice reading a sample: one step, with the tone's low-pass and the bits */
static inline int32_t smp_read(voice_t *v)
{
    uint32_t p = v->ph[0];
    int32_t a = smp_at(p), b = smp_at(p + 1u);
    return a + (((b - a) * (int32_t)(v->ph[1] >> 1)) >> 15);
}
static inline void smp_advance(voice_t *v, uint32_t step)
{
    uint32_t f = v->ph[1] + step;
    v->ph[0] += f >> 16;
    v->ph[1] = f & 0xFFFFu;
}

static int32_t tone_lp(voice_t *v, int32_t x, int32_t cut)   /* cut 0..127: a one-pole low-pass, 127 open */
{
    int32_t a = 1200 + cut * cut * 2;    /* Q15 */
    if (cut >= 127)
        return x;
    v->s[2] += ((x - v->s[2]) * (a > 32767 ? 32767 : a)) >> 15;
    return v->s[2];
}

static int32_t bits_hold(voice_t *v, int32_t x, int32_t bits, uint32_t i)   /* bits 0 (off) .. 12 */
{
    if (!bits)
        return x;
    if ((i & ((1u << (bits / 4)) - 1u)) == 0u)
        v->s[4] = (x >> bits) << bits;
    return v->s[4];
}

/* ---------------------------------------------------------------- SAMPLE --- */
static void sample_note_on(track_t *t, voice_t *v)
{
    int set = set_of(N_SMP_SET[clamp(t->p[P_E0], 0, 5)]), z;
    smp_init();
    z = set < 0 ? -1 : smp_zone((uint32_t)set, v->note);
    v->s[0] = z;
    v->ph[0] = z < 0 ? 0 : SMP_ZONE[z].at;
    v->ph[1] = 0;
    v->s[2] = v->s[4] = v->s[6] = 0;
    v->s[3] = z < 0 ? 0 : (int32_t)(SMP_ZONE[z].at + SMP_ZONE[z].len - 2u);
}

static void sample_render(track_t *t, voice_t *v, int32_t *out, uint32_t n, const vmod_t *m)
{
    int z = v->s[0];
    uint32_t i, step;
    int32_t cut = clamp(t->p[P_E4] + (m->cutoff >> 8), 0, 127), drv = clamp(t->p[P_E6], 0, 127);
    int32_t bits = clamp(t->p[P_E2], 0, 12), loop = t->p[P_E3];
    if (z < 0) {                          /* no sample: a sine at the note's pitch */
        for (i = 0; i < n; i++) {
            v->ph[2] += m->inc;
            out[i] += voice_amp(sine_i(v->ph[2]) >> 1, m, i);
        }
        return;
    }
    step = smp_step(SMP_ZONE[z].root, m->pitch16, clamp(t->p[P_E1], -24, 24));
    for (i = 0; i < n; i++) {
        int32_t x;
        if ((int32_t)v->ph[0] >= v->s[3]) {
            if (loop && SMP_ZONE[z].loop >= 0)
                v->ph[0] = SMP_ZONE[z].at + (uint32_t)SMP_ZONE[z].loop;
            else
                break;                   /* (the end: silent until the voice's release ends it) */
        }
        x = smp_read(v);
        smp_advance(v, step);
        x = tone_lp(v, x, cut);
        if (drv)
            x = softclip(x * (16 + drv / 4) >> 4);
        x = bits_hold(v, x, bits, i);
        out[i] += voice_amp(x, m, i);
    }
}

static const preset_t SAMPLE_PRESETS[] = {
    /*                SET TUNE BITS LOOP  CUT -  DRV - */
    {.name = "PIANO", .e = {0, 0, 0, 0, 120, 0, 0, 0}, .env = {0, 90, 0, 60}},
    {.name = "FLUTE", .e = {1, 0, 0, 1, 110, 0, 0, 0}, .env = {6, 64, 110, 40}},
    {.name = "SAX", .e = {2, 0, 0, 1, 115, 0, 10, 0}, .env = {4, 64, 105, 35}},
};

static const engine_t ENG_SAMPLE = {
    .name = "SAMPLE",
    .page_title = {"SET", "TONE"},
    .edit = {{"SET", F_ENUM, 0, 5, 0, N_SMP_SET, 0}, {"TUNE", F_INT, -24, 24, 0, 0, "st"},
             {"BITS", F_INT, 0, 12, 0, 0, 0}, {"LOOP", F_ONOFF, 0, 1, 0, 0, 0},
             {"CUT", F_INT, 0, 127, 127, 0, 0}, {"-", F_INT, 0, 0, 0, 0, 0},
             {"DRV", F_INT, 0, 127, 0, 0, 0}, {"-", F_INT, 0, 0, 0, 0, 0}},
    .presets = SAMPLE_PRESETS,
    .npresets = NELEM(SAMPLE_PRESETS),
    .knob = {P_E0, P_E1, P_E4, P_REL},
    .sampled = 1,
    .note_on = sample_note_on,
    .render = sample_render,
};

/* ----------------------------------------------------------------- GRAIN --- */
#define NGRAIN 4
typedef struct { uint32_t pos, frac, step; uint16_t age, len; } grain_t;
static grain_t grain_st[NPART][NVOICE][NGRAIN];

static void grain_note_on(track_t *t, voice_t *v)
{
    int set = set_of(N_GRAIN_SRC[clamp(t->p[P_E0], 0, 6)]);
    uint32_t k = (uint32_t)(v - t->v), part = (uint32_t)(t - trk);
    smp_init();
    v->s[0] = set < 0 ? -1 : smp_zone((uint32_t)set, v->note);
    v->s[1] = 0;                         /* the next grain: now */
    v->s[2] = 0;
    v->s[5] = (int32_t)(v->age * 2654435761u) | 1;
    memset(grain_st[part % NPART][k % NVOICE], 0, sizeof grain_st[0][0]);
}

static void grain_render(track_t *t, voice_t *v, int32_t *out, uint32_t n, const vmod_t *m)
{
    int z = v->s[0];
    uint32_t i, g, part = (uint32_t)(t - trk) % NPART, k = (uint32_t)(v - t->v) % NVOICE;
    grain_t *gs = grain_st[part][k];
    int32_t pos = clamp(t->p[P_E1], 0, 127), size = clamp(t->p[P_E2], 0, 127), dens = clamp(t->p[P_E3], 0, 127);
    int32_t ptch = clamp(t->p[P_E4], -24, 24), sprd = clamp(t->p[P_E5], 0, 127), rnd = clamp(t->p[P_E6], 0, 127);
    int32_t tone = clamp(t->p[P_E7] + (m->cutoff >> 8), 0, 127);
    uint32_t glen = (uint32_t)(441 + size * size * 3 / 2), every = (uint32_t)(FS / (2 + dens * dens / 270));
    if (z < 0) {
        for (i = 0; i < n; i++) {
            v->ph[2] += m->inc;
            out[i] += voice_amp(sine_i(v->ph[2]) >> 1, m, i);
        }
        return;
    }
    for (i = 0; i < n; i++) {
        int32_t sum = 0;
        if (v->s[1]-- <= 0) {            /* a new grain, in the freest slot */
            uint32_t r = noise32(&v->s[5]), slot = 0, len = SMP_ZONE[z].len;
            int32_t start = pos * (int32_t)len / 128 + (int32_t)((r >> 16) % (uint32_t)(sprd * (int32_t)len / 256 + 1));
            for (g = 1; g < NGRAIN; g++)
                if (gs[g].age >= gs[g].len && gs[slot].age < gs[slot].len)
                    slot = g;
            gs[slot].pos = SMP_ZONE[z].at + (uint32_t)clamp(start, 0, (int32_t)len - 2);
            gs[slot].frac = 0;
            gs[slot].step = smp_step(SMP_ZONE[z].root, m->pitch16 + (int32_t)((r & 0xFFu) * (uint32_t)rnd / 256u) - rnd / 2,
                                     ptch);
            gs[slot].age = 0;
            gs[slot].len = (uint16_t)(glen > 60000u ? 60000u : glen);
            v->s[1] = (int32_t)every;
        }
        for (g = 0; g < NGRAIN; g++) {
            grain_t *gr = &gs[g];
            int32_t a, b, x, w;
            uint32_t f;
            if (gr->age >= gr->len)
                continue;
            if (gr->pos + 2u >= SMP_ZONE[z].at + SMP_ZONE[z].len) {
                gr->age = gr->len;
                continue;
            }
            a = smp_at(gr->pos);
            b = smp_at(gr->pos + 1u);
            x = a + (((b - a) * (int32_t)(gr->frac >> 1)) >> 15);
            w = (int32_t)((uint32_t)gr->age * 4096u / gr->len);    /* the window: 4 x (1 - x), Q12 */
            w = (w * (4096 - w)) >> 10;
            sum += (x * w) >> 12;
            f = gr->frac + gr->step;
            gr->pos += f >> 16;
            gr->frac = f & 0xFFFFu;
            gr->age++;
        }
        sum = tone_lp(v, sum >> 1, tone);
        out[i] += voice_amp(clamp(sum, -32767, 32767), m, i);
    }
}

static const preset_t GRAIN_PRESETS[] = {
    /*                    SRC POS SIZE DENS PTCH SPRD RAND TONE */
    {.name = "CLOUD PAD", .e = {0, 50, 90, 70, 0, 40, 10, 100}, .env = {70, 80, 110, 90}},
    {.name = "GLITCH", .e = {3, 20, 25, 110, 0, 90, 40, 120}, .env = {0, 60, 100, 20}},
    {.name = "FROZEN", .e = {1, 64, 110, 80, 0, 8, 4, 90}, .env = {90, 80, 120, 100}},
    {.name = "SHIMMER", .e = {2, 40, 70, 90, 12, 60, 20, 115}, .env = {60, 80, 110, 90}},
};

static const engine_t ENG_GRAIN = {
    .name = "GRAIN",
    .page_title = {"GRAN", "SPRY"},
    .edit = {{"SRC", F_ENUM, 0, 6, 0, N_GRAIN_SRC, 0}, {"POS", F_PCT, 0, 127, 64, 0, 0},
             {"SIZE", F_PCT, 0, 127, 64, 0, 0}, {"DENS", F_PCT, 0, 127, 64, 0, 0},
             {"PTCH", F_INT, -24, 24, 0, 0, "st"}, {"SPRD", F_PCT, 0, 127, 30, 0, 0},
             {"RAND", F_PCT, 0, 127, 10, 0, 0}, {"TONE", F_PCT, 0, 127, 100, 0, 0}},
    .presets = GRAIN_PRESETS,
    .npresets = NELEM(GRAIN_PRESETS),
    .knob = {P_E1, P_E2, P_E3, P_E7},
    .poly = 3,
    .sampled = 1,
    .note_on = grain_note_on,
    .render = grain_render,
};

/* ----------------------------------------------------------------- SLICE --- */
static void slice_note_on(track_t *t, voice_t *v)
{
    int set = set_of(N_SLICE_SRC[clamp(t->p[P_E0], 0, 4)]), z;
    uint32_t slcs = (uint32_t)clamp(t->p[P_E1], 2, 16), s = (uint32_t)(v->note + 128 - 36) % slcs, len, sl;
    int32_t keep = clamp(t->p[P_E3], 1, 127);
    smp_init();
    z = set < 0 ? -1 : smp_zone((uint32_t)set, 60);
    v->s[0] = z;
    v->ph[1] = 0;
    v->s[2] = v->s[4] = v->s[6] = 0;
    if (z < 0)
        return;
    len = SMP_ZONE[z].len;
    sl = len / slcs;
    v->s[5] = t->p[P_E4] ? -1 : 1;       /* RVRS: from its end */
    v->ph[0] = SMP_ZONE[z].at + s * sl + (v->s[5] < 0 ? (uint32_t)(sl * (uint32_t)keep / 127u) - 2u : 0u);
    v->s[3] = (int32_t)(SMP_ZONE[z].at + s * sl + (v->s[5] < 0 ? 0u : sl * (uint32_t)keep / 127u - 2u));
}

static void slice_render(track_t *t, voice_t *v, int32_t *out, uint32_t n, const vmod_t *m)
{
    int z = v->s[0];
    uint32_t i, step;
    int32_t tone = clamp(t->p[P_E5] + (m->cutoff >> 8), 0, 127), drv = clamp(t->p[P_E6], 0, 127);
    int32_t crsh = clamp(t->p[P_E7], 0, 12);
    if (z < 0) {
        for (i = 0; i < n; i++) {
            v->ph[2] += m->inc;
            out[i] += voice_amp(sine_i(v->ph[2]) >> 1, m, i);
        }
        return;
    }
    step = smp_step(60, 60 * 16, clamp(t->p[P_E2], -24, 24));
    for (i = 0; i < n; i++) {
        int32_t x;
        if (v->s[5] > 0 ? (int32_t)v->ph[0] >= v->s[3] : (int32_t)v->ph[0] <= v->s[3])
            break;
        x = smp_read(v);
        if (v->s[5] > 0) {
            smp_advance(v, step);
        } else {                         /* backwards */
            uint32_t f = v->ph[1] + (65536u - (step & 0xFFFFu));
            v->ph[0] -= (step >> 16) + 1u - (f >> 16);
            v->ph[1] = f & 0xFFFFu;
        }
        x = tone_lp(v, x, tone);
        if (drv)
            x = softclip(x * (16 + drv / 4) >> 4);
        x = bits_hold(v, x, crsh, i);
        out[i] += voice_amp(x, m, i);
    }
}

/* the FM-1's keys on a SLICE track: one slice a key, every key from F3 (MIDI 36 up), round the slices */
static int32_t slice_key(const track_t *t, uint32_t k)
{
    (void)t;
    return k < 27u ? 36 + (int32_t)k : -1;
}

static const preset_t SLICE_PRESETS[] = {
    /*                SRC SLCS TUNE LEN RVRS TONE DRV CRSH */
    {.name = "CHOP", .e = {0, 16, 0, 127, 0, 127, 0, 0}, .env = {0, 64, 127, 10}},
    {.name = "STUTTER", .e = {0, 16, 0, 40, 0, 110, 20, 2}, .env = {0, 64, 127, 4}},
};

static const engine_t ENG_SLICE = {
    .name = "SLICE",
    .page_title = {"SLICES", "PLAY"},
    .edit = {{"SRC", F_ENUM, 0, 4, 0, N_SLICE_SRC, 0}, {"SLCS", F_INT, 2, 16, 16, 0, 0},
             {"TUNE", F_INT, -24, 24, 0, 0, "st"}, {"LEN", F_PCT, 1, 127, 127, 0, 0},
             {"RVRS", F_ONOFF, 0, 1, 0, 0, 0}, {"TONE", F_PCT, 0, 127, 127, 0, 0},
             {"DRV", F_INT, 0, 127, 0, 0, 0}, {"CRSH", F_INT, 0, 12, 0, 0, 0}},
    .presets = SLICE_PRESETS,
    .npresets = NELEM(SLICE_PRESETS),
    .knob = {P_E1, P_E2, P_E3, P_E5},
    .sampled = 1,
    .note_on = slice_note_on,
    .render = slice_render,
    .keys = slice_key,
};
