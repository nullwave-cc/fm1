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
} prefs_t;
static prefs_t prefs;
static uint8_t set_dirty;                /* changed since the last write: written when the FM-1 is quiet */
static uint32_t set_dirty_at;
static void keys_config(void);
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
    uint32_t f = plat_lock();
    synth_note_off(ch, (uint32_t)note);
    plat_unlock(f);
    seq_note_off(ch, (uint32_t)note);
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
            synth_note_off(ch, d1);
            seq_note_off(ch, d1);
            if (last_note == (int32_t)d1)
                last_note = -1;
        }
    }
}

/* the render's load: its time against the block's, a peak that falls (GLO SYSTEM CPU) */
static volatile uint32_t nw_cpu;
static uint32_t shed_wait;               /* blocks before the overload guard may fade another voice */
static volatile uint32_t sheds;          /* voices it faded (the screen says VOICE LIMIT) */
void app_render(int32_t *lr, uint32_t n, uint32_t master_q12)
{
    uint32_t t0 = plat_ticks24(), dt, budget = n * (PLAT_TICKS_PER_US * 1000000u / 44100u), p;
    midi_in();
    seq_render(n);
    synth_render(lr, n, master_q12);
    dt = (plat_ticks24() - t0) & 0xFFFFFFu;   /* (a 24-bit clock: its wrap is no huge span) */
    p = budget ? dt * 1600u / budget : 0u;   /* (x16: the peak falls a 64th a block, ~1.5 s) */
    if (shed_wait)
        shed_wait--;
    else if (p > 1520u) {                /* the last resort, within 5 % of the deadline: the oldest voice faded, one
                                          * at a time, then a rest of 16 blocks (~12 ms) to see what that did */
        synth_shed();
        shed_wait = 16;
        sheds++;
    }
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
    int ok = plat_store_load(OBJ_SOUND, &prefs, sizeof prefs) == (int)sizeof prefs && prefs.magic == PREFS_MAGIC
             && prefs.key_learn <= 1u && prefs.fixed_vel <= 127u && prefs.prs_mode < PRS_NMODES
             && prefs.display <= 1u && prefs.slot < NSONG && prefs.sc.rec_bars >= 1u && prefs.sc.rec_bars <= 8u
             && prefs.sc.launch <= LQ_NOW && prefs.sc.sync <= SYNC_BLE && prefs.sc.click <= CLICK_ON
             && prefs.in_mode <= 17u && prefs.other <= 1u && prefs.bend >= 1u && prefs.bend <= 12u && prefs.drmap <= 1u
             && prefs.remote <= 16u && prefs.fxch <= 16u && prefs.tune >= -50 && prefs.tune <= 50
             && prefs.reso <= 10u && prefs.pfade <= 15u && prefs.graphs <= 1u;
    for (i = 0; ok && i < NCH; i++)
        ok = prefs.midi_ch[i] <= 16u;
    if (!ok)
        prefs_defaults();
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
} user_t;
_Static_assert(sizeof(user_t) <= 256u, "a user preset fits its 256 bytes");
static const user_t *user_at(uint32_t i)
{
    const uint8_t *d = plat_data(USER_BASE + (i / 16u) * 4096u);
    const user_t *u = d ? (const user_t *)(const void *)(d + (i % 16u) * 256u) : 0;
    return u && u->magic == USER_MAGIC && synth_sound_ok(&u->snd) ? u : 0;
}
static int user_used(uint32_t i) { return user_at(i % 32u) != 0; }
static int user_load(uint32_t i, uint32_t ch)
{
    const user_t *u = user_at(i % 32u);
    if (!u)
        return 0;
    memcpy(synth_fm6_own(ch), u->fm6, SYN_FM6_BYTES);
    synth_set_sound(ch, &u->snd);
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

#include "ui_draw.c"
#include "ui.c"

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
    else if (!strcmp(what, "home_lit"))      /* HOME's light: on with MAIN on screen */
        *got = (int)(ui.lit >> B_HOME & 1u);
    else if (!strcmp(what, "replace"))       /* REC MODE: 1 REPLACE */
        *got = seq_cfg.rec_replace;
    else if (!strcmp(what, "step_page"))     /* SEQ: the bar the keys show */
        *got = ui.step_page;
    else if (!strcmp(what, "fill"))          /* a fill playing (GLO + E4 held, or the next bar) */
        *got = seq_fill();
    else if (!strcmp(what, "vis"))           /* the visualiser's style, 0 off */
        *got = ui.vis;
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
