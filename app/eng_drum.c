/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 nullwave contributors */
/* DRUM: an 8-lane drum kit on the General MIDI key map, an engine in Felucca's interface (felucca_engine.h), written
 * for nullwave (the lanes, the kits and what each kit sounds like; written from scratch, none of
 * its code). The lanes and their GM notes:
 *   KICK 36  SNARE 38  CLAP 39  HATCL 42  HATOP 46  TOM 45  RIM 37  BELL 56
 * (the other GM drum notes go to the nearest lane: 35 kick, 40 snare, 44 hat, 41 / 43 / 47 / 48 / 50 tom, 49 / 51 /
 * 55 / 57 bell). The keys of the FM-1 play the GM notes (drum_key): C4 the kick (36), C#4 RIM, D4
 * SNARE, D#4 CLAP, E4 SNARE, F#4 HATCL, G#4 the pedal hat, A#4 HATOP, the toms on F G A B C5 D5 (pitched by the note),
 * the cymbals C#5 D#5 and up on BELL; F3..B3 are F4..B4 again; the octave buttons leave a kit alone.
 * KIT picks the kit: STD, the standard kit, then five in the manner of classic analogue drum machines, 80, 10, 66, 55, 77, and three put together
 * from their pieces, DEEP, TIGHT, PERC; every lane is its own synthesised voice (a sine with a falling pitch, square
 * "metal", noise through a filter, or a mix). TUNE, DECAY, TONE, DRIVE, CRUSH, ACCENT and LEVEL shape the whole kit,
 * LANES and LANES 2 (the sound's ext[0..7]) each lane's level. A hit plays out (no note-off); the closed hat chokes the
 * open one.
 *
 * State: s[0] the amplitude envelope (Q30), s[1] the pitch envelope (Q30), s[2] the noise, s[3] s[4] the filter
 * (low, band), s[5] the lane (low byte), KS_CHOKED, s[6] a one-pole's state, s[7] the held sample (CRUSH);
 * ph[0..2] the oscillators. */
