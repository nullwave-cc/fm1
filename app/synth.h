/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 nullwave contributors */
/* nullwave's sound: six equal channels, each playing one of Felucca's engines (engines/felucca, MIT) or nullwave's
 * drum kit (eng_kit.c), with its own sound (engine, preset, parameters), voices from the channel's pool, an ADSR and
 * velocity, a filter, a level, a pan and two sends to the shared delay and reverb. synth.c, an o2 unit.
 *
 * Threads: the main loop calls everything but synth_render, which the audio interrupt calls (and the sequencer
 * from it, seq.c). Notes go through a queue the render empties at its start: the main loop puts notes under
 * plat_lock (app.c), so a render that puts its own never meets it half done. A sound's parameters are plain writes
 * the render reads as they are. A change of engine is a note-off of the channel's voices first, then the new sound
 * at the next block. */
#pragma once
#include <stdint.h>

#define NCH 6                            /* channels */
#define SYN_NPARAM 13                    /* a sound's engine and envelope parameters: p[] */
#define SYN_KNOBS 4                      /* KNOB 1..4 */

/* a channel's sound, as a song keeps it: the engine and its eight values (EDIT 1, EDIT 2), the envelope, the filter,
 * the sends, the INSERT, the SLICER, the LFO and its destinations, the envelope's other destinations, the voice, the
 * arpeggiator; ext: what an engine keeps besides (DRUM: its lanes' levels; FM6: the patch slot) */
typedef struct {
    uint8_t engine;                      /* 0..synth_nengines() - 1 */
    uint8_t preset;                      /* the preset it was loaded from (its name), the parameters may differ */
    int8_t fenv;                         /* the envelope's amount on the engine's cutoff, -64..63 (ENV DEST FLT) */
    uint8_t mono;                        /* VCE: 0 POLY, 1 MONO, 2 LEGATO, 3 UNISON */
    int16_t p[SYN_NPARAM];               /* ATK REL FTYPE E0..E7 DEC SUS */
    uint8_t flt, cut, res;               /* the channel's filter: 0 off, 1 low-pass, 2 high-pass, 3 band-pass */
    uint8_t dly, rev;                    /* the sends, 0..127 */
    int8_t pan;                          /* -64 .. 63 */
    uint8_t dst, cho;                    /* the drive and chorus sends */
    uint8_t ins, ins_a, ins_b, ins_c, ins_mix;   /* INSERT: OFF SOFT HARD FOLD FUZZ CRUSH PHASR FLANG CHOR */
    uint8_t slc, slc_pat, slc_rate, slc_depth;   /* SLICER: OFF GATE STUT, 16 patterns, its rate, its depth */
    uint8_t lfo_rate, lfo_wave, lfo_phase, lfo_fade, lfo_sync, lfo_trig, lfo_pol;
    int8_t lfo_pit, lfo_flt, lfo_shp, lfo_amp;   /* LFO DEST */
    int8_t penv, senv;                   /* ENV DEST PIT, SHP */
    uint8_t glide, glmod, prio, alloc, dtune, spread, vmute;   /* VOICE, VOICE 2, VOICE 3 */
    uint8_t arp, arp_rate, arp_oct, arp_gate, arp_swing, arp_prob, arp_hold, arp_order;   /* ARP, ARP 2 */
    int8_t tfilt;                        /* the one-knob track filter: - low-pass, + high-pass, 0 off */
    uint8_t ext[16];
    uint8_t rsv[3];
} sound_t;

