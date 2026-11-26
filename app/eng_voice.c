/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 nullwave contributors */
/* VOICE: a formant voice (sung vowels, choirs, a talkbox), in the engine interface (felucca_engine.h), written for
 * nullwave. A glottal buzz (a saw softened by BUZZ) and breath
 * (BRTH: noise) through three resonant band-passes at a vowel's formants, gliding from VOWL to VOWL2 in TALK, moved
 * by SHIFT (semitones), their width Q; RAND a little wander of the pitch and the formants.
 * The formants: average adult values of the vowels (Peterson and Barney, 1952; Hillenbrand et al., 1995).
 * State: s[0] the softened buzz, s[1..6] the three filters (low, band, as float bits), s[7] the noise; ph[0] the
 * buzz, ph[1] the glide from VOWL to VOWL2 (Q15). Floating point: the FM-1's core has a single-precision FPU. */
static const char *const N_VOWEL[] = {"A", "E", "I", "O", "U", "AE", "UH", "OO", 0};
static const uint16_t VOWEL_F[8][3] = {
    {730, 1090, 2440}, {530, 1840, 2480}, {270, 2290, 3010}, {570, 840, 2410},
    {300, 870, 2240}, {660, 1720, 2410}, {640, 1190, 2390}, {440, 1020, 2240},
};

static void voice_note_on(track_t *t, voice_t *v)
{
    (void)t;
    memset(v->s, 0, 7 * sizeof v->s[0]);   /* (0.0f is all zero bits) */
    v->s[7] = (int32_t)(v->age * 2246822519u) | 1;
    v->ph[1] = 0;
}

static void voice_render(track_t *t, voice_t *v, int32_t *out, uint32_t n, const vmod_t *m)
{
    uint32_t i, k, va = (uint32_t)clamp(t->p[P_E0], 0, 7), vb = (uint32_t)clamp(t->p[P_E1], 0, 7);
    int32_t talk = clamp(t->p[P_E2], 0, 127), shift = clamp(t->p[P_E3], -12, 12);
    int32_t buzz = clamp(t->p[P_E4], 0, 127), brth = clamp(t->p[P_E5], 0, 127), q = clamp(t->p[P_E6], 0, 127);
    int32_t rnd = clamp(t->p[P_E7], 0, 127);
    float g = (float)v->ph[1] / 32767.0f, f[3], fa[3], damp, sh = fm_exp2f((float)shift / 12.0f);
    float wander = rnd ? (float)((int32_t)(noise32(&v->s[7]) >> 20) - 2048) / 2048.0f * (float)rnd / 2500.0f : 0.0f;
    float lp = 0.05f + (float)buzz / 140.0f;
    static const float GAIN[3] = {1.0f, 0.6f, 0.35f};
    uint32_t inc = (uint32_t)((float)m->inc * (1.0f + wander * 0.3f));
    /* the glide towards VOWL2: TALK 0 at once, 127 about 2 s */
    if (va != vb) {
        uint32_t blocks = (uint32_t)(1 + talk * talk * 2 / 3);   /* x CTL samples */
        v->ph[1] = (uint32_t)clamp((int32_t)v->ph[1] + (int32_t)(32767u / blocks), 0, 32767);
    }
    damp = 0.35f - (float)q * 0.0026f;   /* the band-passes' damping: Q 127 narrow */
    for (k = 0; k < 3u; k++) {
        f[k] = ((float)VOWEL_F[va][k] * (1.0f - g) + (float)VOWEL_F[vb][k] * g) * sh * (1.0f + wander);
        f[k] = fm_clampf(f[k] * fm_exp2f((float)(m->cutoff >> 8) / 96.0f), 80.0f, 6500.0f);
        fa[k] = 2.0f * fm_sin_pi(f[k] / (float)FS);
    }
    for (i = 0; i < n; i++) {
        float src, sum = 0.0f;
        int32_t saw;
        v->ph[0] += inc;
        saw = (int32_t)(v->ph[0] >> 16) - 32768;
        v->s[0] += (int32_t)(((float)(saw - v->s[0])) * lp);   /* (s[0]: the softened buzz, then the first filter) */
        src = (float)v->s[0] / 32768.0f;
        if (brth)
            src += ((float)((int32_t)(noise32(&v->s[7]) >> 16) - 32768) / 32768.0f) * (float)brth / 160.0f;
        for (k = 0; k < 3u; k++) {       /* the formants: Chamberlin band-passes, state in s[1..6] (float bits) */
            int32_t *sl = &v->s[1 + 2 * k], *sb = &v->s[2 + 2 * k];
            float bl, bb, hp;
            memcpy(&bl, sl, sizeof bl);
            memcpy(&bb, sb, sizeof bb);
            hp = src - bl - damp * bb;
            bb += fa[k] * hp;
            bl += fa[k] * bb;
            if (bl > 8.0f || bl < -8.0f || bb > 8.0f || bb < -8.0f)
                bl = bb = 0.0f;
            memcpy(sl, &bl, sizeof bl);
            memcpy(sb, &bb, sizeof bb);
            sum += bb * GAIN[k];
        }
        out[i] += voice_amp((int32_t)fm_clampf(sum * 9000.0f, -32767.0f, 32767.0f), m, i);
    }
}

static const preset_t VOICE_PRESETS[] = {
    /*                    VOWL VOWL2 TALK SHIFT BUZZ BRTH Q RAND */
    {.name = "CHOIR AAH", .e = {0, 0, 0, 0, 40, 22, 60, 30}, .env = {60, 80, 110, 80}},
    {.name = "VOX LEAD", .e = {3, 0, 40, 0, 90, 8, 80, 10}, .env = {4, 70, 110, 40}},
    {.name = "WOW BASS", .e = {4, 0, 30, -5, 110, 0, 90, 0}, .env = {0, 60, 100, 30}},
    {.name = "WHISPER", .e = {7, 1, 60, 2, 0, 127, 50, 40}, .env = {30, 80, 110, 70}},
};

static const engine_t ENG_VOICE = {
    .name = "VOICE",
    .page_title = {"VOWL", "TONE"},
    .edit = {{"VOWL", F_ENUM, 0, 7, 0, N_VOWEL, 0}, {"VOWL2", F_ENUM, 0, 7, 0, N_VOWEL, 0},
             {"TALK", F_TIME, 0, 127, 30, 0, 0}, {"SHIFT", F_INT, -12, 12, 0, 0, "st"},
             {"BUZZ", F_PCT, 0, 127, 60, 0, 0}, {"BRTH", F_PCT, 0, 127, 20, 0, 0},
             {"Q", F_PCT, 0, 127, 60, 0, 0}, {"RAND", F_PCT, 0, 127, 10, 0, 0}},
    .presets = VOICE_PRESETS,
    .npresets = NELEM(VOICE_PRESETS),
    .knob = {P_E0, P_E1, P_E2, P_E3},
    .poly = 4,
    .note_on = voice_note_on,
    .render = voice_render,
};
