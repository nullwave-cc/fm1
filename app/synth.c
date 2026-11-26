/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 nullwave contributors */
/* nullwave's sound (synth.h): the host of the six Felucca-Engines engines (engines/felucca/src, MIT, Leo Kuroshita,
 * Hügelton Instruments) and nullwave's own engines: FM6 (eng_fm6.c), SAMPLE, GRAIN, SLICE (eng_sample.c), VOICE
 * (eng_voice.c), PHYS (eng_phys.c), DRUM (eng_drum.c): thirteen in all.
 * One translation unit with them, as felucca_engine.h asks: our definitions, the header, the tables (fe_tables.h, from
 * engines/felucca/tools/gen_engine_tables.py at 44.1 kHz), dsp.c, the engines.
 *
 * Per channel: a track_t (the engine's parameters and NVOICE voices), the voices' ADSR (once per CTL block), the
 * velocity's loudness, the envelope's amount on the cutoff, then the channel's filter, its level and pan, and its
 * sends. The render works in CTL blocks: the queue's notes, then each channel's voices into its buffer, the channels
 * panned into the mix and sent to the delay and the reverb, those added, the master and a soft limit. */
#include <stdint.h>
#include <stddef.h>
#include "fastmath.h"
#define memset __builtin_memset        /* (no string.h for the FM-1: the chip's own memset / memcpy, bsp chip/libc.c) */
#define memcpy __builtin_memcpy
#include "synth.h"
#ifndef PLAT_RAM_CODE                    /* (plat.h's, which this unit does not include: a hot loop run from RAM) */
#ifdef PLAT_HOST
#define PLAT_RAM_CODE
#else
#define PLAT_RAM_CODE __attribute__((section(".app_ram")))
#endif
#endif
#ifdef PLAT_HOST
#define PLAT_POOL
#else
#define PLAT_POOL __attribute__((section(".pool")))
#endif

/* ------------------------------------------------- the engines' host part --- */
#define NVOICE 6
#define NPART NCH
#include "fe_tables.h"
#include "felucca_engine.h"

enum { P_ATK, P_REL, P_FTYPE, P_E0, P_E1, P_E2, P_E3, P_E4, P_E5, P_E6, P_E7, P_DEC, P_SUS, P_COUNT };
_Static_assert(P_COUNT == SYN_NPARAM, "synth.h SYN_NPARAM");
typedef struct track {
    int16_t p[P_COUNT];
    voice_t v[NVOICE];
    uint8_t ext[16];                     /* the sound's ext: an engine's extras (DRUM: its lanes' levels) */
    uint8_t lmute;                       /* DRUM: the lanes muted (EDIT held, a black key): they play on, unheard */
} track_t;
static track_t trk[NPART];

#include "dsp.c"
#include "eng_analog.c"
#include "eng_lofi.c"
#include "eng_trio.c"
#include "eng_wheel.c"
#include "eng_noise.c"
#include "eng_phase.c"
#include "eng_fm6.c"
#include "eng_sample.c"
#include "eng_voice.c"
#include "eng_phys.c"
#include "eng_drum.c"
#include "eng_super.c"

/* the fourteen engines, by number (kept in songs and presets: a new one goes at the end); PRESETS lists them in
 * KIND_ORDER */
static const engine_t *const ENGINES[] = {&ENG_ANALOG, &ENG_FM6, &ENG_PHASE, &ENG_LOFI, &ENG_SAMPLE, &ENG_VOICE,
                                          &ENG_TRIO, &ENG_WHEEL, &ENG_GRAIN, &ENG_PHYS, &ENG_NOISE, &ENG_SLICE,
                                          &ENG_DRUM, &ENG_SUPER};
enum { E_ANALOG, E_FM6, E_PHASE, E_LOFI, E_SAMPLE, E_VOICE, E_TRIO, E_WHEEL, E_GRAIN, E_PHYS, E_NOISE, E_SLICE, E_DRUM,
       E_SUPER };
#define NENG ((uint32_t)NELEM(ENGINES))

/* ------------------------------------------------------------- the state --- */
static sound_t snd[NCH];                 /* the main loop's: what each channel plays */
static int16_t ovr[NCH][SP_N];           /* the sequencer's locks (OVR_NONE: none) */
#define OVR_NONE 0x7FFF
static uint8_t run_eng[NCH];             /* the render's: the engine each track runs (a change resets it) */
static int32_t last_amp[NCH][NVOICE];    /* each voice's amplitude at the end of the last block (the ramp) */
static uint8_t prs_to[NCH][NVOICE];      /* each held voice's pressure, as it came in */
static int32_t prs_now[NCH][NVOICE];     /* and glided to, a block at a time (x 256) */
static uint8_t prs_mode = PRS_BOTH;
static uint32_t vage;

/* the envelope's times, per parameter value: attack's step a tick (Q24), decay's / release's share a tick (Q15) */
static int32_t atk_step[128];
static int32_t fall_k[128];              /* Q15 */
static int32_t vel_gain[128];            /* Q15, the loudness of a velocity */

/* notes from the main loop and the sequencer to the render */
#define QN 128u
static struct { uint8_t on, ch, note, vel; } q[QN];   /* on: 0 off, 1 on, 2 pressure (vel), 3 channel off */
static volatile uint32_t q_w, q_r;
static volatile uint8_t all_off, silence;
static int16_t shed_q[NPART][NVOICE];     /* a voice being shed: its fade, Q15 (32767: not) */
static volatile uint32_t budget_sheds;    /* voices the budget faded (the screen says VOICE LIMIT) */
uint32_t synth_budget_sheds(void) { return budget_sheds; }

/* --------------------------------------------------------------- tables --- */
static void times_init(void)
{
    uint32_t p;
    for (p = 0; p < 128u; p++) {
        float x = (float)p / 127.0f;
        float ms = 1.0f + 4000.0f * x * x * x;                  /* 1 ms .. 4 s, most of the range below a second */
        float ticks = ms * (float)FS / 1000.0f / (float)CTL;
        if (ticks < 1.0f)
            ticks = 1.0f;
        atk_step[p] = (int32_t)((float)(1 << 24) / ticks);
        fall_k[p] = (int32_t)(32768.0f * fm_minf(1.0f, 4.6f / ticks));   /* ~1 % left after the time */
        if (fall_k[p] < 1)
            fall_k[p] = 1;
    }
}

void synth_vel_sens(int32_t sens)
{
    /* a floor (a soft press is quieter, never near silent), then the curve: + lifts the soft presses */
    float gamma = 1.0f - 0.13f * (float)clamp(sens, -5, 5), floor = 0.25f;
    uint32_t v;
    vel_gain[0] = 0;
    for (v = 1; v < 128u; v++) {
        float g = floor + (1.0f - floor) * fm_powf((float)v / 127.0f, gamma);
        vel_gain[v] = (int32_t)(g * 32767.0f);
    }
}

/* ---------------------------------------------------------- the sounds --- */
static const param_desc_t D_ATK = {"ATK", F_TIME, 0, 127, 0, 0, 0};
static const param_desc_t D_REL = {"REL", F_TIME, 0, 127, 20, 0, 0};
static const param_desc_t D_DEC = {"DEC", F_TIME, 0, 127, 40, 0, 0};
static const param_desc_t D_SUS = {"SUS", F_PCT, 0, 127, 100, 0, 0};
static const char *const N_FTYPE[] = {"LP", "BP", "HP"};
static const param_desc_t D_FTYPE = {"TYPE", F_ENUM, 0, 2, 0, N_FTYPE, 0};
/* the sound's other values: where they are in sound_t and what they are */
static const char *const N_VCE[] = {"POLY", "MONO", "LEG", "UNI"};
static const char *const N_FLT[] = {"OFF", "LP", "HP", "BP"};
static const char *const N_INS[] = {"OFF", "SOFT", "HARD", "FOLD", "FUZZ", "CRUSH", "PHASR", "FLANG", "CHOR"};
static const char *const N_SLC[] = {"OFF", "GATE", "STUT"};
static const char *const N_SLCR[] = {"1/8", "1/16", "1/32", "8T", "16T", "32T"};
static const char *const N_WAVE[] = {"SIN", "TRI", "SAW", "SQR", "S&H", "RND"};   /* RND: a smooth random */
static const char *const N_LSYNC[] = {"OFF", "4BAR", "2BAR", "1BAR", "1/2", "1/4", "1/8", "8T", "1/16", "16T", "1/32"};
static const char *const N_TRIG[] = {"NOTE", "FREE"};
static const char *const N_POL[] = {"BI", "UNI"};
static const char *const N_GLMOD[] = {"RATE", "TIME"};
static const char *const N_PRIO[] = {"LAST", "LOW", "HIGH"};
static const char *const N_ALLOC[] = {"ROT", "REUSE"};
static const char *const N_ARP[] = {"OFF", "UP", "DN", "UPDN", "RND", "ORD", "REPEAT", "DNUP", "UP+8", "CONV", "DIVG",
                                    "PINKY", "THUMB", "WALK", "CHORD"};
