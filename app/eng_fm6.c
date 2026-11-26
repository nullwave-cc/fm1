/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 nullwave contributors */
/* FM6: six-operator FM in the manner of the DX7, in the engine interface (felucca_engine.h), written for nullwave from
 * manual (the engine, its pages and knobs; none of its code; no code of Dexed or msfa either). Each operator: a sine at
 * a ratio (or a fixed frequency) of the note, its own four-stage envelope (rates and levels 0..99), its output level,
 * velocity sensitivity and detune; the 32 algorithms of the DX7 (its published chart, encoded here) wire them, with
 * feedback on one operator. A patch (fm6_patch_t) comes from a slot: F1..F8 nullwave's factory patches, OWN the
 * channel's own (the operator pages edit it), B1..B32 a voice bank sent as a DX7 SysEx (32 voices).
 * The eight values are macros on top of the patch: ALG (PAT: the patch's, or 1..32), FB, MLVL (the modulators' levels
 * up or down), MRAT (their ratios), MEG (their envelopes faster or slower), VMOD (velocity -> brightness), DTUN (the
 * carriers spread), SLOT.
 * Six voices a channel; their operators' state in fm6_vs. Floating point (the FPU). */
typedef struct {
    uint8_t r[4], l[4];                  /* the envelope: rates and levels 0..99 */
    uint8_t level, coarse, fine;         /* output 0..99; frequency: coarse 0..31 (0 = 0.5), fine 0..99 */
    uint8_t mode, detune, vel;           /* 0 ratio, 1 fixed; detune 0..14 (7 none); velocity sensitivity 0..7 */
} fm6_op_t;
typedef struct {
    char name[10];
    uint8_t alg, fb;                     /* 0..31, 0..7 */
    fm6_op_t op[6];                      /* op[0] is OP1 */
} fm6_patch_t;

/* the DX7 algorithms: for each, which operators modulate each one (bit j: OP j+1), which are carriers, the
 * feedback operator */
typedef struct { uint8_t mod[6], car, fb; } fm6_alg_t;
#define M(n) (1u << ((n) - 1))
static const fm6_alg_t FM6_ALG[32] = {
    /* 1 */ {{M(2), 0, M(4), M(5), M(6), 0}, M(1) | M(3), 6},
    /* 2 */ {{M(2), 0, M(4), M(5), M(6), 0}, M(1) | M(3), 2},
    /* 3 */ {{M(2), M(3), 0, M(5), M(6), 0}, M(1) | M(4), 6},
    /* 4 */ {{M(2), M(3), 0, M(5), M(6), 0}, M(1) | M(4), 6},
    /* 5 */ {{M(2), 0, M(4), 0, M(6), 0}, M(1) | M(3) | M(5), 6},
    /* 6 */ {{M(2), 0, M(4), 0, M(6), 0}, M(1) | M(3) | M(5), 6},
    /* 7 */ {{M(2), 0, M(4) | M(5), 0, M(6), 0}, M(1) | M(3), 6},
    /* 8 */ {{M(2), 0, M(4) | M(5), 0, M(6), 0}, M(1) | M(3), 4},
    /* 9 */ {{M(2), 0, M(4) | M(5), 0, M(6), 0}, M(1) | M(3), 2},
    /* 10 */ {{M(2), M(3), 0, M(5) | M(6), 0, 0}, M(1) | M(4), 3},
    /* 11 */ {{M(2), M(3), 0, M(5) | M(6), 0, 0}, M(1) | M(4), 6},
    /* 12 */ {{M(2), 0, M(4) | M(5) | M(6), 0, 0, 0}, M(1) | M(3), 2},
    /* 13 */ {{M(2), 0, M(4) | M(5) | M(6), 0, 0, 0}, M(1) | M(3), 6},
    /* 14 */ {{M(2), 0, M(4), M(5) | M(6), 0, 0}, M(1) | M(3), 6},
    /* 15 */ {{M(2), 0, M(4), M(5) | M(6), 0, 0}, M(1) | M(3), 2},
    /* 16 */ {{M(2) | M(3) | M(5), 0, M(4), 0, M(6), 0}, M(1), 6},
    /* 17 */ {{M(2) | M(3) | M(5), 0, M(4), 0, M(6), 0}, M(1), 2},
    /* 18 */ {{M(2) | M(3) | M(4), 0, 0, M(5), M(6), 0}, M(1), 3},
    /* 19 */ {{M(2), M(3), 0, M(6), M(6), 0}, M(1) | M(4) | M(5), 6},
    /* 20 */ {{M(3), M(3), 0, M(5) | M(6), 0, 0}, M(1) | M(2) | M(4), 3},
    /* 21 */ {{M(3), M(3), 0, M(6), M(6), 0}, M(1) | M(2) | M(4) | M(5), 3},
    /* 22 */ {{M(2), 0, M(6), M(6), M(6), 0}, M(1) | M(3) | M(4) | M(5), 6},
    /* 23 */ {{0, M(3), 0, M(6), M(6), 0}, M(1) | M(2) | M(4) | M(5), 6},
    /* 24 */ {{0, 0, M(6), M(6), M(6), 0}, M(1) | M(2) | M(3) | M(4) | M(5), 6},
    /* 25 */ {{0, 0, 0, M(6), M(6), 0}, M(1) | M(2) | M(3) | M(4) | M(5), 6},
    /* 26 */ {{0, M(3), 0, M(5) | M(6), 0, 0}, M(1) | M(2) | M(4), 6},
    /* 27 */ {{0, M(3), 0, M(5) | M(6), 0, 0}, M(1) | M(2) | M(4), 3},
    /* 28 */ {{M(2), 0, M(4), M(5), 0, 0}, M(1) | M(3) | M(6), 5},
    /* 29 */ {{0, 0, M(4), 0, M(6), 0}, M(1) | M(2) | M(3) | M(5), 6},
    /* 30 */ {{0, 0, M(4), M(5), 0, 0}, M(1) | M(2) | M(3) | M(6), 5},
    /* 31 */ {{0, 0, 0, 0, M(6), 0}, M(1) | M(2) | M(3) | M(4) | M(5), 6},
    /* 32 */ {{0, 0, 0, 0, 0, 0}, 0x3F, 6},
};
#undef M

