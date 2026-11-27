/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Charles Vestal
 * Copyright (C) 2026 nullwave contributors */
/* nullwave on fm1-base: a groovebox of six equal channels, each a synth (the engines in synth.c) or a drum kit (eng_drum.c),
 * with patterns, loop recording, scenes and songs (seq.c), played from the keys with velocity and pressure and from
 * MIDI. The controls follow the FM-1 firmwares players know (a tap opens a button's pages, a hold makes the keys and
 * knobs its layer): ui.c. docs: app/docs/MANUAL.md.
 *
 * This file: the settings the FM-1 keeps (OBJ_SETTINGS, the preferences in OBJ_SOUND), the notes' way to the synth,
 * MIDI in, the render, the platform's contract. Part of the unity build (device/unity.c, host/plat_host.c), after the
 * platform: gfx.c and announce.c are in scope; seq.c, ui_draw.c and ui.c are included here. */
#include <stddef.h>
#include "fastmath.h"
#include "synth.h"
#include "seq.h"
#include "art.h"

#define SET_MAGIC 0x54504C31u            /* "TPL1": the settings record; another magic loads defaults */
typedef struct {
    uint32_t magic;
    int8_t octave;                       /* -2 .. +2 */
    int8_t vsens;                        /* velocity sensitivity -5 .. +5 (synth_vel_sens) */
    uint8_t ble_off;                     /* Bluetooth switched off; 0, the default: on */
    uint8_t channel;                     /* the channel the keys play, 0..5 */
} settings_t;
static settings_t set;

#define PREFS_MAGIC 0x4E575036u          /* "NWP6": the preferences (in OBJ_SOUND's place: the sounds are the songs' now) */
typedef struct {
    uint32_t magic;
    uint8_t key_learn;                   /* 1: each key learns its own range (lib/sense.c sense_learn) */
    uint8_t fixed_vel;                   /* 0: the keys' velocity; else every key plays at this */
    uint8_t prs_mode;                    /* what a held key's pressure does (synth.h PRS_*) */
    uint8_t display;                     /* the screen: 0 dark, 1 light */
    uint8_t midi_ch[NCH];                /* each channel's MIDI channel, 1..16, 0 none */
    uint8_t knob_accel, lights, scale_keys, autosave, slot;
    uint8_t in_mode;                     /* MIDI IN: 0 NOTES, 1 CLOCK only, 2..17 one channel (to the selected) */
    uint8_t other;                       /* a channel no channel has: 0 the selected one, 1 ignored */
    uint8_t bend;                        /* pitch bend range, 1..12 semitones */
    uint8_t drmap;                       /* a kit's notes from MIDI: 0 GM, 1 36..51 in a row */
    uint8_t remote;                      /* CC 102..115 press the buttons on this channel, 0 off */
    uint8_t fxch;                        /* the punch-in effects from notes on this channel, 0 off */
    int8_t tune;                         /* -50..+50 cents */
    uint8_t reso, pfade, lowcut, usb_full, step_adv, drum_prev;   /* MENU AUDIO, SEQ; the punch-ins' fade */
    uint8_t fav[16];                     /* PRESETS FAV: bit i, sound i of the list */
    uint8_t graphs;                      /* SCREEN GRAPHS: the graph view's default, 0 off */
    uint16_t graph_on, graph_set;        /* SHIFT + a page's button: bit i the graph kind i on, and set (else the default) */
    uint8_t learn[8][3];                 /* MIDI LEARN: a CC (0 none), the MIDI channel it comes on, the parameter (SP_*) */
    seq_cfg_t sc;                        /* the sequencer's: REC, the click, the launch, sync, clock and notes out */
    uint8_t home_view;                   /* SCREEN 2 HOME VIEW: MAIN at power-on, 0 PLAY, 1 LOOP */
    uint8_t loop_manual;                 /* SCREEN 2 AUTO LOOP ON REC: 0 ON (REC shows LOOP), 1 OFF */
    uint8_t spare;                       /* (once PLAY's style in test builds: ignored; PLAY draws the wave) */
    uint32_t seen_build;                 /* the build that last started (FM1B_BUILD): another says INSTALLED once */
} prefs_t;
#define PREFS_V6_SIZE 88u                /* (the preferences before HOME VIEW: they load, with the two at their defaults) */
_Static_assert(offsetof(prefs_t, home_view) == PREFS_V6_SIZE, "the older preferences end where HOME VIEW starts");
#define PREFS_V7_SIZE 92u                /* (and before the build seen: they load, it 0, so INSTALLED shows once) */
_Static_assert(offsetof(prefs_t, seen_build) == PREFS_V7_SIZE, "the older preferences end where the build seen starts");
static uint8_t fresh_build;              /* this build starts for the first time: the logo says INSTALLED */
static prefs_t prefs;
static uint8_t set_dirty;                /* changed since the last write: written when the FM-1 is quiet */
static uint32_t set_dirty_at;
static void keys_config(void);
static uint32_t boot_ignore;             /* the buttons held at power-on for app_boot, ignored till released (ui.c) */
static uint8_t boot_reset;               /* app_boot reset the settings: the screen says so */
static void app_mark(void)
{
    set_dirty = 1;
    set_dirty_at = plat_ms();
}

static uint8_t nw_midi_ch(uint32_t ch) { return prefs.midi_ch[ch % NCH]; }

/* ------------------------------------------------------------------ notes --- */
static volatile int32_t last_note = -1;  /* the last note started and still held (tests), -1 none */
static volatile uint32_t last_ch;

/* the main loop's notes go to the synth's queue under the lock (the render puts the sequencer's and MIDI's there) */
static void note_on(uint32_t ch, int32_t note, uint32_t vel)
{
    uint32_t f = plat_lock();
    synth_note_on(ch, (uint32_t)note, vel);
    plat_unlock(f);
    seq_note_on(ch, (uint32_t)note, vel);
    last_note = note;
    last_ch = ch;
}

static void note_off(uint32_t ch, int32_t note)
{
    if (!seq_note_off(ch, (uint32_t)note)) {   /* (a drone recorded: it goes on) */
        uint32_t f = plat_lock();
        synth_note_off(ch, (uint32_t)note);
        plat_unlock(f);
    }
    if (last_note == note && last_ch == ch)
        last_note = -1;
}

static void pressure(uint32_t ch, uint32_t note, uint32_t prs)
{
    uint32_t f = plat_lock();
    synth_pressure(ch, note, prs);
    plat_unlock(f);
}

#include "seq.c"

/* MIDI in: the channels by their MIDI channels, another to the selected one (OTHER), or only one channel (IN); a
 * kit's notes on the GM map, or 36..51 in a row (DR MAP); pitch bend; the clock to the sequencer; REMOTE: CC 102..115 hold the
 * buttons */
static volatile uint32_t midi_last, midi_last_port, midi_count;
static volatile uint32_t remote_btn;     /* the buttons held over MIDI (REMOTE) */
/* the CCs and program changes, for the main loop (the sounds are its): ch << 16 | cc << 8 | value, cc 128 a program */
#define CCQ_N 64u
static volatile uint32_t ccq[CCQ_N], ccq_w, ccq_r;
static uint8_t sus_on[NCH];              /* the sustain pedal (CC 64) down */
static uint32_t sus_held[NCH][4];        /* the notes let go while it was down: their ends when it comes up */
static void ccq_put(uint32_t v)
{
    if (ccq_w - ccq_r < CCQ_N)
        ccq[ccq_w++ % CCQ_N] = v;
}
static volatile uint32_t midi_note_t;     /* when a MIDI note last came in (0: never) */
static void midi_in(void)                    /* audio interrupt: the packets that came in */
{
    uint32_t pkt;
    int port;
    while (plat_midi_in(&pkt, &port)) {
        uint32_t st = (pkt >> 8) & 0xFFu, d1 = (pkt >> 16) & 0x7Fu, d2 = pkt >> 24, i, ch = set.channel, mc;
        int mine = 0;
        if (st >= 0xF8u) {
            seq_midi_rt(st, port);
            continue;
        }
        if (st >= 0xF0u)
            continue;
        if ((st & 0xF0u) == 0x90u || (st & 0xF0u) == 0x80u)
            midi_note_t = plat_ms() | 1u;   /* (notes come from outside: the stuck-note sweep leaves them be) */
        mc = (st & 0x0Fu) + 1u;
        if (prefs.remote && mc == prefs.remote && (st & 0xF0u) == 0xB0u && d1 >= 102u && d1 <= 115u) {
            uint32_t b = d1 - 102u;      /* REMOTE: FX, SEL, ENV ... OCT+ (plat.h's order) */
            remote_btn = d2 >= 64u ? remote_btn | 1u << b : remote_btn & ~(1u << b);
            continue;
        }
        if (prefs.fxch && mc == prefs.fxch && ((st & 0xF0u) == 0x90u || (st & 0xF0u) == 0x80u)) {
            if (d1 >= 36u && d1 < 36u + 27u) /* FX CH: notes 36.. hold the 27 punch-ins (the FX layer's keys in order) */
                synth_punch(d1 - 36u, (st & 0xF0u) == 0x90u && d2);
            continue;
        }
        if (prefs.in_mode == 1u)             /* IN CLOCK: nothing but the clock */
            continue;
        midi_last = pkt;
        midi_last_port = (uint32_t)port;
        midi_count++;
        if (prefs.in_mode >= 2u) {           /* IN CH n: that channel alone, to the selected channel */
            if (mc != prefs.in_mode - 1u)
                continue;
        } else {
            for (i = 0; i < NCH; i++)
                if (prefs.midi_ch[i] == mc) {
                    ch = i;
                    mine = 1;
                    break;
                }
            if (!mine && prefs.other)        /* OTHER OFF: a channel no channel has is left alone */
                continue;
        }
        if ((st & 0xF0u) == 0xB0u) {         /* a CC: the sustain here, the rest to the main loop */
            if (d1 == 64u || d1 == 121u || d1 == 123u) {
                sus_on[ch] = d1 == 64u && d2 >= 64u;
                if (!sus_on[ch]) {
                    for (i = 0; i < 128u; i++)
                        if (sus_held[ch][i >> 5] >> (i & 31u) & 1u) {
                            synth_note_off(ch, i);
                            seq_note_off(ch, i);
                            if (last_note == (int32_t)i)
                                last_note = -1;
                        }
                    memset(sus_held[ch], 0, sizeof sus_held[ch]);
                }
                if (d1 == 121u)
                    synth_bend(ch, 0, prefs.bend);
                if (d1 != 123u)
                    ccq_put(ch << 16 | d1 << 8 | d2 | mc << 24);
                continue;
            }
            ccq_put(ch << 16 | d1 << 8 | d2 | mc << 24);
            continue;
        }
        if ((st & 0xF0u) == 0xC0u) {         /* a program change: the channel's sound */
            ccq_put(ch << 16 | 128u << 8 | d1 | mc << 24);
            continue;
        }
        if ((st & 0xF0u) == 0xE0u) {         /* pitch bend */
            synth_bend(ch, (int32_t)(d1 | d2 << 7) - 8192, prefs.bend);
            continue;
        }
        if ((st & 0xF0u) >= 0x80u && (st & 0xF0u) <= 0xA0u && synth_is_kit(ch) && prefs.drmap) {
            if (d1 < 36u || d1 > 51u)        /* DR MAP 1-16: 16 notes in a row from 36, the lanes twice over (GM: as they are) */
                continue;
            d1 = synth_drum_note(d1 - 36u);
        }
        if ((st & 0xF0u) == 0xA0u)           /* poly aftertouch: the note's pressure */
            synth_pressure(ch, d1, d2);
        else if ((st & 0xF0u) == 0x90u && d2) {
            sus_held[ch][d1 >> 5] &= ~(1u << (d1 & 31u));
            synth_note_on(ch, d1, d2);
            seq_note_on(ch, d1, d2);
            last_note = (int32_t)d1;
            last_ch = ch;
        } else if ((st & 0xF0u) == 0x80u || (st & 0xF0u) == 0x90u) {
            if (sus_on[ch]) {                /* held by the pedal */
                sus_held[ch][d1 >> 5] |= 1u << (d1 & 31u);
                continue;
            }
            if (!seq_note_off(ch, d1))   /* (a drone recorded: it goes on) */
                synth_note_off(ch, d1);
            if (last_note == (int32_t)d1)
                last_note = -1;
        }
    }
}

