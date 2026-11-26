/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 nullwave contributors */
/* nullwave's effects and modulation (in synth.c's unit, after the voices), written for nullwave:
 *   per channel: the LFO (RATE or SYNC, five waves, phase, fade-in, NOTE / FREE, BI / UNI) on pitch, filter, shape and
 *   level; the drive (DST); the INSERT (SOFT, HARD, FOLD, FUZZ, CRUSH, PHASR, FLANG, CHOR, with MIX); the SLICER (GATE
 *   or STUT, 16 patterns, in time); the one-knob FILT; DUCK (the kicks pump the other channels)
 *   master: the stereo chorus (its send), the TEXTURE bed, DUST, the 27 punch-in effects, the DJ FILT, the limiter.
 * Floating point (the FPU). The delay lines are int16 in the pool. */

/* the transport, from the sequencer (synth_transport): the tick and whether it plays */
static volatile uint32_t tp_tick, tp_play;
static float tp_frac;
void synth_transport(uint32_t tick, int playing)
{
    tp_tick = tick;
    tp_play = (uint32_t)!!playing;
    tp_frac = 0.0f;
}                    /* the fraction of a tick, advanced by the render */
static float beat_samples(void) { return (float)FS * 60.0f * 10.0f / (float)tempo10; }

static float fm_fmodf(float x, float y)  /* x mod y, y > 0 */
{
    return y > 0.0f ? x - y * (float)(int32_t)(x / y) : 0.0f;
}
static float frand(int32_t *st) { return (float)((int32_t)(noise32(st) >> 8) - 8388608) / 8388608.0f; }

/* ------------------------------------------------------------------ LFO --- */
static struct { float ph, sh, fade; uint32_t had; int32_t rnd; } lfo[NCH];
typedef struct { int32_t pit16, cut, shp, amp_q15; } chmod_t;
float synth_lfo_now(uint32_t ch, float *depth)   /* the graph view: the LFO's phase 0..1, its fade's depth now */
{
    const sound_t *s = &snd[ch % NCH];
    float fade_s = (1.0f + 4000.0f * fm_powf((float)s->lfo_fade / 127.0f, 3.0f)) / 1000.0f;
    *depth = s->lfo_fade ? fm_minf(1.0f, lfo[ch % NCH].fade / fade_s) : 1.0f;
    return lfo[ch % NCH].ph;
}

static void lfo_block(uint32_t ch, const sound_t *s, uint32_t voices, chmod_t *cm)
{
    static const float SYNC_Q[11] = {0, 16, 8, 4, 2, 1, 0.5f, 1.0f / 3.0f, 0.25f, 1.0f / 6.0f, 0.125f};
    float hz, x, depth = 1.0f, fade_s;
    memset(cm, 0, sizeof *cm);
    cm->amp_q15 = 32767;
    if (!s->lfo_pit && !s->lfo_flt && !s->lfo_shp && !s->lfo_amp)
        return;
    if (s->lfo_sync && s->lfo_sync <= 10u)
        hz = (float)tempo10 / 600.0f / SYNC_Q[s->lfo_sync];
    else
        hz = 0.05f * fm_exp2f((float)s->lfo_rate / 14.0f);
    if (voices && !lfo[ch].had) {        /* a new phrase: NOTE restarts it at its phase, the fade begins */
        if (s->lfo_trig == 0u)
            lfo[ch].ph = (float)s->lfo_phase / 128.0f;
        lfo[ch].fade = 0.0f;
    }
    lfo[ch].had = voices;
    lfo[ch].ph += hz * (float)CTL / (float)FS;
    if (lfo[ch].ph >= 1.0f) {
        lfo[ch].ph -= (float)(int32_t)lfo[ch].ph;
        if (!lfo[ch].rnd)
            lfo[ch].rnd = (int32_t)(ch * 977u + 12345u);
        lfo[ch].sh = frand(&lfo[ch].rnd);
    }
    switch (s->lfo_wave) {
    case 1: x = lfo[ch].ph < 0.5f ? lfo[ch].ph * 4.0f - 1.0f : 3.0f - lfo[ch].ph * 4.0f; break;
    case 2: x = lfo[ch].ph * 2.0f - 1.0f; break;
    case 3: x = lfo[ch].ph < 0.5f ? 1.0f : -1.0f; break;
    case 4: x = lfo[ch].sh; break;
    default: x = (float)sine_i((uint32_t)(lfo[ch].ph * 4294967295.0f)) / 32768.0f; break;
    }
    if (s->lfo_pol)
        x = (x + 1.0f) * 0.5f;
    fade_s = (1.0f + 4000.0f * fm_powf((float)s->lfo_fade / 127.0f, 3.0f)) / 1000.0f;
    if (s->lfo_fade) {
        lfo[ch].fade += (float)CTL / (float)FS;
        depth = fm_minf(1.0f, lfo[ch].fade / fade_s);
    }
    x *= depth;
    cm->pit16 = (int32_t)(x * (float)s->lfo_pit * 192.0f / 63.0f);   /* 63: an octave */
    cm->cut = (int32_t)(x * (float)s->lfo_flt * (float)(64 << 8) / 63.0f);
    cm->shp = (int32_t)(x * (float)s->lfo_shp * (float)(64 << 8) / 63.0f);
    if (s->lfo_amp) {
        float a = (float)(s->lfo_amp < 0 ? -s->lfo_amp : s->lfo_amp) / 63.0f;
        cm->amp_q15 = (int32_t)(32767.0f * (1.0f - a * (1.0f - x) * 0.5f));
    }
}