static const char *const N_ARATE[] = {"1/4", "1/8", "1/8T", "1/16", "1/16T", "1/32"};
static const char *const N_ORD[] = {"NOTE", "PLAY"};
static const char *const N_OFFON[] = {"OFF", "ON"};
#define SOFF(f) ((uint16_t)offsetof(sound_t, f))
static const struct { uint16_t off; uint8_t sgn; param_desc_t d; } SPX[SP_X0 - SP_FENV] = {
    [SP_FENV - SP_FENV] = {SOFF(fenv), 1, {"FLT", F_INT, -64, 63, 0, 0, 0}},
    [SP_MONO - SP_FENV] = {SOFF(mono), 0, {"VCE", F_ENUM, 0, 3, 0, N_VCE, 0}},
    [SP_FLT - SP_FENV] = {SOFF(flt), 0, {"FILTER", F_ENUM, 0, 3, 0, N_FLT, 0}},
    [SP_CUT - SP_FENV] = {SOFF(cut), 0, {"CUT", F_INT, 0, 127, 127, 0, 0}},
    [SP_RES - SP_FENV] = {SOFF(res), 0, {"RES", F_INT, 0, 127, 0, 0, 0}},
    [SP_DLY - SP_FENV] = {SOFF(dly), 0, {"DLY", F_INT, 0, 127, 0, 0, 0}},
    [SP_REV - SP_FENV] = {SOFF(rev), 0, {"REV", F_INT, 0, 127, 0, 0, 0}},
    [SP_PAN - SP_FENV] = {SOFF(pan), 1, {"PAN", F_INT, -64, 63, 0, 0, 0}},
    [SP_DST - SP_FENV] = {SOFF(dst), 0, {"DST", F_INT, 0, 127, 0, 0, 0}},
    [SP_CHO - SP_FENV] = {SOFF(cho), 0, {"CHO", F_INT, 0, 127, 0, 0, 0}},
    [SP_INS - SP_FENV] = {SOFF(ins), 0, {"INSRT", F_ENUM, 0, 8, 0, N_INS, 0}},
    [SP_INS_A - SP_FENV] = {SOFF(ins_a), 0, {"A", F_INT, 0, 127, 64, 0, 0}},
    [SP_INS_B - SP_FENV] = {SOFF(ins_b), 0, {"B", F_INT, 0, 127, 64, 0, 0}},
    [SP_INS_C - SP_FENV] = {SOFF(ins_c), 0, {"C", F_INT, 0, 127, 64, 0, 0}},
    [SP_INS_MIX - SP_FENV] = {SOFF(ins_mix), 0, {"MIX", F_PCT, 0, 127, 127, 0, 0}},
    [SP_SLC - SP_FENV] = {SOFF(slc), 0, {"SLCR", F_ENUM, 0, 2, 0, N_SLC, 0}},
    [SP_SLC_PAT - SP_FENV] = {SOFF(slc_pat), 0, {"PAT", F_INT, 1, 16, 1, 0, 0}},
    [SP_SLC_RATE - SP_FENV] = {SOFF(slc_rate), 0, {"RATE", F_ENUM, 0, 5, 1, N_SLCR, 0}},
    [SP_SLC_DEPTH - SP_FENV] = {SOFF(slc_depth), 0, {"DEPTH", F_PCT, 0, 127, 127, 0, 0}},
    [SP_LFO_RATE - SP_FENV] = {SOFF(lfo_rate), 0, {"RATE", F_INT, 0, 127, 40, 0, 0}},
    [SP_LFO_WAVE - SP_FENV] = {SOFF(lfo_wave), 0, {"WAVE", F_ENUM, 0, 5, 0, N_WAVE, 0}},
    [SP_LFO_PHASE - SP_FENV] = {SOFF(lfo_phase), 0, {"PHS", F_INT, 0, 127, 0, 0, 0}},
    [SP_LFO_FADE - SP_FENV] = {SOFF(lfo_fade), 0, {"FADE", F_TIME, 0, 127, 0, 0, 0}},
    [SP_LFO_SYNC - SP_FENV] = {SOFF(lfo_sync), 0, {"SYNC", F_ENUM, 0, 10, 0, N_LSYNC, 0}},
    [SP_LFO_TRIG - SP_FENV] = {SOFF(lfo_trig), 0, {"TRIG", F_ENUM, 0, 1, 0, N_TRIG, 0}},
    [SP_LFO_POL - SP_FENV] = {SOFF(lfo_pol), 0, {"POL", F_ENUM, 0, 1, 0, N_POL, 0}},
    [SP_LFO_PIT - SP_FENV] = {SOFF(lfo_pit), 1, {"PIT", F_INT, -64, 63, 0, 0, 0}},
    [SP_LFO_FLT - SP_FENV] = {SOFF(lfo_flt), 1, {"FLT", F_INT, -64, 63, 0, 0, 0}},
    [SP_LFO_SHP - SP_FENV] = {SOFF(lfo_shp), 1, {"SHP", F_INT, -64, 63, 0, 0, 0}},
    [SP_LFO_AMP - SP_FENV] = {SOFF(lfo_amp), 1, {"AMP", F_INT, -64, 63, 0, 0, 0}},
    [SP_PENV - SP_FENV] = {SOFF(penv), 1, {"PIT", F_INT, -64, 63, 0, 0, 0}},
    [SP_SENV - SP_FENV] = {SOFF(senv), 1, {"SHP", F_INT, -64, 63, 0, 0, 0}},
    [SP_GLIDE - SP_FENV] = {SOFF(glide), 0, {"GLD", F_TIME, 0, 127, 0, 0, 0}},
    [SP_GLMOD - SP_FENV] = {SOFF(glmod), 0, {"GLMOD", F_ENUM, 0, 1, 0, N_GLMOD, 0}},
    [SP_PRIO - SP_FENV] = {SOFF(prio), 0, {"PRIO", F_ENUM, 0, 2, 0, N_PRIO, 0}},
    [SP_ALLOC - SP_FENV] = {SOFF(alloc), 0, {"ALLOC", F_ENUM, 0, 1, 0, N_ALLOC, 0}},
    [SP_DTUNE - SP_FENV] = {SOFF(dtune), 0, {"DTUNE", F_INT, 0, 127, 20, 0, 0}},
    [SP_SPREAD - SP_FENV] = {SOFF(spread), 0, {"SPRD", F_PCT, 0, 127, 0, 0, 0}},
    [SP_VMUTE - SP_FENV] = {SOFF(vmute), 0, {"MUTE", F_ENUM, 0, 1, 0, N_OFFON, 0}},
    [SP_ARP - SP_FENV] = {SOFF(arp), 0, {"MODE", F_ENUM, 0, 14, 0, N_ARP, 0}},
    [SP_ARP_RATE - SP_FENV] = {SOFF(arp_rate), 0, {"RATE", F_ENUM, 0, 5, 3, N_ARATE, 0}},
    [SP_ARP_OCT - SP_FENV] = {SOFF(arp_oct), 0, {"OCT", F_INT, 1, 4, 1, 0, 0}},
    [SP_ARP_GATE - SP_FENV] = {SOFF(arp_gate), 0, {"GATE", F_PCT, 1, 127, 64, 0, 0}},
    [SP_ARP_SWING - SP_FENV] = {SOFF(arp_swing), 0, {"SWG", F_INT, 0, 100, 0, 0, 0}},
    [SP_ARP_PROB - SP_FENV] = {SOFF(arp_prob), 0, {"PROB", F_PCT, 0, 127, 127, 0, 0}},
    [SP_ARP_HOLD - SP_FENV] = {SOFF(arp_hold), 0, {"HOLD", F_ENUM, 0, 1, 0, N_OFFON, 0}},
    [SP_ARP_ORDER - SP_FENV] = {SOFF(arp_order), 0, {"ORD", F_ENUM, 0, 1, 0, N_ORD, 0}},
    [SP_TFILT - SP_FENV] = {SOFF(tfilt), 1, {"FILT", F_INT, -64, 63, 0, 0, 0}},
};
static const param_desc_t D_LANE[8] = {
    {"KICK", F_INT, 0, 127, 100, 0, 0}, {"SNARE", F_INT, 0, 127, 100, 0, 0}, {"CLAP", F_INT, 0, 127, 100, 0, 0},
    {"HATCL", F_INT, 0, 127, 100, 0, 0}, {"HATOP", F_INT, 0, 127, 100, 0, 0}, {"TOM", F_INT, 0, 127, 100, 0, 0},
    {"RIM", F_INT, 0, 127, 100, 0, 0}, {"BELL", F_INT, 0, 127, 100, 0, 0}};
static const param_desc_t D_NOX = {"-", F_INT, 0, 0, 0, 0, 0};
/* the channel's modulation (synth_mod_t): ENV 2 and the four slots */
static const char *const N_MSRC[MS_N] = {"OFF", "ENV", "ENV2", "LFO", "VEL", "PRS", "RND"};
static const char *const N_MDST[MT_N] = {"OFF", "CUT", "PIT", "SHP", "AMP", "PAN", "LFO"};
static const param_desc_t D_E2[4] = {{"ATK", F_TIME, 0, 127, 0, 0, 0}, {"DEC", F_TIME, 0, 127, 40, 0, 0},
                                     {"SUS", F_PCT, 0, 127, 0, 0, 0}, {"REL", F_TIME, 0, 127, 20, 0, 0}};
static const param_desc_t D_MSRC = {"SRC", F_ENUM, 0, MS_N - 1, 0, N_MSRC, 0};
static const param_desc_t D_MDST = {"DEST", F_ENUM, 0, MT_N - 1, 0, N_MDST, 0};
static const param_desc_t D_MAMT = {"AMT", F_INT, -64, 63, 0, 0, 0};
static synth_mod_t smod[NCH];            /* each channel's (the main loop's; the render reads it, as snd) */
void synth_default_mod(synth_mod_t *m)
{
    memset(m, 0, sizeof *m);
    m->ver = SYN_MOD_VER;
    m->e2[0] = 0;
    m->e2[1] = 40;
    m->e2[2] = 0;
    m->e2[3] = 20;
    m->m[0].src = MS_ENV2;               /* (slot 1 ready as a filter envelope: turn its AMT) */
    m->m[0].dst = MT_CUT;
}
int synth_mod_ok(const synth_mod_t *m)
{
    uint32_t k;
    if (m->ver != SYN_MOD_VER)
        return 0;
    for (k = 0; k < 4u; k++)
        if (m->e2[k] > 127u)
            return 0;
    for (k = 0; k < SYN_MODS; k++)
        if (m->m[k].src >= MS_N || m->m[k].dst >= MT_N || m->m[k].amt < -64)
            return 0;
    return 1;
}
void synth_set_mod(uint32_t ch, const synth_mod_t *m)
{
    synth_mod_t d;
    if (!synth_mod_ok(m)) {
        synth_default_mod(&d);
        m = &d;
    }
    smod[ch % NCH] = *m;
}
const synth_mod_t *synth_mod(uint32_t ch) { return &smod[ch % NCH]; }

/* the default sounds: a lead, keys, chords, an organ and a pad, then the drum kit last (channel 6, named DRUMS) */
static const uint8_t DEF_ENG[NCH] = {E_ANALOG, E_FM6, E_SAMPLE, E_WHEEL, E_PHASE, E_DRUM};

static void sound_from_preset(sound_t *s, uint32_t engine, uint32_t preset)
{
    const engine_t *e = ENGINES[engine];
    const preset_t *pr = &e->presets[preset];
    uint32_t k;
    memset(s, 0, sizeof *s);
    s->engine = (uint8_t)engine;
    s->preset = (uint8_t)preset;
    s->fenv = pr->fenv;
    s->mono = pr->mono;
    for (k = 0; k < 8u; k++)
        s->p[P_E0 + k] = pr->e[k];
    s->p[P_ATK] = pr->env[0];
    s->p[P_DEC] = pr->env[1];
    s->p[P_SUS] = pr->env[2];
    s->p[P_REL] = pr->env[3];
    s->p[P_FTYPE] = 0;
    s->flt = 0;
    s->cut = 127;
    s->res = 0;
    s->dst = pr->fx[0] ? (uint8_t)clamp(pr->fx[0] - 1, 0, 127) : 0;
    s->cho = pr->fx[1] ? (uint8_t)clamp(pr->fx[1] - 1, 0, 127) : 0;
    s->dly = pr->fx[2] ? (uint8_t)clamp(pr->fx[2] - 1, 0, 127) : 0;
    s->rev = pr->fx[3] ? (uint8_t)clamp(pr->fx[3] - 1, 0, 127) : (e->oneshot ? 8 : 0);
    s->pan = 0;
    for (k = 0; k < SP_X0 - SP_FENV; k++)   /* the rest at their defaults (fenv and mono came from the preset) */
        if (k + SP_FENV != SP_FENV && k + SP_FENV != SP_MONO && k + SP_FENV != SP_DST && k + SP_FENV != SP_CHO
            && k + SP_FENV != SP_DLY && k + SP_FENV != SP_REV && k + SP_FENV != SP_PAN)
            ((uint8_t *)s)[SPX[k].off] = (uint8_t)SPX[k].d.def;
    if (engine == E_DRUM)
        for (k = 0; k < 8u; k++)
            s->ext[k] = 100;
}