/* the render's load: its time against the block's, a peak that falls (GLO SYSTEM CPU) */
static volatile uint32_t nw_cpu;
static uint32_t shed_wait;               /* blocks before the overload guard may fade another voice */
#ifdef PLAT_HOST
static uint64_t rd_ticks, rd_budget;
#endif
void app_render(int32_t *lr, uint32_t n, uint32_t master_q12)
{
    uint32_t t0 = plat_ticks24(), dt, budget = n * (PLAT_TICKS_PER_US * 1000000u / 44100u), p;
    midi_in();
    seq_render(n);
    synth_render(lr, n, master_q12);
    dt = (plat_ticks24() - t0) & 0xFFFFFFu;   /* (a 24-bit clock: its wrap is no huge span) */
#ifdef PLAT_HOST
    rd_ticks += dt;                      /* (the host: the render's time, for expect render_permille) */
    rd_budget += budget;
#endif
    p = budget ? dt * 1600u / budget : 0u;   /* (x16: the peak falls a 64th a block, ~1.5 s) */
#ifdef PLAT_HOST
    {                                    /* (the host: NW_LOADX=40 scales its render time to the FM-1's, for the guard) */
        static int lx = -1;
        if (lx < 0)
            lx = getenv("NW_LOADX") ? atoi(getenv("NW_LOADX")) : 0;
        if (lx > 0)
            p = budget ? (uint32_t)((uint64_t)dt * 1600u * (uint32_t)lx / budget) : 0u;
    }
#endif
    if (shed_wait)
        shed_wait--;
    else if (p > 1150u) {                /* over ~72 %: the voices' budget tightens (and above ~85 % a voice fades now),
                                          * then a rest of 8 blocks (~6 ms) to see what that did (synth.c synth_load) */
        synth_load(p);
        shed_wait = 8;
    } else
        synth_load(p);                   /* (and eases back when it is easy) */
    nw_cpu = p > nw_cpu ? p : nw_cpu - nw_cpu / 64u;
}

static volatile uint32_t overloads;      /* the renders that ran late (the screen tells) */
void app_overload(void) { overloads++; }
void app_panic(void)
{
    synth_panic();
    last_note = -1;
}

/* --------------------------------------------------------------- settings --- */
static void settings_defaults(void)
{
    set.magic = SET_MAGIC;
    set.octave = 0;
    set.vsens = 0;
    set.ble_off = 0;
    set.channel = 0;
}

static void settings_load(void)          /* the saved settings, or the defaults when there are none or they are wrong */
{
    if (plat_store_load(OBJ_SETTINGS, &set, sizeof set) != (int)sizeof set || set.magic != SET_MAGIC
        || set.octave < -2 || set.octave > 2 || set.vsens < -5 || set.vsens > 5 || set.ble_off > 1u
        || set.channel >= NCH)
        settings_defaults();
    synth_vel_sens(set.vsens);
    if (touch_is_on())
        keys_config();
}

/* the keys' sensing: each key learning its own firm and light ends (off in fm1-base by default; the keys differ by
 * tens of counts, so without it some play quieter than others at the same touch) */
static void keys_config(void)
{
    sn_cfg.learn = prefs.key_learn;
    synth_pressure_mode(prefs.prs_mode);
}

static void prefs_defaults(void)
{
    uint32_t i;
    memset(&prefs, 0, sizeof prefs);
    prefs.magic = PREFS_MAGIC;
    prefs.key_learn = 1;
    prefs.prs_mode = PRS_BOTH;
    for (i = 0; i < NCH; i++)
        prefs.midi_ch[i] = (uint8_t)(i + 1u);
    prefs.knob_accel = 1;
    prefs.lights = 1;
    prefs.scale_keys = 1;
    prefs.autosave = 1;
    prefs.bend = 2;
    prefs.pfade = 2;
    prefs.step_adv = 1;
    prefs.sc = seq_cfg;
}

static void prefs_load(void)
{
    uint32_t i;
    int n = plat_store_load(OBJ_SOUND, &prefs, sizeof prefs);
    int ok = (n == (int)sizeof prefs || n == (int)PREFS_V6_SIZE || n == (int)PREFS_V7_SIZE) && prefs.magic == PREFS_MAGIC
             && prefs.key_learn <= 1u && prefs.fixed_vel <= 127u && prefs.prs_mode < PRS_NMODES
             && prefs.display <= 1u && prefs.slot < NSONG && prefs.sc.rec_bars >= 1u && prefs.sc.rec_bars <= 8u
             && prefs.sc.launch <= LQ_NOW && prefs.sc.sync <= SYNC_BLE && prefs.sc.click <= CLICK_ON
             && prefs.in_mode <= 17u && prefs.other <= 1u && prefs.bend >= 1u && prefs.bend <= 12u && prefs.drmap <= 1u
             && prefs.remote <= 16u && prefs.fxch <= 16u && prefs.tune >= -50 && prefs.tune <= 50
             && prefs.reso <= 10u && prefs.pfade <= 15u && prefs.graphs <= 1u;
    if (ok && n == (int)PREFS_V6_SIZE)
        prefs.home_view = prefs.loop_manual = 0;
    if (prefs.home_view > 1u || prefs.loop_manual > 1u)
        prefs.home_view = prefs.loop_manual = 0;
    prefs.spare = 0;
    if (ok && n != (int)sizeof prefs)
        prefs.seen_build = 0;
    for (i = 0; ok && i < NCH; i++)
        ok = prefs.midi_ch[i] <= 16u;
    if (!ok)
        prefs_defaults();
    fresh_build = prefs.seen_build != (uint32_t)FM1B_BUILD;
#ifdef PLAT_HOST
    fresh_build = fresh_build && getenv("NW_FRESH");   /* (the host: every run a first start; said only when asked,
                                                         * so the tests' and the pictures' screens stay as they were) */
#endif
    seq_cfg = prefs.sc;
    synth_tune(prefs.tune);
    synth_master_set(MST_RESO, prefs.reso);
    synth_punch_fade(prefs.pfade);
    keys_config();
}

static int save_prefs(void)
{
    prefs.sc = seq_cfg;
    return plat_store_save(OBJ_SETTINGS, &set, sizeof set) | plat_store_save(OBJ_SOUND, &prefs, sizeof prefs);
}

static void fmbank_load(void);
void app_init(void)
{
    synth_init();
    seq_init();
    settings_load();
    prefs_load();
    fmbank_load();
    if (seq_load(prefs.slot) != 0)       /* the last song, or a new one */
        seq_new();
    synth_vel_sens(set.vsens);
}

/* HOME held (alone) at power-on: the settings and preferences back to their defaults, the songs, user presets, names
 * and the FM6 bank kept, and the song that was open still opens (a way back from a setting that silenced the FM-1,
 * with no computer). The platform's own power-on keys (REC, OCT- + OCT+) are seen before this */
void app_boot(void)
{
    uint8_t slot = prefs.slot, fav[sizeof prefs.fav];
    boot_reset = 0;
    if ((plat_buttons() & ((1u << NB) - 1u)) != 1u << B_HOME)
        return;
    memcpy(fav, prefs.fav, sizeof fav);
    settings_defaults();
    prefs_defaults();
    prefs.slot = slot;                   /* (the favourites and the song open are the player's, not settings) */
    memcpy(prefs.fav, fav, sizeof fav);
    seq_cfg = prefs.sc;
    synth_vel_sens(set.vsens);
    synth_tune(prefs.tune);
    synth_master_set(MST_RESO, prefs.reso);
    synth_punch_fade(prefs.pfade);
    synth_pressure_mode(prefs.prs_mode);
    save_prefs();
    boot_ignore = 1u << B_HOME;
    boot_reset = 1;
}

int nullwave_ble_off(void) { return set.ble_off; }

/* --------------------------------------------- user presets, the FM6 bank --- */
/* SAVE USER: 32 slots in two flash sectors (16 a sector, 256 bytes each: a magic, the sound, its FM6 patch); the
 * FM6 voice bank (B1..B32) in the sector before them. Written only while stopped */
#define USER_BASE 0xD8000u
#define FMBANK_BASE 0xD7000u
#define USER_MAGIC 0x4E575550u           /* "NWUP" */
#define FMBANK_MAGIC 0x4E574642u         /* "NWFB" */
typedef struct {
    uint32_t magic;
    sound_t snd;
    uint8_t fm6[SYN_FM6_BYTES];
    synth_mod_t mod;                     /* (a preset from before it: its bytes erased, 0xFF: not ok, the default) */
    char name[SYN_NAME];                 /* its name, 0-padded (from before it: not a name, "USER n") */
} user_t;
_Static_assert(sizeof(user_t) <= 256u, "a user preset fits its 256 bytes");
static const user_t *user_at(uint32_t i)
{
    const uint8_t *d = plat_data(USER_BASE + (i / 16u) * 4096u);
    const user_t *u = d ? (const user_t *)(const void *)(d + (i % 16u) * 256u) : 0;
    return u && u->magic == USER_MAGIC && synth_sound_ok(&u->snd) ? u : 0;
}
static int user_used(uint32_t i) { return user_at(i % 32u) != 0; }
static const char *user_name(uint32_t i)   /* a slot's name, "EMPTY", or "-" (a preset from before names) */
{
    static char nm[SYN_NAME + 1];
    const user_t *u = user_at(i % 32u);
    uint32_t k;
    if (!u)
        return "EMPTY";
    if (!((u->name[0] >= 'A' && u->name[0] <= 'Z') || (u->name[0] >= '0' && u->name[0] <= '9')))
        return "-";
    for (k = 0; k < SYN_NAME && u->name[k]; k++)
        nm[k] = u->name[k];
    nm[k] = 0;
    return nm;
}
static int user_load(uint32_t i, uint32_t ch)
{
    const user_t *u = user_at(i % 32u);
    if (!u)
        return 0;
    memcpy(synth_fm6_own(ch), u->fm6, SYN_FM6_BYTES);
    synth_set_sound(ch, &u->snd);
    synth_set_mod(ch, &u->mod);          /* (not ok: the default) */
    if ((u->name[0] >= 'A' && u->name[0] <= 'Z') || (u->name[0] >= '0' && u->name[0] <= '9'))
        synth_set_name(ch, u->name, SYN_NAME);
    else {
        char t[8] = "USER ";
        uint32_t s = i % 32u + 1u, k = 5;
        if (s >= 10u)
            t[k++] = (char)('0' + s / 10u);
        t[k++] = (char)('0' + s % 10u);
        t[k] = 0;
        synth_set_name(ch, t, 8);
    }
    seq_touch();
    return 1;
}
static int user_write(uint32_t i, const user_t *rec)   /* the slot's sector rewritten with rec (0: emptied) */
{
    uint32_t sec = USER_BASE + (i / 16u) * 4096u;
    const uint8_t *d = plat_data(sec);
    if (seq_playing() || !d)
        return 0;
    memcpy(sbuf, d, 4096);
    if (rec)
        memcpy(sbuf + (i % 16u) * 256u, rec, sizeof *rec);
    else
        memset(sbuf + (i % 16u) * 256u, 0xFF, 256);
    return plat_data_erase(sec) == 0 && plat_data_write(sec, sbuf, 4096) == 0;
}
static int user_save(uint32_t i, uint32_t ch)
{
    user_t u;
    memset(&u, 0, sizeof u);
    u.magic = USER_MAGIC;
    u.snd = *synth_sound(ch);
    memcpy(u.fm6, synth_fm6_own(ch), SYN_FM6_BYTES);
    u.mod = *synth_mod(ch);
    {                                    /* (its name: the sound's, as shown, a user preset's or the editor's) */
        const char *nm = synth_preset_name(ch);
        uint32_t k;
        for (k = 0; k < SYN_NAME && nm[k]; k++)
            u.name[k] = nm[k];
    }
    return user_write(i % 32u, &u);
}
static int user_erase(uint32_t i) { return user_write(i % 32u, 0); }