/* ------------------------------------------------ drive, INSERT, SLICER --- */
#define INS_N 1024u
static int16_t ins_buf[NCH][INS_N] PLAT_POOL;
static struct { uint32_t w; float lfo, ap[4], fb, lp, hold; uint32_t hc; float sl_env, flt_l, flt_b; } chs[NCH];

static const uint16_t SLC_PAT[16] = {0xFFFF, 0xAAAA, 0xEEEE, 0x5555, 0xF0F0, 0xCCCC, 0xB6DB, 0x9249, 0xFF00, 0xE4E4,
                                     0xA5A5, 0xDB6D, 0x8888, 0xF6F6, 0xBBBB, 0x7E7E};
static const uint8_t SLC_TICKS[6] = {48, 24, 12, 32, 16, 8};    /* 1/8 1/16 1/32 8T 16T 32T (ticks of 1/96 beat) */

static inline float fold(float x)
{
    while (x > 1.0f || x < -1.0f)
        x = x > 1.0f ? 2.0f - x : -2.0f - x;
    return x;
}

static void channel_post(uint32_t ch, const sound_t *s, int32_t *buf)
{
    uint32_t i;
    float dg = 1.0f + (float)s->dst / 12.0f;
    if (s->dst)                          /* drive (DST): more gain into a soft clip */
        for (i = 0; i < CTL; i++)
            buf[i] = (int32_t)(fm_tanhf((float)buf[i] / 40000.0f * dg) * 40000.0f / fm_minf(dg, 2.5f));
    if (s->ins && s->ins_mix) {          /* INSERT */
        float a = (float)s->ins_a / 127.0f, b = (float)s->ins_b / 127.0f, c = (float)s->ins_c / 127.0f;
        float mix = (float)s->ins_mix / 127.0f, rate = 0.05f + a * a * 8.0f;
        for (i = 0; i < CTL; i++) {
            float x = (float)buf[i] / 40000.0f, y = x;
            switch (s->ins) {
            case 1: y = fm_tanhf(x * (1.0f + a * 10.0f)); break;                              /* SOFT */
            case 2: y = fm_clampf(x * (1.0f + a * 10.0f), -0.8f, 0.8f); break;                /* HARD */
            case 3: y = fold(x * (1.0f + a * 6.0f)); break;                                   /* FOLD */
            case 4: y = fm_tanhf(x * (1.0f + a * 20.0f) + 0.3f * a) - 0.3f * a; break;        /* FUZZ */
            case 5: {                                                                          /* CRUSH */
                float q = fm_exp2f(1.0f + (1.0f - a) * 14.0f);
                uint32_t hold = 1u + (uint32_t)(b * b * 63.0f);
                if (chs[ch].hc++ % hold == 0u)
                    chs[ch].hold = (float)(int32_t)(x * q) / q;
                y = chs[ch].hold;
                break;
            }
            case 6: {                                                                          /* PHASR */
                float m, coef;
                uint32_t k;
                chs[ch].lfo += rate / (float)FS;
                if (chs[ch].lfo >= 1.0f)
                    chs[ch].lfo -= 1.0f;
                m = 0.5f + 0.5f * fm_sin_turns(chs[ch].lfo) * b;
                coef = 0.2f + 0.7f * m;
                y = x + chs[ch].fb * c * 0.7f;
                for (k = 0; k < 4u; k++) {
                    float o = -coef * y + chs[ch].ap[k];
                    chs[ch].ap[k] = y + coef * o;
                    y = o;
                }
                chs[ch].fb = y;
                y = (x + y) * 0.5f;
                break;
            }
            case 7: case 8: {                                                                  /* FLANG, CHOR */
                float d, rp, fr;
                uint32_t r0;
                chs[ch].lfo += rate / (float)FS;
                if (chs[ch].lfo >= 1.0f)
                    chs[ch].lfo -= 1.0f;
                d = s->ins == 7 ? 22.0f + 180.0f * (0.5f + 0.5f * fm_sin_turns(chs[ch].lfo)) * b
                                : 350.0f + 600.0f * (0.5f + 0.5f * fm_sin_turns(chs[ch].lfo)) * b;
                rp = (float)chs[ch].w - d;
                if (rp < 0.0f)
                    rp += (float)INS_N;
                r0 = (uint32_t)rp;
                fr = rp - (float)r0;
                y = ((float)ins_buf[ch][r0 % INS_N] * (1.0f - fr) + (float)ins_buf[ch][(r0 + 1u) % INS_N] * fr) / 16384.0f;
                ins_buf[ch][chs[ch].w] = (int16_t)fm_clampf((x + y * (s->ins == 7 ? c * 0.85f : c * 0.3f)) * 16384.0f,
                                                            -32767.0f, 32767.0f);
                chs[ch].w = (chs[ch].w + 1u) % INS_N;
                y = (x + y) * 0.7f;
                break;
            }
            default: break;
            }
            if (s->ins <= 4u) {          /* the drives: B the tone, C the level */
                chs[ch].lp += (y - chs[ch].lp) * (0.05f + b * 0.95f);
                y = chs[ch].lp * (0.3f + c * 1.0f);
            } else if (s->ins == 5u) {   /* CRUSH: C a low-pass */
                chs[ch].lp += (y - chs[ch].lp) * (0.05f + c * 0.95f);
                y = chs[ch].lp;
            }
            buf[i] = (int32_t)((x * (1.0f - mix) + y * mix) * 40000.0f);
        }
    }
    if (s->slc && s->slc_depth) {        /* SLICER: a gate or a stutter in time */
        uint32_t st = SLC_TICKS[s->slc_rate % 6u], pat = SLC_PAT[(s->slc_pat ? s->slc_pat - 1u : 0u) % 16u];
        float depth = (float)s->slc_depth / 127.0f, tps = beat_samples() / 96.0f;
        for (i = 0; i < CTL; i++) {
            float pos = (float)tp_tick + tp_frac + (float)i / tps, inst;
            uint32_t step = (uint32_t)(pos / (float)st), on = pat >> (step % 16u) & 1u;
            float phase = (pos - (float)step * (float)st) / (float)st, g;
            if (s->slc == 1u)
                g = on ? 1.0f : 1.0f - depth;
            else                         /* STUT: each step struck again, dying fast */
                g = on ? fm_expf(-phase * 6.0f) : 1.0f - depth;
            inst = chs[ch].sl_env += (g - chs[ch].sl_env) * 0.08f;
            buf[i] = (int32_t)((float)buf[i] * (tp_play ? inst : 1.0f));
        }
    }
    if (s->tfilt) {                      /* FILT: one knob, left a low-pass closing, right a high-pass opening */
        tsvf_t c;
        int32_t t = s->tfilt, cut = t < 0 ? (127 + t * 2) : t * 2, k = 8192 - flt_reso * 700;
        int32_t *ic1 = &flt_ic2[ch][0], *ic2 = &flt_ic2[ch][1];
        tsvf_coef_k(&c, clamp(cut, 0, 127) << 8, k);
        for (i = 0; i < CTL; i++) {
            int32_t in = buf[i] >> 1, v3 = in - *ic2;
            int32_t v1 = (c.a1 * *ic1 + c.a2 * v3) >> 13;
            int32_t v2 = *ic2 + ((c.a2 * *ic1 + c.a3 * v3) >> 13);
            *ic1 = clamp(2 * v1 - *ic1, -150000, 150000);
            *ic2 = clamp(2 * v2 - *ic2, -150000, 150000);
            buf[i] = (t < 0 ? v2 : in - ((k * v1) >> 12) - v2) * 2;
        }
    }
}

