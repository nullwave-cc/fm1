/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 nullwave contributors */
/* nullwave's sequencer (seq.h): the transport, the patterns' notes, recording, scenes and their launches, the chain,
 * the songs in flash. In app.c's unit (after the platform and synth.h).
 *
 * The render calls seq_render before the synth: the time of the block in ticks (the tempo, or the MIDI clock's
 * pulses), and for each tick the bar's jobs (launches, the chain, the click), each channel's notes there, and what
 * waits (note-offs, ratchets, the locks' ends). The main loop's edits of the song take plat_lock, so the render never
 * sees one half done. */
#include "seq.h"
#define SEQ_FS 44100u                    /* the render's rate (fe_tables.h FS) */

song_t song PLAT_POOL;
seq_cfg_t seq_cfg = {RM_TEMPO, 2, RS_NOTE, 1, CLICK_REC, LQ_BAR, SYNC_INT, 0, 0, 0};

static volatile uint8_t playing, rec, rec_ch, cur_scene, slot, dirty_f;
static volatile int8_t queued = -1;
static uint32_t tick;                    /* ticks since PLAY (the render's) */
static uint32_t acc;                     /* the tempo's remainder: samples x BPM10 x ticks */
static uint32_t pstart[NCH];             /* the tick each channel's pattern started at */
static uint32_t scene_t0, bars_played;
static volatile int32_t ext_owed;        /* MIDI clock: ticks owed by the pulses that came */
static uint32_t smp;                     /* samples since boot (the clock's times) */
static uint32_t ext_t[25], ext_n;        /* the last pulses' times */
static int16_t count_t = -1;             /* the count-in: its tick, -1 none */
static volatile int16_t erase_note[NCH] = {-1, -1, -1, -1, -1, -1};
static uint8_t qchain[8], qchain_n, qchain_i;
static uint8_t sect_i, sect_bars, sect_rep, song_rec;
static volatile uint8_t song_end;        /* the chain played to its end with END STOP: stop after the tick */
static uint32_t sect_t0;

/* what waits: note-offs, ratchets' notes, locks' ends */
enum { PD_OFF, PD_ON, PD_UNLOCK };
#define NPEND 96u
static struct { uint32_t t; uint8_t kind, ch, note, vel; uint16_t dur; } pend[NPEND];
static uint32_t npend;

/* the notes being recorded: the event and when it started */
static struct { int16_t idx; uint32_t t0; } open_n[NCH][128];
static uint32_t drone_on[NCH][4];        /* the DRONE notes sounding (as stored, before TRANS): bit note */
static uint8_t drone_tr[NCH][128];       /* (the note each sounds at: TRANS when it started) */
void seq_drones_off(int32_t ch)
{
    uint32_t c, n;
    for (c = 0; c < NCH; c++) {
        if (ch >= 0 && (int32_t)c != ch)
            continue;
        for (n = 0; n < 128u; n++)
            if (drone_on[c][n >> 5] >> (n & 31u) & 1u) {
                synth_note_off(c, drone_tr[c][n]);
                synth_protect(c, drone_tr[c][n], 0);
            }
        memset(drone_on[c], 0, sizeof drone_on[c]);
    }
}
int seq_drones(uint32_t ch)
{
    uint32_t k, n = 0;
    for (k = 0; k < 4u; k++) {
        uint32_t x = drone_on[ch % NCH][k];
        while (x) {
            n += x & 1u;
            x >>= 1;
        }
    }
    return (int)n;
}

/* a free take: times in samples */
#define TAKE_N 160u
static struct { uint32_t t, len; uint8_t note, vel; } take[TAKE_N];
static uint32_t take_n, take_t;
static int16_t take_open[128];

/* undo: one pattern as it was before the last pass or edit */
static pat_t undo_buf PLAT_POOL;
static uint8_t undo_ch, undo_pi, undo_ok, redo_ok;

/* REPLACE: one pattern, no copy. The notes the pass records are marked EV_NEW; the others (the last full take) are
 * silent while it records. A full pass done: the old ones go, the new ones are the take. Stopped short: the new ones
 * go, the take is as it was. (The undo before the recording is the general one, undo_take: no copy of its own.) */
static uint32_t rp_ticks;                /* ticks of the pass under way */
static uint8_t rp_kept;                  /* a pass stopped short: said once */

static const uint16_t DIV_TICKS[NDIV] = {24, 48, 12, 16, 96, 32, 192, 384, 768};
static const uint8_t DIV_ORDER[NDIV] = {DIV_32, DIV_16T, DIV_16, DIV_8T, DIV_8, DIV_4, DIV_2, DIV_1BAR, DIV_2BAR};

static uint32_t lock_(void) { return plat_lock(); }
static void unlock_(uint32_t f) { plat_unlock(f); }

/* ----------------------------------------------------------- the patterns --- */
pat_t *seq_pat(uint32_t ch) { return &song.pat[ch % NCH][song.scene[cur_scene].pat[ch % NCH] % NPAT]; }
uint32_t seq_pat_index(uint32_t ch) { return song.scene[cur_scene].pat[ch % NCH] % NPAT; }
uint32_t seq_step_ticks(const pat_t *p) { return DIV_TICKS[p->div % NDIV]; }
uint32_t seq_steps(const pat_t *p)
{
    uint32_t n = (uint32_t)p->bars * TBAR / DIV_TICKS[p->div % NDIV];
    return n ? n : 1u;
}
static uint32_t pat_len(const pat_t *p) { return (uint32_t)(p->bars ? p->bars : 1u) * TBAR; }
int seq_pat_empty(uint32_t ch, uint32_t pi) { return song.pat[ch % NCH][pi % NPAT].n == 0u; }

static void pat_init(pat_t *p)
{
    p->bars = 1;
    p->div = DIV_16;
    p->swing = 0;
    p->n = 0;
}

void seq_touch(void) { dirty_f = 1; }
int seq_dirty(void) { return dirty_f; }

static void undo_take(uint32_t ch)       /* before an edit: the pattern kept */
{
    undo_buf = *seq_pat(ch);
    undo_ch = (uint8_t)ch;
    undo_pi = (uint8_t)seq_pat_index(ch);
    undo_ok = 1;
    redo_ok = 0;
}

int seq_undo(void)
{
    pat_t *p;
    uint32_t f;
    if (!undo_ok)
        return 0;
    f = lock_();
    p = &song.pat[undo_ch][undo_pi];
    {
        pat_t t = *p;
        *p = undo_buf;
        undo_buf = t;
    }
    unlock_(f);
    undo_ok = 0;
    redo_ok = 1;
    seq_touch();
    return 1;
}

int seq_redo(void)
{
    if (!redo_ok)
        return 0;
    undo_ok = 1;
    seq_undo();                          /* (the same swap back) */
    undo_ok = 1;
    redo_ok = 0;
    return 1;
}

static void rp_settle(int keep_new)       /* the REPLACE pass ends: the new notes are the take (1) or go (0) */
{
    pat_t *p = seq_pat(rec_ch);
    uint32_t i, j = 0, k;
    int16_t to[PAT_EV];                  /* (each note's new place, for the notes still held) */
    for (i = 0; i < p->n; i++) {
        ev_t e = p->ev[i];
        to[i] = -1;
        if (((e.fl & EV_NEW) != 0) == !!keep_new) {
            e.fl &= (uint8_t)~EV_NEW;
            to[i] = (int16_t)j;
            p->ev[j++] = e;
        }
    }
    p->n = (uint8_t)j;
    for (k = 0; k < 128u; k++)
        if (open_n[rec_ch][k].idx >= 0)
            open_n[rec_ch][k].idx = open_n[rec_ch][k].idx < (int16_t)PAT_EV ? to[open_n[rec_ch][k].idx] : -1;
    rp_ticks = 0;
}

static void rec_ends(void)               /* REC_ON is over: a REPLACE pass not complete leaves the last take */
{
    if (rec == REC_ON && seq_cfg.rec_replace) {
        int short_pass = rp_ticks != 0u;
        rp_settle(0);
        if (short_pass)
            rp_kept = 1;
    }
}

int seq_take_kept(void)
{
    int k = rp_kept;
    rp_kept = 0;
    return k;
}

void seq_replace_changed(void)           /* REC MODE turned while recording */
{
    uint32_t f = lock_(), i;
    if (rec == REC_ON && !seq_cfg.rec_replace) {   /* to OVERDUB: what this pass has is added to the take */
        pat_t *p = seq_pat(rec_ch);
        for (i = 0; i < p->n; i++)
            p->ev[i].fl &= (uint8_t)~EV_NEW;
    }
    rp_ticks = 0;                        /* (to REPLACE: a pass starts here) */
    unlock_(f);
}

static void ev_del(pat_t *p, uint32_t i)
{
    if (i < p->n) {
        p->ev[i] = p->ev[p->n - 1u];
        p->n--;
    }
}

static int ev_add(pat_t *p, uint32_t pos, uint32_t note, uint32_t vel, uint32_t len, uint32_t fl)
{
    ev_t *e;
    if (p->n >= PAT_EV)
        return -1;
    e = &p->ev[p->n];
    e->pos = (uint16_t)(pos % pat_len(p));
    e->note = (uint8_t)note;
    e->vel = (uint8_t)vel;
    e->len = (uint8_t)(len < 1u ? 1u : len > 255u ? 255u : len);
    e->fl = (uint8_t)fl;
    return p->n++;
}

static uint32_t ev_step(const pat_t *p, const ev_t *e)   /* the step it sits nearest */
{
    uint32_t st = seq_step_ticks(p);
    return ((e->pos + st / 2u) / st) % seq_steps(p);
}

int32_t seq_pos(uint32_t ch)
{
    if (!playing)
        return -1;
    return (int32_t)((tick - pstart[ch % NCH]) % pat_len(seq_pat(ch)));
}

uint32_t seq_cells(uint32_t ch, uint32_t bar)
{
    const pat_t *p = seq_pat(ch);
    uint32_t i, m = 0, b0 = bar * TBAR, len = pat_len(p);
    for (i = 0; i < p->n; i++) {
        const ev_t *e = &p->ev[i];
        uint32_t pos = (e->pos + TPS / 2u) % len;
        if (!(e->fl & EV_LOCK) && pos >= b0 && pos < b0 + TBAR)
            m |= 1u << ((pos - b0) / TPS);
    }
    return m;
}

int seq_step_find(uint32_t ch, uint32_t s)
{
    const pat_t *p = seq_pat(ch);
    uint32_t i;
    for (i = 0; i < p->n; i++)
        if (!(p->ev[i].fl & EV_LOCK) && ev_step(p, &p->ev[i]) == s)
            return (int)i;
    return -1;
}

