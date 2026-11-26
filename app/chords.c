/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 nullwave contributors */
/* Naming what sounds (in ui.c's unit; also built alone by tests/chord_test.c): the notes held, in any octave and
 * inversion, as a chord name. Triads (maj, m, dim, aug, sus2, sus4, 5), sixths (6, m6), sevenths (7, maj7, m7, m7b5,
 * dim7, mMaj7, 7sus4, aug7), extensions (add9, madd9, 9, maj9, m9, 11, m11, 13, 7b9, 7#9, 7#11). A missing 5th is
 * allowed (a chord of three or more notes). The lowest note not the root: a slash chord (C/E). When two names fit, the
 * simplest wins: the root in the bass first, then the plainer chord, then a root in the song's key (C6 rather than
 * Am7/C with C in the bass, Cm6 rather than Am7b5/C: the same notes). One note: its name and octave; two: their names and the interval (C5 for a fifth).
 * Spelled in the song's key: flats in a flat key (Bb in F), else the usual names (C# Eb F# Ab Bb). */

static const char *const CH_SHARP[12] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
static const char *const CH_FLAT[12] = {"C", "Db", "D", "Eb", "E", "F", "Gb", "G", "Ab", "A", "Bb", "B"};
static const char *const CH_PLAIN[12] = {"C", "C#", "D", "Eb", "E", "F", "F#", "G", "Ab", "A", "Bb", "B"};

/* the scales' (ui.c SCALE_NAME's order) relative major: how far up from the root its major scale's tonic is; -1 none */
static const int8_t CH_REL[16] = {-1, 0, 3, 10, 8, 7, 5, 1, 3, 3, 0, 3, 3, -1, -1, -1};

/* the names a key spells with: 0 the plain, 1 sharps, 2 flats */
static int chord_key_spell(int32_t root, uint32_t scale)
{
    int32_t rel = CH_REL[scale & 15u], tonic;
    if (rel < 0 || scale == 0u)
        return 0;
    tonic = (root + rel) % 12;
    if (tonic == 5 || tonic == 10 || tonic == 3 || tonic == 8 || tonic == 1)   /* F Bb Eb Ab Db */
        return 2;
    return tonic == 0 ? 0 : 1;           /* (C: the plain names; G D A E B F#: sharps) */
}
static const char *chord_pc(int32_t pc, int spell)
{
    pc = ((pc % 12) + 12) % 12;
    return spell == 2 ? CH_FLAT[pc] : spell == 1 ? CH_SHARP[pc] : CH_PLAIN[pc];
}

typedef struct { uint16_t set; uint8_t rank; const char *nm; } chord_t;
static const chord_t CHORDS[] = {        /* intervals from the root as bits (bit i: i semitones), how plain (0 most) */
    {0x091, 0, ""}, {0x089, 0, "m"}, {0x049, 2, "dim"}, {0x111, 2, "aug"}, {0x085, 3, "sus2"}, {0x0A1, 3, "sus4"},
    {0x291, 4, "6"}, {0x289, 5, "m6"},
    {0x491, 3, "7"}, {0x891, 3, "maj7"}, {0x489, 3, "m7"}, {0x449, 4, "m7b5"}, {0x249, 4, "dim7"}, {0x889, 5, "mMaj7"},
    {0x4A1, 5, "7sus4"}, {0x511, 5, "aug7"},
    {0x095, 5, "add9"}, {0x08D, 5, "madd9"}, {0x495, 5, "9"}, {0x895, 5, "maj9"}, {0x48D, 5, "m9"},
    {0x4B5, 6, "11"}, {0x4A5, 6, "11"}, {0x4AD, 6, "m11"}, {0x695, 6, "13"}, {0x691, 6, "13"},
    {0x493, 6, "7b9"}, {0x499, 6, "7#9"}, {0x4D1, 6, "7#11"}};
static const char *const CH_IVL[12] = {"P8", "m2", "M2", "m3", "M3", "P4", "TT", "P5", "m6", "M6", "m7", "M7"};

static void ch_cat(char *d, const char *s, uint32_t room)
{
    uint32_t n = 0;
    while (d[n])
        n++;
    while (*s && n + 1u < room)
        d[n++] = *s++;
    d[n] = 0;
}

/* notes[n] (MIDI, any order), the song's key (root 0..11, scale ui.c's index) -> name (the big line: "Cmaj7/E",
 * "C5", "C E", "C4") and detail (the interval of two notes: "M3"; else ""). 0: nothing sounds */