/* ---------------------------------------------------------- the master --- */
#define CHO_N 2048u
static int16_t cho_buf[CHO_N] PLAT_POOL;
static uint32_t cho_hold;                /* samples the chorus still runs (a send seen: its buffer's length) */
static uint32_t cho_w;
static float cho_ph;
static int16_t mst[MST_N];               /* DUST, DUCK, FILT, ROLL, TEXTURE..., the chorus */
static float duck_env;
static int32_t tex_rnd = 7777;
static float tex_s[6], dust_hold[2], dust_lp[2], dj_lp[2][2], lim_g = 1.0f;
static uint32_t dust_hc;

void synth_master_set(uint32_t id, int32_t v) { if (id < MST_N) mst[id] = (int16_t)v; }
int32_t synth_master(uint32_t id) { return id < MST_N ? mst[id] : 0; }
static void synth_duck_hit(void) { duck_env = 1.0f; }   /* a kick: DUCK pumps the other channels */
static float duck_gain(uint32_t ch)
{
    return snd[ch].engine == E_DRUM ? 1.0f : 1.0f - duck_env * (float)mst[MST_DUCK] / 127.0f * 0.85f;
}

/* the chorus: two taps, left and right, swaying in opposite phase */
static inline void chorus_tick(float in, float *l, float *r)
{
    float rate = 0.1f + (float)mst[MST_CRT] / 127.0f * 3.0f, dep = (float)mst[MST_CDP] / 127.0f;
    float m = fm_sin_turns(cho_ph), d1 = 440.0f + 300.0f * dep * m, d2 = 440.0f - 300.0f * dep * m;
    uint32_t r1 = (cho_w + CHO_N - (uint32_t)d1) % CHO_N, r2 = (cho_w + CHO_N - (uint32_t)d2) % CHO_N;
    cho_ph += rate / (float)FS;
    if (cho_ph >= 1.0f)
        cho_ph -= 1.0f;
    cho_buf[cho_w] = (int16_t)fm_clampf(in * 16384.0f, -32767.0f, 32767.0f);
    cho_w = (cho_w + 1u) % CHO_N;
    *l += (float)cho_buf[r1] / 16384.0f;
    *r += (float)cho_buf[r2] / 16384.0f;
}

