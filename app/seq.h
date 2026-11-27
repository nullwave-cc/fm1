/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 nullwave contributors */
/* nullwave's sequencer, scenes and songs (seq.c, in app.c's unit).
 *
 * A song: the six channels' sounds and levels, 16 patterns a channel (1..8 bars, notes and parameter locks), 16
 * scenes (each the pattern every channel plays, the mutes, the levels and, if asked, the sounds), a chain of up to
 * 64 sections, the tempo, the key and scale, the effects. One song is in RAM; eight are kept in flash.
 *
 * Time: ticks of 1/96 of a quarter note (24 a sixteenth, 384 a bar). The transport runs in the render (seq_render,
 * before the synth): the notes start at the block they fall in. The main loop edits the song under plat_lock. */
#pragma once
#include <stdint.h>
#include "synth.h"

#define NPAT 16                          /* patterns a channel */
#define NSCENE 16                        /* scenes a song: one a white key */
#define NSONG 8                          /* songs in flash */
#define NCHAIN 64                        /* the chain's sections */
#define PAT_EV 128                       /* notes and locks a pattern */
#define TPQ 96
#define TPS 24                           /* ticks a sixteenth */
#define TBAR 384                         /* ticks a bar (4/4) */
#define SEQ_NOLOCK 0x7FFF                /* seq_step_lock: none */
#define SONG_NAME 8                      /* a channel's name: up to 8 characters (A-Z 0-9 space - . / + # & !) */

enum { EV_RATCH = 3, EV_LOCK = 4, EV_FILL = 8, EV_NOFILL = 16, EV_DRONE = 32, EV_SKIP = 64, EV_NEW = 128 };   /* DRONE:
                                         * held over the whole pattern when recorded: sounds on, never struck again */   /* FILL: plays only in a
                                         * fill; NEW: recorded in the REPLACE pass under way (never kept in a song) */
typedef struct {
    uint16_t pos;                        /* ticks from the pattern's start */
    uint8_t note, vel;                   /* a lock: the parameter (SP_*) and its value + 128 */
    uint8_t len;                         /* 1/4 sixteenths (6 ticks), 1..255 */
    uint8_t fl;                          /* ratchet - 1 (bits 0..1), EV_LOCK, EV_SKIP (recorded ahead: not this pass) */
} ev_t;

enum { DIV_16, DIV_8, DIV_32, DIV_16T, DIV_4, DIV_8T, DIV_2, DIV_1BAR, DIV_2BAR, NDIV };   /* (kept in songs: new ones after) */
typedef struct {
    uint8_t bars, div, swing, n;         /* 1..8, DIV_*, 0..100, events */
    ev_t ev[PAT_EV];
} pat_t;

typedef struct {
    uint8_t pat[NCH];                    /* each channel's pattern */
    uint8_t mute, solo, has_sound, rsv;
    uint8_t level[NCH];
    sound_t snd[NCH];                    /* with has_sound: the sounds it brings */
} scene_t;

typedef struct {
    uint8_t scene, bars, reps, rsv;      /* bars 0: until the next launch */
} sect_t;

enum { MODE_LOOP, MODE_SONG };
typedef struct {
    uint16_t bpm10;                      /* 400..2400 */
    uint8_t root, scale, keys, chord;    /* SEL */
    int8_t transpose;
    uint8_t nchain, mode, swing;         /* swing: GLO GLOBAL's, over every pattern's own, 0..100 */
    int8_t strum;                        /* SEL 2: -60..60 ms between a chord's notes (+ low to high) */
    uint8_t vlead;                       /* SEL 2: voice leading */
    int16_t mst[MST_N];                  /* the master: DUST, DUCK, FILT, ROLL, TEXTURE, the chorus */
    int16_t fx[FX_N];
    sound_t snd[NCH];                    /* the sounds (a scene with its own brings them instead) */
    scene_t scene[NSCENE];
    sect_t chain[NCHAIN];
    uint8_t fm6[NCH][SYN_FM6_BYTES];     /* each channel's own FM6 patch */
    char name[NCH][SONG_NAME];           /* each channel's name, 0-padded; empty: the screen names its engine */
    synth_mod_t mod[NCH];                /* each channel's ENV 2 and mod slots (SONG_VER 5) */
    char sname[NCH][SYN_NAME];           /* each channel's sound's own name (a user preset's), 0-padded; empty:
                                          * its preset's (SONG_VER 5) */
    pat_t pat[NCH][NPAT];
} song_t;