static void fmbank_load(void)            /* the FM6 bank from flash, at the start */
{
    const uint8_t *d = plat_data(FMBANK_BASE);
    uint32_t h[2];
    if (!d)
        return;
    memcpy(h, d, sizeof h);
    if (h[0] == FMBANK_MAGIC && h[1] <= 32u)
        synth_fm6_bank_load(d + 8, h[1]);
}
static int fmbank_save(void)
{
    uint32_t n, h[2];
    const void *b = synth_fm6_bank(&n);
    if (seq_playing())
        return 0;
    memset(sbuf, 0xFF, 4096);
    h[0] = FMBANK_MAGIC;
    h[1] = n;
    memcpy(sbuf, h, sizeof h);
    memcpy(sbuf + 8, b, n * SYN_FM6_BYTES);
    return plat_data_erase(FMBANK_BASE) == 0 && plat_data_write(FMBANK_BASE, sbuf, 4096) == 0;
}

/* a DX7 SysEx from the computer (APP_SYSEX_BYTE: a bank is 4104 bytes), gathered in the USB interrupt, read in the
 * main loop: a voice to the selected channel's OWN patch, a bank to B1..B32 */
#define SX_N 4200u
static uint8_t sx_buf[SX_N] PLAT_POOL;
static volatile uint32_t sx_n, sx_done;
void app_sysex_byte(uint8_t b)
{
    if (sx_done)
        return;
    if (b == 0xF0u) {
        sx_n = 0;
        return;
    }
    if (b == 0xF7u) {
        if (sx_n >= 6u && sx_buf[0] == 0x43u)
            sx_done = 1;
        return;
    }
    if (sx_n < SX_N)
        sx_buf[sx_n++] = b;
}
static void sysex_poll(void)
{
    uint32_t got;
    if (!sx_done)
        return;
    got = synth_fm6_sysex(set.channel, sx_buf, sx_n);
    if (got == 32u) {
        app_say("FM6 BANK", fmbank_save() ? "B1-B32" : "STOP FIRST");
    } else if (got == 1u) {
        seq_touch();
        app_say("FM6 VOICE", "OWN");
    }
    sx_done = 0;
}

#include "hd.c"                          /* (host only, NW_HD: the UI at 4x as well) */
#include "ui_draw.c"
#include "ui.c"

/* the editor's follow (docs/EDITOR-PROTOCOL.md): 0x74 FOCUS from the editor, 0x75 NAV and 0x76 HELLO-FOCUS to it,
 * in the update's framing (F0 pack7(00 59 cmd len24 body ~sum8(body)) F7), as the companion's services */
#define ED_FOCUS 0x74u
#define ED_NAV 0x75u
#define ED_HELLO 0x76u
#define ED_MAX 600u                      /* a reply's body at most (the editor's frames from the FM-1) */
static uint8_t ed_raw[ED_MAX + 8], ed_out[ED_MAX + ED_MAX / 7u + 16u], ed_body[ED_MAX];
static uint32_t ed_wire(uint32_t n) { return 2u + ((n + 7u) * 8u + 6u) / 7u; }   /* a body's frame: its bytes */
static int ed_send(uint32_t cmd, const uint8_t *body, uint32_t n)   /* 0 sent (queued whole), -1 not */
{
    uint8_t *raw = ed_raw, *out = ed_out;
    uint32_t i, k = 0, acc = 0, nb = 0, sum = 0;
    if (n > ED_MAX)
        return -1;
    raw[0] = 0;
    raw[1] = 0x59;
    raw[2] = (uint8_t)cmd;
    raw[3] = (uint8_t)n;
    raw[4] = (uint8_t)(n >> 8);
    raw[5] = 0;
    for (i = 0; i < n; i++) {
        raw[6 + i] = body[i];
        sum += body[i];
    }
    raw[6 + n] = (uint8_t)~sum;
    out[k++] = 0xF0;
    for (i = 0; i < n + 7u; i++) {       /* (pack7: eight bits in, seven a byte out, low first) */
        acc |= (uint32_t)raw[i] << nb;
        nb += 8;
        while (nb >= 7u) {
            out[k++] = (uint8_t)(acc & 0x7Fu);
            acc >>= 7;
            nb -= 7;
        }
    }
    if (nb)
        out[k++] = (uint8_t)(acc & 0x7Fu);
    out[k++] = 0xF7;
    return plat_sysex_send(out, k);
}
void editor_nav(uint32_t ev, int32_t arg)   /* 0x75 NAV: 1 CH, 2 PAGE, 3 PRESET (arg -1 / +1), 4 SAVE, 5 SCENE n, 6 EXIT */
{
    uint8_t b[2] = {(uint8_t)ev, (uint8_t)(int8_t)arg};
    ed_send(ED_NAV, b, 2);
}
void editor_hello(uint32_t reason)       /* 0x76 HELLO-FOCUS: 0 a computer connected, 1 EDITOR opened on the FM-1 */
{
    uint8_t b[3] = {1, set.channel, (uint8_t)reason};
    ed_send(ED_HELLO, b, 3);
}
void ui_editor_focus(uint32_t ch, const char *title, uint32_t tlen, const uint16_t *ids, uint32_t n);
static int ed_more(uint32_t cmd, const uint8_t *q, uint32_t bl);
static int ed_frame(const uint8_t *d, uint32_t n)   /* a decoded frame (cmd d[2], body d + 6): 1 the editor's */
{
    uint32_t bl, tl, k, cnt;
    uint16_t ids[8];
    uint8_t st = 0;
    if (n >= 7u && d[2] >= 0x70u && d[2] <= 0x73u)
        return ed_more(d[2], d + 6, n - 7u);
    if (n < 7u || d[2] != ED_FOCUS)
        return 0;
    bl = n - 7u;                         /* body: ver, channel, title length, title, count, ids (u16) */
    tl = bl >= 3u ? d[8] : 0u;
    cnt = bl >= 4u + tl ? d[9 + tl] : 0u;
    if (bl < 4u + tl || d[6] != 1u || d[7] >= NCH || tl > 16u || cnt > 8u || bl < 4u + tl + 2u * cnt)
        st = 1;                          /* (not one this firmware reads: nothing changes) */
    else {
        for (k = 0; k < cnt; k++)
            ids[k] = (uint16_t)(d[10 + tl + 2u * k] | d[11 + tl + 2u * k] << 8);
        ui_editor_focus(d[7], (const char *)d + 9, tl, ids, cnt);
    }
    ed_send(ED_FOCUS, &st, 1);           /* (answered, as every request: 0 taken, 1 not) */
    return 1;
}
void app_usb_cmd(const uint8_t *d, uint32_t n) { ed_frame(d, n); }
int app_sysex(const uint8_t *p, uint32_t n)   /* (the host's way in; on the FM-1 the frames come by app_usb_cmd) */
{
#ifdef PLAT_HOST
    static uint8_t dec[700];
    uint32_t i, k = 0, acc = 0, nb = 0, sum = 0, len;
    for (i = 0; i < n && k < sizeof dec; i++) {
        acc |= (uint32_t)(p[i] & 0x7Fu) << nb;
        nb += 7;
        if (nb >= 8u) {
            dec[k++] = (uint8_t)acc;
            acc >>= 8;
            nb -= 8;
        }
    }
    if (k < 7u || dec[0] != 0 || dec[1] != 0x59u || dec[2] < 0x70u || dec[2] > ED_FOCUS)
        return 0;
    len = dec[3] | dec[4] << 8 | (uint32_t)dec[5] << 16;
    if (len + 7u > k)
        return 0;
    for (i = 0; i < len; i++)
        sum += dec[6 + i];
    if ((uint8_t)~sum != dec[6 + len])
        return 0;
    return ed_frame(dec, len + 7u);
#else
    (void)p;
    (void)n;
    return 0;
#endif
}
/* the editor's views of the FM-1 (docs/EDITOR-PROTOCOL.md 0x70..0x73): each request a version byte and an op; each
 * answer the version asked, a status (ED_OK ...) and the op, then its data. Bytes past what a version reads: ignored.
 * STATE and STEPS speak versions 1 and 2 (2: the patterns' time and step in the channel blocks, the STEPS header);
 * SONG and PRESETS 1 */