/* TEXTURE: a bed under the mix, made here (no samples) */
static float texture_tick(void)
{
    float x = 0.0f, n = frand(&tex_rnd), lvl = (float)mst[MST_TLEVEL] / 127.0f, move, bars;
    uint32_t src = (uint32_t)mst[MST_TSRC];
    if (!src || !lvl || (mst[MST_TWHEN] == 0 && !tp_play))
        return 0.0f;
    bars = (float)(1u << clamp(mst[MST_TRATE], 0, 4));      /* 1, 2, 4, 8, 16 bars */
    move = 1.0f - (float)mst[MST_TMOVE] / 127.0f * 0.5f * (1.0f + fm_sin_turns(((float)tp_tick / 384.0f) / bars));
    switch (src) {
    case 1:                              /* VINYL: hiss, rumble, crackle */
        tex_s[0] += (n - tex_s[0]) * 0.3f;
        tex_s[1] += (n - tex_s[1]) * 0.004f;
        x = (n - tex_s[0]) * 0.15f + tex_s[1] * 2.0f;
        if ((noise32(&tex_rnd) & 0xFFFu) < 3u)
            tex_s[2] = frand(&tex_rnd) * 0.9f;
        x += tex_s[2];
        tex_s[2] *= 0.7f;
        break;
    case 2:                              /* TAPE: a dark hiss, drifting */
        tex_s[0] += (n - tex_s[0]) * 0.08f;
        tex_s[3] += (frand(&tex_rnd) * 0.5f - tex_s[3]) * 0.0002f;
        x = tex_s[0] * (0.5f + tex_s[3]);
        break;
    case 3:                              /* RAIN: a bed and droplets */
        tex_s[0] += (n - tex_s[0]) * 0.5f;
        x = (n - tex_s[0]) * 0.2f;
        if ((noise32(&tex_rnd) & 0x1FFFu) < 2u) {
            tex_s[4] = 0.6f;
            tex_s[5] = 0.02f + (float)(noise32(&tex_rnd) & 255u) / 255.0f * 0.06f;
        }
        tex_s[1] += tex_s[5];
        x += fm_sin_turns(tex_s[1]) * tex_s[4];
        tex_s[4] *= 0.9985f;
        break;
    case 4: {                            /* WIND: a resonant band of noise, its centre moving in gusts */
        float f;
        tex_s[3] += (frand(&tex_rnd) - tex_s[3]) * 0.00005f;
        f = 0.02f + 0.03f * (tex_s[3] + 1.0f);
        tex_s[1] += f * (n - tex_s[1] - 0.2f * tex_s[2]);
        tex_s[2] += f * tex_s[1];
        x = tex_s[2] * 2.5f;
        break;
    }
    case 5:                              /* RADIO: bursts of static */
        if ((noise32(&tex_rnd) & 0x3FFFu) < 4u)
            tex_s[4] = (float)(noise32(&tex_rnd) & 0x7FFFu);
        x = tex_s[4] > 0.0f ? n * 0.6f : n * 0.04f;
        tex_s[4] -= 1.0f;
        break;
    default:                             /* ROOM: room tone */
        tex_s[0] += (n - tex_s[0]) * 0.02f;
        x = tex_s[0] * 1.5f;
        break;
    }
    {   /* TONE: left a low-pass, right a high-pass */
        int32_t t = mst[MST_TTONE];
        if (t < 0) {
            static float lp;
            lp += (x - lp) * (1.0f + (float)t / 66.0f);
            x = lp;
        } else if (t > 0) {
            static float lp2;
            lp2 += (x - lp2) * ((float)t / 70.0f);
            x -= lp2;
        }
    }
    if (mst[MST_TDUCK])
        x *= 1.0f - duck_env * 0.8f;
    return x * lvl * move * 0.25f;
}