void seq_step_set(uint32_t ch, uint32_t s, uint32_t note, uint32_t vel)
{
    pat_t *p = seq_pat(ch);
    uint32_t f, st = seq_step_ticks(p);
    undo_take(ch);
    f = lock_();
    ev_add(p, s * st, note, vel, st / 6u ? st / 6u : 1u, 0);
    unlock_(f);
    seq_touch();
}

void seq_step_clear(uint32_t ch, uint32_t s)
{
    pat_t *p = seq_pat(ch);
    uint32_t f, i;
    undo_take(ch);
    f = lock_();
    for (i = p->n; i-- > 0;)
        if (ev_step(p, &p->ev[i]) == s)
            ev_del(p, i);
    unlock_(f);
    seq_touch();
}

/* the NOTES page: the pattern's notes in time order (the locks left out), one moved, made longer, another pitch */
uint32_t seq_ev_list(uint32_t ch, uint8_t *idx, uint32_t max)
{
    const pat_t *p = seq_pat(ch);
    uint32_t i, j, n = 0;
    for (i = 0; i < p->n && n < max; i++)
        if (!(p->ev[i].fl & EV_LOCK)) {
            for (j = n; j > 0 && (p->ev[idx[j - 1]].pos > p->ev[i].pos
                                  || (p->ev[idx[j - 1]].pos == p->ev[i].pos && p->ev[idx[j - 1]].note > p->ev[i].note)); j--)
                idx[j] = idx[j - 1];
            idx[j] = (uint8_t)i;
            n++;
        }
    return n;
}
const ev_t *seq_ev(uint32_t ch, uint32_t i)
{
    const pat_t *p = seq_pat(ch);
    return i < p->n ? &p->ev[i] : 0;
}
void seq_ev_edit(uint32_t ch, uint32_t i, int32_t dnote, int32_t dpos, int32_t dlen)
{
    pat_t *p = seq_pat(ch);
    uint32_t f, len = pat_len(p);
    ev_t *e;
    if (i >= p->n)
        return;
    f = lock_();
    e = &p->ev[i];
    e->note = (uint8_t)clamp(e->note + dnote, 0, 127);
    e->pos = (uint16_t)(((int32_t)e->pos + dpos % (int32_t)len + (int32_t)len) % (int32_t)len);
    e->len = (uint8_t)clamp(e->len + dlen, 1, 255);
    unlock_(f);
    seq_touch();
}
void seq_ev_delete(uint32_t ch, uint32_t i)
{
    pat_t *p = seq_pat(ch);
    uint32_t f;
    if (i >= p->n)
        return;
    undo_take(ch);
    f = lock_();
    ev_del(p, i);
    unlock_(f);
    seq_touch();
}

/* the DRUMS screen: a lane's hit on a step (its note), found, set (vel 0: taken out) */
int seq_hit_find(uint32_t ch, uint32_t s, uint32_t note)
{
    const pat_t *p = seq_pat(ch);
    uint32_t i;
    for (i = 0; i < p->n; i++)
        if (!(p->ev[i].fl & EV_LOCK) && p->ev[i].note == note && ev_step(p, &p->ev[i]) == s)
            return (int)i;
    return -1;
}
void seq_hit_set(uint32_t ch, uint32_t s, uint32_t note, uint32_t vel)
{
    pat_t *p = seq_pat(ch);
    int i = seq_hit_find(ch, s, note);
    uint32_t f, st = seq_step_ticks(p);
    if (i < 0 && !vel)
        return;
    undo_take(ch);
    f = lock_();
    if (i >= 0 && !vel)
        ev_del(p, (uint32_t)i);
    else if (i >= 0)
        p->ev[i].vel = (uint8_t)clamp((int32_t)vel, 1, 127);
    else
        ev_add(p, s * st, note, vel, st / 6u ? st / 6u : 1u, 0);
    unlock_(f);
    seq_touch();
}

static void step_each(uint32_t ch, uint32_t s, int32_t d, int what)   /* every note of the step */
{
    pat_t *p = seq_pat(ch);
    uint32_t f = lock_(), i, st = seq_step_ticks(p);
    for (i = 0; i < p->n; i++) {
        ev_t *e = &p->ev[i];
        if ((e->fl & EV_LOCK) || ev_step(p, e) != s)
            continue;
        if (what == 0)
            e->note = (uint8_t)clamp(e->note + d, 0, 127);
        else if (what == 1)
            e->vel = (uint8_t)clamp(e->vel + d * 4, 1, 127);
        else if (what == 2)
            e->fl = (uint8_t)((e->fl & ~EV_RATCH) | (uint32_t)clamp((e->fl & EV_RATCH) + d, 0, 3));
        else {                           /* nudge: within half a step either side */
            int32_t base = (int32_t)(s * st), off = (int32_t)e->pos - base, lim = (int32_t)st / 2 - 1;
            if (off > (int32_t)pat_len(p) / 2)
                off -= (int32_t)pat_len(p);
            off = clamp(off + d, -lim, lim);
            e->pos = (uint16_t)((base + off + (int32_t)pat_len(p)) % (int32_t)pat_len(p));
        }
    }
    unlock_(f);
    seq_touch();
}
void seq_step_note(uint32_t ch, uint32_t s, int32_t d) { step_each(ch, s, d, 0); }
void seq_step_vel(uint32_t ch, uint32_t s, int32_t d) { step_each(ch, s, d, 1); }
void seq_step_ratchet(uint32_t ch, uint32_t s, int32_t d) { step_each(ch, s, d, 2); }
void seq_step_nudge(uint32_t ch, uint32_t s, int32_t d) { step_each(ch, s, d, 3); }

int32_t seq_step_lock(uint32_t ch, uint32_t s, uint32_t id)
{
    const pat_t *p = seq_pat(ch);
    uint32_t i;
    for (i = 0; i < p->n; i++)
        if ((p->ev[i].fl & EV_LOCK) && p->ev[i].note == id && ev_step(p, &p->ev[i]) == s)
            return (int32_t)p->ev[i].vel - 128;
    return SEQ_NOLOCK;
}

void seq_step_lock_set(uint32_t ch, uint32_t s, uint32_t id, int32_t v)
{
    pat_t *p = seq_pat(ch);
    uint32_t i, f = lock_();
    for (i = 0; i < p->n; i++)
        if ((p->ev[i].fl & EV_LOCK) && p->ev[i].note == id && ev_step(p, &p->ev[i]) == s) {
            p->ev[i].vel = (uint8_t)(v + 128);
            break;
        }
    if (i == p->n)
        ev_add(p, s * seq_step_ticks(p), id, (uint32_t)(v + 128), 1, EV_LOCK);
    unlock_(f);
    seq_touch();
}

void seq_step_unlock(uint32_t ch, uint32_t s)
{
    pat_t *p = seq_pat(ch);
    uint32_t i, f, st = seq_step_ticks(p);
    undo_take(ch);
    f = lock_();
    for (i = p->n; i-- > 0;) {
        ev_t *e = &p->ev[i];
        if (ev_step(p, e) != s)
            continue;
        if (e->fl & EV_LOCK)
            ev_del(p, i);
        else {
            e->pos = (uint16_t)(s * st);
            e->fl &= (uint8_t)~EV_RATCH;
        }
    }
    unlock_(f);
    seq_touch();
}

void seq_pat_set_bars(uint32_t ch, uint32_t bars)
{
    pat_t *p = seq_pat(ch);
    uint32_t f, i;
    bars = bars < 1u ? 1u : bars > 8u ? 8u : bars;
    f = lock_();
    p->bars = (uint8_t)bars;
    for (i = p->n; i-- > 0;)             /* what is past the end goes */
        if (p->ev[i].pos >= pat_len(p))
            ev_del(p, i);
    unlock_(f);
    seq_touch();
}

void seq_pat_bars(uint32_t ch, int32_t d)   /* x2: the notes doubled into the new half; 1/2: the first half */
{
    pat_t *p = seq_pat(ch);
    uint32_t f, i, n;
    if (d == 0)
        return;
    undo_take(ch);
    if (d > 0) {
        if (p->bars >= 8u)
            return;
        f = lock_();
        n = p->n;
        for (i = 0; i < n; i++) {
            ev_t e = p->ev[i];
            if (ev_add(p, 0, 0, 0, 1, 0) < 0)
                break;
            e.pos = (uint16_t)(e.pos + pat_len(p));
            p->ev[p->n - 1u] = e;
        }
        p->bars = (uint8_t)(p->bars * 2u > 8u ? 8u : p->bars * 2u);
        unlock_(f);
        seq_touch();
    } else if (p->bars > 1u) {
        seq_pat_set_bars(ch, p->bars / 2u);
    }
}

void seq_pat_div(uint32_t ch, int32_t d)
{
    pat_t *p = seq_pat(ch);
    int32_t i;
    for (i = 0; i < NDIV && DIV_ORDER[i] != p->div; i++)
        ;
    p->div = DIV_ORDER[clamp(i + d, 0, NDIV - 1)];   /* fine to coarse: 1/32 .. 2 BAR */
    seq_touch();
}

void seq_pat_swing(uint32_t ch, int32_t d)
{
    pat_t *p = seq_pat(ch);
    p->swing = (uint8_t)clamp(p->swing + d * 2, 0, 100);
    seq_touch();
}

void seq_pat_shift(uint32_t ch, int32_t d)
{
    pat_t *p = seq_pat(ch);
    uint32_t f, i, len = pat_len(p);
    int32_t by = d * (int32_t)seq_step_ticks(p);
    undo_take(ch);
    f = lock_();
    for (i = 0; i < p->n; i++)
        p->ev[i].pos = (uint16_t)(((int32_t)p->ev[i].pos + by % (int32_t)len + (int32_t)len) % (int32_t)len);
    unlock_(f);
    seq_touch();
}

void seq_pat_transpose(uint32_t ch, int32_t d)
{
    pat_t *p = seq_pat(ch);
    uint32_t f = lock_(), i;
    for (i = 0; i < p->n; i++)
        if (!(p->ev[i].fl & EV_LOCK))
            p->ev[i].note = (uint8_t)clamp(p->ev[i].note + d, 0, 127);
    unlock_(f);
    seq_touch();
}

void seq_erase(uint32_t ch, int32_t note)
{
    pat_t *p = seq_pat(ch);
    uint32_t f, i;
    undo_take(ch);
    f = lock_();
    for (i = p->n; i-- > 0;)
        if (!(p->ev[i].fl & EV_LOCK) && p->ev[i].note == note)
            ev_del(p, i);
    unlock_(f);
    seq_touch();
}