/* nullwave's factory patches F1..F8, named (TINE EP, BELL, FM BASS, BRASS, PAD,
 * MARIMBA, ORGAN, PLUCK); the values are nullwave's. op: {rates}, {levels}, level, coarse, fine, mode, detune, vel */
#define OP(r1, r2, r3, r4, l1, l2, l3, l4, lv, co, fi, de, ve) {{r1, r2, r3, r4}, {l1, l2, l3, l4}, lv, co, fi, 0, de, ve}
static const fm6_patch_t FM6_FACTORY[8] = {
    {"TINE EP", 4, 6, {OP(96, 30, 20, 50, 99, 80, 0, 0, 99, 1, 0, 7, 2), OP(95, 50, 35, 60, 99, 75, 0, 0, 72, 14, 0, 8, 5),
                       OP(95, 28, 18, 50, 99, 78, 0, 0, 99, 1, 0, 6, 2), OP(98, 45, 30, 60, 99, 70, 0, 0, 68, 1, 0, 7, 6),
                       OP(96, 26, 20, 50, 99, 80, 0, 0, 92, 1, 0, 8, 2), OP(97, 40, 30, 60, 99, 65, 0, 0, 74, 1, 0, 7, 5)}},
    {"BELL", 4, 3, {OP(98, 22, 14, 45, 99, 70, 0, 0, 99, 1, 0, 7, 3), OP(97, 25, 16, 45, 99, 68, 0, 0, 80, 3, 50, 9, 4),
                    OP(98, 20, 12, 45, 99, 72, 0, 0, 92, 2, 0, 6, 3), OP(97, 24, 15, 45, 99, 66, 0, 0, 78, 7, 0, 8, 4),
                    OP(98, 21, 13, 45, 99, 70, 0, 0, 90, 4, 0, 7, 3), OP(96, 26, 18, 45, 99, 64, 0, 0, 70, 11, 0, 7, 4)}},
    {"FM BASS", 15, 5, {OP(99, 60, 40, 70, 99, 85, 70, 0, 99, 0, 0, 7, 1), OP(99, 70, 50, 70, 99, 60, 40, 0, 84, 1, 0, 7, 3),
                        OP(99, 75, 50, 70, 99, 55, 30, 0, 76, 1, 0, 7, 3), OP(99, 80, 60, 70, 99, 50, 20, 0, 70, 2, 0, 7, 3),
                        OP(99, 65, 50, 70, 99, 60, 30, 0, 66, 1, 0, 7, 3), OP(99, 85, 60, 70, 99, 45, 0, 0, 74, 1, 0, 7, 3)}},
    {"BRASS", 21, 7, {OP(72, 50, 40, 60, 99, 92, 88, 0, 99, 1, 0, 7, 1), OP(70, 48, 40, 60, 99, 90, 85, 0, 97, 1, 0, 8, 1),
                      OP(74, 55, 45, 60, 99, 88, 80, 0, 82, 1, 0, 6, 2), OP(72, 50, 40, 60, 99, 92, 88, 0, 98, 1, 0, 6, 1),
                      OP(70, 48, 40, 60, 99, 90, 85, 0, 96, 1, 0, 8, 1), OP(76, 56, 46, 60, 99, 86, 78, 0, 80, 1, 0, 7, 2)}},
    {"PAD", 0, 4, {OP(40, 30, 30, 50, 99, 96, 94, 0, 99, 1, 0, 6, 0), OP(38, 30, 30, 50, 99, 94, 90, 0, 70, 2, 0, 8, 1),
                   OP(40, 30, 30, 50, 99, 96, 94, 0, 98, 1, 0, 8, 0), OP(36, 30, 30, 50, 99, 92, 88, 0, 66, 1, 0, 6, 1),
                   OP(40, 30, 30, 50, 99, 96, 94, 0, 0, 1, 0, 7, 0), OP(34, 30, 30, 50, 99, 90, 86, 0, 60, 3, 0, 7, 1)}},
    {"MARIMBA", 6, 0, {OP(99, 40, 25, 50, 99, 60, 0, 0, 99, 1, 0, 7, 2), OP(99, 72, 50, 60, 99, 40, 0, 0, 80, 4, 0, 7, 4),
                       OP(99, 42, 26, 50, 99, 60, 0, 0, 94, 1, 0, 7, 2), OP(99, 75, 50, 60, 99, 35, 0, 0, 74, 10, 0, 7, 4),
                       OP(99, 44, 28, 50, 99, 58, 0, 0, 0, 1, 0, 7, 2), OP(99, 80, 50, 60, 99, 30, 0, 0, 0, 1, 0, 7, 4)}},
    {"ORGAN", 31, 2, {OP(90, 30, 30, 70, 99, 99, 99, 0, 97, 1, 0, 7, 0), OP(90, 30, 30, 70, 99, 99, 99, 0, 92, 2, 0, 7, 0),
                      OP(90, 30, 30, 70, 99, 99, 99, 0, 88, 4, 0, 7, 0), OP(90, 30, 30, 70, 99, 99, 99, 0, 84, 3, 0, 7, 0),
                      OP(90, 30, 30, 70, 99, 99, 99, 0, 78, 6, 0, 7, 0), OP(90, 30, 30, 70, 99, 99, 99, 0, 72, 8, 0, 7, 0)}},
    {"PLUCK", 1, 5, {OP(99, 45, 25, 55, 99, 70, 0, 0, 99, 1, 0, 7, 2), OP(99, 65, 40, 60, 99, 50, 0, 0, 82, 3, 0, 7, 4),
                     OP(99, 48, 28, 55, 99, 68, 0, 0, 90, 1, 0, 7, 2), OP(99, 70, 45, 60, 99, 45, 0, 0, 74, 2, 0, 7, 4),
                     OP(99, 75, 50, 60, 99, 40, 0, 0, 70, 5, 0, 7, 4), OP(99, 80, 55, 60, 99, 35, 0, 0, 64, 7, 0, 7, 4)}},
};
#undef OP

