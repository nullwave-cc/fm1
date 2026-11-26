/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 nullwave contributors */
/* SUPER: stacks of detuned oscillators through a 24 dB low-pass, in the engine interface (felucca_engine.h), written for
 * nullwave. WAVE picks the stack: SAW, SQR, TRI (seven oscillators, spread by DTN, the outer six at MIX against the
 * middle one), or a single shape that the shape modulation moves (a mod slot's SHP target, the LFO's SHP, ENV's SHP):
 * PULSE (its width), SYNC (a saw synced to the note, its ratio) and MORPH (saw, then square, then triangle). SHAPE sets
 * where that sits. DRIFT lets each oscillator wander a little, slowly, as old hardware does; SUB adds a square an
 * octave down. CUT and RES are the filter (four one-pole stages, the last fed back), CUT moved by the filter's
 * modulation (ENV's and the slots' CUT).
 * The oscillators are integer phases (the outer ones without band-limiting: they are detuned and mixed under the
 * middle one, which is band-limited, polyBLEP); the filter is floating point. State: sup[][] (beside the voice),
 * its phases, the filter's four stages, each oscillator's drift. */
#define SUP_N 7
static const char *const N_SUPER_WAVE[] = {"SAW", "SQR", "TRI", "PULSE", "SYNC", "MORPH", 0};
static const float SUP_SPREAD[SUP_N] = {0.0f, -1.0f, 1.0f, -0.62f, 0.62f, -0.31f, 0.31f};   /* each one's share of DTN */
static struct {
    uint32_t ph[SUP_N], sub, sync;
    float y[4], drift[SUP_N];
    int32_t rnd;
} sup[NPART][NVOICE];

static void super_note_on(track_t *t, voice_t *v)
{
    uint32_t ch = (uint32_t)(t - trk), k = (uint32_t)(v - t->v), i;
    sup[ch][k].rnd = (int32_t)(v->age * 2654435761u) | 1;
    for (i = 0; i < SUP_N; i++)          /* (free phases: a stack that starts the same each note sounds dull) */
        sup[ch][k].ph[i] = noise32(&sup[ch][k].rnd);
    for (i = 0; i < 4u; i++)
        sup[ch][k].y[i] = 0.0f;
}

static inline float sup_blep(float t, float dt)   /* the polyBLEP residual at phase t (0..1), step dt */
{
    if (t < dt) {
        t /= dt;
        return t + t - t * t - 1.0f;
    }
    if (t > 1.0f - dt) {
        t = (t - 1.0f) / dt;
        return t * t + t + t + 1.0f;
    }
    return 0.0f;
}