void seq_erase_hold(uint32_t ch, int32_t note)
{
    if (note >= 0 && erase_note[ch % NCH] < 0)
        undo_take(ch);
    erase_note[ch % NCH] = (int16_t)note;
}

void seq_clear(uint32_t ch)
{
    uint32_t f;
    undo_take(ch);
    f = lock_();
    seq_pat(ch)->n = 0;
    seq_drones_off((int32_t)ch);
    unlock_(f);
    seq_touch();
}

/* ------------------------------------------------------------ the scenes --- */
uint32_t seq_scene(void) { return cur_scene; }
int32_t seq_queued(void) { return queued; }
uint32_t seq_mute(void) { return song.scene[cur_scene].mute; }
uint32_t seq_solo(void) { return song.scene[cur_scene].solo; }
uint32_t seq_section(void) { return sect_i; }
int seq_song_rec_on(void) { return song_rec; }

int seq_scene_used(uint32_t s)
{
    uint32_t ch;
    for (ch = 0; ch < NCH; ch++)
        if (song.pat[ch][song.scene[s % NSCENE].pat[ch] % NPAT].n)
            return 1;
    return 0;
}

void seq_set_mute(uint32_t m)
{
    song.scene[cur_scene].mute = (uint8_t)m;
    synth_mute(m);
    seq_touch();
}
void seq_set_solo(uint32_t m)
{
    song.scene[cur_scene].solo = (uint8_t)m;
    synth_solo(m);
    seq_touch();
}
void seq_set_level(uint32_t ch, uint32_t l)
{
    song.scene[cur_scene].level[ch % NCH] = (uint8_t)(l > 127u ? 127u : l);
    synth_set_level(ch, l);
    seq_touch();
}

/* the sounds playing now, into the scene (if it has its own) or the song */
static void sounds_keep(void)
{
    uint32_t ch;
    scene_t *sc = &song.scene[cur_scene];
    for (ch = 0; ch < NCH; ch++) {
        memcpy(song.fm6[ch], synth_fm6_own(ch), SYN_FM6_BYTES);
        song.mod[ch] = *synth_mod(ch);
    }
    for (ch = 0; ch < NCH; ch++) {
        if (sc->has_sound)
            sc->snd[ch] = *synth_sound(ch);
        else {
            uint32_t k;
            const char *nm = synth_own_name(ch);
            song.snd[ch] = *synth_sound(ch);
            memset(song.sname[ch], 0, SYN_NAME);   /* (its own name with it: a scene's sounds have none) */
            for (k = 0; k < SYN_NAME && nm[k]; k++)
                song.sname[ch][k] = nm[k];
        }
    }
}

static void scene_apply(uint32_t s, uint32_t t)
{
    uint32_t ch, had = song.scene[cur_scene].has_sound;
    scene_t *sc = &song.scene[s % NSCENE];
    sounds_keep();
    if (rec == REC_ON && seq_cfg.rec_replace && s % NSCENE != cur_scene) {
        rec_ends();                      /* (another scene: the REPLACE pass stops there) */
        rec = REC_OFF;
    }
    cur_scene = (uint8_t)(s % NSCENE);
    seq_drones_off(-1);                  /* (another scene: its own drones, struck at their place) */
    for (ch = 0; ch < NCH; ch++) {
        pstart[ch] = t;
        synth_set_level(ch, sc->level[ch]);
        if (sc->has_sound)
            synth_set_sound(ch, &sc->snd[ch]);
        else if (had) {
            synth_set_sound(ch, &song.snd[ch]);
            synth_set_name(ch, song.sname[ch], SYN_NAME);
        }
        synth_unlock(ch);
    }
    synth_mute(sc->mute);
    synth_solo(sc->solo);
    scene_t0 = t;
    if (song_rec && playing) {           /* song recording: the launch a section of the chain */
        if (song.nchain && song.chain[song.nchain - 1u].bars == 0u) {
            uint32_t b = (t - sect_t0 + TBAR / 2u) / TBAR;
            song.chain[song.nchain - 1u].bars = (uint8_t)(b < 1u ? 1u : b > 64u ? 64u : b);
        }
        if (song.nchain < NCHAIN) {
            song.chain[song.nchain].scene = cur_scene;
            song.chain[song.nchain].bars = 0;
            song.chain[song.nchain].reps = 1;
            song.nchain++;
        }
        sect_t0 = t;
    }
}

void seq_launch(uint32_t s)
{
    uint32_t f;
    qchain_n = 0;
    if (!playing) {                      /* stopped: at once */
        f = lock_();
        scene_apply(s, 0);
        queued = -1;
        unlock_(f);
    } else {
        queued = (int8_t)(s % NSCENE);
    }
    seq_touch();
}

void seq_qchain_add(uint32_t s)
{
    if (qchain_n == 0u) {
        qchain[0] = (uint8_t)(queued >= 0 ? queued : cur_scene);
        qchain_n = 1;
        qchain_i = 0;
    }
    if (qchain_n < 8u)
        qchain[qchain_n++] = (uint8_t)(s % NSCENE);
}
uint32_t seq_qchain_len(void) { return qchain_n; }

void seq_scene_copy(uint32_t from, uint32_t to)
{
    uint32_t ch, f;
    scene_t *a = &song.scene[from % NSCENE], *b = &song.scene[to % NSCENE];
    if (from % NSCENE == to % NSCENE)
        return;
    sounds_keep();
    f = lock_();
    for (ch = 0; ch < NCH; ch++)         /* the patterns copied into the target's own */
        song.pat[ch][b->pat[ch] % NPAT] = song.pat[ch][a->pat[ch] % NPAT];
    b->mute = a->mute;
    b->solo = a->solo;
    b->has_sound = a->has_sound;
    for (ch = 0; ch < NCH; ch++) {
        b->level[ch] = a->level[ch];
        b->snd[ch] = a->snd[ch];
    }
    unlock_(f);
    seq_touch();
}

void seq_scene_sound(uint32_t s, int on)
{
    uint32_t ch;
    scene_t *sc = &song.scene[s % NSCENE];
    if (!!sc->has_sound == !!on)
        return;
    if (s % NSCENE == cur_scene)
        sounds_keep();
    if (on)
        for (ch = 0; ch < NCH; ch++)
            sc->snd[ch] = s % NSCENE == cur_scene ? *synth_sound(ch) : song.snd[ch];
    sc->has_sound = (uint8_t)!!on;
    seq_touch();
}

void seq_scene_pat(uint32_t s, uint32_t ch, uint32_t pi)
{
    uint32_t f = lock_();
    song.scene[s % NSCENE].pat[ch % NCH] = (uint8_t)(pi % NPAT);
    unlock_(f);
    seq_touch();
}

void seq_mode(uint32_t m)
{
    song.mode = (uint8_t)(m ? MODE_SONG : MODE_LOOP);
    seq_touch();
}

void seq_song_rec(int on)
{
    uint32_t f = lock_();
    song_rec = (uint8_t)!!on;
    if (on) {                            /* a new chain, from the scene playing */
        song.nchain = 1;
        song.chain[0].scene = cur_scene;
        song.chain[0].bars = 0;
        song.chain[0].reps = 1;
        sect_t0 = tick;
    } else {
        if (song.nchain && song.chain[song.nchain - 1u].bars == 0u) {
            uint32_t b = (tick - sect_t0 + TBAR / 2u) / TBAR;
            song.chain[song.nchain - 1u].bars = (uint8_t)(b < 1u ? 1u : b > 64u ? 64u : b);
        }
        if (song.nchain)                 /* (recorded: the song plays it from now on) */
            song.mode = MODE_SONG;
    }
    unlock_(f);
    seq_touch();
}

/* the editor's STEPS (docs/EDITOR-PROTOCOL.md 0x71): one bar of a channel's pattern, each note 6 bytes: its step at
 * the pattern's DIV, its offset from that step (ticks, i8), the note, the velocity, the length (6 ticks a unit; DRONE
 * a flag) and its flags (EV_RATCH, EV_FILL, EV_NOFILL, EV_DRONE). The parameter locks are not notes: kept as they are */
#define BAR_FL (EV_RATCH | EV_FILL | EV_NOFILL | EV_DRONE)
int seq_bar_read(uint32_t ch, uint32_t bar, uint8_t *out, uint32_t max)   /* the notes written (-1: no such bar) */
{
    const pat_t *p = seq_pat(ch % NCH);
    uint32_t i, n = 0, st = seq_step_ticks(p), b0 = bar * TBAR, f;
    if (bar >= p->bars)
        return -1;
    f = lock_();
    for (i = 0; i < p->n && n < max; i++) {
        const ev_t *e = &p->ev[i];
        uint32_t in;
        int32_t step, off;
        if ((e->fl & EV_LOCK) || e->pos < b0 || e->pos >= b0 + TBAR)
            continue;
        in = e->pos - b0;
        step = (int32_t)((in + st / 2u) / st);
        off = (int32_t)in - step * (int32_t)st;
        out[6 * n] = (uint8_t)step;
        out[6 * n + 1] = (uint8_t)(int8_t)off;
        out[6 * n + 2] = e->note;
        out[6 * n + 3] = e->vel;
        out[6 * n + 4] = e->len;
        out[6 * n + 5] = (uint8_t)(e->fl & BAR_FL);
        n++;
    }
    unlock_(f);
    return (int)n;
}
int seq_bar_write(uint32_t ch, uint32_t bar, const uint8_t *in, uint32_t n)   /* the bar's notes replaced: 0, -1 out of
                                                                               * range, -2 busy (recording it) */
{
    pat_t *p;
    uint32_t i, st, b0 = bar * TBAR, f;
    ch %= NCH;
    p = seq_pat(ch);
    if (bar >= p->bars)
        return -1;
    if ((rec == REC_ON || rec == REC_COUNT || rec == REC_TAKE) && rec_ch == ch)
        return -2;
    st = seq_step_ticks(p);
    for (i = 0; i < n; i++) {            /* (all in the bar, or nothing changes) */
        int32_t pos = (int32_t)(in[6 * i] * st) + (int8_t)in[6 * i + 1];
        if (pos < 0 || pos >= (int32_t)TBAR || in[6 * i + 2] > 127u || in[6 * i + 3] == 0u || in[6 * i + 3] > 127u)
            return -1;
    }
    undo_take(ch);                       /* (EDIT + OCT- undoes it, as a recording) */
    f = lock_();
    for (i = 0; i < p->n;)
        if (!(p->ev[i].fl & EV_LOCK) && p->ev[i].pos >= b0 && p->ev[i].pos < b0 + TBAR)
            ev_del(p, i);
        else
            i++;
    for (i = 0; i < n; i++)
        ev_add(p, b0 + (uint32_t)((int32_t)(in[6 * i] * st) + (int8_t)in[6 * i + 1]), in[6 * i + 2], in[6 * i + 3],
               in[6 * i + 4] ? in[6 * i + 4] : 1u, in[6 * i + 5] & BAR_FL);
    seq_drones_off((int32_t)ch);         /* (its drones struck again from the bar as it is now) */
    unlock_(f);
    seq_touch();
    return 0;
}
void seq_scene_set(uint32_t s, const uint8_t *pat, uint32_t mute, uint32_t solo, const uint8_t *level)
{
    uint32_t f = lock_(), ch;
    scene_t *sc = &song.scene[s % NSCENE];
    for (ch = 0; ch < NCH; ch++) {
        sc->pat[ch] = (uint8_t)(pat[ch] % NPAT);
        sc->level[ch] = (uint8_t)(level[ch] > 127u ? 127u : level[ch]);
    }
    sc->mute = (uint8_t)(mute & 0x3Fu);
    sc->solo = (uint8_t)(solo & 0x3Fu);
    unlock_(f);
    seq_touch();
}
int seq_chain_set(const uint8_t *sec, uint32_t n, int end_stop)   /* the song's order replaced: 0, -2 busy (SONG REC) */
{
    uint32_t f, i;
    if (song_rec || n > NCHAIN)
        return song_rec ? -2 : -1;
    f = lock_();
    for (i = 0; i < n; i++) {
        song.chain[i].scene = (uint8_t)(sec[3 * i] % NSCENE);
        song.chain[i].bars = (uint8_t)(sec[3 * i + 1] > 64u ? 64u : sec[3 * i + 1]);
        song.chain[i].reps = (uint8_t)(sec[3 * i + 2] < 1u ? 1u : sec[3 * i + 2] > 16u ? 16u : sec[3 * i + 2]);
        song.chain[i].rsv = 0;
    }
    song.nchain = (uint8_t)n;
    song.chain[0].rsv = (uint8_t)(end_stop ? 1u : 0u);
    if (sect_i >= song.nchain)
        sect_i = 0;
    unlock_(f);
    seq_touch();
    return 0;
}