/* ------------------------------------------------- the punch-in effects --- */
#define PR_N 16384u                       /* the mix, mono at half rate: ~0.74 s */
static int16_t pr_buf[PR_N] PLAT_POOL;
static uint32_t pr_w, pr_half;
static volatile uint32_t punch_mask;      /* bit k: punch-in k held */
static uint32_t punch_on, punch_t0[27];
static float punch_mix[27], pr_rp, pr_spd, pr_lp[3], pr_flp, pr_fhp, pr_fade_g = 1.0f;
static uint32_t pr_loop0, pr_loopn, pr_ac;
static uint8_t punch_fade = 2;
static volatile uint8_t throw_rev, throw_echo;

void synth_punch(uint32_t k, int on)
{
    if (k < 27u)
        punch_mask = on ? punch_mask | 1u << k : punch_mask & ~(1u << k);
}
uint32_t synth_punches(void) { return punch_mask; }
void synth_punch_fade(uint32_t f) { punch_fade = (uint8_t)(f > 15u ? 15u : f); }
void synth_throw(int rev, int echo) { throw_rev = (uint8_t)!!rev; throw_echo = (uint8_t)!!echo; }

static float pr_read(float pos)          /* the ring at pos (half-rate samples), interpolated */
{
    uint32_t i;
    float f;
    while (pos < 0.0f)
        pos += (float)PR_N;
    i = (uint32_t)pos;
    f = pos - (float)i;
    return ((float)pr_buf[i % PR_N] * (1.0f - f) + (float)pr_buf[(i + 1u) % PR_N] * f) / 16384.0f;
}

static int punch_busy(void) { return punch_mask || punch_on; }
static void punch_idle(uint32_t n)       /* n silent samples: the ring written silent, the clock on */
{
    uint32_t i;
    for (i = 0; i < n; i++) {
        if (!(pr_half ^= 1u)) {
            pr_buf[pr_w] = 0;
            pr_w = pr_w + 1u >= PR_N ? 0u : pr_w + 1u;
        }
    }
    pr_ac += n;
}