static void super_render(track_t *t, voice_t *v, int32_t *out, uint32_t n, const vmod_t *m)
{
    uint32_t ch = (uint32_t)(t - trk), k = (uint32_t)(v - t->v), i, j;
    int32_t wave = clamp(t->p[P_E0], 0, 5), dtn = clamp(t->p[P_E1], 0, 127), mixp = clamp(t->p[P_E2], 0, 127);
    int32_t shp = clamp(t->p[P_E3] + ((m->shape - (64 << 8)) >> 8), 0, 127);   /* (SHAPE, moved by the modulation) */
    int32_t cut = clamp(t->p[P_E4] * 256 + m->cutoff, 0, 127 << 8), res = clamp(t->p[P_E5], 0, 127);
    int32_t drift = clamp(t->p[P_E6], 0, 127), subl = clamp(t->p[P_E7], 0, 127);
    uint32_t inc[SUP_N], nosc = wave <= 2 ? SUP_N : 3u, sinc = m->inc >> 1, w;
    float side = (float)mixp / 127.0f, norm, fc, g, fb = (float)res / 127.0f * 3.6f, dt = (float)m->inc / 4294967296.0f;
    float sh = (float)shp / 127.0f, subg = (float)subl / 127.0f * 0.7f;
    for (j = 0; j < nosc; j++) {         /* each one's pitch: its share of DTN (up to ~ +-60 cents), its drift */
        float d = sup[ch][k].drift[j];
        if (drift) {                     /* (a slow random walk, a block at a time, pulled back to the middle) */
            d += ((float)((int32_t)(noise32(&sup[ch][k].rnd) >> 20) - 2048) / 2048.0f) * 0.02f - d * 0.01f;
            sup[ch][k].drift[j] = d;
        }
        inc[j] = (uint32_t)((float)m->inc * fm_exp2f((SUP_SPREAD[j] * (float)dtn * 0.6f + d * (float)drift * 0.12f)
                                                      / 1200.0f));
    }
    norm = 1.0f / (1.0f + side * (float)(nosc - 1u) * 0.55f);
    fc = 30.0f * fm_exp2f((float)cut / (127.0f * 256.0f) * 9.4f);   /* 30 Hz .. ~20 kHz */
    g = fm_minf(0.95f, 6.2831853f * fc / (float)FS);
    w = (uint32_t)(0.05f + sh * 0.9f > 0.95f ? 0.95f * 4294967295.0f : (0.05f + sh * 0.9f) * 4294967295.0f);
    for (i = 0; i < n; i++) {
        float x = 0.0f, y;
        for (j = 0; j < nosc; j++) {
            uint32_t p = sup[ch][k].ph[j] += inc[j];
            float o, tt = (float)p / 4294967296.0f;
            switch (wave) {
            case 1: o = p < 0x80000000u ? 1.0f : -1.0f; break;                          /* SQR */
            case 2: o = tt < 0.5f ? tt * 4.0f - 1.0f : 3.0f - tt * 4.0f; break;          /* TRI */
            case 3: o = p < w ? 1.0f : -1.0f; break;                                    /* PULSE: SHAPE the width */
            case 4:                                                                     /* SYNC: a saw reset each cycle */
                if (j == 0u) {
                    sup[ch][k].sync += (uint32_t)((float)inc[0] * (1.0f + sh * 3.0f));
                    if (p < inc[0])      /* (the note's cycle begins again: so does the synced saw) */
                        sup[ch][k].sync = 0;
                    o = (float)sup[ch][k].sync / 2147483648.0f - 1.0f;
                } else
                    o = tt * 2.0f - 1.0f;
                break;
            case 5: {                                                                   /* MORPH: saw, square, tri */
                float saw = tt * 2.0f - 1.0f, sq = p < 0x80000000u ? 1.0f : -1.0f;
                float tri = tt < 0.5f ? tt * 4.0f - 1.0f : 3.0f - tt * 4.0f;
                o = sh < 0.5f ? saw + (sq - saw) * sh * 2.0f : sq + (tri - sq) * (sh - 0.5f) * 2.0f;
                break;
            }
            default:                                                                    /* SAW: the middle band-limited */
                o = tt * 2.0f - 1.0f;
                if (j == 0u)
                    o -= sup_blep(tt, dt);
                break;
            }
            x += j ? o * side : o;
        }
        x *= norm;
        if (subg > 0.0f) {               /* SUB: a square an octave down */
            sup[ch][k].sub += sinc;
            x += (sup[ch][k].sub < 0x80000000u ? subg : -subg);
        }
        /* the filter: four one-pole stages, the last fed back (RES), softly clipped */
        x -= fb * sup[ch][k].y[3];
        x = fm_tanhf(x);
        sup[ch][k].y[0] += g * (x - sup[ch][k].y[0]);
        sup[ch][k].y[1] += g * (sup[ch][k].y[0] - sup[ch][k].y[1]);
        sup[ch][k].y[2] += g * (sup[ch][k].y[1] - sup[ch][k].y[2]);
        sup[ch][k].y[3] += g * (sup[ch][k].y[2] - sup[ch][k].y[3]);
        y = sup[ch][k].y[3] * (1.0f + fb * 0.6f);
        out[i] += voice_amp((int32_t)fm_clampf(y * 26000.0f, -32767.0f, 32767.0f), m, i);
    }
}

static const preset_t SUPER_PRESETS[] = {
    /*                     WAVE DTN MIX SHAPE CUT RES DRIFT SUB */
    {.name = "SUPERSAW", .e = {0, 60, 90, 64, 100, 20, 20, 0}, .env = {6, 70, 110, 60}, FX(0, 30, 25, 50)},
    {.name = "HOOVER", .e = {0, 110, 127, 64, 85, 40, 40, 60}, .env = {10, 70, 100, 50}, .mono = 1},
    {.name = "SQR PAD", .e = {1, 40, 80, 64, 70, 10, 30, 0}, .env = {70, 80, 110, 90}, FX(0, 50, 20, 70)},
    {.name = "PWM PAD", .e = {3, 20, 60, 40, 80, 10, 25, 0}, .env = {60, 80, 110, 90}, .fenv = 0, FX(0, 40, 20, 70)},
    {.name = "SYNC LEAD", .e = {4, 10, 30, 60, 110, 20, 10, 30}, .env = {2, 70, 100, 40}, .mono = 1},
    {.name = "MORPH KEYS", .e = {5, 15, 40, 30, 90, 15, 15, 0}, .env = {2, 60, 70, 40}},
    {.name = "TRI BASS", .e = {2, 8, 30, 64, 70, 30, 5, 80}, .env = {0, 60, 90, 20}, .mono = 1},
};

static const engine_t ENG_SUPER = {
    .name = "SUPER",
    .page_title = {"STACK", "FILTER"},
    .edit = {{"WAVE", F_ENUM, 0, 5, 0, N_SUPER_WAVE, 0}, {"DTN", F_PCT, 0, 127, 60, 0, 0},
             {"MIX", F_PCT, 0, 127, 90, 0, 0}, {"SHAPE", F_PCT, 0, 127, 64, 0, 0},
             {"CUT", F_PCT, 0, 127, 100, 0, 0}, {"RES", F_PCT, 0, 127, 20, 0, 0},
             {"DRIFT", F_PCT, 0, 127, 20, 0, 0}, {"SUB", F_PCT, 0, 127, 0, 0, 0}},
    .presets = SUPER_PRESETS,
    .npresets = NELEM(SUPER_PRESETS),
    .knob = {P_E0, P_E1, P_E2, P_E3},
    .poly = 4,                           /* (seven oscillators a voice: four voices at most) */
    .note_on = super_note_on,
    .render = super_render,
};