/* a sound's parameters by number, for the pages, the knobs and the sequencer's locks */
enum { SP_ATK, SP_REL, SP_FTYPE, SP_E0, SP_E1, SP_E2, SP_E3, SP_E4, SP_E5, SP_E6, SP_E7, SP_DEC, SP_SUS,
       SP_FENV, SP_MONO, SP_FLT, SP_CUT, SP_RES, SP_DLY, SP_REV, SP_PAN,
       SP_DST, SP_CHO, SP_INS, SP_INS_A, SP_INS_B, SP_INS_C, SP_INS_MIX, SP_SLC, SP_SLC_PAT, SP_SLC_RATE, SP_SLC_DEPTH,
       SP_LFO_RATE, SP_LFO_WAVE, SP_LFO_PHASE, SP_LFO_FADE, SP_LFO_SYNC, SP_LFO_TRIG, SP_LFO_POL,
       SP_LFO_PIT, SP_LFO_FLT, SP_LFO_SHP, SP_LFO_AMP, SP_PENV, SP_SENV,
       SP_GLIDE, SP_GLMOD, SP_PRIO, SP_ALLOC, SP_DTUNE, SP_SPREAD, SP_VMUTE,
       SP_ARP, SP_ARP_RATE, SP_ARP_OCT, SP_ARP_GATE, SP_ARP_SWING, SP_ARP_PROB, SP_ARP_HOLD, SP_ARP_ORDER,
       SP_TFILT, SP_X0,
       /* (after the engine's extras: kept apart from sound_t, in the channel's synth_mod_t, never locked) */
       SP_E2_ATK = SP_X0 + 16, SP_E2_DEC, SP_E2_SUS, SP_E2_REL,
       SP_M1_SRC, SP_M1_DST, SP_M1_AMT, SP_M2_SRC, SP_M2_DST, SP_M2_AMT, SP_M3_SRC, SP_M3_DST, SP_M3_AMT,
       SP_M4_SRC, SP_M4_DST, SP_M4_AMT, SP_N };
#define SP_LOCKABLE(id) ((id) != SP_FTYPE && (id) != SP_MONO && (id) != SP_FLT && (id) != SP_INS && (id) != SP_SLC \
                         && (id) != SP_LFO_WAVE && (id) != SP_LFO_SYNC && (id) != SP_LFO_TRIG && (id) != SP_LFO_POL \
                         && ((id) < SP_ARP || (id) > SP_ARP_ORDER) && (id) < SP_X0)

/* a channel's modulation, beside its sound (the song keeps it, a user preset too): ENV 2 (its own ADSR) and four
 * slots, each a source, a target and a bipolar amount. Saved as it is: new sources and targets go after the last */
#define SYN_MODS 4
#define SYN_MOD_VER 1
enum { MS_OFF, MS_ENV, MS_ENV2, MS_LFO, MS_VEL, MS_PRS, MS_RND, MS_N };   /* ENV the volume's, PRS the key's pressure,
                                                                         * RND a value each note */
enum { MT_OFF, MT_CUT, MT_PIT, MT_SHP, MT_AMP, MT_PAN, MT_LFO, MT_N };   /* SHP: the engine's shape (PWM, the sync
                                                                         * sweep, SUPER's morph), LFO: its depth */
typedef struct {
    uint8_t ver;                         /* SYN_MOD_VER */
    uint8_t e2[4];                       /* ENV 2: ATK DEC SUS REL, 0..127 */
    struct { uint8_t src, dst; int8_t amt; } m[SYN_MODS];   /* amt -64..63 */
    uint8_t rsv[3];
} synth_mod_t;
void synth_default_mod(synth_mod_t *m);
int synth_mod_ok(const synth_mod_t *m);
void synth_set_mod(uint32_t ch, const synth_mod_t *m);
const synth_mod_t *synth_mod(uint32_t ch);
int32_t synth_mod_now(uint32_t ch, uint32_t dst);
int32_t synth_cut_mod(uint32_t ch);     /* the graph view: the newest voice's cutoff modulation now, << 8 (0 none) */   /* the screen: the sum the slots put on a target now, -32768..32767 */

void synth_init(void);
void synth_note_on(uint32_t ch, uint32_t note, uint32_t vel);   /* vel 1..127 */
void synth_note_off(uint32_t ch, uint32_t note);
void synth_pressure(uint32_t ch, uint32_t note, uint32_t prs);   /* a held note's pressure, 0..127 */
void synth_all_off(void);                /* every channel's notes released */
void synth_ch_off(uint32_t ch);          /* one channel's */
void synth_panic(void);                  /* silence now */
void synth_render(int32_t *lr, uint32_t n, uint32_t master_q12);   /* audio interrupt: n stereo frames, 24-bit */