enum { DL_KICK, DL_SNARE, DL_CLAP, DL_HATCL, DL_HATOP, DL_TOM, DL_RIM, DL_BELL, DL_N };
#define KS_CHOKED (1 << 16)              /* s[5]: the open hat choked by a closed one */
enum { KT_DRUM, KT_SNARE, KT_CLAP, KT_METAL, KT_BELL, KT_NOISE };
static const uint8_t DRUM_GM[DL_N] = {36, 38, 39, 42, 46, 45, 37, 56};
static const char *const DRUM_LANE[DL_N] = {"KICK", "SNARE", "CLAP", "HATCL", "HATOP", "TOM", "RIM", "BELL"};
typedef struct {
    uint8_t type;
    uint16_t f0, f1;       /* Hz: the pitch it settles at, and where it starts (the drop) */
    uint16_t pdec, adec;   /* ms: the pitch drop's and the level's decay */
    uint16_t fc;           /* Hz: the filter (noise, metal) */
    uint8_t noise;         /* 0..127: the noise in it */
    uint8_t level;         /* 0..127 */
} dvoice_t;
#define NKITS 9
static const char *const N_KIT[NKITS + 1] = {"STD", "80", "10", "66", "55", "77", "DEEP", "TIGHT", "PERC", 0};
static const dvoice_t DKIT[NKITS][DL_N] = {
    {   /* STD, the standard kit (first in the KIT list): an all-round kit, a punchy kick, a crisp snare */
        {KT_DRUM, 52, 170, 35, 420, 0, 0, 127}, {KT_SNARE, 195, 270, 16, 180, 2400, 105, 108},
        {KT_CLAP, 0, 0, 0, 220, 1200, 127, 105}, {KT_METAL, 0, 0, 0, 40, 8200, 70, 82},
        {KT_METAL, 0, 0, 0, 360, 7800, 70, 82}, {KT_DRUM, 115, 175, 50, 280, 0, 6, 104},
        {KT_DRUM, 1750, 2200, 3, 24, 2500, 35, 90}, {KT_BELL, 560, 830, 0, 260, 950, 0, 78}},
    {   /* 80: a deep sine kick with a long decay, a noisy snare, six-square hats, a handclap, a cowbell */
        {KT_DRUM, 48, 130, 45, 700, 0, 0, 127}, {KT_SNARE, 180, 240, 20, 200, 1800, 110, 105},
        {KT_CLAP, 0, 0, 0, 170, 1100, 127, 105}, {KT_METAL, 0, 0, 0, 50, 7200, 40, 80},
        {KT_METAL, 0, 0, 0, 420, 6800, 40, 80}, {KT_DRUM, 110, 165, 60, 300, 0, 6, 105},
        {KT_DRUM, 1700, 2100, 4, 26, 2400, 30, 90}, {KT_BELL, 540, 800, 0, 280, 900, 0, 80}},
    {   /* 10: a swept kick, a white-noise snare, long-tailed claps, a cymbal */
        {KT_DRUM, 52, 260, 22, 380, 0, 0, 127}, {KT_SNARE, 200, 260, 15, 170, 2600, 127, 105},
        {KT_CLAP, 0, 0, 0, 420, 1250, 127, 110}, {KT_METAL, 0, 0, 0, 40, 8000, 90, 80},
        {KT_METAL, 0, 0, 0, 380, 7500, 90, 80}, {KT_DRUM, 120, 200, 45, 260, 0, 10, 100},
        {KT_DRUM, 1800, 2400, 3, 22, 2600, 40, 90}, {KT_METAL, 0, 0, 0, 1400, 5200, 70, 70}},
    {   /* 66: a soft round kick, a bright snare, noise hats, a conga */
        {KT_DRUM, 60, 100, 30, 260, 0, 0, 120}, {KT_SNARE, 230, 320, 12, 140, 3200, 90, 105},
        {KT_CLAP, 0, 0, 0, 150, 1400, 127, 100}, {KT_NOISE, 0, 0, 0, 35, 8500, 127, 75},
        {KT_NOISE, 0, 0, 0, 300, 8000, 127, 75}, {KT_DRUM, 320, 370, 25, 190, 0, 0, 100},
        {KT_DRUM, 1600, 2000, 4, 24, 2300, 30, 90}, {KT_BELL, 560, 830, 0, 220, 950, 0, 75}},
    {   /* 55: a dropping kick, high metal hats, a metal bell */
        {KT_DRUM, 45, 190, 70, 420, 0, 0, 127}, {KT_SNARE, 190, 250, 18, 160, 2100, 100, 105},
        {KT_CLAP, 0, 0, 0, 180, 1150, 127, 100}, {KT_METAL, 0, 0, 0, 35, 9500, 25, 80},
        {KT_METAL, 0, 0, 0, 330, 9000, 25, 80}, {KT_DRUM, 100, 160, 65, 320, 0, 4, 105},
        {KT_DRUM, 1700, 2200, 4, 26, 2400, 30, 90}, {KT_BELL, 820, 1230, 0, 600, 1500, 0, 75}},
    {   /* 77: a swelling kick, multi-burst claps, claves, a cymbal */
        {KT_DRUM, 50, 90, 90, 520, 0, 0, 127}, {KT_SNARE, 185, 250, 20, 180, 2000, 100, 105},
        {KT_CLAP, 0, 0, 0, 260, 1150, 127, 115}, {KT_METAL, 0, 0, 0, 45, 7500, 60, 80},
        {KT_METAL, 0, 0, 0, 400, 7000, 60, 80}, {KT_DRUM, 115, 180, 50, 280, 0, 6, 100},
        {KT_DRUM, 2500, 2600, 2, 34, 0, 0, 95}, {KT_METAL, 0, 0, 0, 1500, 5000, 60, 70}},
    {   /* DEEP: a low swelling kick, a long cymbal */
        {KT_DRUM, 40, 80, 110, 900, 0, 0, 127}, {KT_SNARE, 170, 230, 22, 220, 1700, 110, 100},
        {KT_CLAP, 0, 0, 0, 260, 1100, 127, 105}, {KT_METAL, 0, 0, 0, 55, 7000, 40, 75},
        {KT_METAL, 0, 0, 0, 450, 6600, 40, 75}, {KT_DRUM, 95, 150, 70, 380, 0, 6, 105},
        {KT_DRUM, 1700, 2100, 4, 26, 2400, 30, 85}, {KT_METAL, 0, 0, 0, 2400, 4800, 70, 70}},
    {   /* TIGHT: short and dry */
        {KT_DRUM, 58, 170, 25, 180, 0, 0, 127}, {KT_SNARE, 210, 280, 10, 90, 2600, 100, 105},
        {KT_CLAP, 0, 0, 0, 90, 1300, 127, 100}, {KT_METAL, 0, 0, 0, 25, 9000, 60, 80},
        {KT_METAL, 0, 0, 0, 160, 8500, 60, 80}, {KT_DRUM, 130, 190, 30, 150, 0, 6, 100},
        {KT_DRUM, 1800, 2300, 3, 18, 2500, 30, 90}, {KT_BELL, 560, 830, 0, 120, 950, 0, 75}},
    {   /* PERC: a conga, claves, a bell */
        {KT_DRUM, 55, 120, 35, 320, 0, 0, 120}, {KT_SNARE, 220, 300, 12, 130, 3000, 80, 100},
        {KT_CLAP, 0, 0, 0, 160, 1300, 127, 100}, {KT_NOISE, 0, 0, 0, 60, 7000, 127, 75},
        {KT_NOISE, 0, 0, 0, 260, 6800, 127, 75}, {KT_DRUM, 330, 380, 25, 200, 0, 0, 105},
        {KT_DRUM, 2500, 2600, 2, 34, 0, 0, 95}, {KT_BELL, 540, 800, 0, 300, 900, 0, 80}},
};