/* the channels' own patches (a song keeps them) and the bank (flash, a DX7 SysEx) */
static fm6_patch_t fm6_own[NPART];
static fm6_patch_t fm6_bank[32];
static uint8_t fm6_bank_n;               /* voices in the bank */

static const fm6_patch_t *fm6_patch(uint32_t part, int32_t slot)
{
    if (slot < 8)
        return &FM6_FACTORY[slot < 0 ? 0 : slot];
    if (slot == 8)
        return &fm6_own[part % NPART];
    slot -= 9;
    return slot < fm6_bank_n ? &fm6_bank[slot] : &FM6_FACTORY[0];
}

/* the operators of a voice */
typedef struct { uint32_t ph; float env, out1, out2; uint8_t st; } fm6_opst_t;
static fm6_opst_t fm6_vs[NPART][NVOICE][6];
static uint8_t fm6_mute[NPART];           /* EDIT held, an OP key held long: that operator silent (bit o) */

static float fm6_amp_t[100], fm6_rate_t[100];   /* (worked out once: no exp2f a block) */
static void fm6_tables(void)
{
    int32_t i;
    for (i = 0; i < 100; i++) {
        fm6_amp_t[i] = i ? fm_exp2f((float)(i - 99) / 8.0f) : 0.0f;
        fm6_rate_t[i] = 0.6f * fm_exp2f((float)i / 8.2f);
    }
}
static float fm6_amp(int32_t level99)    /* an output or envelope level, 0..99 -> 0..1 (0.75 dB a step) */
{
    return fm6_amp_t[clamp(level99, 0, 99)];
}
static float fm6_rate(int32_t r)         /* an envelope rate 0..99 -> levels (0..99) a second */
{
    return fm6_rate_t[clamp(r, 0, 99)];
}