/* the CHAIN page's keys: the song's order typed in, a scene a key (its own length in bars), deleted, its END */
static uint32_t scene_len(uint32_t s);
uint32_t seq_scene_bars(uint32_t s) { return scene_len(s % NSCENE) / TBAR; }
int seq_chain_insert(uint32_t pos, uint32_t scene)   /* a section with the scene before pos (nchain: at the end) */
{
    uint32_t f, i, b;
    uint8_t end = song.chain[0].rsv & 1u;   /* (the song's END is kept in the first place's spare byte) */
    if (song.nchain >= NCHAIN)
        return -1;
    f = lock_();
    pos = pos > song.nchain ? song.nchain : pos;
    for (i = song.nchain; i > pos; i--)
        song.chain[i] = song.chain[i - 1u];
    b = scene_len(scene % NSCENE) / TBAR;
    song.chain[pos].scene = (uint8_t)(scene % NSCENE);
    song.chain[pos].bars = (uint8_t)(b < 1u ? 1u : b > 64u ? 64u : b);
    song.chain[pos].reps = 1;
    song.chain[pos].rsv = 0;
    song.chain[0].rsv = end;
    if (song.nchain)
        song.chain[1].rsv = 0;
    song.nchain++;
    unlock_(f);
    seq_touch();
    return 0;
}
void seq_chain_delete(uint32_t pos)
{
    uint32_t f, i;
    uint8_t end = song.chain[0].rsv & 1u;
    if (pos >= song.nchain)
        return;
    f = lock_();
    for (i = pos; i + 1u < song.nchain; i++)
        song.chain[i] = song.chain[i + 1u];
    song.nchain--;
    song.chain[0].rsv = end;
    if (sect_i >= song.nchain)
        sect_i = 0;
    unlock_(f);
    seq_touch();
}
int seq_chain_end_stop(void) { return song.chain[0].rsv & 1u; }
void seq_chain_set_end_stop(int on)
{
    song.chain[0].rsv = (uint8_t)((song.chain[0].rsv & ~1u) | (on ? 1u : 0u));
    seq_touch();
}
uint32_t seq_sect(void) { return sect_i; }   /* the section playing (song mode) */

/* ------------------------------------- arp, note repeat, strum, fills --- */
static volatile uint8_t fill_hold, fill_arm, fill_now;
void seq_fill_hold(int on) { fill_hold = (uint8_t)!!on; }
void seq_fill_bar(void) { fill_arm = 1; }
int seq_fill(void) { return fill_hold || fill_now; }

uint32_t seq_step_flag(uint32_t ch, uint32_t s)
{
    int i = seq_step_find(ch, s);
    const ev_t *e = i >= 0 ? &seq_pat(ch)->ev[i] : 0;
    return !e ? 0u : (e->fl & EV_FILL) ? 1u : (e->fl & EV_NOFILL) ? 2u : 0u;
}
void seq_step_flag_turn(uint32_t ch, uint32_t s)   /* (SEQ: a held step + OCT+) normal -> FILL ONLY -> NO FILL */
{
    pat_t *p = seq_pat(ch);
    uint32_t f = lock_(), i, nf = (seq_step_flag(ch, s) + 1u) % 3u;
    for (i = 0; i < p->n; i++)
        if (!(p->ev[i].fl & EV_LOCK) && ev_step(p, &p->ev[i]) == s)
            p->ev[i].fl = (uint8_t)((p->ev[i].fl & ~(EV_FILL | EV_NOFILL)) | (nf == 1u ? EV_FILL : nf == 2u ? EV_NOFILL : 0u));
    unlock_(f);
    seq_touch();
}

/* the arpeggiator: each channel's held notes (in the order pressed), stepped at its RATE on a clock of its own
 * that runs with the transport or without it */
#define ARP_MAX 8
typedef struct {
    uint8_t note[ARP_MAX], vel[ARP_MAX], n, held, latch;
    uint32_t t, step, rnd, last;
    int8_t dir;
} arp_t;
static arp_t arp[NCH];
static uint32_t atick;                   /* ticks, always running */
static uint32_t aacc;
static const uint8_t ARP_TICKS[6] = {96, 48, 32, 24, 16, 12};

static int note_end_(uint32_t ch, uint32_t note);
int seq_arp_on(uint32_t ch) { const sound_t *s = synth_sound(ch); return s->arp != 0u || s->arp_hold; }
/* HOLD on or off (SHIFT + PLAY; ARP 2's HOLD): off, the latched notes end */
void seq_hold(uint32_t ch, int on)
{
    uint32_t f, i;
    ch %= NCH;
    synth_param_set(ch, SP_ARP_HOLD, on ? 1 : 0);
    if (on)
        return;
    f = lock_();
    if (!synth_sound(ch)->arp)
        for (i = 0; i < arp[ch].n; i++) {
            if (!note_end_(ch, arp[ch].note[i]))
                synth_note_off(ch, arp[ch].note[i]);
            synth_protect(ch, arp[ch].note[i], 0);
        }
    arp[ch].n = 0;
    arp[ch].held = 0;
    unlock_(f);
}
int seq_held_n(uint32_t ch) { return synth_sound(ch % NCH)->arp_hold ? (int)arp[ch % NCH].n : 0; }

static void rec_insert(uint32_t ch, uint32_t note, uint32_t vel);
void seq_arp_key(uint32_t ch, uint32_t note, uint32_t vel, int on)
{
    uint32_t f = lock_(), i;
    ch %= NCH;
    if (on && synth_sound(ch)->arp_hold && !synth_sound(ch)->arp) {   /* HOLD, the arp off: each key latches its note,
                                          * a latched one tapped again ends (a drone built up note by note) */
        for (i = 0; i < arp[ch].n && arp[ch].note[i] != note; i++)
            ;
        if (i < arp[ch].n) {
            uint32_t j;
            if (!note_end_(ch, note))
                synth_note_off(ch, note);
            synth_protect(ch, note, 0);
            for (j = i; j + 1u < arp[ch].n; j++) {
                arp[ch].note[j] = arp[ch].note[j + 1u];
                arp[ch].vel[j] = arp[ch].vel[j + 1u];
            }
            arp[ch].n--;
        } else if (arp[ch].n < ARP_MAX) {
            synth_note_on(ch, note, vel);
            synth_protect(ch, note, 1);
            if (rec == REC_ON && playing && ch == rec_ch)
                rec_insert(ch, note, vel);
            arp[ch].note[arp[ch].n] = (uint8_t)note;
            arp[ch].vel[arp[ch].n++] = (uint8_t)vel;
        }
        unlock_(f);
        return;
    }
    if (on) {
        const sound_t *s = synth_sound(ch);
        if (s->arp_hold && !arp[ch].held) {   /* HOLD: the first key with none down replaces what was latched */
            if (!s->arp)                 /* (KBD HOLD, the arp off: the latched notes end here) */
                for (i = 0; i < arp[ch].n; i++)
                    synth_note_off(ch, arp[ch].note[i]);
            arp[ch].n = 0;
            arp[ch].step = 0;
        }
        if (!s->arp) {                   /* KBD HOLD: the note at once, held till the next fresh chord */
            synth_note_on(ch, note, vel);
            if (rec == REC_ON && playing && ch == rec_ch)
                rec_insert(ch, note, vel);
        }
        arp[ch].held++;
        for (i = 0; i < arp[ch].n && arp[ch].note[i] != note; i++)
            ;
        if (i == arp[ch].n && arp[ch].n < ARP_MAX) {
            arp[ch].note[arp[ch].n] = (uint8_t)note;
            arp[ch].vel[arp[ch].n++] = (uint8_t)vel;
        }
        if (arp[ch].n == 1u)
            arp[ch].t = atick;           /* (the first note: at once) */
    } else {
        if (arp[ch].held)
            arp[ch].held--;
        if (!synth_sound(ch)->arp_hold)  /* (without HOLD the note goes) */
            for (i = 0; i < arp[ch].n; i++)
                if (arp[ch].note[i] == note) {
                    uint32_t j;          /* (no memmove on the device) */
                    for (j = i; j + 1u < arp[ch].n; j++) {
                        arp[ch].note[j] = arp[ch].note[j + 1u];
                        arp[ch].vel[j] = arp[ch].vel[j + 1u];
                    }
                    arp[ch].n--;
                    break;
                }
    }
    unlock_(f);
}