static int drum_lane(uint32_t note)      /* a GM note -> its lane, -1 none */
{
    uint32_t i;
    for (i = 0; i < DL_N; i++)
        if (DRUM_GM[i] == note)
            return (int)i;
    switch (note) {
    case 35: return DL_KICK;
    case 40: return DL_SNARE;
    case 44: return DL_HATCL;
    case 41: case 43: case 47: case 48: case 50: return DL_TOM;
    case 49: case 51: case 52: case 53: case 55: case 57: case 59: return DL_BELL;
    case 54: case 69: case 70: return DL_HATCL;
    case 60: case 61: case 62: case 63: case 64: return DL_TOM;
    case 75: case 76: case 77: return DL_RIM;
    default: return -1;
    }
}

#define DRUM_INC(hz) ((uint32_t)((float)(hz) * 97391.55f))     /* a phase increment, 2^32 / 44100 a Hz */
static uint32_t drum_dq(float ms)                               /* a decay's step: Q20 of 1 / samples */
{
    float n = ms * (float)FS / 1000.0f;
    return (uint32_t)(1048576.0f / (n < 1.0f ? 1.0f : n));
}

static void drum_note_on(track_t *t, voice_t *v)
{
    uint32_t k;
    int lane = drum_lane(v->note);
    v->s[0] = lane < 0 ? 0 : 1 << 30;
    v->s[1] = 1 << 30;
    v->s[2] = (int32_t)(0x9E3779B9u ^ (v->age * 2654435761u)) | 1;
    v->s[3] = v->s[4] = v->s[6] = v->s[7] = 0;
    v->s[5] = lane < 0 ? 0 : lane;
    v->ph[0] = v->ph[1] = v->ph[2] = 0;
    if (lane == DL_HATCL)                /* the closed hat chokes the open one */
        for (k = 0; k < NVOICE; k++)
            if (&t->v[k] != v && t->v[k].active && (uint32_t)(t->v[k].s[5] & 0xFF) == DL_HATOP)
                t->v[k].s[5] |= KS_CHOKED;
}

/* the level: the hit's own envelope (the host's ADSR stays at full); the voice ends when it has died away */
static int32_t drum_amp(track_t *t, voice_t *v, int32_t adsr)
{
    (void)t;
    (void)adsr;
    if (v->s[0] < (1 << 30) / 1000) {
        v->active = 0;
        v->stage = 0;
        return 0;
    }
    return 32767;
}