static void fm6_sine_init(void);
static uint8_t fm6_sine_ready;
static void fm6_note_on_(track_t *t, voice_t *v);
static void fm6_note_on(track_t *t, voice_t *v)
{
    if (!fm6_sine_ready)                 /* (the tables, once: before any voice renders) */
        fm6_sine_init();
    fm6_note_on_(t, v);
}
static void fm6_note_on_(track_t *t, voice_t *v)
{
    uint32_t part = (uint32_t)(t - trk) % NPART, k = (uint32_t)(v - t->v) % NVOICE, o;
    for (o = 0; o < 6u; o++) {
        fm6_opst_t *s = &fm6_vs[part][k][o];
        s->st = 0;                       /* the attack: from where it is (a retrigger glides) */
        if (!v->active || v->stage == 0u)
            s->env = 0.0f, s->ph = 0, s->out1 = s->out2 = 0.0f;
    }
    v->s[0] = 1;                         /* gate seen */
}

/* the level: the operators' envelopes are the voice's (the host's ADSR is held at full); it ends when the carriers
 * have died away after the release */
static int32_t fm6_amp_hook(track_t *t, voice_t *v, int32_t adsr)
{
    uint32_t part = (uint32_t)(t - trk) % NPART, k = (uint32_t)(v - t->v) % NVOICE, o;
    const fm6_patch_t *pt = fm6_patch(part, clamp(t->p[P_E7], 0, 40));
    int32_t alg = t->p[P_E0] > 0 ? clamp(t->p[P_E0], 1, 32) - 1 : pt->alg % 32u;
    uint32_t car = FM6_ALG[alg].car;
    float sum = 0.0f;
    (void)adsr;
    for (o = 0; o < 6u; o++)             /* (what is heard: the carriers; a modulator's long tail alone is silence) */
        if (car >> o & 1u)
            sum += fm6_vs[part][k][o].env * fm6_amp(pt->op[o].level);
    v->active = 1;
    v->env = 1 << 24;                    /* (keeps the host's envelope from ending it) */
    if (!v->gate && sum < 0.004f * fm6_amp(99)) {   /* (let go and ~-48 dB: the voice is free, its work stops) */
        v->active = 0;
        v->stage = 0;
        return 0;
    }
    return 32767;
}

/* the operators, sample by sample: the voice's one hot loop, so it runs from RAM (PLAT_RAM_CODE: out of the XIP cache's
 * way) on a RAM copy of the sine table, and calls nothing; everything it needs worked out once a block */