/* the channels' sounds */
const sound_t *synth_sound(uint32_t ch);
void synth_set_sound(uint32_t ch, const sound_t *s);   /* checked, else the channel's default */
int synth_sound_ok(const sound_t *s);
void synth_default_sound(uint32_t ch, sound_t *s);
void synth_engine(uint32_t ch, uint32_t engine);       /* another engine, at its first preset */
void synth_preset(uint32_t ch, uint32_t preset);       /* the engine's preset */
uint32_t synth_nengines(void);
uint32_t synth_npresets(uint32_t ch);
const char *synth_engine_name(uint32_t engine);
const char *synth_preset_name(uint32_t ch);
#define SYN_NAME 16                      /* a user preset's name, at most */
void synth_set_name(uint32_t ch, const char *nm, uint32_t len);
const char *synth_own_name(uint32_t ch);       /* the name synth_set_name gave, "" none */
const char *synth_page_title(uint32_t ch, uint32_t page);   /* the engine's EDIT 1 / EDIT 2 */
/* every sound of every engine in one list (PRESETS): by kind, the kits last */
uint32_t synth_nsounds(void);
uint32_t synth_sound_index(uint32_t ch);
void synth_sound_pick(uint32_t ch, uint32_t index);
int synth_is_kit(uint32_t ch);
int synth_fm6_mute(uint32_t ch, uint32_t op);   /* FM6: an operator muted or back (EDIT + OP key held); 1 muted */
uint32_t synth_fm6_mutes(uint32_t ch);
int synth_lane_mute(uint32_t ch, uint32_t lane);   /* DRUM: a lane muted or back (EDIT + black key); 1 muted */
uint32_t synth_lane_mutes(uint32_t ch);
const char *synth_kit_sound(uint32_t i);   /* DRUM: lane i's name (0..7) */
uint32_t synth_drum_note(uint32_t lane);   /* DRUM: lane's GM note */
int32_t synth_key_note(uint32_t ch, uint32_t k);   /* the note key k plays if the engine maps the keys, else -1 */

/* the parameters: SP_* */
int32_t synth_param(uint32_t ch, uint32_t id);
void synth_param_set(uint32_t ch, uint32_t id, int32_t v);   /* held to its range */
void synth_param_turn(uint32_t ch, uint32_t id, int32_t d);  /* d detents (a wide range moves more) */
void synth_param_range(uint32_t ch, uint32_t id, int32_t *lo, int32_t *hi);
const char *synth_param_label(uint32_t ch, uint32_t id);
const char *const *synth_param_names(uint32_t ch, uint32_t id);   /* an enum's labels, 0 a number */
int32_t synth_param_default(uint32_t ch, uint32_t id);
void synth_param_text(uint32_t ch, uint32_t id, int32_t v, char *out);   /* v as the screen shows it, <= 7 chars */
uint32_t synth_knob_param(uint32_t ch, uint32_t k);          /* the engine's HOME knob k (EDIT's first page) */
/* the sequencer's parameter locks: id held at v until unlocked (the sound itself is not changed) */
void synth_lock(uint32_t ch, uint32_t id, int32_t v);
void synth_unlock(uint32_t ch);

/* FM6: the channel's own patch (a song keeps it), the bank, the operator pages, DX7 SysEx */
#define SYN_FM6_BYTES 96
enum { FM6_RATIO, FM6_FINE, FM6_LEVEL, FM6_RATE, FM6_ELEVEL, FM6_MODE, FM6_DTUN, FM6_VEL };
void *synth_fm6_own(uint32_t ch);
const void *synth_fm6_bank(uint32_t *n);
void synth_fm6_bank_load(const void *p, uint32_t n);
int32_t synth_fm6_op(uint32_t ch, uint32_t op, uint32_t f, uint32_t stage);
void synth_fm6_op_turn(uint32_t ch, uint32_t op, uint32_t f, uint32_t stage, int32_t d);
const char *synth_fm6_slot_name(int32_t slot, char *out);
uint32_t synth_fm6_sysex(uint32_t ch, const uint8_t *d, uint32_t n);

/* the mix: level 0..127 (100: as the engine gives it), mute and solo (bit ch of each mask) */
void synth_set_level(uint32_t ch, uint32_t level);
uint32_t synth_get_level(uint32_t ch);
void synth_mute(uint32_t mask);
void synth_solo(uint32_t mask);
int synth_audible(uint32_t ch);
/* the graph view: the scope of one channel (NCH: none), the newest voice's envelope (Q15; stage 0 off, 1 attack,
 * 2 decay/sustain, 3 release), the LFO's phase (0..1) and its fade's depth */