enum { ED_OK, ED_BAD, ED_BUSY, ED_RANGE };
#define ED_STATE 0x70u
#define ED_STEPS 0x71u
#define ED_SONG 0x72u
#define ED_PRESETS 0x73u
#define ED_BEATS 4u                      /* a pattern's time: 4/4 (TBAR), the same for every pattern in this firmware */
#define ED_UNIT 4u
static uint8_t ed_ver = 1;               /* the version the request asked: its answer's */
static uint8_t ed_state_sub, ed_state_ver = 1, ed_steps_sub, ed_steps_ver = 1, ed_steps_ch;
static int factory_busy(void);
static void ed_name(uint8_t *o, const char *s, uint32_t n)   /* a name, 0-padded to n */
{
    uint32_t i = 0;
    for (; s && s[i] && i < n; i++)
        o[i] = (uint8_t)s[i];
    for (; i < n; i++)
        o[i] = 0;
}
static uint32_t ed_time(uint8_t *b, const pat_t *p)   /* a pattern's DIV, step (ticks, u16), beats, beat unit: 5 bytes */
{
    uint32_t st = seq_step_ticks(p);
    b[0] = p->div;
    b[1] = (uint8_t)st;
    b[2] = (uint8_t)(st >> 8);
    b[3] = ED_BEATS;
    b[4] = ED_UNIT;
    return 5;
}
static uint32_t ed_state(uint8_t *b, uint32_t ver)   /* 0x70's data from b[3]: version 1 162 bytes in all, 2 186 */
{
    uint32_t ch = set.channel, k = 3, i;
    const pat_t *pp = seq_pat(ch);
    int32_t pos = seq_pos(ch), bp = batt_pct();
    uint32_t r = seq_rec();
    b[k++] = (uint8_t)(seq_playing() | (r == REC_ON || r == REC_TAKE) << 1 | (r == REC_ARMED) << 2 | (r == REC_COUNT) << 3
                       | (song.mode == MODE_SONG) << 4 | (seq_song_rec_on() != 0) << 5 | (seq_cfg.rec_replace != 0) << 6);
    b[k++] = (uint8_t)song.bpm10;
    b[k++] = (uint8_t)(song.bpm10 >> 8);
    b[k++] = (uint8_t)ch;
    b[k++] = pp->bars;
    b[k++] = (uint8_t)(pos >= 0 ? (uint32_t)pos / TBAR : 0xFFu);
    b[k++] = (uint8_t)(pos >= 0 ? (uint32_t)pos % TBAR / seq_step_ticks(pp) : 0xFFu);
    b[k++] = (uint8_t)(bp >= 0 ? (uint32_t)bp : 0xFFu);
    b[k++] = (uint8_t)((plat_battery() == 4 || bp < 0) | (plat_battery_charging() == 2) << 1);
    b[k++] = (uint8_t)seq_scene();
    b[k++] = (uint8_t)(seq_queued() >= 0 ? (uint32_t)seq_queued() : 0xFFu);
    b[k++] = song.nchain;
    b[k++] = (uint8_t)(song.nchain ? seq_sect() : 0xFFu);
    b[k++] = (uint8_t)(song.nchain ? song.chain[seq_sect() % NCHAIN].scene : 0xFFu);
    b[k++] = (uint8_t)r;
    for (i = 0; i < NCH; i++) {          /* each channel: 24 bytes (version 2: 28) */
        const sound_t *s = synth_sound(i);
        b[k++] = s->engine;
        b[k++] = s->preset;
        b[k++] = (uint8_t)synth_get_level(i);
        b[k++] = (uint8_t)((seq_mute() >> i & 1u) | (seq_solo() >> i & 1u) << 1 | (synth_audible(i) != 0) << 2
                           | (s->arp_hold != 0) << 3 | (seq_drones(i) != 0) << 4);
        b[k++] = (uint8_t)(seq_pat(i) - &song.pat[i][0]);
        b[k++] = seq_pat(i)->bars;
        b[k++] = (uint8_t)synth_voices(i);
        if (ver >= 2u)
            k += ed_time(b + k, seq_pat(i));
        ed_name(b + k, chan_name(i), 16);
        k += 16;
        if (ver < 2u)
            b[k++] = 0;                  /* (version 1: kept for a later field) */
    }
    return k;
}
static int ed_reply(uint32_t cmd, uint32_t st, uint32_t op, uint32_t n)   /* ed_body[3..n) the data; 0 sent */
{
    ed_body[0] = ed_ver;
    ed_body[1] = (uint8_t)st;
    ed_body[2] = (uint8_t)op;
    return ed_send(cmd, ed_body, n < 3u ? 3u : n);
}
static uint32_t ed_scene(uint8_t *b, uint32_t s)   /* a scene: pat x6, mute, solo, level x6, has_sound, used (16) */
{
    const scene_t *sc = &song.scene[s % NSCENE];
    uint32_t c, k = 0;
    for (c = 0; c < NCH; c++)
        b[k++] = sc->pat[c];
    b[k++] = sc->mute;
    b[k++] = sc->solo;
    for (c = 0; c < NCH; c++)
        b[k++] = sc->level[c];
    b[k++] = sc->has_sound;
    b[k++] = (uint8_t)seq_scene_used(s);
    return k;
}
/* 0x70's ops 2..6: the transport as the FM-1's own buttons and knobs move it; 0 done, else the status */
static uint32_t ed_transport(uint32_t op, const uint8_t *q, uint32_t bl)
{
    uint32_t a = bl >= 3u ? q[2] : 0xFFu, r = seq_rec();
    if (factory_busy())
        return ED_BUSY;
    switch (op) {
    case 2:                              /* PLAY [1 play, 0 stop] (as PLAY: a count-in or a free take, as there) */
        if (a > 1u)
            return ED_BAD;
        if (a && !seq_playing() && r != REC_COUNT && r != REC_TAKE)
            seq_play();
        else if (!a && (seq_playing() || r == REC_COUNT || r == REC_TAKE)) {
            if (r == REC_COUNT || r == REC_TAKE)
                seq_play();              /* (the count or the take dropped, as PLAY does there) */
            seq_stop();
        }
        return ED_OK;
    case 3:                              /* REC [0 the REC button, 1 arm (on), 2 off], on the selected channel */
        if (a > 2u)
            return ED_BAD;
        if (a == 0u || (a == 1u && r == REC_OFF) || (a == 2u && r != REC_OFF))
            seq_rec_button(set.channel);
        return ED_OK;
    case 4: {                            /* TEMPO [bpm x 10, u16]: 40.0 .. 240.0; refused while a clock sets it */
        uint32_t t = bl >= 4u ? (uint32_t)(q[2] | q[3] << 8) : 0u;
        if (bl < 4u)
            return ED_BAD;
        if (t < 400u || t > 2400u)
            return ED_RANGE;
        if (seq_cfg.sync != SYNC_INT)
            return ED_BUSY;
        seq_set_bpm10(t);
        return ED_OK;
    }
    case 5:                              /* LAUNCH [scene]: stopped at once, playing at SCENE LAUNCH's (BAR, LOOP, NOW) */
        if (a >= NSCENE)
            return bl < 3u ? ED_BAD : ED_RANGE;
        seq_launch(a);
        return ED_OK;
    case 6:                              /* MODE [0 LOOP, 1 SONG] */
        if (a > 1u)
            return ED_BAD;
        seq_mode(a);
        return ED_OK;
    default:
        return ED_BAD;
    }
}
static int ed_more(uint32_t cmd, const uint8_t *q, uint32_t bl)
{
    uint32_t op = bl >= 2u ? q[1] : 0xFFu, k = 3, i, maxv = cmd == ED_STATE || cmd == ED_STEPS ? 2u : 1u;
    ed_ver = (uint8_t)(bl && q[0] >= 1u && q[0] <= maxv ? q[0] : 1u);
    if (bl < 2u || q[0] < 1u || q[0] > maxv) {
        ed_reply(cmd, ED_BAD, op, 3);
        return 1;
    }
    switch (cmd) {
    case ED_STATE: {                     /* op 0 get; 1 subscribe (q[2]: 1 on, 0 off); 2..6 the transport */
        uint32_t st = ED_OK;
        if (op == 1u) {
            ed_state_sub = bl >= 3u && q[2];
            ed_state_ver = ed_ver;       /* (the pushes in the subscriber's version) */
        } else if (op >= 2u && op <= 6u)
            st = ed_transport(op, q, bl);
        else if (op != 0u)
            st = ED_BAD;
        ed_reply(cmd, st, op, st == ED_BAD ? 3u : ed_state(ed_body, ed_ver));   /* (refused too: the state as it is) */
        return 1;
    }
    case ED_STEPS: {                     /* op 0 read [ch, bar]; 1 write [ch, bar, n, n x 6]; 2 playhead [ch, on] */
        uint32_t ch = bl >= 3u ? q[2] : 0xFFu, bar = bl >= 4u ? q[3] : 0xFFu;
        int r;
        if (ch >= NCH || (op != 2u && bl < 4u)) {
            ed_reply(cmd, ED_BAD, op, 3);
            return 1;
        }
        if (op == 2u) {
            ed_steps_sub = bl >= 4u && q[3];
            ed_steps_ch = (uint8_t)ch;
            ed_steps_ver = ed_ver;
            ed_reply(cmd, ED_OK, op, 3);
            return 1;
        }
        ed_body[k++] = (uint8_t)ch;
        ed_body[k++] = (uint8_t)bar;
        ed_body[k++] = seq_pat(ch)->bars;
        if (ed_ver >= 2u)                /* (version 2: DIV, the step, the time) */
            k += ed_time(ed_body + k, seq_pat(ch));
        else
            ed_body[k++] = seq_pat(ch)->div;
        if (op == 1u) {
            uint32_t n = bl >= 5u ? q[4] : 0u;
            if (bl < 5u + 6u * n) {
                ed_reply(cmd, ED_BAD, op, 3);
                return 1;
            }
            r = factory_busy() ? -2 : seq_bar_write(ch, bar, q + 5, n);
            if (r) {
                ed_reply(cmd, r == -2 ? ED_BUSY : ED_RANGE, op, k);
                return 1;
            }
        } else if (op != 0u) {
            ed_reply(cmd, ED_BAD, op, 3);
            return 1;
        }
        r = seq_bar_read(ch, bar, ed_body + k + 1, (ED_MAX - k - 1u) / 6u);   /* (a write answers with the bar as it is) */
        if (r < 0) {
            ed_reply(cmd, ED_RANGE, op, k);
            return 1;
        }
        ed_body[k] = (uint8_t)r;
        ed_reply(cmd, ED_OK, op, k + 1u + 6u * (uint32_t)r);
        return 1;
    }
    case ED_SONG:                        /* op 0 scenes; 1 a scene [s, pat x6, mute, solo, level x6]; 2 the song; 3 [n, end,
                                          * n x (scene, bars, reps)] */
        if (op == 0u) {
            ed_body[k++] = NSCENE;
            for (i = 0; i < NSCENE; i++)
                k += ed_scene(ed_body + k, i);
        } else if (op == 1u) {
            if (bl < 2u + 1u + 6u + 2u + 6u || q[2] >= NSCENE) {
                ed_reply(cmd, ED_BAD, op, 3);
                return 1;
            }
            if (factory_busy()) {
                ed_reply(cmd, ED_BUSY, op, 3);
                return 1;
            }
            seq_scene_set(q[2], q + 3, q[9], q[10], q + 11);
            ed_body[k++] = q[2];         /* (the scene as it is now) */
            k += ed_scene(ed_body + k, q[2]);
        } else if (op == 2u || op == 3u) {
            if (op == 3u) {
                uint32_t n = bl >= 3u ? q[2] : 0xFFu;
                int r;
                if (n > NCHAIN || bl < 4u + 3u * n) {
                    ed_reply(cmd, ED_BAD, op, 3);
                    return 1;
                }
                r = factory_busy() ? -2 : seq_chain_set(q + 4, n, q[3] & 1u);
                if (r) {
                    ed_reply(cmd, r == -2 ? ED_BUSY : ED_RANGE, op, 3);
                    return 1;
                }
            }
            ed_body[k++] = song.nchain;
            ed_body[k++] = (uint8_t)seq_chain_end_stop();
            ed_body[k++] = song.mode;
            for (i = 0; i < song.nchain; i++) {
                ed_body[k++] = song.chain[i].scene;
                ed_body[k++] = song.chain[i].bars;
                ed_body[k++] = song.chain[i].reps;
            }
        } else {
            ed_reply(cmd, ED_BAD, op, 3);
            return 1;
        }
        ed_reply(cmd, ED_OK, op, k);
        return 1;
    case ED_PRESETS:                     /* op 0 factory [engine]; 1 user [first, count]; 2 load [ch, kind, engine, index];
                                          * 3 a kit's lanes [ch] */
        if (op == 0u) {
            uint32_t e = bl >= 3u ? q[2] : 0xFFu, n;
            if (e >= synth_nengines()) {
                ed_reply(cmd, ED_RANGE, op, 3);
                return 1;
            }
            n = synth_engine_npresets(e);
            ed_body[k++] = (uint8_t)e;
            ed_body[k++] = (uint8_t)n;
            for (i = 0; i < n && k + 18u < ED_MAX; i++) {   /* each: index, name 16 */
                ed_body[k++] = (uint8_t)i;
                ed_name(ed_body + k, synth_engine_preset(e, i), 16);
                k += 16;
            }
        } else if (op == 1u) {
            uint32_t first = bl >= 3u ? q[2] : 0u, cnt = bl >= 4u ? q[3] : 16u;
            if (first >= 32u) {
                ed_reply(cmd, ED_RANGE, op, 3);
                return 1;
            }
            cnt = cnt > 16u ? 16u : cnt > 32u - first ? 32u - first : cnt;
            ed_body[k++] = (uint8_t)first;
            ed_body[k++] = (uint8_t)cnt;
            for (i = first; i < first + cnt; i++) {   /* each: slot, used, engine, name 16 */
                const user_t *u = user_at(i);
                ed_body[k++] = (uint8_t)i;
                ed_body[k++] = (uint8_t)(u != 0);
                ed_body[k++] = (uint8_t)(u ? u->snd.engine : 0xFFu);
                ed_name(ed_body + k, u ? user_name(i) : "", 16);
                k += 16;
            }
        } else if (op == 2u) {
            uint32_t ch = bl >= 3u ? q[2] : 0xFFu, kind = bl >= 4u ? q[3] : 0xFFu, e = bl >= 5u ? q[4] : 0xFFu;
            uint32_t idx = bl >= 6u ? q[5] : 0xFFu;
            if (ch >= NCH || kind > 1u) {
                ed_reply(cmd, ED_BAD, op, 3);
                return 1;
            }
            if (factory_busy()) {
                ed_reply(cmd, ED_BUSY, op, 3);
                return 1;
            }
            if (kind == 0u) {
                if (e >= synth_nengines() || idx >= synth_engine_npresets(e)) {
                    ed_reply(cmd, ED_RANGE, op, 3);
                    return 1;
                }
                synth_engine(ch, e);
                synth_preset(ch, idx);
            } else if (idx >= 32u || !user_load(idx, ch)) {
                ed_reply(cmd, ED_RANGE, op, 3);
                return 1;
            }
            seq_touch();
            ed_body[k++] = (uint8_t)ch;
            ed_body[k++] = synth_sound(ch)->engine;
            ed_body[k++] = synth_sound(ch)->preset;
            ed_name(ed_body + k, synth_preset_name(ch), 16);
            k += 16;
        } else if (op == 3u) {           /* each lane: lane, note (the GM note it plays), name 16; not a kit: none */
            uint32_t ch = bl >= 3u ? q[2] : 0xFFu, n;
            if (ch >= NCH) {
                ed_reply(cmd, ED_BAD, op, 3);
                return 1;
            }
            n = synth_kit_lanes(ch);
            ed_body[k++] = (uint8_t)ch;
            ed_body[k++] = (uint8_t)n;
            for (i = 0; i < n; i++) {
                ed_body[k++] = (uint8_t)i;
                ed_body[k++] = (uint8_t)synth_drum_note(i);
                ed_name(ed_body + k, synth_kit_sound(i), 16);
                k += 16;
            }
        } else {
            ed_reply(cmd, ED_BAD, op, 3);
            return 1;
        }
        ed_reply(cmd, ED_OK, op, k);
        return 1;
    default:
        return 0;
    }
}
/* the subscriptions: STATE on change (10 a second at most), the playhead's step. A push goes out only when the SysEx
 * ring has room for it whole (never a wait in the main loop); one that can't stays owed, and the newest goes out as
 * soon as there is room: a change is late at worst, never lost */