/* the arp's next note for the mode, from the notes (sorted low to high, or in the order played: ORD) */
static int arp_pick_in(arp_t *a, const sound_t *s, uint32_t *vel)
{
    uint8_t n[ARP_MAX * 4], v[ARP_MAX * 4];
    uint32_t k = a->n, i, j, oct = s->arp_oct ? s->arp_oct : 1u, cnt = 0, st = a->step++, idx;
    uint32_t mode = s->arp;
    if (!k)
        return -1;
    for (j = 0; j < oct; j++)            /* the notes over the octaves */
        for (i = 0; i < k && cnt < sizeof n; i++) {
            n[cnt] = (uint8_t)clamp(a->note[i] + 12 * (int32_t)j, 0, 127);
            v[cnt++] = a->vel[i];
        }
    if (s->arp_order == 0u || mode != 5u)   /* (ORD plays them as pressed; else low to high) */
        for (i = 1; i < cnt; i++)
            for (j = i; j > 0 && n[j - 1] > n[j]; j--) {
                uint8_t a = n[j], b = v[j];
                n[j] = n[j - 1];
                v[j] = v[j - 1];
                n[j - 1] = a;
                v[j - 1] = b;
            }
    switch (mode) {
    case 1: idx = st % cnt; break;                                          /* UP */
    case 2: idx = cnt - 1u - st % cnt; break;                               /* DN */
    case 3: idx = cnt < 2u ? 0u : (st % (2u * cnt - 2u) < cnt ? st % (2u * cnt - 2u) : 2u * cnt - 2u - st % (2u * cnt - 2u)); break;
    case 4: a->rnd = a->rnd * 1103515245u + 12345u; idx = (a->rnd >> 16) % cnt; break;   /* RND */
    case 5: idx = st % cnt; break;                                          /* ORD */
    case 6: idx = cnt - 1u; break;                                          /* REPEAT: the last */
    case 7: idx = cnt < 2u ? 0u : (st % (2u * cnt - 2u) < cnt ? cnt - 1u - st % (2u * cnt - 2u) : st % (2u * cnt - 2u) - cnt + 1u); break;
    case 8: idx = st % (cnt + 1u); if (idx == cnt) { *vel = v[0]; return clamp(n[0] + 12, 0, 127); } break;   /* UP+8 */
    case 9: idx = (st % cnt) % 2u ? cnt - 1u - (st % cnt) / 2u : (st % cnt) / 2u; break;   /* CONV: outside in */
    case 10: { uint32_t c = cnt / 2u, q = st % cnt; idx = q % 2u ? c + (q + 1u) / 2u : c - q / 2u; idx = idx >= cnt ? cnt - 1u : idx; break; }   /* DIVG */
    case 11: idx = st % 2u ? cnt - 1u : (st / 2u) % (cnt > 1u ? cnt - 1u : 1u); break;   /* PINKY: the top between */
    case 12: idx = st % 2u ? 0u : 1u + (st / 2u) % (cnt > 1u ? cnt - 1u : 1u); idx = idx >= cnt ? 0u : idx; break;   /* THUMB */
    case 13: a->rnd = a->rnd * 1103515245u + 12345u;               /* WALK: a step up or down */
        a->last = (uint32_t)clamp((int32_t)a->last + ((a->rnd >> 16) & 1u ? 1 : -1), 0, (int32_t)cnt - 1);
        idx = a->last; break;
    case 14: idx = 0; break;                                                /* CHORD: all at once (arp_run) */
    default: idx = st % cnt; break;
    }
    *vel = v[idx % cnt];
    return n[idx % cnt];
}
static int arp_pick(uint32_t ch, const sound_t *s, uint32_t *vel) { return arp_pick_in(&arp[ch], s, vel); }

/* the graph view: the arp's next notes from its first step, from the notes held (none: C E G) */
uint32_t seq_arp_preview(uint32_t ch, uint8_t *out, uint32_t max)
{
    arp_t a = arp[ch % NCH];
    const sound_t *s = synth_sound(ch % NCH);
    uint32_t i, vel;
    if (!a.n) {
        static const uint8_t CEG[3] = {60, 64, 67};
        memcpy(a.note, CEG, 3);
        memset(a.vel, 100, 3);
        a.n = 3;
    }
    a.step = 0;
    a.last = 0;
    a.rnd = 12345u;
    for (i = 0; i < max; i++) {
        int nt = s->arp == 14u ? a.note[i % a.n] : arp_pick_in(&a, s, &vel);
        out[i] = (uint8_t)(nt < 0 ? 0 : nt);
    }
    return max;
}

/* the note repeat (ARP layer): held notes again at ROLL's rate, recorded as they sound */
static struct { uint8_t note, vel, on; } roll[NCH];
static const uint8_t ROLL_TICKS[5] = {48, 24, 12, 8, 6};
void seq_roll(uint32_t ch, uint32_t note, uint32_t vel, int on)
{
    uint32_t f = lock_();
    roll[ch % NCH].note = (uint8_t)note;
    roll[ch % NCH].vel = (uint8_t)vel;
    roll[ch % NCH].on = (uint8_t)!!on;
    unlock_(f);
}

/* strummed chords: the notes a few ms apart (samples, the render's clock) */
#define NSTRUM 16u
static struct { uint32_t at; uint8_t ch, note, vel; } strum_q[NSTRUM];
static uint32_t nstrum;
static uint32_t smp_now(void);
void seq_strum(uint32_t ch, uint32_t note, uint32_t vel, uint32_t ms)
{
    uint32_t f = lock_();
    if (nstrum < NSTRUM) {
        strum_q[nstrum].at = smp_now() + ms * (SEQ_FS / 1000u);
        strum_q[nstrum].ch = (uint8_t)ch;
        strum_q[nstrum].note = (uint8_t)note;
        strum_q[nstrum++].vel = (uint8_t)vel;
    }
    unlock_(f);
}

/* ------------------------------------------------------------- transport --- */
uint32_t seq_bpm10(void) { return song.bpm10; }
void seq_set_bpm10(uint32_t b)
{
    song.bpm10 = (uint16_t)(b < 400u ? 400u : b > 2400u ? 2400u : b);
    synth_tempo(song.bpm10);
    seq_touch();
}

void seq_tap(void)
{
    static uint32_t last, n, sum;
    uint32_t now = plat_ms(), d = now - last;
    last = now;
    if (d > 2000u || d < 200u) {
        n = sum = 0;
        return;
    }
    sum += d;
    n++;
    seq_set_bpm10(600000u * n / sum);
}

int seq_playing(void) { return playing; }
uint32_t seq_rec(void) { return rec; }
uint32_t seq_rec_ch(void) { return rec_ch; }
uint32_t seq_bar(void) { return bars_played; }
int32_t seq_count(void) { return count_t >= 0 ? 4 - count_t / TPQ : 0; }
int32_t seq_take_ms(void) { return rec == REC_TAKE ? (int32_t)(take_t * 1000u / SEQ_FS) : 0; }

static uint8_t nw_midi_ch(uint32_t ch);     /* (app.c: the channel's MIDI channel, 1..16, 0 none) */
static void midi_seq_out(uint32_t ch, uint32_t note, uint32_t vel)
{
    uint32_t mc = nw_midi_ch(ch);
    if (!seq_cfg.seq_out || !mc)
        return;
    mc--;
    if (vel)
        plat_midi_send(0x09u | (0x90u | mc) << 8 | note << 16 | vel << 24, TO_USB | TO_BLE);
    else
        plat_midi_send(0x08u | (0x80u | mc) << 8 | note << 16, TO_USB | TO_BLE);
}

static void pend_add(uint32_t t, uint32_t kind, uint32_t ch, uint32_t note, uint32_t vel, uint32_t dur)
{
    if (npend >= NPEND) {
        if (kind == PD_OFF) {            /* (full: the note ends now rather than never) */
            synth_note_off(ch, note);
            midi_seq_out(ch, note, 0);
        }
        return;
    }
    pend[npend].t = t;
    pend[npend].kind = (uint8_t)kind;
    pend[npend].ch = (uint8_t)ch;
    pend[npend].note = (uint8_t)note;
    pend[npend].vel = (uint8_t)vel;
    pend[npend].dur = (uint16_t)dur;
    npend++;
}

static void play_note(uint32_t ch, uint32_t note, uint32_t vel, uint32_t dur, uint32_t t)
{
    if (!synth_is_kit(ch))
        note = (uint32_t)clamp((int32_t)note + song.transpose, 0, 127);
    synth_note_on(ch, note, vel);
    midi_seq_out(ch, note, vel);
    pend_add(t + (dur ? dur : 1u), PD_OFF, ch, note, 0, 0);
}

static void pend_run(uint32_t t)
{
    uint32_t i = 0;
    while (i < npend) {
        if ((int32_t)(t - pend[i].t) < 0) {
            i++;
            continue;
        }
        if (pend[i].kind == PD_OFF) {
            synth_note_off(pend[i].ch, pend[i].note);
            midi_seq_out(pend[i].ch, pend[i].note, 0);
        } else if (pend[i].kind == PD_ON) {
            play_note(pend[i].ch, pend[i].note, pend[i].vel, pend[i].dur, t);
        } else {
            synth_unlock(pend[i].ch);
        }
        pend[i] = pend[--npend];
    }
}

static void pend_flush(void)             /* (stop) every note owed its end, the locks gone */
{
    uint32_t i, ch;
    for (i = 0; i < npend; i++)
        if (pend[i].kind == PD_OFF) {
            synth_note_off(pend[i].ch, pend[i].note);
            midi_seq_out(pend[i].ch, pend[i].note, 0);
        }
    npend = 0;
    for (ch = 0; ch < NCH; ch++)
        synth_unlock(ch);
}