static void drum_render(track_t *t, voice_t *v, int32_t *out, uint32_t n, const vmod_t *m)
{
    uint32_t i, lane = (uint32_t)(v->s[5] & 0xFF) % DL_N, kit = (uint32_t)clamp(t->p[P_E0], 0, NKITS - 1);
    const dvoice_t *dv = &DKIT[kit][lane];
    uint32_t type = dv->type;
    float tune = fm_exp2f(((float)clamp(t->p[P_E1], -24, 24)
                          + (lane == DL_TOM && v->note >= 41u && v->note <= 50u ? ((float)v->note - 45.0f) * 1.75f : 0.0f))
                          / 12.0f);   /* (the toms: each GM tom note its own pitch, low floor 41 .. high 50) */
    float dec = fm_exp2f((float)(clamp(t->p[P_E2], 0, 127) - 64) / 32.0f);
    int32_t tone = clamp(t->p[P_E3], 0, 127), drive = clamp(t->p[P_E4], 0, 127), crush = clamp(t->p[P_E5], 0, 127);
    int32_t accent = clamp(t->p[P_E6], 0, 127), punch = 40 + accent / 2;
    int32_t lvl = clamp(t->p[P_E7], 0, 127) * dv->level * (t->ext[lane] && !(t->lmute >> lane & 1u) ? t->ext[lane] : 0) / 100;   /* Q14 */
    float adec = (float)dv->adec * dec;
    uint32_t adq, pdq, inc0, inc1, inc2, f_end, f_start;
    int32_t fa, fq, noise_g, tone_g, lp_a;
    if (v->s[5] & KS_CHOKED)             /* choked: gone in ~8 ms */
        adec = 8.0f;
    adq = drum_dq(adec / 3.0f);          /* (the table's times: about -26 dB; the voice ends at -60 dB) */
    pdq = drum_dq((float)dv->pdec * (0.5f + (float)punch / 127.0f));
    f_end = DRUM_INC((float)dv->f0 * tune);
    f_start = DRUM_INC((float)dv->f1 * tune * (0.7f + (float)punch / 180.0f));
    {   /* the filter: Chamberlin, f = 2 sin(pi fc / FS) (Q15), held below FS / 6.5; TONE moves it two octaves */
        float fc = (float)dv->fc * fm_exp2f((float)(tone - 64) / 32.0f) * (type == KT_METAL ? 1.0f : tune);
        fc = fm_clampf(fc, 40.0f, (float)FS / 6.5f);
        fa = (int32_t)(2.0f * fm_sin_pi(fc / (float)FS) * 32767.0f);
        fq = type == KT_BELL ? 9000 : 22000;   /* damping Q15: the bell rings */
    }
    noise_g = (dv->noise * 96) >> 5;     /* Q8 of the noise's level */
    tone_g = 256 - (dv->noise >> 1);     /* the tone gives way to the noise */
    lp_a = 9000 + tone * 160;            /* the hats' one-pole (Q15) */
    inc0 = DRUM_INC(205.3f * tune);       /* (the metal's partials) */
    inc1 = DRUM_INC(540.0f * tune);
    inc2 = DRUM_INC(800.0f * tune);
    for (i = 0; i < n; i++) {
        int32_t env = v->s[0] >> 15, x = 0, w;      /* env Q15 */
        uint32_t r;
        switch (type) {
        case KT_DRUM: {                             /* a sine falling from f_start to f_end, a click at the start */
            uint32_t pe = (uint32_t)(v->s[1] >> 15);   /* Q15 */
            v->ph[0] += f_end + ((f_start - f_end) >> 15) * pe;
            x = sine_i(v->ph[0]);
            if (dv->noise) {
                r = noise32(&v->s[2]);
                x += ((int32_t)(r >> 16) - 32768) * dv->noise >> 9;
            }
            if (v->s[1] > (1 << 30) - (1 << 26))    /* the click: the first ~1.5 ms */
                x += (punch * 120) * ((v->ph[0] >> 31) ? -1 : 1);
            break;
        }
        case KT_SNARE: {                            /* two tones and band-passed noise */
            uint32_t pe = (uint32_t)(v->s[1] >> 15);
            uint32_t inc = f_end + ((f_start - f_end) >> 15) * pe;
            int32_t nse, hp;
            v->ph[0] += inc;
            v->ph[1] += inc + (inc >> 1) + (inc >> 3);
            r = noise32(&v->s[2]);
            nse = (int32_t)(r >> 16) - 32768;
            hp = nse - v->s[3] - (fq * v->s[4] >> 15);
            v->s[4] += fa * hp >> 15;
            v->s[3] += fa * v->s[4] >> 15;
            x = ((sine_i(v->ph[0]) + (sine_i(v->ph[1]) >> 1)) * (v->s[1] >> 16) >> 14) * tone_g >> 8;
            x += (v->s[4] + (hp >> 2)) * noise_g >> 8;
            break;
        }
        case KT_CLAP: {                             /* three quick bursts, then the tail */
            uint32_t age = v->ph[2]++;
            int32_t nse, g;
            r = noise32(&v->s[2]);
            nse = (int32_t)(r >> 16) - 32768;
            nse = nse - v->s[3] - (fq * v->s[4] >> 15);
            v->s[4] += fa * nse >> 15;
            v->s[3] += fa * v->s[4] >> 15;
            g = age < 1323u ? (int32_t)(32767 - (int32_t)(age % 441u) * 74) : 32767;   /* 10 ms bursts */
            x = (v->s[4] * 2) * g >> 15;
            x = x * noise_g >> 8;
            break;
        }
        case KT_METAL: {                            /* three square partials and noise, high-passed */
            int32_t sq, nse;
            v->ph[0] += inc0;
            v->ph[1] += inc1;
            v->ph[2] += inc2;
            sq = (int32_t)((v->ph[0] >> 31) + (v->ph[1] >> 31) + (v->ph[2] >> 31)) * 16384 - 24576;
            r = noise32(&v->s[2]);
            nse = (int32_t)(r >> 16) - 32768;
            w = (sq * (127 - (dv->noise >> 1)) >> 7) + (nse * noise_g >> 8);
            v->s[6] += (w - v->s[6]) * (32767 - lp_a) >> 15;   /* the high-pass: the input less its low-pass */
            x = w - v->s[6];
            break;
        }
        case KT_BELL: {                             /* two squares through a resonant band-pass */
            int32_t sq, bp;
            v->ph[0] += f_end;
            v->ph[1] += f_start;
            sq = (int32_t)((v->ph[0] >> 31) + (v->ph[1] >> 31)) * 16384 - 16384;
            bp = sq - v->s[3] - (fq * v->s[4] >> 15);
            v->s[4] += fa * bp >> 15;
            v->s[3] += fa * v->s[4] >> 15;
            x = v->s[4];
            break;
        }
        default: {                                  /* KT_NOISE: high-passed noise with a soft start */
            int32_t nse;
            uint32_t age = v->ph[2]++;
            r = noise32(&v->s[2]);
            nse = (int32_t)(r >> 16) - 32768;
            v->s[6] += (nse - v->s[6]) * (32767 - lp_a) >> 15;
            x = (nse - v->s[6]) * noise_g >> 8;
            if (age < 660u)                         /* 15 ms */
                x = x * (int32_t)age / 660;
            break;
        }
        }
        x = x * env >> 15;
        if (drive)                                  /* drive: more gain into the soft clip */
            x = softclip(x * (16 + drive) >> 4);
        else
            x = clamp(x, -32767, 32767);
        if (crush) {                                /* fewer bits, a held sample */
            uint32_t hold = 1u + (uint32_t)crush / 16u, bits = 16u - (uint32_t)crush / 13u;
            if ((v->age + i) % hold == 0u)
                v->s[7] = (x >> (16u - bits)) << (16u - bits);
            x = v->s[7];
        }
        x = x * (lvl >> 7) >> 7;
        out[i] += voice_amp(x, m, i);
        v->s[0] -= ((v->s[0] >> 20) + 1) * (int32_t)adq;   /* (+1: the tail ends too) */
        v->s[1] -= ((v->s[1] >> 20) + 1) * (int32_t)pdq;
        if (v->s[0] < 0)
            v->s[0] = 0;
        if (v->s[1] < 0)
            v->s[1] = 0;
    }
}