static uint8_t ed_owed;                  /* bit 0 STATE, bit 1 the playhead: changed, not yet sent (expect ed_owed) */
static int32_t ed_pushed_tr = -1;        /* (the last STATE pushed: its transport byte, for the host's checks) */
static void ed_push(void)
{
    static uint32_t t, last_h, last_step = 0xFFFFFFFFu;
    uint32_t now = plat_ms();
    uint8_t v = ed_ver;
    if (!ed_state_sub)
        ed_owed &= (uint8_t)~1u;
    if (ed_state_sub && now - t >= 100u) {
        uint32_t n, h = 2166136261u, i;
        ed_ver = ed_state_ver;
        n = ed_state(ed_body, ed_state_ver);
        for (i = 3; i < n; i++)
            h = (h ^ ed_body[i]) * 16777619u;
        if (h != last_h) {
            ed_owed |= 1u;
            if (plat_sysex_room() >= ed_wire(n) && ed_reply(ED_STATE, ED_OK, 0x80u, n) == 0) {   /* (op 0x80: pushed) */
                last_h = h;
                t = now;
                ed_pushed_tr = ed_body[3];
                ed_owed &= (uint8_t)~1u;
            }
        }
    }
    if (!ed_steps_sub)
        ed_owed &= (uint8_t)~2u;
    if (ed_steps_sub) {
        int32_t pos = seq_pos(ed_steps_ch);
        uint32_t stt = seq_step_ticks(seq_pat(ed_steps_ch));
        uint32_t st = pos >= 0 ? (uint32_t)pos / stt : 0xFFFFu;
        if (st != last_step) {
            ed_owed |= 2u;
            ed_ver = ed_steps_ver;
            ed_body[3] = ed_steps_ch;
            ed_body[4] = (uint8_t)(pos >= 0 ? (uint32_t)pos / TBAR : 0xFFu);
            ed_body[5] = (uint8_t)(pos >= 0 ? (uint32_t)pos % TBAR / stt : 0xFFu);
            if (plat_sysex_room() >= ed_wire(6) && ed_reply(ED_STEPS, ED_OK, 0x82u, 6) == 0) {   /* (op 0x82) */
                last_step = st;
                ed_owed &= (uint8_t)~2u;
            }
        }
    }
    ed_ver = v;
}
void editor_poll(void)                   /* a computer that sets the FM-1 up: HELLO-FOCUS, so an editor sends its focus */
{
    static uint32_t seen;
    uint32_t s = plat_usb_session();
    if (s && s != seen) {
        seen = s;
        editor_hello(0);
    }
    ed_push();
}

/* FACTORY RESET (SETTINGS SYSTEM, PLAY held 3 s; or the companion's "factory-reset" file): nullwave's own storage
 * erased (the songs with their scenes and names, the user presets, the FM6 bank: 0x97000..0xD9FFF), its settings and
 * preferences put back to their defaults, then a restart: the next start is a first start. Never the firmware, the
 * update loader or the bootloader, nor fm1-base's own objects (the panel's calibration, the Bluetooth pairings). A
 * sector a frame, the ring filling, so the screen and USB stay alive */
#define FR_LO 0x97000u
#define FR_HI 0xDA000u
static struct { uint8_t on; uint32_t at; } fr;
static int factory_busy(void) { return fr.on; }   /* (the editor's writes refused meanwhile) */
static void factory_start(void)
{
    seq_stop();
    synth_panic();
    fr.on = 1;
    fr.at = FR_LO;
}
static int factory_step(void)            /* one sector, the screen; 1 while under way */
{
    if (!fr.on)
        return 0;
    while (fr.at < FR_HI) {              /* (a sector already empty: skipped) */
        const uint32_t *d = (const uint32_t *)(const void *)plat_data(fr.at);
        uint32_t i, used = 0;
        for (i = 0; d && i < 1024u && !used; i++)
            used = d[i] != 0xFFFFFFFFu;
        if (used) {
            plat_data_erase(fr.at);
            fr.at += 4096u;
            break;
        }
        fr.at += 4096u;
    }
    factory_screen((fr.at - FR_LO) * 1000u / (FR_HI - FR_LO));
    if (fr.at >= FR_HI) {
        settings_defaults();
        prefs_defaults();
        save_prefs();                    /* (seen_build 0: the next start says INSTALLED, as a fresh install) */
        fr.on = 0;
        plat_restart();
    }
    return 1;
}

/* an install begins (the installer's upgrade command): the sequencer stopped, the notes let go, and nullwave's own
 * screen for a moment (the logo, INSTALLING) before fm1-base's steps and the FM-1's updater take the screen */
int app_update_begin(void)
{
    static uint32_t t0;
    uint32_t now = plat_ms();
    if (!t0) {
        t0 = now | 1u;
        seq_stop();
        synth_panic();
        update_screen();
    }
    return now - t0 < 250u;              /* (a quarter second on the screen; fm1-base goes ahead at 300 ms anyway) */
}


/* ------------------------------------------------------------ companion --- */
/* the computer's side (fm1.toml companion = true: docs/COMPANION.md): the saved settings are a file a page can
 * download and send back (app.h APP_FILE_OBJS); the platform checks a file whole before it saves it */
int app_file_busy(void) { return seq_playing(); }   /* (not while the sequencer plays) */
void app_file_changed(uint32_t obj)      /* the computer replaced the settings: use them */
{
    if (obj == OBJ_SETTINGS)
        settings_load();
    else if (obj == OBJ_SOUND)
        prefs_load();
    else
        return;
    set_dirty = 0;
    ui_redraw_all();
}