typedef struct {
    uint32_t inc[6];
    float e[6], de[6], fbk;
    uint8_t mi[6][6], nm[6], act[6], fbo, car;
} fm6_blk_t;
static int16_t fm6_sine[1025];
static void fm6_tables(void);
static void fm6_sine_init(void)
{
    uint32_t i;
    fm6_tables();
    for (i = 0; i < 1025u; i++)
        fm6_sine[i] = SINE[i];
    fm6_sine_ready = 1;
}
static PLAT_RAM_CODE void fm6_ops(fm6_blk_t *bk, fm6_opst_t *s, float *out, uint32_t n)
{
    uint32_t i, o, j;
    for (i = 0; i < n; i++) {
        float sum = 0.0f, opout[6];
        for (o = 6u; o-- > 0;) {         /* OP6 first: the modulators come before what they modulate */
            float mod = 0.0f, y;
            uint32_t ph, x;
            int32_t a, b;
            s[o].ph += bk->inc[o];
            if (!bk->act[o]) {
                opout[o] = 0.0f;
                continue;
            }
            for (j = 0; j < bk->nm[o]; j++)
                mod += opout[bk->mi[o][j]];
            if (o == bk->fbo)
                mod += (s[o].out1 + s[o].out2) * bk->fbk;
            mod *= 683565275.0f;         /* 2^32 / (2 pi); held inside the int32 range (no wrap, on any FPU) */
            mod = mod > 2.1e9f ? 2.1e9f : mod < -2.1e9f ? -2.1e9f : mod;
            ph = s[o].ph + (uint32_t)(int32_t)mod;
            x = ph >> 22;                /* the sine, from the table between two points */
            a = fm6_sine[x];
            b = fm6_sine[x + 1u];
            y = (float)(a + (((b - a) * (int32_t)((ph >> 7) & 0x7FFFu)) >> 15)) * bk->e[o];
            bk->e[o] += bk->de[o];
            s[o].out2 = s[o].out1;
            s[o].out1 = y;
            opout[o] = y;
            if (bk->car >> o & 1u)
                sum += y;
        }
        out[i] = sum;
    }
}

