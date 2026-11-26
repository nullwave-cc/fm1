/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 nullwave contributors */
/* The chord namer (app/chords.c) against a table: every chord type, in root position and inversions, in several keys,
 * spelled in the song's key; one and two notes. Built and run by tests/run_tests.sh:
 *   cc -o app/build/host/chord_test tests/chord_test.c && app/build/host/chord_test */
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "../app/chords.c"

static int fails, runs;
/* notes as MIDI numbers, the key (root, scale: 0 none, 1 MAJOR, 2 MINOR ...), what it should say */
static void t(const char *want, const char *want_d, int32_t root, uint32_t scale, int n, ...)
{
    int32_t notes[8];
    char name[32], det[16];
    __builtin_va_list ap;
    int i;
    __builtin_va_start(ap, n);
    for (i = 0; i < n; i++)
        notes[i] = __builtin_va_arg(ap, int);
    __builtin_va_end(ap);
    chord_spell(notes, (uint32_t)n, root, scale, name, det, sizeof name);
    runs++;
    if (strcmp(name, want) || strcmp(det, want_d ? want_d : "")) {
        fails++;
        printf("FAIL %-10s %-4s got %s %s\n", want, want_d ? want_d : "", name, det);
    }
}
enum { C3 = 48, C4 = 60 };
int main(void)
{
    /* triads, root position and inversions */
    t("C", 0, 0, 0, 3, C4, C4 + 4, C4 + 7);
    t("C/E", 0, 0, 0, 3, C4 + 4, C4 + 7, C4 + 12);
    t("C/G", 0, 0, 0, 3, C4 + 7, C4 + 12, C4 + 16);
    t("Cm", 0, 0, 0, 3, C4, C4 + 3, C4 + 7);
    t("Cm/Eb", 0, 0, 0, 3, C4 + 3, C4 + 7, C4 + 12);
    t("Bdim", 0, 0, 1, 3, 59, 62, 65);
    t("Caug", 0, 0, 0, 3, C4, C4 + 4, C4 + 8);
    t("Csus2", 0, 0, 0, 3, C4, C4 + 2, C4 + 7);
    t("Csus4", 0, 0, 0, 3, C4, C4 + 5, C4 + 7);
    t("D", 0, 2, 1, 3, 62, 66, 69);
    t("F#m", 0, 2, 1, 3, 66, 69, 73);
    t("Bb", 0, 5, 1, 3, 58, 62, 65);   /* in F: Bb, not A# */
    t("Eb/G", 0, 10, 1, 3, 55, 58, 63);
    t("A#", 0, 6, 1, 3, 58, 62, 65);   /* in F# major: sharps */
    t("Bb", 0, 0, 0, 3, 58, 62, 65);   /* no key: the usual name */
    /* power, sixths */
    t("C5", 0, 0, 0, 2, C3, C3 + 7);
    t("C5", 0, 0, 0, 3, C3, C3 + 7, C3 + 12);
    t("C6", 0, 0, 0, 4, C4, C4 + 4, C4 + 7, C4 + 9);
    t("Am7", 0, 0, 0, 4, 57, 60, 64, 67);
    t("C6", 0, 0, 0, 4, C4, C4 + 4, C4 + 7, C4 + 21);   /* (A on top, C in the bass: C6, not Am7/C) */
    t("Am7/E", 0, 0, 0, 4, C4 + 4, C4 + 7, C4 + 9, C4 + 12);
    t("Am7b5/Eb", 0, 0, 0, 4, 63, 67, 69, 72);
    t("Cm6", 0, 0, 0, 4, C4, C4 + 3, C4 + 7, C4 + 9);
    /* sevenths */
    t("G7", 0, 0, 1, 4, 55, 59, 62, 65);
    t("G7/B", 0, 0, 1, 4, 59, 62, 65, 67);
    t("G7/F", 0, 0, 1, 4, 53, 55, 59, 62);
    t("Cmaj7", 0, 0, 0, 4, C4, C4 + 4, C4 + 7, C4 + 11);
    t("Cmaj7/E", 0, 0, 0, 4, C4 + 4, C4 + 7, C4 + 11, C4 + 12);
    t("Dm7", 0, 0, 1, 4, 62, 65, 69, 72);
    t("Bm7b5", 0, 0, 1, 4, 59, 62, 65, 69);
    t("Bdim7", 0, 0, 0, 4, 59, 62, 65, 68);
    t("Ddim7", 0, 0, 0, 4, 62, 65, 68, 71);
    t("CmMaj7", 0, 0, 0, 4, C4, C4 + 3, C4 + 7, C4 + 11);
    t("C7sus4", 0, 0, 0, 4, C4, C4 + 5, C4 + 7, C4 + 10);
    t("Caug7", 0, 0, 0, 4, C4, C4 + 4, C4 + 8, C4 + 10);
    t("C7", 0, 0, 0, 3, C4, C4 + 4, C4 + 10);        /* (no 5th) */
    t("Cmaj7", 0, 0, 0, 3, C4, C4 + 4, C4 + 11);     /* (no 5th) */
    t("Bbmaj7", 0, 5, 1, 4, 58, 62, 65, 69);
    t("Ebm7", 0, 1, 1, 4, 63, 66, 70, 73);           /* in Db: Eb */
    /* extensions */
    t("Cadd9", 0, 0, 0, 4, C4, C4 + 4, C4 + 7, C4 + 14);
    t("Cmadd9", 0, 0, 0, 4, C4, C4 + 3, C4 + 7, C4 + 14);
    t("C9", 0, 0, 0, 5, C4, C4 + 4, C4 + 7, C4 + 10, C4 + 14);
    t("C9", 0, 0, 0, 4, C4, C4 + 4, C4 + 10, C4 + 14);   /* (no 5th) */
    t("Cmaj9", 0, 0, 0, 5, C4, C4 + 4, C4 + 7, C4 + 11, C4 + 14);
    t("Cm9", 0, 0, 0, 5, C4, C4 + 3, C4 + 7, C4 + 10, C4 + 14);
    t("C11", 0, 0, 0, 5, C4, C4 + 7, C4 + 10, C4 + 14, C4 + 17);
    t("C11", 0, 0, 0, 6, C4, C4 + 4, C4 + 7, C4 + 10, C4 + 14, C4 + 17);
    t("Cm11", 0, 0, 0, 6, C4, C4 + 3, C4 + 7, C4 + 10, C4 + 14, C4 + 17);
    t("C13", 0, 0, 0, 5, C4, C4 + 4, C4 + 7, C4 + 10, C4 + 21);
    t("C13", 0, 0, 0, 6, C4, C4 + 4, C4 + 7, C4 + 10, C4 + 14, C4 + 21);
    t("C7b9", 0, 0, 0, 5, C4, C4 + 4, C4 + 7, C4 + 10, C4 + 13);
    t("C7#9", 0, 0, 0, 5, C4, C4 + 4, C4 + 7, C4 + 10, C4 + 15);
    t("C7#11", 0, 0, 0, 5, C4, C4 + 4, C4 + 7, C4 + 10, C4 + 18);
    t("G9", 0, 7, 1, 5, 55, 59, 62, 65, 69);
    /* one note, two notes */
    t("C4", 0, 0, 0, 1, C4);
    t("C4", 0, 0, 0, 2, C4, C4 + 12);
    t("Bb3", 0, 5, 1, 1, 58);
    t("C E", "M3", 0, 0, 2, C4, C4 + 4);
    t("C Eb", "m3", 0, 0, 2, C4, C4 + 3);
    t("E C", "m6", 0, 0, 2, C4 + 4, C4 + 12);
    t("C F#", "TT", 0, 0, 2, C4, C4 + 6);
    t("A Bb", "m2", 5, 1, 2, 57, 58);
    /* the same chord in every key, every inversion */
    {
        int r, inv, fam;
        static const struct { int iv[4]; int n; const char *nm; } F[] = {
            {{0, 4, 7}, 3, ""}, {{0, 3, 7}, 3, "m"}, {{0, 4, 7, 10}, 4, "7"}, {{0, 4, 7, 11}, 4, "maj7"},
            {{0, 3, 7, 10}, 4, "m7"}, {{0, 3, 6, 10}, 4, "m7b5"}};
        for (fam = 0; fam < 6; fam++)
            for (r = 0; r < 12; r++)
                for (inv = 0; inv < F[fam].n; inv++) {
                    int32_t notes[4];
                    char name[32], det[16], want[32];
                    int i;
                    for (i = 0; i < F[fam].n; i++)
                        notes[i] = C4 + r + F[fam].iv[i] + (i < inv ? 12 : 0);
                    chord_spell(notes, (uint32_t)F[fam].n, 0, 0, name, det, sizeof name);
                    snprintf(want, sizeof want, "%s%s", CH_PLAIN[r], F[fam].nm);
                    if (inv == 1 && (fam == 4 || fam == 5))   /* (m7 over its 3rd: the 6 chord; m7b5: the m6) */
                        snprintf(want, sizeof want, "%s%s", CH_PLAIN[(r + 3) % 12], fam == 4 ? "6" : "m6");
                    else if (inv) {
                        strcat(want, "/");
                        strcat(want, CH_PLAIN[(r + F[fam].iv[inv]) % 12]);
                    }
                    runs++;
                    if (strcmp(name, want)) {
                        fails++;
                        printf("FAIL %s got %s\n", want, name);
                    }
                }
    }
    printf("chord_test: %d names, %d failed\n", runs, fails);
    return fails != 0;
}