extern song_t song;

/* transport */
enum { REC_OFF, REC_ARMED, REC_COUNT, REC_TAKE, REC_ON };
enum { RM_FREE, RM_TEMPO };              /* the REC screen's MODE */
enum { RS_NOTE, RS_COUNT };              /* START */
enum { CLICK_OFF, CLICK_REC, CLICK_ON };
enum { LQ_BAR, LQ_LOOP, LQ_NOW };        /* when a launched scene starts */
enum { SYNC_INT, SYNC_USB, SYNC_TRS, SYNC_BLE };
typedef struct {
    uint8_t rec_mode, rec_bars, rec_start, rec_quant, click, launch, sync, clock_out, seq_out;
    uint8_t rec_replace;                 /* REC MODE: 0 OVERDUB (each pass adds), 1 REPLACE (the playhead erases as it records) */
} seq_cfg_t;
extern seq_cfg_t seq_cfg;

void seq_init(void);
void seq_render(uint32_t n);             /* the render: n frames of time */
void seq_play(void);
void seq_stop(void);
int seq_playing(void);
uint32_t seq_rec(void);                  /* REC_* */
uint32_t seq_rec_ch(void);
void seq_rec_button(uint32_t ch);        /* REC tapped, ch the selected channel */
void seq_rec_off(void);
void seq_replace_changed(void);         /* REC MODE turned while recording: the pass as it stands */
int seq_take_kept(void);                 /* 1 once after a REPLACE pass stopped short (the last full take kept) */
int32_t seq_take_ms(void);               /* a free take: its length so far */
void seq_clear(uint32_t ch);             /* hold REC: the channel's pattern emptied (undo brings it back) */
void seq_note_on(uint32_t ch, uint32_t note, uint32_t vel);   /* a played note: recorded, arms a take */
int seq_note_off(uint32_t ch, uint32_t note);   /* 1: the note goes on, recorded as a DRONE (the caller keeps it on) */
void seq_drones_off(int32_t ch);           /* the drones sounding end (-1: every channel) */
int seq_drones(uint32_t ch);               /* how many drones the channel's pattern sounds now */
uint32_t seq_bpm10(void);
void seq_set_bpm10(uint32_t b);
void seq_tap(void);                      /* tap tempo */
uint32_t seq_bar(void);                  /* bars played since PLAY */
int32_t seq_count(void);                 /* the count-in's beat (4..1), 0 none */

/* the patterns */
pat_t *seq_pat(uint32_t ch);             /* the one the channel plays */
uint32_t seq_pat_index(uint32_t ch);
uint32_t seq_steps(const pat_t *p);      /* its steps, at its DIV */
uint32_t seq_step_ticks(const pat_t *p);
int32_t seq_pos(uint32_t ch);            /* the playhead in the pattern (ticks), -1 stopped */
uint32_t seq_cells(uint32_t ch, uint32_t bar);   /* bit k: a note in sixteenth k of that bar */
int seq_step_find(uint32_t ch, uint32_t s);      /* the step's first note, -1 none */
void seq_step_set(uint32_t ch, uint32_t s, uint32_t note, uint32_t vel);
void seq_step_clear(uint32_t ch, uint32_t s);
uint32_t seq_ev_list(uint32_t ch, uint8_t *idx, uint32_t max);   /* NOTES: the notes' indices in time order */
const ev_t *seq_ev(uint32_t ch, uint32_t i);
void seq_ev_edit(uint32_t ch, uint32_t i, int32_t dnote, int32_t dpos, int32_t dlen);   /* dlen in 6 ticks */
void seq_ev_delete(uint32_t ch, uint32_t i);
int seq_hit_find(uint32_t ch, uint32_t s, uint32_t note);           /* DRUMS: a lane's hit on a step */
void seq_hit_set(uint32_t ch, uint32_t s, uint32_t note, uint32_t vel);   /* vel 0: out */
void seq_step_note(uint32_t ch, uint32_t s, int32_t d);
void seq_step_vel(uint32_t ch, uint32_t s, int32_t d);
void seq_step_ratchet(uint32_t ch, uint32_t s, int32_t d);
void seq_step_nudge(uint32_t ch, uint32_t s, int32_t d);
int32_t seq_step_lock(uint32_t ch, uint32_t s, uint32_t id);   /* the lock's value, or SEQ_NOLOCK */
void seq_step_lock_set(uint32_t ch, uint32_t s, uint32_t id, int32_t v);
void seq_step_unlock(uint32_t ch, uint32_t s);  /* its nudge, ratchet and locks back to none */
void seq_pat_bars(uint32_t ch, int32_t d);       /* x2 (the notes doubled) / 1/2 */
void seq_pat_set_bars(uint32_t ch, uint32_t bars);
void seq_pat_div(uint32_t ch, int32_t d);
void seq_pat_swing(uint32_t ch, int32_t d);
void seq_pat_shift(uint32_t ch, int32_t d);      /* the notes a step later / earlier */
void seq_pat_transpose(uint32_t ch, int32_t d);
void seq_erase(uint32_t ch, int32_t note);       /* the note out of the whole pattern (-1: none) */
void seq_erase_hold(uint32_t ch, int32_t note);  /* playing: erased as the playhead passes while held (-1 stop) */
int seq_undo(void);
int seq_redo(void);
int seq_pat_empty(uint32_t ch, uint32_t pat);