/* one sample of the master, l and r in -1..1 (about) */
static void punch_tick(float *l, float *r)
{
    float m = (*l + *r) * 0.5f, beat = beat_samples(), out_l = *l, out_r = *r;
    uint32_t k, active = punch_mask;
    float step;
    if (!(pr_half ^= 1u)) {              /* the ring: every other sample */
        pr_buf[pr_w] = (int16_t)fm_clampf(m * 16384.0f, -32767.0f, 32767.0f);
        pr_w = pr_w + 1u >= PR_N ? 0u : pr_w + 1u;
    }
    if (!active && !punch_on) {          /* none held, none fading: nothing more to do this sample */
        pr_ac++;
        return;
    }
    step = 1.0f / (1.0f + (float)punch_fade * 1100.0f);
    for (k = 0; k < 27u; k++) {          /* each effect fades in and out */
        float target = (float)(active >> k & 1u);
        if (target && !(punch_on >> k & 1u)) {
            punch_on |= 1u << k;
            punch_t0[k] = pr_ac;
            if (k <= 7u || k == 19u || k == 24u) {   /* the ones that play the past: from here */
                static const float LEN_Q[] = {1.0f, 0.5f, 0.25f, 0.125f, 1.0f / 6.0f};
                float ql = k <= 3u ? LEN_Q[k] : k == 4u ? LEN_Q[4] : k == 24u ? 1.0f / 3.0f : 0.5f;
                pr_loopn = (uint32_t)fm_minf(beat * ql * 0.5f, (float)PR_N - 64.0f);
                pr_loop0 = (pr_w + PR_N - pr_loopn) % PR_N;
                pr_rp = (float)pr_w;
                pr_spd = 1.0f;
            }
        } else if (!target && punch_mix[k] <= 0.0f) {
            punch_on &= ~(1u << k);
        }
        punch_mix[k] += target > punch_mix[k] ? step : -step;
        punch_mix[k] = fm_clampf(punch_mix[k], 0.0f, 1.0f);
    }
    pr_ac++;
    if (!punch_on)
        return;
    for (k = 0; k < 27u; k++) {
        float w = punch_mix[k], y, age = (float)(pr_ac - punch_t0[k]);
        if (!(punch_on >> k & 1u) || w <= 0.0f)
            continue;
        switch (k) {
        case 0: case 1: case 2: case 3: case 4: case 24:   /* the loops, the stutter, 8T */
            y = pr_read((float)pr_loop0 + fm_fmodf(age * 0.5f, (float)(pr_loopn ? pr_loopn : 1u)));
            if (k == 4u)
                y *= fm_expf(-fm_fmodf(age * 0.5f, (float)pr_loopn) / (float)(pr_loopn + 1u) * 3.0f);
            out_l = out_l * (1.0f - w) + y * w;
            out_r = out_r * (1.0f - w) + y * w;
            break;
        case 5:                          /* reverse */
            y = pr_read((float)pr_w - age);
            out_l = out_l * (1.0f - w) + y * w;
            out_r = out_r * (1.0f - w) + y * w;
            break;
        case 6:                          /* tape stop: slower and slower over a beat */
        case 7: {                        /* half speed */
            float sp = k == 6u ? fm_maxf(0.0f, 1.0f - age / beat) : 0.5f;
            pr_rp += sp * 0.5f;
            y = pr_read(pr_rp - (float)PR_N * 0.0f) * (k == 6u ? fm_minf(1.0f, sp * 4.0f) : 1.0f);
            out_l = out_l * (1.0f - w) + y * w;
            out_r = out_r * (1.0f - w) + y * w;
            break;
        }
        case 19: {                       /* REWIND: slows, stops, spins backwards faster, then silence */
            float sp = 0.5f - age / (float)FS * 4.0f;
            if (sp < -3.0f)
                y = 0.0f;
            else {
                pr_rp += sp * 0.5f;
                y = pr_read(pr_rp);
            }
            out_l = out_l * (1.0f - w) + y * w;
            out_r = out_r * (1.0f - w) + y * w;
            break;
        }
        case 8: case 9: {                /* the sweeps: a low-pass closing, a high-pass opening, over a bar */
            float p = fm_minf(1.0f, age / (beat * 4.0f)), f = k == 8u ? 0.6f * (1.0f - p) + 0.01f : 0.01f + 0.5f * p;
            pr_flp += (out_l - pr_flp) * f;
            pr_fhp += (out_r - pr_fhp) * f;
            y = k == 8u ? pr_flp : out_l - pr_flp;
            out_l = out_l * (1.0f - w) + y * w;
            out_r = out_r * (1.0f - w) + (k == 8u ? pr_fhp : out_r - pr_fhp) * w;
            break;
        }
        case 10:                         /* phone: a narrow band, driven */
            pr_lp[0] += (m - pr_lp[0]) * 0.35f;
            pr_lp[1] += (pr_lp[0] - pr_lp[1]) * 0.05f;
            y = fm_tanhf((pr_lp[0] - pr_lp[1]) * 3.0f) * 0.6f;
            out_l = out_l * (1.0f - w) + y * w;
            out_r = out_r * (1.0f - w) + y * w;
            break;
        case 11:                         /* bit crush: six bits */
            out_l = out_l * (1.0f - w) + (float)(int32_t)(out_l * 32.0f) / 32.0f * w;
            out_r = out_r * (1.0f - w) + (float)(int32_t)(out_r * 32.0f) / 32.0f * w;
            break;
        case 12:                         /* alias: an eighth of the rate */
            if ((pr_ac & 7u) == 0u) {
                pr_lp[0] = out_l;
                pr_lp[1] = out_r;
            }
            out_l = out_l * (1.0f - w) + pr_lp[0] * w;
            out_r = out_r * (1.0f - w) + pr_lp[1] * w;
            break;
        case 13: {                       /* gate 1/16 */
            float pos = (float)tp_tick + tp_frac;
            float g = fm_fmodf(pos, 24.0f) < 12.0f ? 1.0f : 0.0f;
            out_l *= 1.0f - w + w * g;
            out_r *= 1.0f - w + w * g;
            break;
        }
        case 14:                         /* echo: the delay's dotted eighth, fed the whole mix (below) */
            break;
        case 15: {                       /* tape wobble: the mix read a little late, the lateness swaying */
            float d = 200.0f + 120.0f * fm_sin_turns(age / (float)FS * 1.3f);
            y = pr_read((float)pr_w - d);
            out_l = out_l * (1.0f - w) + y * w;
            out_r = out_r * (1.0f - w) + y * w;
            break;
        }
        case 16: case 17: case 18: {     /* KILL LOW, MID, HIGH */
            float lo, hi;
            pr_lp[0] += (m - pr_lp[0]) * 0.035f;       /* ~250 Hz */
            pr_lp[2] += (m - pr_lp[2]) * 0.30f;        /* ~2.5 kHz */
            lo = pr_lp[0];
            hi = m - pr_lp[2];
            y = k == 16u ? -lo : k == 17u ? -(m - lo - hi) : -hi;
            out_l += y * w;
            out_r += y * w;
            break;
        }
        case 20: {                       /* OCT UP: two read heads at twice the speed, crossfaded */
            float span = 900.0f, ph = fm_fmodf(age * 0.5f, span) / span, a, b;
            a = pr_read((float)pr_w - span + ph * span * 2.0f - span);
            b = pr_read((float)pr_w - span + fm_fmodf(ph + 0.5f, 1.0f) * span * 2.0f - span);
            y = a * fm_sin_pi(ph) + b * fm_sin_pi(fm_fmodf(ph + 0.5f, 1.0f));
            out_l = out_l * (1.0f - w) + y * w;
            out_r = out_r * (1.0f - w) + y * w;
            break;
        }
        case 21: {                       /* FLANGER: a jet over a bar, left and right apart */
            float s = fm_sin_turns(age / (beat * 4.0f));
            out_l = out_l * (1.0f - w * 0.5f) + pr_read((float)pr_w - 6.0f - 40.0f * (0.5f + 0.5f * s)) * w * 0.5f;
            out_r = out_r * (1.0f - w * 0.5f) + pr_read((float)pr_w - 6.0f - 40.0f * (0.5f - 0.5f * s)) * w * 0.5f;
            break;
        }
        case 22: {                       /* AUTO-PAN: the mix in mono, left and right once a beat */
            float p = 0.5f + 0.5f * fm_sin_turns(age / beat);
            out_l = out_l * (1.0f - w) + m * (1.0f - p) * 2.0f * w;
            out_r = out_r * (1.0f - w) + m * p * 2.0f * w;
            break;
        }
        case 23:                         /* FREEZE: into the reverb, held (below) */
            out_l *= 1.0f - w;
            out_r *= 1.0f - w;
            break;
        case 25:                         /* MONO */
            out_l = out_l * (1.0f - w) + m * w;
            out_r = out_r * (1.0f - w) + m * w;
            break;
        case 26: {                       /* FADE OUT: to nothing over two beats */
            float g = fm_maxf(0.0f, 1.0f - age / (beat * 2.0f));
            out_l *= 1.0f - w + w * g;
            out_r *= 1.0f - w + w * g;
            break;
        }
        default: break;
        }
    }
    *l = out_l;
    *r = out_r;
}