/* the channel's notes at tick t */
static void channel_tick(uint32_t ch, uint32_t t)
{
    pat_t *p = seq_pat(ch);
    uint32_t i, len = pat_len(p), pos = (t - pstart[ch]) % len, st = seq_step_ticks(p);
    uint32_t swg = (uint32_t)p->swing + song.swing, sw = (swg > 100u ? 100u : swg) * (TPS / 2u) / 100u;   /* swing: the
                                         * pattern's and the song's, the off sixteenths up to half a 16th late */
    for (i = 0; i < p->n; i++) {
        ev_t *e = &p->ev[i];
        uint32_t at = e->pos;
        if (sw && ((e->pos + TPS / 2u) / TPS & 1u))
            at = (at + sw) % len;
        if (at != pos)
            continue;
        if (((e->fl & EV_FILL) && !seq_fill()) || ((e->fl & EV_NOFILL) && seq_fill()))
            continue;                    /* (a fill's step, or one a fill leaves out) */
        if (e->fl & EV_SKIP) {           /* recorded just ahead of the playhead: already heard */
            e->fl &= (uint8_t)~EV_SKIP;
            continue;
        }
        if (seq_cfg.rec_replace && rec == REC_ON && ch == rec_ch && !(e->fl & EV_NEW))
            continue;                    /* REPLACE recording: the last take silent (kept till the pass is full) */
        if (rec == REC_ON && ch == rec_ch && open_n[ch][e->note & 127u].idx == (int16_t)i)
            continue;                    /* (held on the keys as it records: it sounds already, not struck again) */
        if ((e->fl & EV_DRONE) && !(erase_note[ch] >= 0 && e->note == (uint32_t)erase_note[ch])) {   /* a drone: struck
                                          * once, on till the transport stops or the scene changes (EDIT + key: erased) */
            uint32_t nn = e->note & 127u;
            if (!(drone_on[ch][nn >> 5] >> (nn & 31u) & 1u)) {
                uint32_t pn = synth_is_kit(ch) ? nn : (uint32_t)clamp((int32_t)nn + song.transpose, 0, 127);
                drone_on[ch][nn >> 5] |= 1u << (nn & 31u);
                drone_tr[ch][nn] = (uint8_t)pn;
                synth_note_on(ch, pn, e->vel);
                synth_protect(ch, pn, 1);
                midi_seq_out(ch, pn, e->vel);
            }
            continue;
        }
        if (e->fl & EV_LOCK) {
            synth_lock(ch, e->note, (int32_t)e->vel - 128);
            pend_add(t + st, PD_UNLOCK, ch, 0, 0, 0);
            continue;
        }
        if (erase_note[ch] >= 0 && e->note == (uint32_t)erase_note[ch]) {   /* EDIT held on the key: erased */
            if (drone_on[ch][e->note >> 5 & 3u] >> (e->note & 31u) & 1u) {
                synth_note_off(ch, drone_tr[ch][e->note & 127u]);
                synth_protect(ch, drone_tr[ch][e->note & 127u], 0);
                drone_on[ch][e->note >> 5 & 3u] &= ~(1u << (e->note & 31u));
            }
            ev_del(p, i);
            i--;
            dirty_f = 1;
            continue;
        }
        {
            uint32_t r = (e->fl & EV_RATCH) + 1u, dur = (uint32_t)e->len * 6u, j;
            if (r > 1u) {
                uint32_t sub = st / r;
                if (dur > sub)
                    dur = sub > 2u ? sub - 1u : 1u;
                for (j = 1; j < r; j++)
                    pend_add(t + j * sub, PD_ON, ch, e->note, e->vel, dur);
            }
            play_note(ch, e->note, e->vel, dur, t);
        }
    }
}

static uint32_t scene_len(uint32_t s)    /* the longest pattern the scene plays */
{
    uint32_t ch, m = TBAR;
    for (ch = 0; ch < NCH; ch++) {
        uint32_t l = pat_len(&song.pat[ch][song.scene[s].pat[ch] % NPAT]);
        m = l > m ? l : m;
    }
    return m;
}

static void song_section_start(uint32_t i, uint32_t t)
{
    if (i >= song.nchain && i && (song.chain[0].rsv & 1u) && playing) {   /* END STOP: the last one played, stop */
        song_end = 1;
        return;
    }
    sect_i = (uint8_t)(i < song.nchain ? i : 0u);
    sect_bars = 0;
    sect_rep = 0;
    if (song.nchain)
        scene_apply(song.chain[sect_i].scene, t);
}

/* one tick of the transport */
static void tick_run(uint32_t t)
{
    uint32_t ch;
    if (rec == REC_ON && seq_cfg.rec_replace && ++rp_ticks >= pat_len(seq_pat(rec_ch)))
        rp_settle(1);                    /* REPLACE: a full pass, the new notes the take */
    if (t % TBAR == 0u && t)
        bars_played++;
    if (t % TBAR == 0u) {                /* GLO key 10: the next bar a fill */
        fill_now = fill_arm;
        fill_arm = 0;
    }
    /* the launches: a queued scene at its moment, the song's sections, the quick chain */
    if (song.mode == MODE_SONG && song.nchain && t % TBAR == 0u && t) {
        const sect_t *sc = &song.chain[sect_i];
        if (++sect_bars >= sc->bars && sc->bars) {
            sect_bars = 0;
            if (++sect_rep >= (sc->reps ? sc->reps : 1u))
                song_section_start(sect_i + 1u, t);
        }
    }
    if (queued >= 0) {
        int go = seq_cfg.launch == LQ_NOW || (seq_cfg.launch == LQ_BAR && t % TBAR == 0u)
                 || (seq_cfg.launch == LQ_LOOP && (t - scene_t0) % scene_len(cur_scene) == 0u);
        if (go) {
            if (song.mode == MODE_SONG && song.nchain && !song_rec)
                song_section_start(sect_i + 1u, t);   /* (a launch moves the song on) */
            else
                scene_apply((uint32_t)queued, t);
            queued = -1;
        }
    } else if (qchain_n > 1u && (t - scene_t0) % scene_len(cur_scene) == 0u && t != scene_t0) {
        qchain_i = (uint8_t)((qchain_i + 1u) % qchain_n);
        scene_apply(qchain[qchain_i], t);
    }
    /* the click */
    if (t % TPQ == 0u && (seq_cfg.click == CLICK_ON || (seq_cfg.click == CLICK_REC && rec == REC_ON)))
        synth_click(t % TBAR == 0u);
    /* MIDI clock out (as the master) */
    if (seq_cfg.clock_out && seq_cfg.sync == SYNC_INT && t % 4u == 0u)
        plat_midi_send(0x0Fu | 0xF8u << 8, TO_USB);
    pend_run(t);
    for (ch = 0; ch < NCH; ch++)
        channel_tick(ch, t);
}

static void start_at_zero(void)
{
    uint32_t ch;
    tick = 0;
    acc = 0;
    bars_played = 0;
    for (ch = 0; ch < NCH; ch++)
        pstart[ch] = 0;
    scene_t0 = 0;
    if (song.mode == MODE_SONG && song.nchain)
        song_section_start(0, 0);
    if (song_rec)
        seq_song_rec(1);
    playing = 1;
    if (seq_cfg.clock_out && seq_cfg.sync == SYNC_INT)
        plat_midi_send(0x0Fu | 0xFAu << 8, TO_USB);
    tick_run(0);
}

void seq_play(void)
{
    uint32_t f = lock_();
    if (rec == REC_ARMED && seq_cfg.rec_start == RS_COUNT) {
        count_t = 0;                     /* a bar's count, then the recording */
        rec = REC_COUNT;
        synth_click(1);
    } else if (rec == REC_COUNT || rec == REC_TAKE) {
        rec = REC_ARMED;                 /* the count or the take dropped */
        count_t = -1;
        take_n = 0;
    } else if (!playing) {
        if (rec == REC_ARMED) {
            rec = REC_ON;
            undo_take(rec_ch);
            rp_ticks = 0;
            if (!seq_pat(rec_ch)->n)
                seq_pat(rec_ch)->bars = seq_cfg.rec_bars;
        }
        start_at_zero();
    }
    unlock_(f);
}

void seq_stop(void)
{
    uint32_t f = lock_();
    if (playing && seq_cfg.clock_out && seq_cfg.sync == SYNC_INT)
        plat_midi_send(0x0Fu | 0xFCu << 8, TO_USB);
    if (song_rec)
        seq_song_rec(0);                 /* (a song's recording ends at STOP: the last section's bars) */
    playing = 0;
    pend_flush();
    seq_drones_off(-1);                  /* (the drones end with the transport) */
    queued = -1;
    count_t = -1;
    rec_ends();
    if (rec == REC_ON || rec == REC_COUNT)
        rec = REC_OFF;
    unlock_(f);
}

static int song_empty(void)
{
    uint32_t ch, i;
    for (ch = 0; ch < NCH; ch++)
        for (i = 0; i < NPAT; i++)
            if (song.pat[ch][i].n)
                return 0;
    return 1;
}

/* a free take closed: its length the nearest of 1, 2, 4, 8 bars to the tempo; the tempo from it */
static void take_close(void)
{
    static const uint8_t B[4] = {1, 2, 4, 8};
    uint32_t i, best = 0, bpm = 0, d = take_t ? take_t : 1u;
    int32_t err = 0x7FFFFFFF;
    pat_t *p = seq_pat(rec_ch);
    for (i = 0; i < 4u; i++) {
        uint32_t b10 = (uint32_t)((float)B[i] * 2400.0f * (float)SEQ_FS / (float)d);   /* 4 beats x 60 s x 10 */
        int32_t e = (int32_t)b10 - (int32_t)song.bpm10;
        if (b10 < 400u || b10 > 2400u)
            continue;
        e = e < 0 ? -e : e;
        if (e < err) {
            err = e;
            best = i;
            bpm = b10;
        }
    }
    if (!bpm)
        bpm = song.bpm10;
    seq_set_bpm10(bpm);
    p->bars = B[best];
    p->n = 0;
    for (i = 0; i < take_n; i++) {
        uint32_t len = pat_len(p), pos = (uint32_t)((float)take[i].t * (float)len / (float)d);
        uint32_t ln = (uint32_t)((float)(take[i].len ? take[i].len : SEQ_FS / 10u) * (float)len / (float)d / 6.0f);
        if (seq_cfg.rec_quant) {
            uint32_t st = seq_step_ticks(p);
            pos = (pos + st / 2u) / st * st;
        }
        ev_add(p, pos % len, take[i].note, take[i].vel, ln, 0);
    }
    take_n = 0;
    rec = REC_OFF;
    seq_touch();
    start_at_zero();
}

void seq_rec_button(uint32_t ch)
{
    uint32_t f = lock_();
    switch (rec) {
    case REC_OFF:
        rec_ch = (uint8_t)ch;
        if (playing) {
            rec = REC_ON;
            undo_take(ch);
            rp_ticks = 0;
        } else {
            rec = REC_ARMED;
        }
        break;
    case REC_ARMED:
    case REC_COUNT:
        rec = REC_OFF;
        count_t = -1;
        break;
    case REC_TAKE:
        take_close();
        break;
    default:
        rec_ends();
        rec = REC_OFF;                   /* recording stops, the loop plays on */
        break;
    }
    unlock_(f);
}

void seq_rec_off(void)
{
    uint32_t f = lock_();
    rec_ends();
    rec = REC_OFF;
    unlock_(f);
}