/* scenes, the chain, songs */
uint32_t seq_scene(void);
int32_t seq_queued(void);                /* the scene that starts next, -1 none */
void seq_launch(uint32_t scene);
void seq_qchain_add(uint32_t scene);     /* SAVE held, more keys: a quick chain */
uint32_t seq_scene_bars(uint32_t s);     /* a scene's length: its longest pattern, in bars */
int seq_chain_insert(uint32_t pos, uint32_t scene);   /* CHAIN page: a section with the scene, before pos */
void seq_chain_delete(uint32_t pos);
int seq_chain_end_stop(void);            /* the song's END: 1 stop after the last section, 0 round again */
void seq_chain_set_end_stop(int on);
uint32_t seq_sect(void);                 /* the section playing (song mode) */
int seq_bar_read(uint32_t ch, uint32_t bar, uint8_t *out, uint32_t max);   /* the editor's STEPS: 6 bytes a note */
int seq_bar_write(uint32_t ch, uint32_t bar, const uint8_t *in, uint32_t n);
void seq_scene_set(uint32_t s, const uint8_t *pat, uint32_t mute, uint32_t solo, const uint8_t *level);
int seq_chain_set(const uint8_t *sec, uint32_t n, int end_stop);
uint32_t seq_qchain_len(void);
int seq_scene_used(uint32_t scene);
void seq_scene_copy(uint32_t from, uint32_t to);
void seq_scene_sound(uint32_t scene, int on);
void seq_scene_pat(uint32_t scene, uint32_t ch, uint32_t pat);
void seq_set_mute(uint32_t mask);
void seq_set_solo(uint32_t mask);
uint32_t seq_mute(void);
uint32_t seq_solo(void);
void seq_set_level(uint32_t ch, uint32_t level);
void seq_mode(uint32_t mode);            /* MODE_LOOP / MODE_SONG */
void seq_song_rec(int on);
int seq_song_rec_on(void);
uint32_t seq_section(void);              /* song mode: the section playing */
int seq_save(uint32_t slot);             /* 0 done, -1 too big, -2 the flash */
int seq_load(uint32_t slot);             /* 0 done, -1 none there */
void seq_new(void);
int seq_slot_used(uint32_t slot);
uint32_t seq_slot(void);
int seq_dirty(void);
void seq_touch(void);                    /* the song changed */

/* the arpeggiator (a channel's ARP MODE not OFF), note repeat (the ARP layer), strummed chords, fills */
void seq_arp_key(uint32_t ch, uint32_t note, uint32_t vel, int on);
int seq_arp_on(uint32_t ch);
void seq_hold(uint32_t ch, int on);      /* HOLD (SHIFT + PLAY): the keys latch; off, the latched notes end */
int seq_held_n(uint32_t ch);             /* the notes HOLD keeps sounding now */
uint32_t seq_arp_preview(uint32_t ch, uint8_t *out, uint32_t max);   /* the graph view: the arp's next notes */
void seq_roll(uint32_t ch, uint32_t note, uint32_t vel, int on);
void seq_strum(uint32_t ch, uint32_t note, uint32_t vel, uint32_t ms);
void seq_fill_hold(int on);
void seq_fill_bar(void);
int seq_fill(void);
uint32_t seq_step_flag(uint32_t ch, uint32_t s);      /* 0 always, 1 FILL ONLY, 2 NO FILL */
void seq_step_flag_turn(uint32_t ch, uint32_t s);

/* MIDI clock in: the bytes of a real-time message from port */
void seq_midi_rt(uint32_t st, int port);