void synth_shed(void);
void synth_load(uint32_t p);             /* the render's load, x16 permille: the voices' budget follows it */
uint32_t synth_budget(void);
void synth_budget_reset(void);
void synth_protect(uint32_t ch, uint32_t note, int on);   /* a note held on purpose (a drone, HOLD): shed last */
uint32_t synth_budget_used(void);
uint32_t synth_budget_sheds(void);
uint32_t synth_quiet_pct(void);          /* the render's silent blocks since the last call, % (nothing rendered) */                  /* the render near its time: the oldest voice let go fast */
void synth_lowcut(int on);               /* MENU AUDIO LOWCUT: the lows out while the speaker plays */
void synth_scope_ch(uint32_t ch);
uint32_t synth_scope(int16_t *out, uint32_t n);
int32_t synth_env_now(uint32_t ch, uint32_t *stage);
float synth_lfo_now(uint32_t ch, float *depth);
uint32_t synth_slicer(uint32_t ch, int32_t *step);   /* SLICER's pattern (bit i: step i), the step playing or -1 */
int32_t synth_svf_g(uint32_t cut);       /* a cutoff 0..127's tan(pi fc / FS), Q12 */
float synth_dly_beats(float *fb);        /* DELAY's time in quarter notes, its feedback 0..1 */
int32_t synth_peak(uint32_t ch);         /* the channel's peak since the last call (a voice peaks near 24000) */

/* the shared effects: delay (synced to the tempo) and reverb */
enum { FX_DTIME, FX_DFB, FX_DTONE, FX_DLVL, FX_RSIZE, FX_RDAMP, FX_RLVL, FX_N };
int32_t synth_fx(uint32_t id);
void synth_fx_set(uint32_t id, int32_t v);
void synth_fx_turn(uint32_t id, int32_t d);
const char *synth_fx_label(uint32_t id);
void synth_fx_text(uint32_t id, char *out);
int32_t synth_fx_pos(uint32_t id);      /* 0..255 */
void synth_tempo(uint32_t bpm10);       /* the delay's time follows it */
void synth_click(uint32_t accent);      /* the metronome's tick (accent: the bar's first) */
void synth_tune(int32_t cents);         /* the whole FM-1's tuning, -50..+50 cents */
void synth_bend(uint32_t ch, int32_t v, uint32_t range);   /* pitch bend, v -8192..8191, range in semitones */

/* the master's values (GLO MASTER, TEXTURE, FX REV/CHO's chorus) */
enum { MST_DUST, MST_DUCK, MST_FILT, MST_ROLL, MST_TSRC, MST_TLEVEL, MST_TTONE, MST_TMOVE, MST_TRATE, MST_TWHEN, MST_TDUCK,
       MST_CRT, MST_CDP, MST_RESO, MST_N };
void synth_master_set(uint32_t id, int32_t v);
int32_t synth_master(uint32_t id);
void synth_transport(uint32_t tick, int playing);   /* the sequencer's position, each block (the slicer, the gate) */
/* the 27 punch-in effects (0..15 the white keys, 16..26 the black), the throws, the fade (0..15) */
void synth_punch(uint32_t k, int on);
uint32_t synth_punches(void);
void synth_punch_fade(uint32_t f);
void synth_throw(int rev, int echo);

/* velocity: the loudness a key's velocity gives, sens -5 .. +5 (0 as measured, + softer presses louder) */
void synth_vel_sens(int32_t sens);
/* pressure (aftertouch) on a held note: what it does */
enum { PRS_OFF, PRS_FILTER, PRS_VOLUME, PRS_BOTH, PRS_NMODES };
void synth_pressure_mode(uint32_t mode);
const char *synth_pressure_name(uint32_t mode);
uint32_t synth_voices(uint32_t ch);
/* what a channel's voices are doing now, for the screen: each sounding one's note, velocity, key held, amplitude
 * (0..255) and its start (a number that changes with each new note in that slot); out[NVOICE_MAX], the count */
#define SYN_VMAX 6
typedef struct { uint8_t slot, note, vel, gate, amp; uint32_t start; } synth_vview_t;
uint32_t synth_voice_view(uint32_t ch, synth_vview_t *out);
int synth_drum_lane(uint32_t note);      /* DRUM: the lane (0..7) a note plays, -1 none */      /* voices sounding on ch (the screen; tests) */