#ifdef PLAT_HOST
int app_expect(const char *what, int *got)
{
#ifdef NW_HD
    if (!strncmp(what, "hd:", 3)) {      /* the screen at 4x, to a file (hd.c) */
        *got = hd_save(what + 3);
        return 1;
    }
#endif
    if (!strncmp(what, "why:", 4)) {      /* PLAY's red notice of why the channel is silent ("why:" none) */
        const char *nm = silent_why(set.channel), *w = what + 4;
        uint32_t k = 0;
        nm = nm ? nm : "";
        while (w[k] && nm[k] && (nm[k] == w[k] || (w[k] == '_' && nm[k] == ' ')))
            k++;
        *got = !w[k] && !nm[k];
        return 1;
    }
    if (!strcmp(what, "gated")) {        /* the selected channel's voices still held (gate on) */
        synth_vview_t vv[SYN_VMAX];
        uint32_t n = synth_voice_view(set.channel, vv), j;
        *got = 0;
        for (j = 0; j < n; j++)
            *got += vv[j].gate != 0;
        return 1;
    }
    if (!strncmp(what, "chain", 5) && what[5] >= '0' && what[5] <= '9') {   /* chainN: section N's scene + 1 */
        uint32_t i = (uint32_t)atoi(what + 5);
        *got = i < song.nchain ? song.chain[i].scene + 1 : 0;
        return 1;
    }
    if (!strcmp(what, "song_mode")) {    /* 1: the song plays its order (SAVE + G#), 0 LOOP */
        *got = song.mode == MODE_SONG;
        return 1;
    }
    if (!strcmp(what, "ed_owed")) {      /* the editor's pushes owed (no SysEx room yet): bit 0 STATE, bit 1 the playhead */
        *got = ed_owed;
        return 1;
    }
    if (!strcmp(what, "ed_pushed")) {    /* the last STATE pushed: its transport byte (bit 0 playing); -1 none yet */
        *got = ed_pushed_tr;
        return 1;
    }
    if (!strcmp(what, "song_end")) {     /* the song's END: 1 STOP */
        *got = seq_chain_end_stop();
        return 1;
    }
    if (!strcmp(what, "hold")) {         /* HOLD on the selected channel (SHIFT + PLAY) */
        *got = synth_sound(set.channel)->arp_hold;
        return 1;
    }
    if (!strcmp(what, "drones")) {       /* the selected channel's drones sounding */
        *got = seq_drones(set.channel);
        return 1;
    }
    if (!strcmp(what, "chord")) {        /* the song's CHORD (SEL KEY, KNOB 4): 0 OFF .. 5 POWER */
        *got = song.chord;
        return 1;
    }
    if (!strcmp(what, "transpose")) {    /* the song's TRANS (SEL) */
        *got = song.transpose;
        return 1;
    }
#ifdef PLAT_HOST
    if (!strcmp(what, "prof")) {          /* the render's time by part since the last (printed), 1 */
        static uint32_t since;
        void synth_prof_dump(uint32_t real_ms);
        synth_prof_dump(plat_ms() - since ? plat_ms() - since : 1u);
        since = plat_ms();
        *got = 1;
        return 1;
    }
#endif
    if (!strcmp(what, "budget_used")) {  /* the voices' weighed cost now (0.1 % of the FM-1's render time, synth.c) */
        *got = (int)synth_budget_used();
        return 1;
    }
    if (!strcmp(what, "load_est")) {     /* the FM-1's render load, estimated, %: the voices' cost, and ~20 % for the
                                          * effects, the channels' filters and inserts, the mix (the host's measure x40) */
        *got = (int)(synth_budget_used() / 10u) + 20;
        return 1;
    }
    if (!strcmp(what, "draw_factory")) { /* a factory reset's screen, 42% (its pictures) */
        factory_screen(420);
        *got = 1;
        return 1;
    }
    if (!strcmp(what, "draw_update")) {  /* the screen an install begins with, drawn now (its pictures) */
        update_screen();
        *got = 1;
        return 1;
    }
    if (!strcmp(what, "installed")) {    /* INSTALLED on the screen now (a new build's first start) */
        *got = !strcmp(ui.msg[0], "INSTALLED") && plat_ms() < ui.msg_until;
        return 1;
    }
    if (!strcmp(what, "home_view")) {    /* SETTINGS SCREEN 2 HOME VIEW: 0 PLAY, 1 LOOP */
        *got = prefs.home_view;
        return 1;
    }
    if (!strcmp(what, "batt_pct")) {     /* the battery's percentage SYSTEM shows, -1 none (USB) */
        *got = batt_pct();
        return 1;
    }
    if (!strncmp(what, "layer_kept", 10)) {   /* a layer kept open (HOME tapped while it was held): its button */
        *got = ui.layer_lock ? ui.layer : -1;
        return 1;
    }
    if (!strncmp(what, "name:", 5) || !strncmp(what, "own:", 4) || (what[0] == 'u' && what[3] == ':')) {
        /* name:X the channel's sound named X; own:X its own name X (a user preset's, "" none); uNN:X slot NN's */
        const char *nm = what[0] == 'n' ? synth_preset_name(set.channel) : what[0] == 'o' ? synth_own_name(set.channel)
                                        : user_name((uint32_t)((what[1] - '0') * 10 + what[2] - '0' + 31) % 32u);
        const char *w = what + (what[0] == 'n' ? 5 : 4);
        uint32_t k = 0;                  /* ('_' in the script: a space) */
        while (w[k] && nm[k] && (nm[k] == w[k] || (w[k] == '_' && nm[k] == ' ')))
            k++;
        *got = !w[k] && !nm[k];
        return 1;
    }
    if (!strcmp(what, "octave"))
        *got = set.octave;
    else if (!strcmp(what, "channel"))
        *got = set.channel;
    else if (!strcmp(what, "engine"))
        *got = synth_sound(set.channel)->engine;
    else if (!strcmp(what, "preset"))
        *got = synth_sound(set.channel)->preset;
    else if (!strcmp(what, "vsens"))
        *got = set.vsens;
    else if (!strcmp(what, "key_learn"))
        *got = prefs.key_learn;
    else if (!strcmp(what, "fixed_vel"))
        *got = prefs.fixed_vel;
    else if (!strcmp(what, "prs_mode"))
        *got = prefs.prs_mode;
    else if (!strcmp(what, "display"))       /* 0 dark, 1 light */
        *got = prefs.display;
    else if (!strcmp(what, "named"))         /* the channels with a name: bit ch */
    {
        uint32_t ch;
        *got = 0;
        for (ch = 0; ch < NCH; ch++)
            if (song.name[ch][0])
                *got |= 1 << ch;
    }
    else if (!strcmp(what, "mute"))          /* the channels muted, soloed: bit ch */
        *got = (int)seq_mute();
    else if (!strcmp(what, "solo"))
        *got = (int)seq_solo();
    else if (!strcmp(what, "audible"))       /* the selected channel sounds (synth_audible) */
        *got = synth_audible(set.channel);
    else if (!strcmp(what, "voices"))       /* voices sounding on the selected channel */
        *got = (int)synth_voices(set.channel);
    else if (!strncmp(what, "voices", 6))   /* voicesN: on channel N (1..6) */
        *got = (int)synth_voices((uint32_t)(atoi(what + 6) - 1) % NCH);
    else if (!strcmp(what, "note"))
        *got = last_note;
    else if (!strcmp(what, "dirty"))
        *got = set_dirty;
    else if (!strcmp(what, "song_dirty"))
        *got = seq_dirty();
    else if (!strcmp(what, "vel"))           /* the last key's velocity and pressure */
        *got = ui.vel[ui.last_key];
    else if (!strcmp(what, "prs"))
        *got = ui.prs[ui.last_key];
    else if (!strcmp(what, "playing"))
        *got = seq_playing();
    else if (!strcmp(what, "rec"))           /* REC_*: 0 off, 1 armed, 2 count, 3 free take, 4 recording */
        *got = (int)seq_rec();
    else if (!strcmp(what, "bpm10"))
        *got = (int)seq_bpm10();
    else if (!strcmp(what, "events"))        /* the selected channel's pattern: its notes and locks */
        *got = seq_pat(set.channel)->n;
    else if (!strncmp(what, "events", 6))    /* eventsN: channel N's */
        *got = seq_pat((uint32_t)(atoi(what + 6) - 1) % NCH)->n;
    else if (!strcmp(what, "bars"))
        *got = seq_pat(set.channel)->bars;
    else if (!strcmp(what, "pattern"))
        *got = (int)seq_pat_index(set.channel);
    else if (!strcmp(what, "scene"))
        *got = (int)seq_scene();
    else if (!strcmp(what, "queued"))
        *got = seq_queued();
    else if (!strcmp(what, "nchain"))
        *got = song.nchain;
    else if (!strcmp(what, "section"))
        *got = (int)seq_section();
    else if (!strcmp(what, "slot"))
        *got = (int)seq_slot();
    else if (!strncmp(what, "slot_used", 9))   /* slot_usedN: song N (1..8) in flash */
        *got = seq_slot_used((uint32_t)(atoi(what + 9) - 1) % NSONG);
    else if (!strcmp(what, "page"))
        *got = ui.page;
    else if (!strcmp(what, "layer"))
        *got = ui.layer;
    else if (!strcmp(what, "list"))          /* 1 the SETTINGS screen open */
        *got = ui.group_btn == G_MENU;
    else if (!strcmp(what, "level"))
        *got = (int)synth_get_level(set.channel);
    else if (!strcmp(what, "root"))
        *got = song.root;
    else if (!strcmp(what, "scale"))
        *got = song.scale;
    else if (!strcmp(what, "midi_ch1"))
        *got = prefs.midi_ch[0];
    else if (!strcmp(what, "cut"))
        *got = synth_param(set.channel, SP_CUT);
    else if (!strcmp(what, "atk"))
        *got = synth_param(set.channel, SP_ATK);
    else if (!strcmp(what, "learn_cc0"))     /* MIDI LEARN: the first CC learned */
        *got = prefs.learn[0][0];
    else if (!strcmp(what, "quiet"))         /* the render's blocks done at no cost since the last ask, % */
        *got = (int)synth_quiet_pct();
    else if (!strcmp(what, "logo"))          /* the boot animation playing */
        *got = ui.logo_on;
    else if (!strcmp(what, "fm_op"))         /* the FM6 operator the pages edit (0..5) */
        *got = ui.fm_op;
    else if (!strcmp(what, "fm_mutes"))      /* FM6: the operators muted, bit o */
        *got = (int)synth_fm6_mutes(set.channel);
    else if (!strcmp(what, "mono"))          /* VCE: 0 POLY, 1 MONO, 2 LEG, 3 UNI */
        *got = synth_param(set.channel, SP_MONO);
    else if (!strcmp(what, "glide"))
        *got = synth_param(set.channel, SP_GLIDE);
    else if (!strcmp(what, "home_lit"))      /* HOME's light: on with MAIN on screen */
        *got = (int)(ui.lit >> B_HOME & 1u);
    else if (!strcmp(what, "replace"))       /* REC MODE: 1 REPLACE */
        *got = seq_cfg.rec_replace;
    else if (!strcmp(what, "step_page"))     /* SEQ: the bar the keys show */
        *got = ui.step_page;
    else if (!strcmp(what, "fill"))          /* a fill playing (GLO + E4 held, or the next bar) */
        *got = seq_fill();
    else if (!strncmp(what, "mod_", 4)) {   /* mod_cut, mod_pit, mod_shp, mod_amp, mod_pan, mod_lfo: the size of the slots' sum now */
        static const char *const T[MT_N] = {"off", "cut", "pit", "shp", "amp", "pan", "lfo"};
        uint32_t d;
        *got = 0;
        for (d = 0; d < MT_N; d++)
            if (!strcmp(what + 4, T[d]))
                *got = synth_mod_now(set.channel, d) < 0 ? -synth_mod_now(set.channel, d) : synth_mod_now(set.channel, d);
    }
    else if (!strncmp(what, "slot", 4))     /* slotN: mod slot N's amount (1..4) on the selected channel */
        *got = synth_mod(set.channel)->m[(atoi(what + 4) + 3) % SYN_MODS].amt;
    else if (!strcmp(what, "render_permille")) {   /* the host's render time against the blocks' real time, since asked */
        *got = rd_budget ? (int)(rd_ticks * 1000u / rd_budget) : 0;
        rd_ticks = rd_budget = 0;
    }
    else if (!strcmp(what, "draw_us")) {     /* the host's time a frame's drawing took, on average, since asked last */
        *got = draw_n ? (int)(draw_ns / draw_n / 1000u) : 0;
        draw_ns = draw_n = 0;
    }
    else if (!strcmp(what, "view"))          /* MAIN: 0 PLAY, 1 LOOP */
        *got = ui.loop;
    else if (!strcmp(what, "prow"))          /* PLAY: the first channel of the four shown, 0..2 */
        *got = ui.pv_top;
    else if (!strcmp(what, "graph"))     /* the graph shown: 0 none, else its kind (ui.c GR_*) */
        *got = (int32_t)graph_now();
    else if (!strcmp(what, "tune"))
        *got = prefs.tune;
    else if (!strcmp(what, "overloads"))
        *got = (int)overloads;
    else if (!strcmp(what, "launch"))
        *got = seq_cfg.launch;
    else if (!strncmp(what, "turns", 5))    /* turnsN: encoder N's count (plat.h EN_*) */
        *got = ui.turns[atoi(what + 5) % NE];
    else
        return 0;
    return 1;
}
#endif