static const param_desc_t *desc_of(uint32_t ch, uint32_t pi)
{
    const engine_t *e = ENGINES[snd[ch].engine];
    switch (pi) {
    case SP_ATK: return &D_ATK;
    case SP_REL: return &D_REL;
    case SP_DEC: return &D_DEC;
    case SP_SUS: return &D_SUS;
    case SP_FTYPE: return &D_FTYPE;
    default: break;
    }
    if (pi >= SP_E2_ATK && pi < SP_N)    /* the channel's modulation */
        return pi < SP_M1_SRC ? &D_E2[pi - SP_E2_ATK] : (pi - SP_M1_SRC) % 3u == 0u ? &D_MSRC
             : (pi - SP_M1_SRC) % 3u == 1u ? &D_MDST : &D_MAMT;
    if (pi >= SP_X0)                     /* an engine's extras: DRUM's lanes */
        return snd[ch].engine == E_DRUM && pi - SP_X0 < 8u ? &D_LANE[pi - SP_X0] : &D_NOX;
    if (pi >= SP_FENV)
        return &SPX[pi - SP_FENV].d;
    if (e->desc) {
        const param_desc_t *d = e->desc(&trk[ch], pi - P_E0);
        if (d)
            return d;
    }
    return &e->edit[pi - P_E0];
}

int synth_sound_ok(const sound_t *s)
{
    uint32_t k;
    if (s->engine >= NENG || s->preset >= ENGINES[s->engine]->npresets)
        return 0;
    for (k = 0; k < P_COUNT; k++)
        if (s->p[k] < -128 || s->p[k] > 127)
            return 0;
    for (k = 0; k < SP_X0 - SP_FENV; k++) {   /* every other value within its range */
        int32_t v = SPX[k].sgn ? (int8_t)((const uint8_t *)s)[SPX[k].off] : ((const uint8_t *)s)[SPX[k].off];
        if (v < SPX[k].d.min || v > SPX[k].d.max)
            return 0;
    }
    return 1;
}

const sound_t *synth_sound(uint32_t ch) { return &snd[ch % NCH]; }

static char sname[NCH][SYN_NAME + 1];    /* a channel's own name (a user preset's, the editor's), "" the preset's */
static void sound_put(uint32_t ch, const sound_t *s)
{
    sname[ch][0] = 0;                    /* (a new sound: its preset's name, until a user preset names it) */
    synth_ch_off(ch);                    /* (the render resets the track when the engine changed) */
    snd[ch] = *s;
    if (ENGINES[s->engine]->oneshot)
        snd[ch].mono = 0;
}

void synth_default_sound(uint32_t ch, sound_t *s) { sound_from_preset(s, DEF_ENG[ch % NCH], 0); }

void synth_set_sound(uint32_t ch, const sound_t *s)
{
    sound_t d;
    ch %= NCH;
    if (s && synth_sound_ok(s)) {
        sound_put(ch, s);
        return;
    }
    synth_default_sound(ch, &d);
    sound_put(ch, &d);
}

void synth_engine(uint32_t ch, uint32_t engine)
{
    sound_t s;
    sound_from_preset(&s, engine % NENG, 0);
    sound_put(ch % NCH, &s);
}

void synth_preset(uint32_t ch, uint32_t preset)
{
    sound_t s;
    ch %= NCH;
    sound_from_preset(&s, snd[ch].engine, preset % ENGINES[snd[ch].engine]->npresets);
    sound_put(ch, &s);
}

uint32_t synth_nengines(void) { return NENG; }
uint32_t synth_npresets(uint32_t ch) { return ENGINES[snd[ch % NCH].engine]->npresets; }
const char *synth_engine_name(uint32_t engine) { return ENGINES[engine % NENG]->name; }
const char *synth_preset_name(uint32_t ch)
{
    const sound_t *s = &snd[ch % NCH];
    return sname[ch % NCH][0] ? sname[ch % NCH] : ENGINES[s->engine]->presets[s->preset].name;
}
const char *synth_own_name(uint32_t ch) { return sname[ch % NCH]; }   /* "": the preset's */
/* the channel's sound named (a user preset, a preset from the editor): capitals, digits and - . / + # & kept, the
 * rest a space, SYN_NAME at most; 0 or "" its preset's name again */