static void (*volatile const fm6_ops_far)(fm6_blk_t *, fm6_opst_t *, float *, uint32_t) = fm6_ops;
static void fm6_render(track_t *t, voice_t *v, int32_t *out, uint32_t n, const vmod_t *m)
{
    uint32_t part = (uint32_t)(t - trk) % NPART, k = (uint32_t)(v - t->v) % NVOICE, i, o;
    const fm6_patch_t *pt = fm6_patch(part, clamp(t->p[P_E7], 0, 40));
    int32_t alg = t->p[P_E0] > 0 ? clamp(t->p[P_E0], 1, 32) - 1 : pt->alg % 32u;
    const fm6_alg_t *al = &FM6_ALG[alg];
    float fb = (float)clamp((int32_t)pt->fb + t->p[P_E1], 0, 7) / 7.0f * 0.9f;
    int32_t mlvl = clamp(t->p[P_E2], -50, 50), mrat = clamp(t->p[P_E3], -8, 8), meg = clamp(t->p[P_E4], -64, 63);
    float vmod = (float)clamp(t->p[P_E5], 0, 127) / 127.0f, dtun = (float)clamp(t->p[P_E6], 0, 127);
    float vel = (float)v->vel / 127.0f, bright = 1.0f + (float)(m->cutoff >> 8) / 64.0f;
    float lvl[6], dl[6], f;
    uint32_t inc[6];
    fm6_opst_t *s = fm6_vs[part][k];
    float blk = (float)n / (float)FS;
    for (o = 0; o < 6u; o++) {           /* per block: each operator's frequency and envelope */
        const fm6_op_t *op = &pt->op[o];
        int car = al->car >> o & 1;
        float ratio, target, rate, lv;
        if (op->mode) {
            static const float P10[4] = {1.0f, 10.0f, 100.0f, 1000.0f};
            ratio = P10[op->coarse & 3u] * (1.0f + (float)op->fine / 100.0f * 9.772f);
            inc[o] = (uint32_t)(ratio * 97391.55f);
        } else {
            ratio = (op->coarse ? (float)op->coarse : 0.5f) * (1.0f + (float)op->fine / 100.0f);
            if (!car && mrat)
                ratio *= fm_exp2f((float)mrat / 12.0f);
            ratio *= 1.0f + ((float)op->detune - 7.0f) * 0.0005f + (car ? (float)(o & 1u ? 1 : -1) * dtun * 0.00004f : 0.0f);
            inc[o] = (uint32_t)((float)m->inc * ratio);
        }
        /* the envelope: towards L1, L2, L3 (sustain) while the key is held, L4 after */
        if (!v->gate && s[o].st < 3u)
            s[o].st = 3;
        target = (float)op->l[s[o].st];
        rate = fm6_rate((int32_t)op->r[s[o].st] + (car ? 0 : meg / 2));
        lv = s[o].env * 99.0f;
        if (lv < target) {
            lv += rate * blk;
            if (lv >= target) {
                lv = target;
                if (s[o].st < 2u)
                    s[o].st++;
            }
        } else if (lv > target) {
            lv -= rate * blk;
            if (lv <= target) {
                lv = target;
                if (s[o].st < 2u)
                    s[o].st++;
            }
        } else if (s[o].st < 2u) {
            s[o].st++;
        }
        dl[o] = s[o].env;
        s[o].env = lv / 99.0f;
        {   /* the output level: the patch's, the macros for the modulators, velocity */
            int32_t ol = op->level + (car ? 0 : mlvl);
            float vs = 1.0f - (float)op->vel / 7.0f * (1.0f - vel);
            if (!car)
                vs *= (1.0f - vmod) + vmod * vel * 1.5f;
            lvl[o] = fm6_amp(clamp(ol, 0, 99)) * vs * (car ? 1.0f : bright);
        }
    }
    {   /* per block: who modulates whom as lists, the operators that sound, their envelopes as ramps (Q15 scaled) */
        fm6_blk_t bk;
        float buf[CTL];
        bk.fbo = (uint8_t)(al->fb ? al->fb - 1u : 6u);
        bk.car = al->car;
        bk.fbk = 0.5f * fb;
        for (o = 0; o < 6u; o++) {
            uint32_t j;
            bk.nm[o] = 0;
            for (j = o + 1u; j < 6u; j++)    /* (a modulator is always a higher operator: computed first) */
                if (al->mod[o] >> j & 1u)
                    bk.mi[o][bk.nm[o]++] = (uint8_t)j;
            bk.e[o] = dl[o] * lvl[o] * (1.0f / 32768.0f);
            bk.de[o] = (s[o].env - dl[o]) * lvl[o] * (1.0f / 32768.0f) / (float)n;
            bk.inc[o] = inc[o];
            bk.act[o] = lvl[o] > 0.0f && (dl[o] > 0.0f || s[o].env > 0.0f) && !(fm6_mute[part] >> o & 1u);
        }
        fm6_ops_far(&bk, s, buf, n > CTL ? CTL : n);   /* (through a pointer: RAM is out of a direct call's reach) */
        for (i = 0; i < n && i < CTL; i++) {
            f = buf[i] * 14000.0f;
            out[i] += voice_amp((int32_t)fm_clampf(f, -32767.0f, 32767.0f), m, i);
        }
    }
}

static const char *const N_FM6_SLOT[] = {"F1", "F2", "F3", "F4", "F5", "F6", "F7", "F8", "OWN", "B1", "B2", "B3", "B4",
                                         "B5", "B6", "B7", "B8", 0};
static const char *const N_FM6_ALG[] = {"PAT", 0};

static const preset_t FM6_PRESETS[] = {
    /*                 ALG FB MLVL MRAT MEG VMOD DTUN SLOT */
    {.name = "TINE EP", .e = {0, 0, 0, 0, 0, 40, 10, 0}, .env = {0, 64, 127, 64}},
    {.name = "BELL", .e = {0, 0, 0, 0, 0, 30, 8, 1}, .env = {0, 64, 127, 64}},
    {.name = "FM BASS", .e = {0, 0, 0, 0, 0, 50, 0, 2}, .env = {0, 64, 127, 64}},
    {.name = "BRASS", .e = {0, 0, 0, 0, 0, 40, 12, 3}, .env = {0, 64, 127, 64}},
    {.name = "PAD", .e = {0, 0, 0, 0, 0, 10, 30, 4}, .env = {0, 64, 127, 64}},
    {.name = "MARIMBA", .e = {0, 0, 0, 0, 0, 50, 0, 5}, .env = {0, 64, 127, 64}},
    {.name = "ORGAN", .e = {0, 0, 0, 0, 0, 0, 6, 6}, .env = {0, 64, 127, 64}},
    {.name = "PLUCK", .e = {0, 0, 0, 0, 0, 60, 4, 7}, .env = {0, 64, 127, 64}},
};