/* a note recorded at the playhead: on the step (QUANT) or where it fell (the offset kept) */
static void rec_insert(uint32_t ch, uint32_t note, uint32_t vel)
{
    pat_t *p = seq_pat(ch);
    uint32_t len = pat_len(p), now = (tick - pstart[ch]) % len, pos = now, fl = 0, st = seq_step_ticks(p);
    int i;
    if (seq_cfg.rec_quant) {
        pos = (now + st / 2u) / st * st % len;
        if (pos != now)
            fl = EV_SKIP;                /* ahead of the playhead: it would sound again at once */
    }
    if (seq_cfg.rec_replace)
        fl |= EV_NEW;                    /* (this pass's: the take once the pass is full) */
    i = ev_add(p, pos, note, vel, st / 6u, fl);
    if (i >= 0) {
        open_n[ch][note & 127u].idx = (int16_t)i;
        open_n[ch][note & 127u].t0 = tick;
        dirty_f = 1;
    }
}

void seq_note_on(uint32_t ch, uint32_t note, uint32_t vel)
{
    uint32_t f = lock_();
    ch %= NCH;
    note &= 127u;
    if (rec == REC_ARMED && !playing && ch == rec_ch) {
        if (seq_cfg.rec_mode == RM_FREE && song_empty()) {   /* the first note starts a free take */
            rec = REC_TAKE;
            take_t = 0;
            take_n = 0;
        } else if (seq_cfg.rec_start == RS_NOTE) {   /* the first note starts the loop, on its first step */
            seq_pat(ch)->bars = (uint8_t)(seq_pat(ch)->n ? seq_pat(ch)->bars : seq_cfg.rec_bars);
            undo_take(ch);
            rec = REC_ON;
            rp_ticks = 0;
            start_at_zero();
        }
    }
    if (rec == REC_TAKE && ch == rec_ch && take_n < TAKE_N) {
        take[take_n].t = take_t;
        take[take_n].len = 0;
        take[take_n].note = (uint8_t)note;
        take[take_n].vel = (uint8_t)vel;
        take_open[note] = (int16_t)take_n++;
    } else if (rec == REC_ON && playing && ch == rec_ch) {
        rec_insert(ch, note, vel);
    }
    unlock_(f);
}

static int note_end_(uint32_t ch, uint32_t note);
int seq_note_off(uint32_t ch, uint32_t note)
{
    uint32_t f = lock_();
    int drone = note_end_(ch % NCH, note & 127u);
    unlock_(f);
    return drone;
}
static int note_end_(uint32_t ch, uint32_t note)   /* (locked) a note's end: its recorded length, a drone */
{
    int drone = 0;
    if (rec == REC_TAKE && ch == rec_ch && take_open[note] >= 0 && (uint32_t)take_open[note] < take_n) {
        take[take_open[note]].len = take_t - take[take_open[note]].t;
        take_open[note] = -1;
    } else if (open_n[ch][note].idx >= 0) {
        pat_t *p = seq_pat(ch);
        int i = open_n[ch][note].idx;
        if (i < p->n && p->ev[i].note == note && !(p->ev[i].fl & EV_LOCK)) {
            uint32_t held = tick - open_n[ch][note].t0, d = held / 6u;
            p->ev[i].len = (uint8_t)(d < 1u ? 1u : d > 255u ? 255u : d);
            if (held >= pat_len(p) && playing) {   /* held over the whole loop: a drone, on from now */
                uint32_t pn = synth_is_kit(ch) ? note : (uint32_t)clamp((int32_t)note + song.transpose, 0, 127);
                p->ev[i].fl |= EV_DRONE;
                p->ev[i].len = 255;
                drone_on[ch][note >> 5] |= 1u << (note & 31u);
                drone_tr[ch][note] = (uint8_t)pn;
                synth_protect(ch, pn, 1);
                drone = 1;
            }
        }
        open_n[ch][note].idx = -1;
    }
    return drone;
}

/* MIDI clock: the pulses move the transport (SYNC USB / TRS / BLE), START / CONTINUE / STOP */
void seq_midi_rt(uint32_t st, int port)
{
    static const int PORT_OF[4] = {-1, PORT_USB, PORT_TRS, PORT_BLE};
    if (seq_cfg.sync == SYNC_INT || port != PORT_OF[seq_cfg.sync & 3u])
        return;
    if (st == 0xF8u) {
        if (playing)
            ext_owed += 4;
        ext_t[ext_n % 25u] = smp;
        ext_n++;
        if (ext_n >= 25u) {              /* the tempo over the last 24 pulses (a quarter note) */
            uint32_t d = smp - ext_t[ext_n % 25u];
            if (d)
                song.bpm10 = (uint16_t)clamp((int32_t)(600u * SEQ_FS / d), 400, 2400);
        }
    } else if (st == 0xFAu) {
        ext_owed = 0;
        start_at_zero();
    } else if (st == 0xFBu) {
        playing = 1;
    } else if (st == 0xFCu) {
        seq_stop();
    }
}

static uint32_t smp_now(void) { return smp; }
static void rec_insert(uint32_t ch, uint32_t note, uint32_t vel);

/* the arp, the rolls, the strums: on their own clock */
static void arp_run(uint32_t n)
{
    uint32_t ch, adv, i, k;
    aacc += n * song.bpm10 * TPQ;
    adv = aacc / (600u * SEQ_FS);
    aacc -= adv * 600u * SEQ_FS;
    for (i = 0; i < nstrum;) {           /* strums due */
        if ((int32_t)(smp - strum_q[i].at) >= 0) {
            synth_note_on(strum_q[i].ch, strum_q[i].note, strum_q[i].vel);
            strum_q[i] = strum_q[--nstrum];
        } else {
            i++;
        }
    }
    for (k = 0; k < adv; k++) {
        atick++;
        for (ch = 0; ch < NCH; ch++) {
            const sound_t *s = synth_sound(ch);
            if (roll[ch].on) {           /* the roll */
                uint32_t rt = ROLL_TICKS[clamp(synth_master(MST_ROLL), 0, 4)];
                if (atick % rt == 0u) {
                    play_note(ch, roll[ch].note, roll[ch].vel, rt / 2u, atick + (playing ? tick - atick : 0u));
                    if (rec == REC_ON && playing && ch == rec_ch)
                        rec_insert(ch, roll[ch].note, roll[ch].vel);
                }
            }
            if (s->arp && arp[ch].n && (int32_t)(atick - arp[ch].t) >= 0) {   /* the arp's next */
                uint32_t rt = ARP_TICKS[s->arp_rate % 6u], vel, gate = rt * (s->arp_gate ? s->arp_gate : 64u) / 128u;
                int note;
                uint32_t sw = (arp[ch].step & 1u) ? rt * s->arp_swing / 200u : 0u;
                arp[ch].t = atick + rt + sw;
                arp[ch].rnd = arp[ch].rnd * 1103515245u + 12345u;
                if (s->arp_prob < 127u && ((arp[ch].rnd >> 16) & 127u) > s->arp_prob) {
                    arp[ch].step++;
                    continue;
                }
                if (s->arp == 14u) {     /* CHORD: every note together */
                    for (i = 0; i < arp[ch].n; i++)
                        play_note(ch, arp[ch].note[i], arp[ch].vel[i], gate ? gate : 1u, playing ? tick : atick);
                    continue;
                }
                note = arp_pick(ch, s, &vel);
                if (note >= 0) {
                    play_note(ch, (uint32_t)note, vel, gate ? gate : 1u, playing ? tick : atick);
                    if (rec == REC_ON && playing && ch == rec_ch)
                        rec_insert(ch, (uint32_t)note, vel);
                }
            }
        }
        if (!playing)                    /* (stopped: the notes' ends on the arp's clock) */
            pend_run(atick);
    }
}

void seq_render(uint32_t n)
{
    uint32_t adv, i;
    smp += n;
    synth_transport(tick, playing);
    arp_run(n);
    if (rec == REC_TAKE) {
        take_t += n;
        if (take_t > 24u * SEQ_FS)           /* a take closes itself after 24 s */
            take_close();
    }
    if (count_t >= 0) {                  /* the count-in: a bar of clicks, then the loop recording */
        uint32_t t0 = (uint32_t)count_t, t1;
        acc += n * song.bpm10 * TPQ;
        adv = acc / (600u * SEQ_FS);
        acc -= adv * 600u * SEQ_FS;
        t1 = t0 + adv;
        for (i = t0 + 1u; i <= t1 && i < TBAR; i++)
            if (i % TPQ == 0u)
                synth_click(0);
        if (t1 >= TBAR) {
            count_t = -1;
            rec = REC_ON;
            undo_take(rec_ch);
            rp_ticks = 0;
            if (!seq_pat(rec_ch)->n)
                seq_pat(rec_ch)->bars = seq_cfg.rec_bars;
            start_at_zero();
        } else {
            count_t = (int16_t)t1;
        }
        return;
    }
    if (!playing) {                      /* stopped, as the master: the clock goes on, so the others keep the tempo */
        static uint32_t sacc, stick;
        if (seq_cfg.clock_out && seq_cfg.sync == SYNC_INT) {
            sacc += n * song.bpm10 * TPQ;
            adv = sacc / (600u * SEQ_FS);
            sacc -= adv * 600u * SEQ_FS;
            for (i = 0; i < adv; i++)
                if (++stick % 4u == 0u)
                    plat_midi_send(0x0Fu | 0xF8u << 8, TO_USB);
        }
        return;
    }
    if (seq_cfg.sync != SYNC_INT && ext_n && smp - ext_t[(ext_n - 1u) % 25u] > SEQ_FS / 2u && !ext_owed) {
        acc += n * song.bpm10 * TPQ;     /* the clock lost half a second: on at its last tempo, ours, till it comes back */
        adv = acc / (600u * SEQ_FS);
        acc -= adv * 600u * SEQ_FS;
    } else if (seq_cfg.sync != SYNC_INT) {
        adv = ext_owed > 0 ? (uint32_t)ext_owed : 0u;
        ext_owed = 0;
    } else {
        acc += n * song.bpm10 * TPQ;     /* samples x BPM10 x ticks a quarter: 600 x SEQ_FS of them a tick */
        adv = acc / (600u * SEQ_FS);
        acc -= adv * 600u * SEQ_FS;
    }
    for (i = 0; i < adv && playing; i++)
        tick_run(++tick);
    if (song_end) {                      /* the song's last section done, END STOP: the transport stops */
        song_end = 0;
        seq_stop();
    }
}