/* -------------------------------------------------------------- the editor --- */
/* (docs/EDITOR-PROTOCOL.md) The preset file, version 1, little-endian:
 *   u32 magic "NWPR", u8 version 1, u8 engine, u8 flags (bit 0 play it on the selected channel, bit 1 keep it in user
 *   slot SLOT; reading gives 1), u8 slot 0..31, u8 name length 0..16, 16 bytes the name (padded with 0), u8 n, n x
 *   (u16 id, i16 value) the sound's values by parameter id (synth.h SP_*: the engine's eight, the envelope, the filter,
 *   the LFO, the sends, ENV 2 and the four mod slots; an id this firmware does not know is skipped, one left out keeps
 *   the engine preset's value), u8 has FM6 patch, then its 96 bytes when 1.
 * A value by id, not a struct: a sound saved by one firmware loads in the next, whatever it added. */
#define PRESET_MAGIC 0x5250574Eu         /* "NWPR" */
#define PRESET_VER 1u
#define PRESET_MAX 640u
static uint8_t pre_buf[PRESET_MAX], pre_in[PRESET_MAX];
static uint32_t pre_n, pre_w;            /* the sound as a file (read), the bytes of a write under way (pre_in) */

/* the parameters the editor sees (and a preset carries), each with its group */
enum { PG_OSC, PG_FILTERG, PG_ENVG, PG_LFOG, PG_FXG, PG_MODG, PG_KEYG, PG_MIDIG, PG_CLOCKG, PG_NGROUPS };
static const struct { uint16_t id; uint8_t group; } EDP[] = {
    {SP_E0, PG_OSC}, {SP_E1, PG_OSC}, {SP_E2, PG_OSC}, {SP_E3, PG_OSC},
    {SP_E4, PG_OSC}, {SP_E5, PG_OSC}, {SP_E6, PG_OSC}, {SP_E7, PG_OSC},   /* (EDIT 2: filter on ANALOG and SUPER) */
    {SP_MONO, PG_OSC}, {SP_GLIDE, PG_OSC},
    {SP_FLT, PG_FILTERG}, {SP_CUT, PG_FILTERG}, {SP_RES, PG_FILTERG}, {SP_TFILT, PG_FILTERG}, {SP_FTYPE, PG_FILTERG},
    {SP_ATK, PG_ENVG}, {SP_DEC, PG_ENVG}, {SP_SUS, PG_ENVG}, {SP_REL, PG_ENVG},
    {SP_FENV, PG_ENVG}, {SP_PENV, PG_ENVG}, {SP_SENV, PG_ENVG},
    {SP_E2_ATK, PG_ENVG}, {SP_E2_DEC, PG_ENVG}, {SP_E2_SUS, PG_ENVG}, {SP_E2_REL, PG_ENVG},
    {SP_LFO_RATE, PG_LFOG}, {SP_LFO_WAVE, PG_LFOG}, {SP_LFO_PHASE, PG_LFOG}, {SP_LFO_FADE, PG_LFOG},
    {SP_LFO_SYNC, PG_LFOG}, {SP_LFO_TRIG, PG_LFOG}, {SP_LFO_POL, PG_LFOG},
    {SP_LFO_PIT, PG_LFOG}, {SP_LFO_FLT, PG_LFOG}, {SP_LFO_SHP, PG_LFOG}, {SP_LFO_AMP, PG_LFOG},
    {SP_DST, PG_FXG}, {SP_CHO, PG_FXG}, {SP_DLY, PG_FXG}, {SP_REV, PG_FXG}, {SP_PAN, PG_FXG},
    {SP_M1_SRC, PG_MODG}, {SP_M1_DST, PG_MODG}, {SP_M1_AMT, PG_MODG}, {SP_M2_SRC, PG_MODG}, {SP_M2_DST, PG_MODG},
    {SP_M2_AMT, PG_MODG}, {SP_M3_SRC, PG_MODG}, {SP_M3_DST, PG_MODG}, {SP_M3_AMT, PG_MODG}, {SP_M4_SRC, PG_MODG},
    {SP_M4_DST, PG_MODG}, {SP_M4_AMT, PG_MODG},
};
#define NEDP ((uint32_t)(sizeof EDP / sizeof EDP[0]))
#define EDP_ENGINE 1000u                 /* (the engine: a parameter of its own id) */
#define EDP_WAVE 1001u                   /* (read only: the oscillator type now, WAVE_NAMES) */
#define EDP_SHAPE 1002u                  /* (read only: the shape now, 0..127, with its modulation) */
static const char *const ENGINE_NAMES[] = {"ANALOG", "FM6", "PHASE", "LOFI", "SAMPLE", "VOICE", "TRIO", "WHEEL",
                                           "GRAIN", "PHYS", "NOISE", "SLICE", "DRUM", "SUPER"};
/* every oscillator type, for the editor to draw (docs/EDITOR-PROTOCOL.md, "waveform hints") */
static const char *const WAVE_NAMES[] = {"SAW", "SQR", "TRI", "SIN", "PWM", "SYNC", "SUB", "SUPERSAW", "SUPERSQR",
                                         "SUPERTRI", "PULSE", "MORPH", "FM", "PD", "CHIP", "SAMPLE", "FORMANT", "TRIO",
                                         "ORGAN", "GRAIN", "MODAL", "NOISE", "SLICE", "KIT"};
static int32_t wave_now(uint32_t ch)
{
    static const int8_t OTHER[14] = {-1, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, -1};
    uint32_t e = synth_sound(ch)->engine;
    int32_t w = synth_param(ch, SP_E0);
    if (e == 0u)                         /* ANALOG: SAW SQR TRI SIN PWM SYNC SUB, as they are */
        return clamp(w, 0, 6);
    if (e == 13u)                        /* SUPER: SAW SQR TRI stacks, PULSE, SYNC, MORPH */
        return w <= 2 ? 7 + w : w == 3 ? 10 : w == 4 ? 5 : 11;
    return e < 14u ? OTHER[e] : 0;
}

/* the FM-1's own settings (not a channel's): ids 0xF000 on, after the channel's (docs/EDITOR-PROTOCOL.md "Global
 * parameters"), set as SEL KEY, SETTINGS MIDI and SEQ set them */
#define GP_ROOT 0xF000u
#define GP_SCALE 0xF001u
#define GP_KEYS 0xF002u
#define GP_CHORD 0xF003u
#define GP_TRANS 0xF004u
#define GP_OCT 0xF005u
#define GP_MIDI 0xF010u                  /* 0xF010 .. 0xF015: channel 1..6's MIDI channel, in and out */
#define GP_IN 0xF016u
#define GP_OUT 0xF017u
#define GP_SYNC 0xF020u
#define GP_CLKOUT 0xF021u
#define GP_LAUNCH 0xF022u
static const uint16_t GPID[] = {GP_ROOT, GP_SCALE, GP_KEYS, GP_CHORD, GP_TRANS, GP_OCT, GP_MIDI, GP_MIDI + 1u,
                                GP_MIDI + 2u, GP_MIDI + 3u, GP_MIDI + 4u, GP_MIDI + 5u, GP_IN, GP_OUT, GP_SYNC, GP_CLKOUT,
                                GP_LAUNCH};
#define NGP ((uint32_t)(sizeof GPID / sizeof GPID[0]))
#define GP_AT (NEDP + 3u)                /* (the first global's index) */
static const char *const GP_MCH[17] = {"OFF", "1", "2", "3", "4", "5", "6", "7", "8", "9", "10", "11", "12", "13",
                                       "14", "15", "16"};
static const char *const GP_INN[18] = {"NOTES", "CLOCK", "CH 1", "CH 2", "CH 3", "CH 4", "CH 5", "CH 6", "CH 7",
                                       "CH 8", "CH 9", "CH 10", "CH 11", "CH 12", "CH 13", "CH 14", "CH 15", "CH 16"};
static const char *const GP_OUTN[2] = {"KEYS", "SEQ"};
static const char *const GP_SYNCN[4] = {"INT", "USB", "TRS", "BLE"};
static const char *const GP_ONOFF[2] = {"OFF", "ON"};
static const char *const GP_LAUNCHN[3] = {"BAR", "LOOP", "NOW"};
static const char *const GP_MCHN[NCH] = {"MIDI CH 1", "MIDI CH 2", "MIDI CH 3", "MIDI CH 4", "MIDI CH 5", "MIDI CH 6"};
static void gp_desc(uint32_t id, plat_param_t *p)
{
    const char *const *l = 0;
    int32_t lo = 0, hi = 0, def = 0;
    const char *nm = "", *unit = 0;
    uint32_t g = PG_KEYG;
    switch (id) {
    case GP_ROOT: nm = "ROOT"; hi = 11; l = NOTE_NAME; break;
    case GP_SCALE: nm = "SCALE"; hi = 15; l = SCALE_NAME; break;
    case GP_KEYS: nm = "KEYS"; hi = 2; l = KEYS_NAME; break;
    case GP_CHORD: nm = "CHORD"; hi = 5; l = CHORD_NAME; break;
    case GP_TRANS: nm = "TRANS"; lo = -24; hi = 24; unit = "st"; break;
    case GP_OCT: nm = "OCTAVE"; lo = -2; hi = 2; break;
    case GP_IN: nm = "MIDI IN"; hi = 17; l = GP_INN; g = PG_MIDIG; break;
    case GP_OUT: nm = "MIDI OUT"; hi = 1; l = GP_OUTN; g = PG_MIDIG; break;
    case GP_SYNC: nm = "CLOCK"; hi = FM1B_BLE ? 3 : 2; l = GP_SYNCN; g = PG_CLOCKG; break;
    case GP_CLKOUT: nm = "CLOCK OUT"; hi = 1; l = GP_ONOFF; g = PG_CLOCKG; break;
    case GP_LAUNCH: nm = "SCENE LAUNCH"; hi = 2; l = GP_LAUNCHN; g = PG_CLOCKG; break;
    default:                             /* GP_MIDI + channel */
        nm = GP_MCHN[(id - GP_MIDI) % NCH]; hi = 16; l = GP_MCH; g = PG_MIDIG; def = (int32_t)(id - GP_MIDI) + 1; break;
    }
    p->id = (uint16_t)id;
    p->type = l ? PLAT_PT_ENUM : PLAT_PT_NUMBER;
    p->group = (uint8_t)g;
    p->min = lo;
    p->max = hi;
    p->def = def;
    p->name = nm;
    p->unit = unit;
    p->labels = l;
}
static int32_t gp_get(uint32_t id)
{
    switch (id) {
    case GP_ROOT: return song.root;
    case GP_SCALE: return song.scale;
    case GP_KEYS: return song.keys;
    case GP_CHORD: return song.chord;
    case GP_TRANS: return song.transpose;
    case GP_OCT: return set.octave;
    case GP_IN: return prefs.in_mode;
    case GP_OUT: return seq_cfg.seq_out;
    case GP_SYNC: return seq_cfg.sync;
    case GP_CLKOUT: return seq_cfg.clock_out;
    case GP_LAUNCH: return seq_cfg.launch;
    default: return prefs.midi_ch[(id - GP_MIDI) % NCH];
    }
}
static void gp_set(uint32_t id, int32_t v)   /* (v within the range) */
{
    switch (id) {
    case GP_ROOT: case GP_SCALE: case GP_KEYS: case GP_CHORD: case GP_TRANS:
        keys_let_go();                   /* (the notes the keys hold end, as the knobs do) */
        if (id == GP_ROOT) song.root = (uint8_t)v;
        else if (id == GP_SCALE) song.scale = (uint8_t)v;
        else if (id == GP_KEYS) song.keys = (uint8_t)v;
        else if (id == GP_CHORD) song.chord = (uint8_t)v;
        else song.transpose = (int8_t)v;
        dirty();
        return;
    case GP_OCT:
        if (v != set.octave) {
            keys_let_go();
            set.octave = (int8_t)v;
            app_mark();
        }
        return;
    case GP_IN: prefs.in_mode = (uint8_t)v; break;
    case GP_OUT: seq_cfg.seq_out = (uint8_t)v; break;
    case GP_SYNC: seq_cfg.sync = (uint8_t)v; break;
    case GP_CLKOUT: seq_cfg.clock_out = (uint8_t)v; break;
    case GP_LAUNCH: seq_cfg.launch = (uint8_t)v; break;
    default: {                           /* two channels never share a MIDI channel: one taken is refused */
        uint32_t c = (id - GP_MIDI) % NCH, i;
        for (i = 0; i < NCH && v; i++)
            if (i != c && prefs.midi_ch[i] == v)
                return;
        prefs.midi_ch[c] = (uint8_t)v;
        break;
    }
    }
    app_mark();
}