static int chord_spell(const int32_t *notes, uint32_t n, int32_t key_root, uint32_t key_scale, char *name,
                       char *detail, uint32_t room)
{
    uint32_t pcs = 0, i, k, r, cnt = 0;
    int32_t low = -1, high = -1, best_s = 1000, best_r = -1, best_k = -1;
    int spell = chord_key_spell(key_root, key_scale);
    uint32_t scale_pcs = 0;
    name[0] = detail[0] = 0;
    for (i = 0; i < n; i++) {
        pcs |= 1u << (((notes[i] % 12) + 12) % 12);
        low = low < 0 || notes[i] < low ? notes[i] : low;
        high = notes[i] > high ? notes[i] : high;
    }
    if (!pcs)
        return 0;
    for (r = 0; r < 12u; r++)
        cnt += pcs >> r & 1u;
    if (cnt == 1u) {                     /* one note (an octave or more of it): its name and octave */
        char o[4];
        int32_t oc = low / 12 - 1;
        ch_cat(name, chord_pc(low, spell), room);
        o[0] = oc < 0 ? '-' : (char)('0' + oc);
        o[1] = oc < 0 ? (char)('0' - oc) : 0;
        o[2] = 0;
        ch_cat(name, o, room);
        return 1;
    }
    if (cnt == 2u) {                     /* two: a fifth is a power chord, else the names and the interval */
        int32_t other = -1, iv;
        for (i = 0; i < n; i++)
            if ((notes[i] - low) % 12 != 0 && (other < 0 || notes[i] < other))
                other = notes[i];
        iv = ((other - low) % 12 + 12) % 12;
        if (iv == 7) {
            ch_cat(name, chord_pc(low, spell), room);
            ch_cat(name, "5", room);
            return 1;
        }
        ch_cat(name, chord_pc(low, spell), room);
        ch_cat(name, " ", room);
        ch_cat(name, chord_pc(other, spell), room);
        ch_cat(detail, CH_IVL[iv], room);
        return 1;
    }
    if (CH_REL[key_scale & 15u] >= 0 && key_scale) {   /* the key's notes, for the tie-break */
        static const uint16_t MASK[16] = {0xFFF, 0xAB5, 0x5AD, 0x6AD, 0x5AB, 0xAD5, 0x6B5, 0x56B,
                                          0x9AD, 0xAAD, 0x295, 0x4A9, 0x4E9, 0x555, 0x18D, 0xB6D};
        uint32_t m = MASK[key_scale & 15u];
        scale_pcs = ((m << key_root) | (m >> (12 - key_root))) & 0xFFFu;
    }
    for (r = 0; r < 12u; r++) {          /* each note sounding as the root: the templates that fit */
        uint32_t rot;
        if (!(pcs >> r & 1u))
            continue;
        rot = ((pcs >> r) | (pcs << (12u - r))) & 0xFFFu;
        for (k = 0; k < sizeof CHORDS / sizeof CHORDS[0]; k++) {
            uint32_t t = CHORDS[k].set;
            int32_t s;
            int fits = rot == t;
            if (!fits && (t & 0x080u) && !(t & 0x140u) && rot == (t & ~0x080u))
                fits = 2;                /* (the 5th left out) */
            if (!fits)
                continue;
            s = (int32_t)CHORDS[k].rank * 2 + (fits == 2) + ((int32_t)r != low % 12 ? 7 : 0)
                + (scale_pcs && !(scale_pcs >> r & 1u) ? 1 : 0);
            if (s < best_s) {
                best_s = s;
                best_r = (int32_t)r;
                best_k = (int32_t)k;
            }
        }
    }
    if (best_k < 0) {                    /* no chord: the notes' names, lowest first (three at most) */
        int32_t last = -1000;
        uint32_t shown = 0;
        for (shown = 0; shown < 3u; shown++) {
            int32_t nx = 1000;
            for (i = 0; i < n; i++)
                if (notes[i] > last && notes[i] < nx)
                    nx = notes[i];
            if (nx == 1000)
                break;
            if (shown)
                ch_cat(name, " ", room);
            ch_cat(name, chord_pc(nx, spell), room);
            last = nx;
        }
        return 1;
    }
    ch_cat(name, chord_pc(best_r, spell), room);
    ch_cat(name, CHORDS[best_k].nm, room);
    if (best_r != low % 12) {
        ch_cat(name, "/", room);
        ch_cat(name, chord_pc(low, spell), room);
    }
    (void)high;
    return 1;
}