/* ---------------------------------------------------------------- songs --- */
#define SONG_BASE 0x97000u
#define SONG_SIZE 0x8000u                /* eight sectors a song */
#define SONG_MAGIC 0x4E575347u           /* "NWSG" */
#define SONG_VER 5u                      /* 4: the channels' names (a 3 loads with none); 5: their modulation and their
                                          * sounds' own names (a 3 or 4 loads with the defaults, the presets' names) */
#define SONG_HEAD 16u

uint32_t seq_slot(void) { return slot; }

static uint32_t crc32_add(uint32_t c, const uint8_t *d, uint32_t n)
{
    uint32_t k;
    c = ~c;
    while (n--) {
        c ^= *d++;
        for (k = 0; k < 8u; k++)
            c = c >> 1 ^ (0xEDB88320u & (0u - (c & 1u)));
    }
    return ~c;
}

/* the song as bytes: the fixed part, then each pattern's head and its notes */
static uint8_t sbuf[4096] PLAT_POOL;
static struct { uint32_t off, at, crc, size; int err; } sw;

static void sw_put(const void *src, uint32_t n)
{
    const uint8_t *s = (const uint8_t *)src;
    while (n && !sw.err) {
        uint32_t k = sw.at % 4096u, c = 4096u - k;
        if (sw.at + n > SONG_SIZE) {
            sw.err = -1;
            return;
        }
        c = c < n ? c : n;
        memcpy(sbuf + k, s, c);
        sw.crc = crc32_add(sw.crc, s, c);
        sw.at += c;
        s += c;
        n -= c;
        if (sw.at % 4096u == 0u) {       /* a sector full: written */
            if (plat_data_erase(sw.off + sw.at - 4096u) || plat_data_write(sw.off + sw.at - 4096u, sbuf, 4096))
                sw.err = -2;
            memset(sbuf, 0xFF, sizeof sbuf);
        }
    }
}

static void song_fixed(void (*put)(const void *, uint32_t))
{
    put(&song.bpm10, (uint32_t)((const uint8_t *)&song.pat - (const uint8_t *)&song.bpm10));
}

int seq_save(uint32_t s)
{
    uint32_t ch, i, head[4], size = SONG_HEAD + (uint32_t)((const uint8_t *)&song.pat - (const uint8_t *)&song.bpm10);
    s %= NSONG;
    for (ch = 0; ch < NCH; ch++)         /* too big for its eight sectors: refused before the flash is touched */
        for (i = 0; i < NPAT; i++)
            size += 4u + (uint32_t)song.pat[ch][i].n * sizeof(ev_t);
    if (size > SONG_SIZE)
        return -1;
    sounds_keep();
    memset(&sw, 0, sizeof sw);
    memset(sbuf, 0xFF, sizeof sbuf);
    sw.off = SONG_BASE + s * SONG_SIZE;
    sw.at = SONG_HEAD;                   /* the head last, into its erased bytes: a save cut short reads as none */
    song_fixed(sw_put);
    for (ch = 0; ch < NCH; ch++)
        for (i = 0; i < NPAT; i++) {
            const pat_t *p = &song.pat[ch][i];
            sw_put(p, 4);
            sw_put(p->ev, (uint32_t)p->n * sizeof(ev_t));
        }
    sw.size = sw.at;
    if (!sw.err && sw.at % 4096u) {      /* the last sector */
        uint32_t k = sw.at / 4096u * 4096u;
        if (plat_data_erase(sw.off + k) || plat_data_write(sw.off + k, sbuf, 4096))
            sw.err = -2;
    }
    if (sw.err)
        return sw.err;
    head[0] = SONG_MAGIC;
    head[1] = SONG_VER;
    head[2] = sw.size;
    head[3] = sw.crc;
    if (plat_data_write(sw.off, head, sizeof head))
        return -2;
    slot = (uint8_t)s;
    dirty_f = 0;
    return 0;
}

static const uint8_t *slot_data(uint32_t s, uint32_t *size, uint32_t *ver)
{
    const uint8_t *d = plat_data(SONG_BASE + (s % NSONG) * SONG_SIZE);
    uint32_t head[4];
    if (!d)
        return 0;
    memcpy(head, d, sizeof head);
    if (head[0] != SONG_MAGIC || head[1] < 3u || head[1] > SONG_VER || head[2] < SONG_HEAD || head[2] > SONG_SIZE
        || crc32_add(0, d + SONG_HEAD, head[2] - SONG_HEAD) != head[3])
        return 0;
    *size = head[2];
    if (ver)
        *ver = head[1];
    return d;
}

int seq_slot_used(uint32_t s)
{
    uint32_t n;
    return slot_data(s, &n, 0) != 0;
}

static void song_defaults(void)
{
    uint32_t ch, i;
    memset(&song, 0, sizeof song);
    song.bpm10 = 1200;
    song.mode = MODE_LOOP;
    song.mst[MST_CRT] = 40;
    song.mst[MST_CDP] = 60;
    song.mst[MST_TLEVEL] = 60;
    song.mst[MST_TRATE] = 2;
    song.mst[MST_ROLL] = 1;
    for (i = 0; i < FX_N; i++)
        song.fx[i] = (int16_t)synth_fx(i);
    memcpy(song.name[NCH - 1], "DRUMS", 5);   /* (the kit: the last channel, synth.c DEF_ENG) */
    for (ch = 0; ch < NCH; ch++)
        synth_default_mod(&song.mod[ch]);
    for (ch = 0; ch < NCH; ch++) {
        synth_default_sound(ch, &song.snd[ch]);
        for (i = 0; i < NPAT; i++)
            pat_init(&song.pat[ch][i]);
    }
    for (i = 0; i < NSCENE; i++)
        for (ch = 0; ch < NCH; ch++) {
            song.scene[i].pat[ch] = (uint8_t)i;   /* scene n plays each channel's pattern n */
            song.scene[i].level[ch] = 100;
        }
}

static void song_start(void)             /* the song in RAM to the synth, from its first scene */
{
    uint32_t ch, i;
    seq_stop();
    rec = REC_OFF;
    synth_all_off();                     /* (nothing of the last song left sounding; the voices' budget afresh) */
    synth_budget_reset();
    for (i = 0; i < FX_N; i++)
        synth_fx_set(i, song.fx[i]);
    for (i = 0; i < MST_N; i++)
        if (i != MST_RESO)
            synth_master_set(i, song.mst[i]);
    synth_tempo(song.bpm10);
    for (ch = 0; ch < NCH; ch++) {
        memcpy(synth_fm6_own(ch), song.fm6[ch], SYN_FM6_BYTES);
        synth_set_mod(ch, &song.mod[ch]);
    }
    cur_scene = 0;
    for (ch = 0; ch < NCH; ch++) {
        synth_set_sound(ch, song.scene[0].has_sound ? &song.scene[0].snd[ch] : &song.snd[ch]);
        if (!song.scene[0].has_sound)
            synth_set_name(ch, song.sname[ch], SYN_NAME);
    }
    {
        scene_t *sc = &song.scene[0];
        for (ch = 0; ch < NCH; ch++)
            synth_set_level(ch, sc->level[ch]);
        synth_mute(sc->mute);
        synth_solo(sc->solo);
    }
    undo_ok = redo_ok = 0;
    dirty_f = 0;
}

void seq_new(void)
{
    uint32_t f = lock_();
    song_defaults();
    unlock_(f);
    song_start();
    dirty_f = 1;
}

int seq_load(uint32_t s)
{
    uint32_t size, at, ch, i, f, ver = 0, fixed = (uint32_t)((const uint8_t *)&song.pat - (const uint8_t *)&song.bpm10);
    const uint8_t *d = slot_data(s, &size, &ver);
    if (!d)
        return -1;
    if (ver < 5u)                        /* (a song from before the modulation, or the names: the same, less them) */
        fixed -= (uint32_t)(sizeof song.mod + sizeof song.sname);
    if (ver < 4u)
        fixed -= (uint32_t)sizeof song.name;
    f = lock_();
    song_defaults();
    at = SONG_HEAD;
    memcpy(&song.bpm10, d + at, fixed);
    at += fixed;
    if (ver < 4u)
        memset(song.name, 0, sizeof song.name);
    for (ch = 0; ch < NCH; ch++) {       /* the modulation checked (else the default), the sounds' names (else none) */
        if (ver < 5u || !synth_mod_ok(&song.mod[ch]))
            synth_default_mod(&song.mod[ch]);
        if (ver < 5u || !((song.sname[ch][0] >= 'A' && song.sname[ch][0] <= 'Z')
                          || (song.sname[ch][0] >= '0' && song.sname[ch][0] <= '9')))
            memset(song.sname[ch], 0, SYN_NAME);
    }
    for (ch = 0; ch < NCH; ch++)         /* a name's characters checked (else none) */
        for (i = 0; i < SONG_NAME; i++) {
            char c = song.name[ch][i];
            if (c && !(c == ' ' || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' || c == '.' || c == '/'
                       || c == '+' || c == '#' || c == '&' || c == '!')) {
                memset(song.name[ch], 0, SONG_NAME);
                break;
            }
        }
    for (ch = 0; ch < NCH && at + 4u <= size; ch++)
        for (i = 0; i < NPAT && at + 4u <= size; i++) {
            pat_t *p = &song.pat[ch][i];
            memcpy(p, d + at, 4);
            at += 4u;
            if (p->n > PAT_EV || at + p->n * sizeof(ev_t) > size || p->bars < 1u || p->bars > 8u || p->div >= NDIV)
                p->n = 0;
            memcpy(p->ev, d + at, (uint32_t)p->n * sizeof(ev_t));
            at += (uint32_t)p->n * sizeof(ev_t);
            if (p->bars < 1u || p->bars > 8u)
                pat_init(p);
        }
    for (ch = 0; ch < NCH; ch++)         /* each sound checked (else its default) */
        if (!synth_sound_ok(&song.snd[ch]))
            synth_default_sound(ch, &song.snd[ch]);
    for (i = 0; i < NSCENE; i++)
        for (ch = 0; ch < NCH; ch++)
            if (song.scene[i].has_sound && !synth_sound_ok(&song.scene[i].snd[ch]))
                song.scene[i].snd[ch] = song.snd[ch];
    if (song.nchain > NCHAIN)
        song.nchain = 0;
    unlock_(f);
    song_start();
    slot = (uint8_t)(s % NSONG);
    return 0;
}

void seq_init(void)
{
    uint32_t ch, k;
    for (ch = 0; ch < NCH; ch++)
        for (k = 0; k < 128u; k++)
            open_n[ch][k].idx = -1;
    for (k = 0; k < 128u; k++)
        take_open[k] = -1;
    song_defaults();
    song_start();
}