/* DUST: an old sampler and a record: drive, a lower rate, fewer bits, a closing low-pass, a little noise */
static inline void dust_tick(float *l, float *r)
{
    float d = (float)mst[MST_DUST] / 127.0f, *x[2] = {l, r};
    uint32_t c, hold;
    if (d <= 0.0f)
        return;
    hold = 1u + (uint32_t)(d * 3.0f);
    for (c = 0; c < 2u; c++) {
        float v = fm_tanhf(*x[c] * (1.0f + d * 2.0f)), q = fm_exp2f(15.0f - d * 7.0f);
        if (dust_hc % hold == 0u)
            dust_hold[c] = (float)(int32_t)(v * q) / q;
        dust_lp[c] += (dust_hold[c] - dust_lp[c]) * (1.0f - d * 0.6f);
        *x[c] = dust_lp[c] + (tp_play ? frand(&tex_rnd) * d * 0.004f : 0.0f);
    }
    dust_hc++;
}

/* the DJ filter: left a low-pass closing, right a high-pass opening, gliding */
static float dj_cur;
static inline void dj_tick(float *l, float *r)
{
    float t = (float)mst[MST_FILT] / 64.0f, f;
    uint32_t c;
    float *x[2] = {l, r};
    dj_cur += (t - dj_cur) * 0.0005f;
    if (dj_cur > -0.02f && dj_cur < 0.02f)
        return;
    f = dj_cur < 0.0f ? fm_exp2f(dj_cur * 7.0f) * 0.9f : fm_exp2f((1.0f - dj_cur) * -7.0f) * 0.9f + 0.002f;
    for (c = 0; c < 2u; c++) {
        dj_lp[c][0] += (*x[c] - dj_lp[c][0]) * f;
        dj_lp[c][1] += (dj_lp[c][0] - dj_lp[c][1]) * f;
        *x[c] = dj_cur < 0.0f ? dj_lp[c][1] : *x[c] - dj_lp[c][1];
    }
}