/* the FM-1's keys on a DRUM track: the white keys play the lanes, F3 KICK .. C4 BELL, again from D4 */
static int32_t drum_key(const track_t *t, uint32_t k)
{
    int32_t note = 53 + (int32_t)k - 24; /* C4 the kick (GM 36), whatever the octave; F3..B3 up an octave (41..47) */
    (void)t;
    if (k >= 27u)
        return -1;
    return note < 35 ? note + 12 : note;
}

static const preset_t DRUM_PRESETS[] = {
    /*                 KIT TUNE DEC TONE DRIVE CRUSH ACC LEVEL */
    {.name = "DRUM KIT", .e = {0, 0, 64, 64, 0, 0, 60, 110}, .env = {0, 64, 127, 64}},
    {.name = "DEEP KIT", .e = {6, -2, 72, 50, 10, 0, 60, 110}, .env = {0, 64, 127, 64}},
    {.name = "TIGHT KIT", .e = {7, 2, 50, 80, 0, 0, 70, 112}, .env = {0, 64, 127, 64}},
    {.name = "PERC KIT", .e = {8, 0, 64, 70, 0, 0, 60, 110}, .env = {0, 64, 127, 64}},
};

static const engine_t ENG_DRUM = {
    .name = "DRUM",
    .page_title = {"KIT", "KIT 2"},
    .edit = {{"KIT", F_ENUM, 0, NKITS - 1, 0, N_KIT, 0}, {"TUNE", F_INT, -24, 24, 0, 0, "st"},
             {"DECAY", F_INT, 0, 127, 64, 0, 0}, {"TONE", F_INT, 0, 127, 64, 0, 0},
             {"DRIVE", F_INT, 0, 127, 0, 0, 0}, {"CRUSH", F_INT, 0, 127, 0, 0, 0},
             {"ACCENT", F_INT, 0, 127, 60, 0, 0}, {"LEVEL", F_INT, 0, 127, 110, 0, 0}},
    .presets = DRUM_PRESETS,
    .npresets = NELEM(DRUM_PRESETS),
    .knob = {P_E0, P_E1, P_E2, P_E3},
    .oneshot = 1,
    .note_on = drum_note_on,
    .render = drum_render,
    .amp = drum_amp,
    .keys = drum_key,
};