void synth_set_name(uint32_t ch, const char *nm, uint32_t len)
{
    uint32_t i, k = 0;
    ch %= NCH;
    for (i = 0; nm && i < len && nm[i] && k < SYN_NAME; i++) {
        char c = nm[i];
        if (c >= 'a' && c <= 'z')
            c = (char)(c - 32);
        if (!((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' || c == '.' || c == '/' || c == '+' ||
              c == '#' || c == '&'))
            c = ' ';
        sname[ch][k++] = c;
    }
    while (k && sname[ch][k - 1] == ' ')
        k--;
    sname[ch][k] = 0;
}
const char *synth_page_title(uint32_t ch, uint32_t page) { return ENGINES[snd[ch % NCH].engine]->page_title[page & 1u]; }
int synth_is_kit(uint32_t ch) { return ENGINES[snd[ch % NCH].engine]->oneshot; }
const char *synth_kit_sound(uint32_t i) { return DRUM_LANE[i % DL_N]; }
uint32_t synth_drum_note(uint32_t lane) { return DRUM_GM[lane % DL_N]; }
/* the note key k (0 = F3) plays when the engine maps the keys itself (DRUM, SLICE), else -1 */
int32_t synth_key_note(uint32_t ch, uint32_t k)
{
    const engine_t *e = ENGINES[snd[ch % NCH].engine];
    return e->keys ? e->keys(&trk[ch % NCH], k) : -1;
}

/* PRESETS: every engine's sounds in one list, engine by engine */
static const uint8_t KIND_ORDER[NENG] = {E_ANALOG, E_FM6, E_PHASE, E_LOFI, E_SAMPLE, E_VOICE, E_TRIO, E_WHEEL,
                                        E_GRAIN, E_PHYS, E_NOISE, E_SLICE, E_DRUM, E_SUPER};   /* (a new engine after the
                                        * rest: the favourites, marked by their place in the list, stay theirs) */
uint32_t synth_nsounds(void)
{
    uint32_t e, n = 0;
    for (e = 0; e < NENG; e++)
        n += ENGINES[e]->npresets;
    return n;
}
uint32_t synth_sound_index(uint32_t ch)
{
    uint32_t i, n = 0;
    const sound_t *s = &snd[ch % NCH];
    for (i = 0; i < NENG; i++) {
        if (KIND_ORDER[i] == s->engine)
            return n + s->preset;
        n += ENGINES[KIND_ORDER[i]]->npresets;
    }
    return 0;
}
void synth_sound_pick(uint32_t ch, uint32_t index)
{
    uint32_t i;
    index %= synth_nsounds();
    for (i = 0; i < NENG; i++) {
        uint32_t np = ENGINES[KIND_ORDER[i]]->npresets;
        if (index < np) {
            sound_t s;
            sound_from_preset(&s, KIND_ORDER[i], index);
            sound_put(ch % NCH, &s);
            return;
        }
        index -= np;
    }
}

/* ------------------------------------------------------- the parameters --- */
static int32_t sp_get(const sound_t *s, uint32_t id)
{
    if (id >= SP_E2_ATK)                 /* (the modulation's: not the sound's) */
        return 0;
    if (id < SYN_NPARAM)
        return s->p[id];
    if (id >= SP_X0)
        return id - SP_X0 < 16u ? s->ext[id - SP_X0] : 0;
    {
        uint8_t b = ((const uint8_t *)s)[SPX[id - SP_FENV].off];
        return SPX[id - SP_FENV].sgn ? (int8_t)b : b;
    }
}
static void sp_put(sound_t *s, uint32_t id, int32_t v)
{
    if (id >= SP_E2_ATK)
        return;
    if (id < SYN_NPARAM)
        s->p[id] = (int16_t)v;
    else if (id >= SP_X0) {
        if (id - SP_X0 < 16u)
            s->ext[id - SP_X0] = (uint8_t)v;
    } else
        ((uint8_t *)s)[SPX[id - SP_FENV].off] = (uint8_t)v;
}

static int32_t mod_get(uint32_t ch, uint32_t id)
{
    const synth_mod_t *m = &smod[ch % NCH];
    uint32_t k;
    if (id < SP_M1_SRC)
        return m->e2[id - SP_E2_ATK];
    k = id - SP_M1_SRC;
    return k % 3u == 0u ? m->m[k / 3u].src : k % 3u == 1u ? m->m[k / 3u].dst : m->m[k / 3u].amt;
}
static void mod_put(uint32_t ch, uint32_t id, int32_t v)
{
    synth_mod_t *m = &smod[ch % NCH];
    uint32_t k;
    if (id < SP_M1_SRC) {
        m->e2[id - SP_E2_ATK] = (uint8_t)v;
        return;
    }
    k = id - SP_M1_SRC;
    if (k % 3u == 0u)
        m->m[k / 3u].src = (uint8_t)v;
    else if (k % 3u == 1u)
        m->m[k / 3u].dst = (uint8_t)v;
    else
        m->m[k / 3u].amt = (int8_t)v;
}

int32_t synth_param(uint32_t ch, uint32_t id)
{
    return id >= SP_E2_ATK && id < SP_N ? mod_get(ch, id) : sp_get(&snd[ch % NCH], id);
}
void synth_param_range(uint32_t ch, uint32_t id, int32_t *lo, int32_t *hi)
{
    const param_desc_t *d = desc_of(ch % NCH, id % SP_N);
    *lo = d->min;
    *hi = d->max;
}
void synth_param_set(uint32_t ch, uint32_t id, int32_t v)
{
    int32_t lo, hi;
    ch %= NCH;
    if (id >= SP_N)
        return;
    if (id == SP_MONO && ENGINES[snd[ch].engine]->oneshot)
        return;                          /* (a kit is always poly) */
    if (id >= SP_X0 && desc_of(ch, id) == &D_NOX)
        return;
    synth_param_range(ch, id, &lo, &hi);
    if (id >= SP_E2_ATK)
        mod_put(ch, id, clamp(v, lo, hi));
    else
        sp_put(&snd[ch], id, clamp(v, lo, hi));
}
void synth_param_turn(uint32_t ch, uint32_t id, int32_t d)
{
    int32_t lo, hi;
    synth_param_range(ch, id, &lo, &hi);
    if (hi - lo > 100)
        d *= 2;                          /* a wide range moves two a detent */
    synth_param_set(ch, id, synth_param(ch, id) + d);
}
const char *synth_param_label(uint32_t ch, uint32_t id) { return desc_of(ch % NCH, id % SP_N)->label; }
const char *const *synth_param_names(uint32_t ch, uint32_t id)   /* an enum's labels (min .. max), 0 a number */
{
    const param_desc_t *d = desc_of(ch % NCH, id % SP_N);
    return d->fmt == F_ENUM ? d->names : 0;
}
int32_t synth_param_default(uint32_t ch, uint32_t id) { return desc_of(ch % NCH, id % SP_N)->def; }
uint32_t synth_knob_param(uint32_t ch, uint32_t k) { return ENGINES[snd[ch % NCH].engine]->knob[k % SYN_KNOBS]; }

static void num(int32_t v, char *b)
{
    char t[12];
    int n = 0;
    uint32_t u = (uint32_t)(v < 0 ? -v : v);
    do
        t[n++] = (char)('0' + u % 10u);
    while ((u /= 10u) && n < 11);
    if (v < 0)
        *b++ = '-';
    while (n)
        *b++ = t[--n];
    *b = 0;
}

static void copy7(char *out, const char *s)
{
    uint32_t i;
    for (i = 0; i < 7u && s[i]; i++)
        out[i] = s[i];
    out[i] = 0;
}

void synth_param_text(uint32_t ch, uint32_t id, int32_t v, char *out)
{
    const param_desc_t *ds = desc_of(ch % NCH, id % SP_N);
    if (snd[ch % NCH].engine == E_FM6 && id == SP_E7) {   /* FM6's SLOT, ALG */
        char b[6];
        copy7(out, synth_fm6_slot_name(v, b));
        return;
    }
    if (snd[ch % NCH].engine == E_FM6 && id == SP_E0 && v == 0) {
        copy7(out, "PAT");
        return;
    }
    switch (ds->fmt) {
    case F_ENUM:
        if (ds->names && v >= ds->min && v <= ds->max) {
            copy7(out, ds->names[v - ds->min]);
            return;
        }
        break;
    case F_ONOFF:
        copy7(out, v ? "ON" : "OFF");
        return;
    case F_PCT:
        num(v * 100 / 127, out);
        return;
    case F_BIPCT:
        num(v * 100 / 64, out);
        return;
    default:
        if (ds->names && v >= ds->min && v - ds->min < 9 && ds->names[0]) {   /* (NOISE's LEN names) */
            uint32_t k = 0;
            while (ds->names[k] && (int32_t)k < v - ds->min)
                k++;
            if (ds->names[k] && (int32_t)k == v - ds->min) {
                copy7(out, ds->names[k]);
                return;
            }
        }
        break;
    }
    num(v, out);
}

void synth_lock(uint32_t ch, uint32_t id, int32_t v)
{
    if (id < SP_N && SP_LOCKABLE(id))
        ovr[ch % NCH][id] = (int16_t)v;
}
void synth_unlock(uint32_t ch)
{
    uint32_t i;
    for (i = 0; i < SP_N; i++)
        ovr[ch % NCH][i] = OVR_NONE;
}

/* --------------------------------------------------------------- notes --- */
static void q_put(uint32_t on, uint32_t ch, uint32_t note, uint32_t vel)
{
    uint32_t w = q_w;
    if (w - q_r >= QN)
        return;                          /* full: dropped (128 notes within a block) */
    q[w % QN].on = (uint8_t)on;
    q[w % QN].ch = (uint8_t)(ch % NCH);
    q[w % QN].note = (uint8_t)(note & 127u);
    q[w % QN].vel = (uint8_t)vel;
    q_w = w + 1u;
}

void synth_note_on(uint32_t ch, uint32_t note, uint32_t vel) { q_put(1, ch, note, vel ? vel : 1u); }
void synth_note_off(uint32_t ch, uint32_t note) { q_put(0, ch, note, 0); }
void synth_pressure(uint32_t ch, uint32_t note, uint32_t prs) { q_put(2, ch, note, prs > 127u ? 127u : prs); }
void synth_ch_off(uint32_t ch) { q_put(3, ch, 0, 0); }

void synth_pressure_mode(uint32_t mode) { prs_mode = (uint8_t)(mode % PRS_NMODES); }
const char *synth_pressure_name(uint32_t mode)
{
    static const char *const N[PRS_NMODES] = {"OFF", "FILTER", "VOLUME", "BOTH"};
    return N[mode % PRS_NMODES];
}
void synth_all_off(void) { all_off = 1; }

/* -------------------------------------------------------------- the mix --- */
/* mute and solo: a channel sounds when it is not muted and, while any is soloed, is soloed itself; its gain glides
 * over a block to the new state, so nothing clicks. Each channel's peak, kept by the render for the screen's meters */
static volatile uint8_t mute_mask, solo_mask;
static uint8_t level[NCH];               /* 0..127 */
static int32_t ch_gain[NCH];             /* Q8, the render's (level and mute) */
static uint16_t ch_quiet[NCH];           /* blocks since the channel last had a voice (the render skips it after a while) */
static volatile int32_t ch_peak[NCH];
void synth_mute(uint32_t mask) { mute_mask = (uint8_t)(mask & 0x3Fu); }
void synth_solo(uint32_t mask) { solo_mask = (uint8_t)(mask & 0x3Fu); }
void synth_set_level(uint32_t ch, uint32_t l) { level[ch % NCH] = (uint8_t)(l > 127u ? 127u : l); }
uint32_t synth_get_level(uint32_t ch) { return level[ch % NCH]; }
int synth_audible(uint32_t ch)
{
    uint32_t b = 1u << (ch % NCH);
    return !(mute_mask & b) && (!solo_mask || (solo_mask & b));
}
int32_t synth_peak(uint32_t ch)
{
    int32_t p = ch_peak[ch % NCH];
    ch_peak[ch % NCH] = 0;
    return p;
}
void synth_panic(void) { silence = 1; }
/* the render ran close to its time (app.c): the oldest sounding voice let go fast (its level halved, released), so a
 * thick chord thins out instead of the audio breaking up */
void synth_shed(void)
{
    uint32_t ch, k, best = 0xFFFFFFFFu, bc = 0, bk = 0;
    for (ch = 0; ch < NCH; ch++)
        for (k = 0; k < NVOICE; k++) {
            voice_t *v = &trk[ch].v[k];
            if (v->active && v->stage && shed_q[ch][k] == 32767 && v->age < best) {
                best = v->age;
                bc = ch;
                bk = k;
            }
        }
    if (best != 0xFFFFFFFFu)
        shed_q[bc][bk] = 32766;          /* (channel_block fades it out) */
}
int synth_fm6_mute(uint32_t ch, uint32_t op)   /* FM6: an operator silent or back; 1 silent */
{
    fm6_mute[ch % NPART] ^= (uint8_t)(1u << (op % 6u));
    return fm6_mute[ch % NPART] >> (op % 6u) & 1u;
}
uint32_t synth_fm6_mutes(uint32_t ch) { return fm6_mute[ch % NPART]; }
int synth_lane_mute(uint32_t ch, uint32_t lane)   /* DRUM: a lane muted or back; 1 muted now */
{
    trk[ch % NCH].lmute ^= (uint8_t)(1u << (lane & 7u));
    return trk[ch % NCH].lmute >> (lane & 7u) & 1u;
}
uint32_t synth_lane_mutes(uint32_t ch) { return trk[ch % NCH].lmute; }
static volatile uint32_t lowcut_on;
static float lc[4];
void synth_lowcut(int on) { lowcut_on = on != 0; }

/* the graph view: the channel's output as it plays, its newest voice's envelope, its LFO's phase */
#define SCOPE_N 512u
static int16_t scope_buf[SCOPE_N];
static volatile uint32_t scope_w, scope_ch = NCH;
void synth_scope_ch(uint32_t ch) { scope_ch = ch < NCH ? ch : NCH; }
uint32_t synth_scope(int16_t *out, uint32_t n)
{
    uint32_t w = scope_w, i, start, back = n + 128u;
    if (n > SCOPE_N - 128u)
        n = SCOPE_N - 128u;
    start = w - back;                    /* a rising zero crossing in the oldest 128, so a wave stands still */
    for (i = 0; i < 127u; i++)
        if (scope_buf[(start + i) % SCOPE_N] < 0 && scope_buf[(start + i + 1u) % SCOPE_N] >= 0)
            break;
    start += i < 127u ? i + 1u : 0u;
    for (i = 0; i < n; i++)
        out[i] = scope_buf[(start + i) % SCOPE_N];
    return n;
}
int32_t synth_env_now(uint32_t ch, uint32_t *stage)
{
    uint32_t k, best = 0;
    int32_t e = 0;
    *stage = 0;
    for (k = 0; k < NVOICE; k++) {
        const voice_t *v = &trk[ch % NCH].v[k];
        if (v->active && v->stage && v->age >= best) {
            best = v->age;
            e = v->env >> 9;
            *stage = v->stage;
        }
    }
    return e;
}

uint32_t synth_voices(uint32_t ch)
{
    uint32_t k, n = 0;
    for (k = 0; k < NVOICE; k++)
        n += trk[ch % NCH].v[k].active;
    return n;
}

int synth_drum_lane(uint32_t note) { int l = (int)drum_lane(note); return l >= 0 && l < 8 ? l : -1; }

uint32_t synth_voice_view(uint32_t ch, synth_vview_t *out)
{
    uint32_t k, n = 0;
    ch %= NCH;
    for (k = 0; k < NVOICE && k < SYN_VMAX; k++) {
        const voice_t *v = &trk[ch].v[k];
        int32_t a = last_amp[ch][k];
        if (!v->active)
            continue;
        out[n].slot = (uint8_t)k;
        out[n].note = v->note;
        out[n].vel = v->vel;
        out[n].gate = v->gate;
        out[n].amp = (uint8_t)(a <= 0 ? 0 : a >= 32767 ? 255 : a >> 7);
        out[n].start = v->age;
        n++;
    }
    return n;
}

/* ---------------------------------------------------------- the effects --- */
/* the delay: one line, its time a note value at the tempo, its repeats through a one-pole (TONE); the reverb: four
 * combs with damping, two all-passes (Schroeder / Moorer, as Freeverb lays them out). Both mono in, the same on both
 * sides; their buffers in the pool */
#define DLY_N 22050u                      /* at half rate: 1 s */
#define RV_C0 1116u
#define RV_C1 1188u
#define RV_C2 1277u
#define RV_C3 1356u
#define RV_A0 556u
#define RV_A1 441u
static int16_t dly_buf[DLY_N] PLAT_POOL;
static int16_t rv_buf[RV_C0 + RV_C1 + RV_C2 + RV_C3 + RV_A0 + RV_A1] PLAT_POOL;
static const uint16_t RV_LEN[6] = {RV_C0, RV_C1, RV_C2, RV_C3, RV_A0, RV_A1};
static uint16_t rv_pos[6];
static int32_t rv_lp[4];
static uint32_t dly_w, dly_half;
static int32_t dly_prev;
static int32_t dly_lp;
static uint32_t tempo10 = 1200;
static int16_t fxv[FX_N] = {3, 50, 90, 70, 70, 50, 60};
static const char *const N_DTIME[] = {"1/32", "1/16", "1/16D", "1/8", "1/8D", "1/4", "1/4D", "1/2"};
static const uint16_t DTIME_X8[] = {1, 2, 3, 4, 6, 8, 12, 16};   /* eighths of a quarter note */
static const struct { const char *label; int16_t lo, hi; } FXD[FX_N] = {
    {"TIME", 0, 7}, {"FDBK", 0, 127}, {"TONE", 0, 127}, {"LEVEL", 0, 127},
    {"SIZE", 0, 127}, {"DAMP", 0, 127}, {"LEVEL", 0, 127},
};
static volatile uint32_t click_n;        /* the metronome: samples left */
static uint32_t click_ph, click_inc;

int32_t synth_fx(uint32_t id) { return id < FX_N ? fxv[id] : 0; }
void synth_fx_set(uint32_t id, int32_t v)
{
    if (id < FX_N)
        fxv[id] = (int16_t)clamp(v, FXD[id].lo, FXD[id].hi);
}
void synth_fx_turn(uint32_t id, int32_t d)
{
    if (id < FX_N)
        synth_fx_set(id, fxv[id] + (FXD[id].hi > 100 ? 2 * d : d));
}
const char *synth_fx_label(uint32_t id) { return id < FX_N ? FXD[id].label : ""; }
void synth_fx_text(uint32_t id, char *out)
{
    if (id == FX_DTIME)
        copy7(out, N_DTIME[fxv[id] & 7]);
    else
        num(synth_fx(id), out);
}
int32_t synth_fx_pos(uint32_t id)
{
    if (id >= FX_N)
        return 0;
    return (fxv[id] - FXD[id].lo) * 255 / (FXD[id].hi - FXD[id].lo);
}
void synth_tempo(uint32_t bpm10) { tempo10 = bpm10 < 200u ? 200u : bpm10 > 3000u ? 3000u : bpm10; }
void synth_click(uint32_t accent)
{
    click_inc = accent ? 2 * 97391u * 1000u : 97391u * 1400u;   /* (2^32 / FS a Hz) */
    click_ph = 0;
    click_n = FS / 40u;                  /* 25 ms */
}

static uint32_t dly_samples(void)
{
    uint32_t q = FS * 600u / tempo10;    /* a quarter note in samples (FS * 60 / BPM) */
    uint32_t d = q * DTIME_X8[fxv[FX_DTIME] & 7] / 8u;
    return d >= 2u * DLY_N - 4u ? 2u * DLY_N - 4u : d < 2u ? 2u : d;
}

/* a x b / 32768 rounded towards zero: the loops' quiet tails die out instead of sticking at a step (a limit cycle) */
static inline int32_t mulz15(int32_t a, int32_t b)
{
    int32_t p = a * b;
    return p < 0 ? -((-p) >> 15) : p >> 15;
}

static inline int32_t mulr15(int32_t a, int32_t b) { return (a * b + 16384) >> 15; }   /* rounded: a one-pole reaches its input */

/* one sample of the effects: the sends in (dl, rv, in the mix's units), their sum out */
static uint32_t fx_quiet;                /* samples since the delay or the reverb last wrote anything above -80 dB */
static inline int32_t fx_tick(int32_t dl, int32_t rv, uint32_t dly_d, int32_t dfb, int32_t dtone, int32_t dlvl,
                              int32_t rfb, int32_t rdamp, int32_t rlvl)
{
    int32_t y, out = 0, x, k;
    int16_t *b;
    uint32_t i, base, pos = (2u * DLY_N + 2u * dly_w + dly_half - dly_d) % (2u * DLY_N), r = pos >> 1;
    /* delay: the line holds the mix's units / 4, a sample for every two (read between them) */
    y = (pos & 1u) ? (dly_buf[r] + dly_buf[(r + 1u) % DLY_N]) * 2 : dly_buf[r] * 4;
    dly_lp += mulr15(y - dly_lp, dtone);
    x = (dl >> 2) + mulz15(dly_lp >> 2, dfb);
    if (dly_half) {
        dly_buf[dly_w] = (int16_t)clamp((x + dly_prev) >> 1, -32767, 32767);
        dly_w = dly_w + 1u >= DLY_N ? 0u : dly_w + 1u;
    }
    dly_prev = x;
    dly_half ^= 1u;
    out += y * dlvl >> 7;
    /* reverb: the combs in parallel, the all-passes in series */
    x = rv >> 3;
    y = 0;
    base = 0;
    for (i = 0; i < 4u; i++) {
        b = rv_buf + base;
        k = b[rv_pos[i]];
        y += k;
        rv_lp[i] += mulr15(k - rv_lp[i], 32767 - rdamp);
        b[rv_pos[i]] = (int16_t)clamp(x + mulz15(rv_lp[i], rfb), -32767, 32767);
        rv_pos[i] = rv_pos[i] + 1u >= RV_LEN[i] ? 0u : (uint16_t)(rv_pos[i] + 1u);
        base += RV_LEN[i];
    }
    for (i = 4u; i < 6u; i++) {
        b = rv_buf + base;
        k = b[rv_pos[i]];
        b[rv_pos[i]] = (int16_t)clamp(y + k / 2, -32767, 32767);
        y = k - y;
        rv_pos[i] = rv_pos[i] + 1u >= RV_LEN[i] ? 0u : (uint16_t)(rv_pos[i] + 1u);
        base += RV_LEN[i];
    }
    out += y * rlvl >> 5;
    if ((x > 3 || x < -3 || y > 3 || y < -3 || dly_prev > 3 || dly_prev < -3))
        fx_quiet = 0;
    else if (fx_quiet < 0xFFFFFFu)
        fx_quiet++;
    return out;
}

static void synth_duck_hit(void);
/* the render's side: a voice for a note (a free one, else the oldest released, else the oldest) */
static voice_t *voice_for(track_t *t, const engine_t *e, uint32_t mono, uint32_t note, uint32_t *idx)
{
    uint32_t k, best = 0, cap = (mono == 1u || mono == 2u) ? 1u : (e->poly && e->poly < NVOICE ? e->poly : NVOICE);
    uint32_t best_age = 0xFFFFFFFFu;
    int best_rel = 0;
    if (snd[t - trk].alloc == 1u)        /* ALLOC REUSE: the voice that played this note, if it is free or let go */
        for (k = 0; k < cap; k++)
            if (t->v[k].note == note && (!t->v[k].active || !t->v[k].gate)) {
                *idx = k;
                return &t->v[k];
            }
    for (k = 0; k < cap; k++) {
        voice_t *v = &t->v[k];
        int rel = v->stage == 3u;
        if (!v->active) {
            *idx = k;
            return v;
        }
        if ((rel && !best_rel) || (rel == best_rel && v->age < best_age)) {
            best = k;
            best_age = v->age;
            best_rel = rel;
        }
    }
    *idx = best;
    return &t->v[best];
}

/* nullwave's engines whose ph[] is the note's own state (a sample's position, an exciter's age, a vowel's glide), not
 * an oscillator's phase: a voice taken for a new note starts them afresh (kept, the new note played the old one's end:
 * silence on PHYS and SAMPLE when a chord took voices) */
static int own_state(const engine_t *e)
{
    return e == &ENG_PHYS || e == &ENG_SAMPLE || e == &ENG_GRAIN || e == &ENG_SLICE || e == &ENG_VOICE;
}
/* the voices' cost, all channels together: each engine's voice weighed (in 0.1 % of the FM-1's render time, from
 * the host's measure, x40), their sum kept under a budget. A note that would go over it takes the place of the voice
 * that matters least anywhere (one already let go, the quietest; else the oldest of the heaviest), faded over four
 * blocks; and the budget follows the render's real load (synth_load, from app.c): tighter when a block runs long,
 * back up when it is easy again. So a loop of FM6 chords with effects thins out a little before it can overload */
static const uint8_t ENG_COST[] = {4, 40, 7, 2, 4, 29, 5, 17, 16, 13, 17, 3, 3, 59};   /* (synth.c ENGINES' order) */
#define VB_DEF 650u                      /* the voices' share: 65 %, the rest the effects, the mix, the screen */
#define VB_MIN 160u
static uint32_t vbudget = VB_DEF, vb_easy;
static uint32_t eng_cost(uint32_t ch) { return run_eng[ch] < sizeof ENG_COST ? ENG_COST[run_eng[ch]] : 10u; }
static uint32_t voices_cost(void)        /* the voices sounding (not fading out), weighed */
{
    uint32_t ch, k, c = 0;
    for (ch = 0; ch < NCH; ch++)
        for (k = 0; k < NVOICE; k++)
            if (trk[ch].v[k].active && shed_q[ch][k] == 32767)
                c += eng_cost(ch);
    return c;
}
static int voice_shed_one(int32_t except_ch, uint32_t except_k)   /* the voice that matters least, faded: 1 one was */
{
    uint32_t ch, k, bc = 0, bk = 0;
    int64_t best = -1;
    for (ch = 0; ch < NCH; ch++)
        for (k = 0; k < NVOICE; k++) {
            voice_t *v = &trk[ch].v[k];
            int64_t score;
            if (!v->active || shed_q[ch][k] != 32767 || ((int32_t)ch == except_ch && k == except_k))
                continue;
            /* (let go first, the quietest of them; then the heaviest, the oldest) */
            score = !v->gate ? ((int64_t)1 << 40) + (32767 - (v->env_out < 0 ? 0 : v->env_out))
                             : ((int64_t)eng_cost(ch) << 32) + (int64_t)(0xFFFFFFFFu - v->age);
            if (score > best) {
                best = score;
                bc = ch;
                bk = k;
            }
        }
    if (best < 0)
        return 0;
    shed_q[bc][bk] = 32766;              /* (channel_block fades it out) */
    budget_sheds++;
    return 1;
}
static void voices_budget(uint32_t ch, uint32_t k)   /* a voice starts on ch: room made for it */
{
    uint32_t n = 0;
    while (voices_cost() > vbudget && n++ < 4u && voice_shed_one((int32_t)ch, k))
        ;
}
/* the render's load (app.c, x16 permille of a block's time): the budget tightened above ~72 %, a voice shed now above
 * ~85 %; eased back after ~3 s under ~45 % */
void synth_load(uint32_t p)
{
    uint32_t used;
    if (p > 1150u) {
        used = voices_cost();
        vb_easy = 0;
        if (used > VB_MIN && used < vbudget + 60u)
            vbudget = used - (used > VB_MIN + 40u ? 40u : used - VB_MIN);
        if (p > 1360u)
            voice_shed_one(-1, 0);
    } else if (p < 720u && vbudget < VB_DEF && ++vb_easy > 512u) {
        vb_easy = 0;
        vbudget += 20u;
    }
}
uint32_t synth_budget(void) { return vbudget; }
uint32_t synth_budget_used(void) { return voices_cost(); }

static void voice_start(uint32_t ch, uint32_t note, uint32_t vel)
{
    track_t *t = &trk[ch];
    const engine_t *e = ENGINES[run_eng[ch]];
    uint32_t idx, k;
    voice_t *v = voice_for(t, e, snd[ch].mono, note, &idx);
    int sounding = v->active && v->stage != 0u;
    uint32_t ph0 = v->ph[0], ph1 = v->ph[1], ph2 = v->ph[2];
    int32_t s[8];
    if (run_eng[ch] != snd[ch].engine)   /* (the engine changes at the block: its first notes wait for it) */
        return;
    memcpy(s, v->s, sizeof s);
    v->note = (uint8_t)note;
    v->vel = v->mvel = (uint8_t)vel;
    v->gate = 1;
    v->active = 1;
    shed_q[ch][idx] = 32767;             /* (a voice taken while it faded: its own again) */
    voices_budget(ch, idx);
    v->stage = 1;
    v->age = ++vage;
    shed_q[(uint32_t)(t - trk) % NPART][(uint32_t)(v - t->v) % NVOICE] = 32767;
    v->pitch16 = (int32_t)note * 16;
    if (!sounding || !snd[ch].glide)     /* GLD: from the note before (the glide is in channel_block) */
        v->pitch_cur = v->pitch16;
    v->fine = 0;
    v->side = (uint8_t)(vage & 1u);
    if (!sounding || e->oneshot) {
        v->env = v->env_out = 0;
        last_amp[ch][idx] = 0;
    } else if (snd[ch].mono == 2u) {     /* LEG: the envelope goes on */
        v->stage = 2;
    }
    if (e == &ENG_DRUM && drum_lane(note) == DL_KICK)
        synth_duck_hit();
    prs_to[ch][idx] = 0;                 /* a new note starts with no pressure */
    prs_now[ch][idx] = 0;
    memcpy(t->p, snd[ch].p, sizeof t->p);
    e->note_on(t, v);
    if (sounding && !e->oneshot && !own_state(e)) {   /* a retrigger keeps the phases and the engine's keep slots */
        v->ph[0] = ph0;
        v->ph[1] = ph1;
        v->ph[2] = ph2;
        for (k = 0; k < 8u; k++)
            if (k != 2u && k != 3u && (e->keep >> k & 1u))
                v->s[k] = s[k];
    }
}

static void voice_release(uint32_t ch, uint32_t note)
{
    uint32_t k;
    if (ENGINES[run_eng[ch]]->oneshot)
        return;                          /* (a hit plays out) */
    for (k = 0; k < NVOICE; k++) {
        voice_t *v = &trk[ch].v[k];
        if (v->active && v->gate && v->note == note) {
            v->gate = 0;
            v->stage = 3;
        }
    }
}

static void voice_pressure(uint32_t ch, uint32_t note, uint32_t prs)
{
    uint32_t k;
    for (k = 0; k < NVOICE; k++)
        if (trk[ch].v[k].active && trk[ch].v[k].gate && trk[ch].v[k].note == note)
            prs_to[ch][k] = (uint8_t)prs;
}

static void track_reset(uint32_t ch)
{
    memset(trk[ch].v, 0, sizeof trk[ch].v);
    memset(last_amp[ch], 0, sizeof last_amp[ch]);
    memset(&drw_t[ch], 0, sizeof drw_t[ch]);
    memset(drw_v[ch], 0, sizeof drw_v[ch]);
    run_eng[ch] = snd[ch].engine;
}

/* the ADSR once a block: Q15 out */
static int32_t env_tick(voice_t *v, const int16_t *p)
{
    int32_t sus = (int32_t)clamp(p[P_SUS], 0, 127) << 17;    /* Q24 */
    switch (v->stage) {
    case 1:
        v->env += atk_step[clamp(p[P_ATK], 0, 127)];
        if (v->env >= 1 << 24) {
            v->env = 1 << 24;
            v->stage = 2;
        }
        break;
    case 2:
        v->env -= ((v->env - sus) >> 9) * fall_k[clamp(p[P_DEC], 0, 127)] >> 6;   /* 32-bit: no 64-bit multiply */
        break;
    case 3:
        v->env -= (v->env >> 9) * fall_k[clamp(p[P_REL], 0, 127)] >> 6;
        if (v->env < (1 << 24) / 2000) {
            v->env = 0;
            v->stage = 0;
            v->active = 0;
        }
        break;
    default:
        break;
    }
    return v->env >> 9;
}

/* ------------------------------------------------------------- render --- */
static int32_t gain_q12;                 /* the master, glided a block at a time */

static void events(void)
{
    uint32_t ch, k;
    if (silence) {
        silence = 0;
        q_r = q_w;
        for (ch = 0; ch < NCH; ch++)
            track_reset(ch);
        return;
    }
    if (all_off) {
        all_off = 0;
        for (ch = 0; ch < NCH; ch++)
            for (k = 0; k < NVOICE; k++)
                if (trk[ch].v[k].active) {
                    trk[ch].v[k].gate = 0;
                    trk[ch].v[k].stage = 3;
                }
    }
    while (q_r != q_w) {
        uint32_t r = q_r % QN;
        ch = q[r].ch;
        if (run_eng[ch] != snd[ch].engine)   /* another engine: its track from silence */
            track_reset(ch);
        if (q[r].on == 3u) {
            for (k = 0; k < NVOICE; k++)
                if (trk[ch].v[k].active && !ENGINES[run_eng[ch]]->oneshot) {
                    trk[ch].v[k].gate = 0;
                    trk[ch].v[k].stage = 3;
                }
        } else if (q[r].on == 2u)
            voice_pressure(ch, q[r].note, q[r].vel);
        else if (q[r].on) {
            voice_start(ch, q[r].note, q[r].vel);
            if (snd[ch].mono == 3u && !ENGINES[run_eng[ch]]->oneshot) {   /* UNI: two more, detuned around it */
                uint32_t j;
                for (j = 0; j < 2u; j++) {
                    voice_start(ch, q[r].note, q[r].vel);
                    for (k = 0; k < NVOICE; k++)
                        if (trk[ch].v[k].age == vage) {
                            int32_t d = (int32_t)snd[ch].dtune / 8 + 1;
                            trk[ch].v[k].pitch16 += j ? d : -d;
                            trk[ch].v[k].pitch_cur = trk[ch].v[k].pitch16;
                        }
                }
            }
        }
        else
            voice_release(ch, q[r].note);
        q_r = q_r + 1u;
    }
}

/* the channel's filter: the trapezoidal SVF (dsp.c's), low-pass, high-pass or band-pass */
static int32_t flt_ic[NCH][2], flt_ic2[NCH][2];
static int32_t flt_reso;                 /* the one-knob filters' resonance, 0..10 (MENU AUDIO FILTER RESO) */
static void channel_filter(uint32_t ch, const sound_t *s, int32_t *buf)
{
    tsvf_t c;
    uint32_t i, mode = s->flt;
    int32_t k = 8192 - s->res * 7600 / 127;
    if (!mode)
        return;
    tsvf_coef_k(&c, (int32_t)s->cut << 8, k);
    for (i = 0; i < CTL; i++) {
        int32_t in = buf[i] >> 1, *ic1 = &flt_ic[ch][0], *ic2 = &flt_ic[ch][1];
        int32_t v3 = in - *ic2;
        int32_t v1 = (c.a1 * *ic1 + c.a2 * v3) >> 13;
        int32_t v2 = *ic2 + ((c.a2 * *ic1 + c.a3 * v3) >> 13);
        *ic1 = clamp(2 * v1 - *ic1, -150000, 150000);
        *ic2 = clamp(2 * v2 - *ic2, -150000, 150000);
        buf[i] = (mode == 1u ? v2 : mode == 3u ? v1 : in - ((k * v1) >> 12) - v2) * 2;
    }
}

/* ------------------------------------------------------------------ FM6 --- */
_Static_assert(sizeof(fm6_patch_t) == SYN_FM6_BYTES, "synth.h SYN_FM6_BYTES");
void *synth_fm6_own(uint32_t ch) { return &fm6_own[ch % NCH]; }
const void *synth_fm6_bank(uint32_t *n) { *n = fm6_bank_n; return fm6_bank; }
void synth_fm6_bank_load(const void *p, uint32_t n)
{
    fm6_bank_n = (uint8_t)(n > 32u ? 32u : n);
    memcpy(fm6_bank, p, fm6_bank_n * sizeof(fm6_patch_t));
}
/* the channel's own patch, a copy of the one it plays when that is another (so its pages edit OWN) */
static fm6_patch_t *fm6_edit(uint32_t ch)
{
    ch %= NCH;
    if (snd[ch].engine != E_FM6)
        return 0;
    if (snd[ch].p[P_E7] != 8) {
        fm6_own[ch] = *fm6_patch(ch, snd[ch].p[P_E7]);
        snd[ch].p[P_E7] = 8;
    }
    return &fm6_own[ch];
}
/* an operator's value (OPERATOR, OP ENV, OPERATOR 2): f the field (FM6_*), stage for the envelope's */
int32_t synth_fm6_op(uint32_t ch, uint32_t op, uint32_t f, uint32_t stage)
{
    const fm6_patch_t *p = fm6_patch(ch % NCH, snd[ch % NCH].p[P_E7]);
    const fm6_op_t *o = &p->op[op % 6u];
    switch (f) {
    case FM6_RATIO: return o->coarse;
    case FM6_FINE: return o->fine;
    case FM6_LEVEL: return o->level;
    case FM6_RATE: return o->r[stage & 3u];
    case FM6_ELEVEL: return o->l[stage & 3u];
    case FM6_MODE: return o->mode;
    case FM6_DTUN: return (int32_t)o->detune - 7;
    case FM6_VEL: return o->vel;
    default: return 0;
    }
}
void synth_fm6_op_turn(uint32_t ch, uint32_t op, uint32_t f, uint32_t stage, int32_t d)
{
    fm6_patch_t *p = fm6_edit(ch);
    fm6_op_t *o;
    if (!p)
        return;
    o = &p->op[op % 6u];
    switch (f) {
    case FM6_RATIO: o->coarse = (uint8_t)clamp(o->coarse + d, 0, 31); break;
    case FM6_FINE: o->fine = (uint8_t)clamp(o->fine + d, 0, 99); break;
    case FM6_LEVEL: o->level = (uint8_t)clamp(o->level + d, 0, 99); break;
    case FM6_RATE: o->r[stage & 3u] = (uint8_t)clamp(o->r[stage & 3u] + d, 0, 99); break;
    case FM6_ELEVEL: o->l[stage & 3u] = (uint8_t)clamp(o->l[stage & 3u] + d, 0, 99); break;
    case FM6_MODE: o->mode = (uint8_t)clamp(o->mode + d, 0, 1); break;
    case FM6_DTUN: o->detune = (uint8_t)clamp(o->detune + d, 0, 14); break;
    case FM6_VEL: o->vel = (uint8_t)clamp(o->vel + d, 0, 7); break;
    default: break;
    }
}
const char *synth_fm6_slot_name(int32_t slot, char *out)
{
    static const char *const F[8] = {"F1", "F2", "F3", "F4", "F5", "F6", "F7", "F8"};
    if (slot < 8)
        return F[slot < 0 ? 0 : slot];
    if (slot == 8)
        return "OWN";
    out[0] = 'B';
    out[1] = (char)('0' + (slot - 8) / 10);
    out[2] = (char)('0' + (slot - 8) % 10);
    out[3] = 0;
    if (out[1] == '0') {
        out[1] = out[2];
        out[2] = 0;
    }
    return out;
}
/* a DX7 SysEx (the bytes between F0 and F7): one voice into the channel's OWN, or a bank of 32 into B1..B32; the
 * voices taken, 0 not a DX7 voice or bank */
uint32_t synth_fm6_sysex(uint32_t ch, const uint8_t *d, uint32_t n)
{
    uint32_t i;
    if (n < 6u || d[0] != 0x43u || (d[1] & 0xF0u) != 0u)
        return 0;
    if (d[2] == 0u && n >= 5u + 155u) {      /* one voice (VCED) */
        ch %= NCH;
        fm6_from_vced(&fm6_own[ch], d + 5);
        if (snd[ch].engine == E_FM6)
            snd[ch].p[P_E7] = 8;
        return 1;
    }
    if (d[2] == 9u && n >= 5u + 4096u) {     /* 32 voices (VMEM) */
        for (i = 0; i < 32u; i++)
            fm6_from_vmem(&fm6_bank[i], d + 5 + i * 128u);
        fm6_bank_n = 32;
        return 32;
    }
    return 0;
}

#include "fx.c"

/* the pitch offsets: the tuning (cents) and each channel's bend, in 1/16 semitones */
static int32_t tune16, bend16[NCH];
void synth_tune(int32_t cents) { tune16 = clamp(cents, -50, 50) * 16 / 100; }
void synth_bend(uint32_t ch, int32_t v, uint32_t range) { bend16[ch % NCH] = v * (int32_t)range * 16 / 8192; }

/* ENV 2, a voice each (as the volume's ADSR, from ENV 2's own times), and what the slots put on each target */
static int32_t e2v[NCH][NVOICE];         /* Q24 */
static uint8_t e2s[NCH][NVOICE];         /* 0 off, 1 attack, 2 decay / sustain, 3 release */
static uint32_t e2age[NCH][NVOICE];
static int32_t modq[NCH][MT_N];          /* the newest voice's sums (the screen; PAN and LFO depth, a channel's) */
int32_t synth_mod_now(uint32_t ch, uint32_t dst) { return dst < MT_N ? modq[ch % NCH][dst] : 0; }
#ifdef PLAT_HOST                        /* the host: where the render's time goes (expect prof: printed, then cleared) */
#include <time.h>
#include <stdio.h>
static uint64_t prof_ns[8];
static uint64_t prof_now(void)
{
    struct timespec q;
    clock_gettime(CLOCK_MONOTONIC, &q);
    return (uint64_t)q.tv_sec * 1000000000u + (uint64_t)q.tv_nsec;
}
#define PROF_T(v) uint64_t v = prof_now()
#define PROF_ADD(i, v) (prof_ns[i] += prof_now() - (v))
static const char *const PROF_NAME[8] = {"FM6 voices", "other voices", "voice control+mods", "channel filter",
                                         "channel post (dst/insert/slicer)", "mix", "buses+master", "-"};
void synth_prof_dump(uint32_t real_ms)
{
    uint32_t i;
    for (i = 0; i < 7u; i++)
        printf("  prof %-34s %5.2f %% of real time\n", PROF_NAME[i], (double)prof_ns[i] / 1e4 / (double)real_ms);
    memset(prof_ns, 0, sizeof prof_ns);
}
#else
#define PROF_T(v)
#define PROF_ADD(i, v)
#endif
static int32_t cutq[NCH];                /* the newest voice's cutoff modulation, << 8 (the graph view) */
int32_t synth_cut_mod(uint32_t ch) { return cutq[ch % NCH]; }

static int32_t env2_tick(uint32_t ch, uint32_t k, const voice_t *v, const synth_mod_t *mo)
{
    int32_t *e = &e2v[ch][k], sus = (int32_t)mo->e2[2] << 17;
    if (v->age != e2age[ch][k]) {        /* a new note in this voice: from the start */
        e2age[ch][k] = v->age;
        e2s[ch][k] = 1;
        *e = 0;
    }
    if (!v->gate && e2s[ch][k] && e2s[ch][k] < 3u)
        e2s[ch][k] = 3;
    switch (e2s[ch][k]) {
    case 1:
        *e += atk_step[mo->e2[0] & 127u];
        if (*e >= 1 << 24) {
            *e = 1 << 24;
            e2s[ch][k] = 2;
        }
        break;
    case 2:
        *e -= ((*e - sus) >> 9) * fall_k[mo->e2[1] & 127u] >> 6;
        break;
    case 3:
        *e -= (*e >> 9) * fall_k[mo->e2[3] & 127u] >> 6;
        if (*e < 0)
            *e = 0;
        break;
    default:
        break;
    }
    return *e >> 9;                      /* Q15 */
}

static void channel_block(uint32_t ch, int32_t *out, sound_t *s)
{
    track_t *t = &trk[ch];
    const engine_t *e;
    uint32_t k;
    chmod_t cm;
    uint32_t newest_age = 0;
    if (run_eng[ch] != s->engine)        /* another engine: its track from silence */
        track_reset(ch);
    e = ENGINES[run_eng[ch]];
    memcpy(t->p, s->p, sizeof t->p);
    memcpy(t->ext, s->ext, sizeof t->ext);
    if (e->block)
        e->block(t);
    {
        uint32_t nv = 0;
        for (k = 0; k < NVOICE; k++)
            nv += t->v[k].active;
        {
            int need = 0;
            for (k = 0; k < SYN_MODS; k++)
                need |= smod[ch].m[k].src == MS_LFO && smod[ch].m[k].dst && smod[ch].m[k].amt;
            lfo_block(ch, s, nv, &cm, need);
        }
        if (modq[ch][MT_LFO]) {          /* a slot on the LFO's depth (the last block's: the newest voice's) */
            int32_t g = 32768 + modq[ch][MT_LFO];
            g = g < 0 ? 0 : g > 65536 ? 65536 : g;
            cm.pit16 = (cm.pit16 * g) >> 15;
            cm.cut = (cm.cut >> 1) * (g >> 1) >> 13;
            cm.shp = (cm.shp >> 1) * (g >> 1) >> 13;
            cm.amp_q15 = 32767 - (((32767 - cm.amp_q15) >> 1) * (g >> 1) >> 13);
        }
    }
    {
        uint32_t newest = 0, any = 0;
        for (k = 0; k < SYN_MODS; k++)
            any |= smod[ch].m[k].src && smod[ch].m[k].dst && smod[ch].m[k].amt;
        if (!any)
            memset(modq[ch], 0, sizeof modq[ch]);
        (void)newest;
    }
    for (k = 0; k < NVOICE; k++) {
        voice_t *v = &t->v[k];
        vmod_t m;
        int32_t a, pr, md[MT_N];
        uint32_t base;
        if (!v->active)
            continue;
        a = env_tick(v, t->p);
        if (e->amp)
            a = e->amp(t, v, a);
        if (!v->active)
            continue;
        a = (a * vel_gain[v->vel & 127u]) >> 15;
        if (shed_q[ch][k] < 32767) {     /* shed (the render near its time): faded out over four blocks */
            a = (a * shed_q[ch][k]) >> 15;
            shed_q[ch][k] = shed_q[ch][k] > 8192 ? (int16_t)(shed_q[ch][k] - 8192) : 0;
            if (!shed_q[ch][k]) {
                v->active = 0;
                v->stage = 0;
                shed_q[ch][k] = 32767;
                continue;
            }
        }
        /* pressure: glided (~20 ms), then louder (up to +50 %, +25 % with BOTH) and / or brighter */
        prs_now[ch][k] += (((int32_t)prs_to[ch][k] << 8) - prs_now[ch][k]) >> 3;
        pr = prs_now[ch][k] >> 8;        /* 0..127 */
        if (prs_mode == PRS_VOLUME || prs_mode == PRS_BOTH)
            a += (a * pr * (prs_mode == PRS_BOTH ? 1 : 2)) >> 9;
        a = a > 32767 ? 32767 : a;
        a = (a * cm.amp_q15) >> 15;      /* LFO -> AMP */
        {                                /* the slots: each source times its amount, summed on its target */
            const synth_mod_t *mo = &smod[ch];
            int32_t src[MS_N], j;
            src[MS_OFF] = 0;
            src[MS_ENV] = v->env >> 9;
            src[MS_ENV2] = env2_tick(ch, k, v, mo);
            src[MS_LFO] = cm.lfo_q15;
            src[MS_VEL] = (int32_t)(v->vel & 127u) * 258;
            src[MS_PRS] = pr * 258;
            src[MS_RND] = v->mrnd;
            memset(md, 0, sizeof md);
            for (j = 0; j < SYN_MODS; j++)
                if (mo->m[j].src && mo->m[j].dst && mo->m[j].amt)
                    md[mo->m[j].dst] += src[mo->m[j].src % MS_N] * mo->m[j].amt;
            if (md[MT_AMP]) {
                int32_t g = 32768 + (md[MT_AMP] >> 6);
                a = (a * (g < 0 ? 0 : g > 65536 ? 65536 : g)) >> 15;
                a = a > 32767 ? 32767 : a;
            }
            if (v->age >= newest_age) {  /* (the newest voice's, for the channel and the screen) */
                newest_age = v->age;
                for (j = 0; j < MT_N; j++)
                    modq[ch][j] = clamp(md[j] >> 6, -32768, 32767);
            }
        }
        v->env_out = a;
        if (v->pitch_cur != v->pitch16) {   /* GLD: towards the note, a share a block */
            int32_t k16 = 32768 / (1 + s->glide * s->glide / 24);
            int32_t d = ((v->pitch16 - v->pitch_cur) * k16) >> 15;
            v->pitch_cur += d ? d : (v->pitch16 > v->pitch_cur ? 1 : -1);
        }
        {
            int32_t p16 = v->pitch_cur + tune16 + bend16[ch] + cm.pit16 + ((s->penv * (v->env >> 9)) >> 15) * 384 / 63
                          + (md[MT_PIT] >> 15) * 384 / 63;
            base = pitch_inc((uint32_t)clamp(p16, 0, 2047));
            memset(&m, 0, sizeof m);
            m.pitch16 = p16;
        }
        m.inc = base;
        m.amp0 = last_amp[ch][k];
        m.amp1 = a;
        m.envq15 = v->env >> 9;
        m.cutoff = (s->fenv * (v->env >> 9)) >> 7;   /* ENV -> FILTER: 63 opens it ~63 << 8 at the envelope's top */
        if (prs_mode == PRS_FILTER || prs_mode == PRS_BOTH)
            m.cutoff += pr * 97;         /* full pressure: the cutoff ~48 steps up */
        m.cutoff += cm.cut + (md[MT_CUT] >> 7);
        m.shape = (64 << 8) + cm.shp + ((s->senv * (v->env >> 9)) >> 7) + (md[MT_SHP] >> 7);
        if (v->age >= newest_age)
            cutq[ch] = m.cutoff;
        last_amp[ch][k] = a;
        {
            PROF_T(pr0);
            e->render(t, v, out, CTL, &m);
            PROF_ADD(run_eng[ch] == 1u ? 0 : 1, pr0);
            (void)0;
        }
    }
    {
        PROF_T(pf0);
        channel_filter(ch, s, out);
        PROF_ADD(3, pf0);
    }
    {
        PROF_T(pp0);
        channel_post(ch, s, out);
        PROF_ADD(4, pp0);
    }
}

static int32_t level_q8(uint32_t l) { return (int32_t)(l * l * 256u / 10000u); }   /* 100: 1.0, 127: ~1.6 */

static volatile uint32_t quiet_blocks, all_blocks;   /* the render's blocks, and those it found silent (the tests) */
uint32_t synth_quiet_pct(void)           /* of the blocks since the last call, the silent ones, % */
{
    uint32_t a = all_blocks, q = quiet_blocks;
    all_blocks = quiet_blocks = 0;
    return a ? q * 100u / a : 0u;
}
void synth_render(int32_t *lr, uint32_t n, uint32_t master_q12)
{
    static int32_t mixl[CTL], mixr[CTL], sdl[CTL], srv[CTL], scho[CTL], chb[CTL];
    static sound_t live;
    uint32_t done = 0, i, ch, dly_d = dly_samples(), mixed;
    int32_t dfb = fxv[FX_DFB] * 235, dtone = 3000 + fxv[FX_DTONE] * 232, dlvl = fxv[FX_DLVL];
    int32_t rfb = 23000 + fxv[FX_RSIZE] * 70, rdamp = fxv[FX_RDAMP] * 200, rlvl = fxv[FX_RLVL];
    float tick_per_smp = 96.0f / beat_samples(), echo_w = punch_mix[14], freeze_w = punch_mix[23];
    flt_reso = clamp(mst[MST_RESO], 0, 10);
    if (echo_w > 0.0f) {                 /* the echo punch-in: the delay at a dotted eighth, more repeats */
        dly_d = (uint32_t)fm_minf(beat_samples() * 0.75f, 2.0f * (float)DLY_N - 4.0f);
        dfb = (int32_t)fm_maxf((float)dfb, 20000.0f * echo_w);
        dlvl = (int32_t)fm_maxf((float)dlvl, 90.0f * echo_w);
    }
    if (freeze_w > 0.0f)                 /* FREEZE: the reverb holds */
        rfb = (int32_t)((float)rfb * (1.0f - freeze_w) + 32600.0f * freeze_w);
    events();
    while (done < n) {
        uint32_t len = n - done < CTL ? n - done : CTL;
        int32_t g0 = gain_q12, g1 = (int32_t)master_q12;
        memset(mixl, 0, sizeof mixl);
        memset(mixr, 0, sizeof mixr);
        memset(sdl, 0, sizeof sdl);
        memset(srv, 0, sizeof srv);
        memset(scho, 0, sizeof scho);
        duck_env *= fm_expf(-(float)CTL / (beat_samples() * 0.5f) * 4.0f);   /* gone in an eighth */
        mixed = 0;
        all_blocks++;
        for (ch = 0; ch < NCH; ch++) {
            int32_t c0 = ch_gain[ch], c1 = synth_audible(ch) ? level_q8(level[ch]) : 0, pk = 0;
            int32_t gl, gr, ds, rs, cs, dg = (int32_t)(duck_gain(ch) * 256.0f);
            uint32_t id, k, nv = 0;
            for (k = 0; k < NVOICE; k++)
                nv |= trk[ch].v[k].active;
            if (nv || run_eng[ch] != snd[ch].engine)
                ch_quiet[ch] = 0;
            else if (ch_quiet[ch] < 2000u)
                ch_quiet[ch]++;
            if (ch_quiet[ch] > 160u) {   /* nothing sounding for ~0.12 s (its effects' tails gone): not rendered */
                ch_gain[ch] = (c1 * dg) >> 8;
                continue;
            }
            live = snd[ch];
            for (id = 0; id < SP_N; id++)  /* the locks */
                if (ovr[ch][id] != OVR_NONE)
                    sp_put(&live, id, ovr[ch][id]);
            memset(chb, 0, sizeof chb);
            {
                PROF_T(pc0);
                channel_block(ch, chb, &live);
                PROF_ADD(2, pc0);        /* (the whole: the parts are taken off it in synth_prof_dump's reading) */
            }
            for (i = 0; i < CTL; i++) {
                int32_t a = chb[i] < 0 ? -chb[i] : chb[i];
                pk = a > pk ? a : pk;
            }
            if (pk > ch_peak[ch])
                ch_peak[ch] = pk;
            if (ch == scope_ch)              /* the graph view's scope: every second sample of the channel shown */
                for (i = 0; i < CTL; i += 2u)
                    scope_buf[scope_w++ % SCOPE_N] = (int16_t)clamp(chb[i] >> 1, -32767, 32767);
            c1 = (c1 * dg) >> 8;
            ch_gain[ch] = c1;
            if ((!c0 && !c1) || !pk)         /* (silent this block: nothing to mix) */
                continue;
            mixed = 1;
            if (modq[ch][MT_PAN])            /* a slot on PAN (the newest voice's) */
                live.pan = (int8_t)clamp(live.pan + (modq[ch][MT_PAN] >> 9), -64, 63);
            gl = live.pan > 0 ? (63 - live.pan) * 260 >> 6 : 256;   /* the balance: the far side falls */
            gr = live.pan < 0 ? (64 + live.pan) * 256 >> 6 : 256;
            ds = throw_echo ? 127 : live.dly;   /* the throws: every channel's send full while held */
            rs = throw_rev ? 127 : live.rev;
            cs = live.cho;
            for (i = 0; i < CTL; i++) {
                int32_t g = c0 + (((c1 - c0) * (int32_t)i) >> CTL_LOG2);
                int32_t x = (chb[i] >> 4) * g >> 4;   /* x level (Q8) */
                mixl[i] += x * gl >> 8;
                mixr[i] += x * gr >> 8;
                sdl[i] += x * ds >> 7;
                srv[i] += x * rs >> 7;
                scho[i] += x * cs >> 7;
            }
        }
        if (!mixed && fx_quiet >= 2u * DLY_N + 64u && !cho_hold && !click_n && !punch_busy() && !mst[MST_TSRC]
            && !mst[MST_DUST] && !mst[MST_FILT] && dj_cur > -0.02f && dj_cur < 0.02f) {
            for (i = 0; i < len; i++)    /* all quiet: the block silent at once (the punch-ins' ring kept in time) */
                lr[2 * (done + i)] = lr[2 * (done + i) + 1] = 0;
            punch_idle(len);
            quiet_blocks++;
            tp_frac += tick_per_smp * (float)len;
            gain_q12 = g1;
            done += len;
            continue;
        }
        PROF_T(pb0);
        for (i = 0; i < len; i++) {
            int32_t g = g0 + (((g1 - g0) * (int32_t)i) >> CTL_LOG2), fx, dl = sdl[i], rv = srv[i];
            float l, r, lg;
            if (echo_w > 0.0f)
                dl += (int32_t)((float)(mixl[i] + mixr[i]) * 0.5f * echo_w);
            if (freeze_w > 0.0f && pr_ac - punch_t0[23] < (uint32_t)(FS / 10u))
                rv += (int32_t)((float)(mixl[i] + mixr[i]) * 0.5f * freeze_w);
            if (dl || rv || fx_quiet < 2u * DLY_N + 64u)
                fx = fx_tick(dl, rv, dly_d, dfb, dtone, dlvl, rfb, rdamp, rlvl);
            else                             /* (their lines silent, nothing coming in: skipped) */
                fx = 0;
            if (click_n) {
                click_ph += click_inc;
                fx += sine_i(click_ph) * (int32_t)click_n / (int32_t)(FS / 40u) >> 2;
                click_n--;
            }
            l = (float)(mixl[i] + fx) / 65536.0f;
            r = (float)(mixr[i] + fx) / 65536.0f;
            if (scho[i])
                cho_hold = CHO_N;
            if (cho_hold) {                  /* the chorus: while something is sent, and its buffer's length after */
                cho_hold--;
                chorus_tick((float)scho[i] / 65536.0f, &l, &r);
            }
            if (mst[MST_TSRC]) {
                float tx = texture_tick();
                l += tx;
                r += tx;
            }
            dust_tick(&l, &r);
            punch_tick(&l, &r);
            dj_tick(&l, &r);
            l *= (float)g / 4096.0f;
            r *= (float)g / 4096.0f;
            if (lowcut_on) {             /* LOWCUT: the speaker's lows (it cannot play them), a 2-pole high-pass ~150 Hz */
                lc[0] += (l - lc[0]) * 0.0211f;
                lc[1] += (lc[0] - lc[1]) * 0.0211f;
                lc[2] += (r - lc[2]) * 0.0211f;
                lc[3] += (lc[2] - lc[3]) * 0.0211f;
                l -= lc[1];
                r -= lc[3];
            }
            lg = limit_tick(l, r);
            /* a voice peaks near 24000 (VOICE_FS): at full MASTER about -9 dBFS; the limiter keeps the sum below
             * full scale, the soft knee takes what it lets through */
            lr[2 * (done + i)] = clamp(soft_knee((int32_t)(l * lg * 8388607.0f), 7000000), -8388607, 8388607);
            lr[2 * (done + i) + 1] = clamp(soft_knee((int32_t)(r * lg * 8388607.0f), 7000000), -8388607, 8388607);
            tp_frac += tick_per_smp;
        }
        PROF_ADD(6, pb0);
        gain_q12 = g1;
        done += len;
    }
}

void synth_init(void)
{
    uint32_t ch;
    times_init();
    synth_vel_sens(0);
    {
        uint32_t c, k;
        for (c = 0; c < NPART; c++)
            for (k = 0; k < NVOICE; k++)
                shed_q[c][k] = 32767;
    }
    for (ch = 0; ch < NCH; ch++) {
        synth_default_mod(&smod[ch]);
        synth_set_sound(ch, 0);
        track_reset(ch);
        synth_unlock(ch);
        level[ch] = 100;
        ch_gain[ch] = level_q8(100);
    }
    q_r = q_w;
    all_off = 0;
}