static const engine_t ENG_FM6 = {
    .name = "FM6",
    .page_title = {"OPS", "PATCH"},
    .edit = {{"ALG", F_INT, 0, 32, 0, 0, 0}, {"FB", F_INT, -7, 7, 0, 0, 0},
             {"MLVL", F_INT, -50, 50, 0, 0, 0}, {"MRAT", F_INT, -8, 8, 0, 0, 0},
             {"MEG", F_INT, -64, 63, 0, 0, 0}, {"VMOD", F_PCT, 0, 127, 40, 0, 0},
             {"DTUN", F_PCT, 0, 127, 0, 0, 0}, {"SLOT", F_INT, 0, 40, 0, 0, 0}},
    .presets = FM6_PRESETS,
    .npresets = NELEM(FM6_PRESETS),
    .knob = {P_E2, P_E3, P_E5, P_E7},
    .poly = 4,                           /* (the heaviest engine: four voices, the oldest taken by a fifth) */
    .note_on = fm6_note_on,
    .render = fm6_render,
    .amp = fm6_amp_hook,
};

/* ------------------------------------------------- DX7 SysEx, the bank --- */
/* a DX7 voice (VCED, 155 bytes; its operators OP6 first) -> a patch */
static void fm6_from_vced(fm6_patch_t *p, const uint8_t *d)
{
    uint32_t o, k;
    for (o = 0; o < 6u; o++) {
        const uint8_t *s = d + (5u - o) * 21u;   /* OP6 first in the data */
        fm6_op_t *op = &p->op[o];
        for (k = 0; k < 4u; k++) {
            op->r[k] = (uint8_t)(s[k] > 99 ? 99 : s[k]);
            op->l[k] = (uint8_t)(s[4 + k] > 99 ? 99 : s[4 + k]);
        }
        op->vel = (uint8_t)(s[15] & 7u);
        op->level = (uint8_t)(s[16] > 99 ? 99 : s[16]);
        op->mode = (uint8_t)(s[17] & 1u);
        op->coarse = (uint8_t)(s[18] & 31u);
        op->fine = (uint8_t)(s[19] > 99 ? 99 : s[19]);
        op->detune = (uint8_t)(s[20] > 14 ? 14 : s[20]);
    }
    p->alg = (uint8_t)(d[134] & 31u);
    p->fb = (uint8_t)(d[135] & 7u);
    for (k = 0; k < 10u; k++)
        p->name[k] = (char)(d[145 + k] >= 32 && d[145 + k] < 127 ? d[145 + k] : ' ');
}

/* a packed voice of a 32-voice bank (VMEM, 128 bytes) -> a patch */
static void fm6_from_vmem(fm6_patch_t *p, const uint8_t *d)
{
    uint32_t o, k;
    for (o = 0; o < 6u; o++) {
        const uint8_t *s = d + (5u - o) * 17u;
        fm6_op_t *op = &p->op[o];
        for (k = 0; k < 4u; k++) {
            op->r[k] = (uint8_t)(s[k] > 99 ? 99 : s[k]);
            op->l[k] = (uint8_t)(s[4 + k] > 99 ? 99 : s[4 + k]);
        }
        op->vel = (uint8_t)((s[13] >> 2) & 7u);
        op->level = (uint8_t)(s[14] > 99 ? 99 : s[14]);
        op->mode = (uint8_t)(s[15] & 1u);
        op->coarse = (uint8_t)((s[15] >> 1) & 31u);
        op->fine = (uint8_t)(s[16] > 99 ? 99 : s[16]);
        op->detune = (uint8_t)((s[12] >> 3) & 15u);
        if (op->detune > 14u)
            op->detune = 14;
    }
    p->alg = (uint8_t)(d[110] & 31u);
    p->fb = (uint8_t)(d[111] & 7u);
    for (k = 0; k < 10u; k++)
        p->name[k] = (char)(d[118 + k] >= 32 && d[118 + k] < 127 ? d[118 + k] : ' ');
}