int app_param(uint32_t i, plat_param_t *p)
{
    uint32_t ch = set.channel;
    int32_t lo, hi;
    memset(p, 0, sizeof *p);
    if (i < NEDP) {
        uint32_t id = EDP[i].id;
        const char *const *names = synth_param_names(ch, id);
        synth_param_range(ch, id, &lo, &hi);
        p->id = (uint16_t)id;
        p->type = names ? PLAT_PT_ENUM : PLAT_PT_NUMBER;
        p->group = EDP[i].group;
        if (id >= SP_E4 && id <= SP_E7 && synth_page_title(ch, 1)[0] == 'F')   /* (EDIT 2 a filter: its group) */
            p->group = PG_FILTERG;
        p->min = lo;
        p->max = hi;
        p->def = synth_param_default(ch, id);
        p->name = synth_param_label(ch, id);
        p->labels = names;
        return 0;
    }
    switch (i - NEDP) {
    case 0: p->id = EDP_ENGINE; p->type = PLAT_PT_ENUM; p->group = PG_OSC; p->max = 13; p->name = "ENGINE";
        p->labels = ENGINE_NAMES; return 0;
    case 1: p->id = EDP_WAVE; p->type = PLAT_PT_ENUM; p->group = PG_OSC; p->max = 23; p->name = "WAVEFORM";
        p->labels = WAVE_NAMES; p->flags = PLAT_PA_READONLY; return 0;
    case 2: p->id = EDP_SHAPE; p->type = PLAT_PT_NUMBER; p->group = PG_OSC; p->max = 127; p->def = 64; p->name = "SHAPE NOW";
        p->flags = PLAT_PA_READONLY; return 0;
    default:
        if (i - GP_AT >= NGP)
            return -1;
        gp_desc(GPID[i - GP_AT], p);
        return 0;
    }
}
const char *app_param_group(uint32_t g)
{
    static const char *const G[PG_NGROUPS] = {"osc", "filter", "env", "lfo", "fx", "mod", "key", "midi", "clock"};
    return g < PG_NGROUPS ? G[g] : 0;
}
int32_t app_param_get(uint32_t i)
{
    uint32_t ch = set.channel;
    if (i < NEDP)
        return synth_param(ch, EDP[i].id);
    switch (i - NEDP) {
    case 0: return synth_sound(ch)->engine;
    case 1: return wave_now(ch);
    case 2: return clamp(synth_param(ch, SP_E3) + (synth_mod_now(ch, MT_SHP) >> 9), 0, 127);
    default: return i - GP_AT < NGP ? gp_get(GPID[i - GP_AT]) : 0;
    }
}
int32_t app_param_set(uint32_t i, int32_t v)
{
    uint32_t ch = set.channel;
    if (i < NEDP) {
        synth_param_set(ch, EDP[i].id, v);
        seq_touch();
    } else if (i == NEDP) {
        synth_engine(ch, (uint32_t)clamp(v, 0, 13));
        seq_touch();
    } else if (i - GP_AT < NGP) {
        plat_param_t p;
        gp_desc(GPID[i - GP_AT], &p);
        gp_set(GPID[i - GP_AT], clamp(v, p.min, p.max));
    }
    return app_param_get(i);
}

static uint32_t preset_make(uint32_t ch, uint8_t *b)   /* the channel's sound as a preset file: its size */
{
    uint32_t n = 0, i, k = 0;
    const char *nm = synth_preset_name(ch);
    uint32_t magic = PRESET_MAGIC;
    memcpy(b, &magic, 4);
    b[4] = PRESET_VER;
    b[5] = (uint8_t)synth_sound(ch)->engine;
    b[6] = 1;
    b[7] = 0;
    while (nm[k] && k < 16u)
        k++;
    b[8] = (uint8_t)k;
    memset(b + 9, 0, 16);
    memcpy(b + 9, nm, k);
    n = 26;
    for (i = 0; i < NEDP; i++) {
        int16_t v = (int16_t)synth_param(ch, EDP[i].id);
        uint16_t id = EDP[i].id;
        memcpy(b + n, &id, 2);
        memcpy(b + n + 2, &v, 2);
        n += 4;
    }
    b[25] = (uint8_t)NEDP;
    b[n++] = synth_sound(ch)->engine == 1u;
    if (b[n - 1]) {
        memcpy(b + n, synth_fm6_own(ch), SYN_FM6_BYTES);
        n += SYN_FM6_BYTES;
    }
    return n;
}

static int preset_take(const uint8_t *b, uint32_t size, uint32_t ch)   /* a preset file played on ch (and kept): 0 ok */
{
    uint32_t magic, i, n, at;
    if (size < 27u)
        return -1;
    memcpy(&magic, b, 4);
    if (magic != PRESET_MAGIC || b[4] < 1u || b[5] >= synth_nengines() || b[8] > 16u)
        return -1;                       /* (a later version: the fields it adds come after these; this one reads on) */
    n = b[25];
    if (26u + n * 4u + 1u > size)
        return -1;
    synth_engine(ch, b[5]);              /* the engine at its first preset, then the values the file gives */
    for (i = 0, at = 26; i < n; i++, at += 4u) {
        uint16_t id;
        int16_t v;
        memcpy(&id, b + at, 2);
        memcpy(&v, b + at + 2, 2);
        if (id < SP_N)
            synth_param_set(ch, id, v);
    }
    if (b[at] == 1u && at + 1u + SYN_FM6_BYTES <= size)
        memcpy(synth_fm6_own(ch), b + at + 1, SYN_FM6_BYTES);
    if (b[8])                            /* (its name: on the screen, kept with it in a user slot) */
        synth_set_name(ch, (const char *)b + 9, b[8]);
    seq_touch();
    if (b[6] & 2u) {                     /* kept in a user slot (while stopped) */
        if (!user_save(b[7] % 32u, ch)) {
            app_say("PRESET", "STOP FIRST");
            return 0;
        }
        {
            char t[8] = "USER ";
            t[5] = (char)('0' + (b[7] % 32u + 1u) / 10u);
            t[6] = (char)('0' + (b[7] % 32u + 1u) % 10u);
            t[7] = 0;
            app_say("PRESET SAVED", t);
        }
    } else
        app_say("PRESET", "FROM THE EDITOR");
    return 0;
}

/* the app's own files: 0 "preset" (the editor's), 1..8 "song-a".."song-h" (read: a backup before a factory reset),
 * 9 "user-presets", 10 "fm6-bank" (read), 11 "factory-reset" (write the words FR_WORDS: everything nullwave keeps
 * erased, then a restart) */
#define FR_WORDS "nullwave factory reset"
enum { AF_PRESET, AF_SONG, AF_USERS = AF_SONG + NSONG, AF_BANK, AF_FACTORY, AF_N };
static const char *const SONG_FILE[NSONG] = {"song-a", "song-b", "song-c", "song-d", "song-e", "song-f", "song-g",
                                              "song-h"};
static const uint8_t *af_raw(uint32_t i, uint32_t *size)   /* a read-only file's bytes in the flash, its size */
{
    uint32_t ver;
    const uint8_t *d = 0;
    *size = 0;
    if (i >= AF_SONG && i < AF_SONG + NSONG)
        d = slot_data(i - AF_SONG, size, &ver);
    else if (i == AF_USERS || i == AF_BANK) {
        uint32_t k, n = i == AF_USERS ? 8192u : 4096u, used = 0;
        d = plat_data(i == AF_USERS ? USER_BASE : FMBANK_BASE);
        for (k = 0; d && k < n && !used; k++)
            used = d[k] != 0xFFu;
        *size = used ? n : 0u;
    }
    return d;
}
int app_file(uint32_t i, plat_file_info_t *f)
{
    if (i >= AF_N)
        return -1;
    if (i == AF_PRESET) {
        pre_n = preset_make(set.channel, pre_buf);
        f->name = "preset";
        f->kind = PLAT_FK_PRESET;
        f->flags = PLAT_FF_READ | PLAT_FF_WRITE | PLAT_FF_PLAYING;   /* (heard while playing too; kept only stopped) */
        f->size = pre_n;
        f->room = PRESET_MAX;
        return 0;
    }
    if (i == AF_FACTORY) {
        f->name = "factory-reset";
        f->kind = PLAT_FK_OTHER;
        f->flags = PLAT_FF_WRITE | PLAT_FF_PLAYING;   /* (it stops the sequencer itself) */
        f->size = 0;
        f->room = 32;
        return 0;
    }
    af_raw(i, &f->size);
    f->name = i == AF_USERS ? "user-presets" : i == AF_BANK ? "fm6-bank" : SONG_FILE[i - AF_SONG];
    f->kind = i == AF_BANK ? PLAT_FK_BANK : i == AF_USERS ? PLAT_FK_PRESET : PLAT_FK_PROJECT;
    f->flags = PLAT_FF_READ;
    f->room = i == AF_USERS ? 8192u : i == AF_BANK ? 4096u : SONG_SIZE;
    return 0;
}
int app_file_read(uint32_t i, uint32_t off, uint8_t *dst, uint32_t n)
{
    uint32_t size;
    const uint8_t *d;
    if (i == AF_PRESET) {
        if (off + n > pre_n)
            return -1;
        memcpy(dst, pre_buf + off, n);
        return 0;
    }
    d = af_raw(i, &size);
    if (!d || off + n > size)
        return -1;
    memcpy(dst, d + off, n);
    return 0;
}
int app_file_begin(uint32_t i, uint32_t size)
{
    pre_w = 0;
    return (i == AF_PRESET && size <= PRESET_MAX) || (i == AF_FACTORY && size <= 32u) ? 0 : -1;
}
int app_file_write(uint32_t i, uint32_t off, const uint8_t *src, uint32_t n)
{
    if ((i != AF_PRESET && i != AF_FACTORY) || off != pre_w || off + n > PRESET_MAX)
        return -1;
    memcpy(pre_in + off, src, n);
    pre_w += n;
    return 0;
}
int app_file_commit(uint32_t i, uint32_t size, uint32_t crc)
{
    (void)crc;                           /* (the platform checked it) */
    if (size != pre_w)
        return -1;
    if (i == AF_FACTORY) {               /* (the words exactly: nothing else erases anything) */
        uint32_t k;
        if (size != sizeof FR_WORDS - 1u)
            return -1;
        for (k = 0; k < size; k++)       /* (no memcmp on the device) */
            if (pre_in[k] != (uint8_t)FR_WORDS[k])
                return -1;
        factory_start();
        return 0;
    }
    if (i != AF_PRESET)
        return -1;
    return preset_take(pre_in, size, set.channel);
}
int app_file_delete(uint32_t i) { (void)i; return -1; }