/* the limiter: a gain that falls at once to keep the peak below the knee, and comes back in ~100 ms */
static inline float limit_tick(float l, float r)
{
    float p = fm_maxf(l < 0 ? -l : l, r < 0 ? -r : r) * lim_g, lim = 0.95f;
    if (p > lim)
        lim_g *= lim / p;
    else
        lim_g += (1.0f - lim_g) * 0.0002f;
    return lim_g;
}

/* ------------------------------------------------------- the graph view --- */
uint32_t synth_slicer(uint32_t ch, int32_t *step)   /* SLICER's pattern (bit i: step i on), the step playing or -1 */
{
    const sound_t *s = &snd[ch % NCH];
    uint32_t st = SLC_TICKS[s->slc_rate % 6u];
    *step = tp_play ? (int32_t)(tp_tick / st % 16u) : -1;
    return SLC_PAT[(s->slc_pat ? s->slc_pat - 1u : 0u) % 16u];
}
int32_t synth_svf_g(uint32_t cut) { return SVF_G[cut > 127u ? 127u : cut]; }   /* tan(pi fc / FS), Q12 */
float synth_dly_beats(float *fb)         /* the echo's time in quarter notes, its feedback 0..1 */
{
    *fb = (float)(fxv[FX_DFB] * 235) / 32768.0f;
    return (float)DTIME_X8[fxv[FX_DTIME] & 7] / 8.0f;
}
