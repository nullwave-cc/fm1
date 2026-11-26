#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
"""The built-in samples as C tables: app/samples.h (SAMPLE, GRAIN and SLICE play them).

  tools/gen_samples.py DIR      DIR: the CC0 recordings (assets/samples-cc0's layout, see its ATTRIBUTION.txt)

Each recording is mixed to mono, its silence at the start cut, a length kept (a fade at the end, or a loop for the
sustained ones), resampled to 16 kHz and coded in 8-bit mu-law. BREAK, a bar of drums at 100 BPM, is synthesised here
(nullwave's own, no recording). Pure Python: no numpy needed.
"""
import math
import struct
import sys
import wave
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / "app" / "samples.h"
RATE = 16000

# set, file, root note, seconds kept, loop (seconds from the end that loop; 0 a one-shot)
SOURCES = [
    ("PIANO", "PIANO/02_GPiano_sus_C3_v2_rr1_Player.wav", 48, 1.1, 0),
    ("PIANO", "PIANO/03_GPiano_sus_C4_v2_rr1_Player.wav", 60, 1.1, 0),
    ("PIANO", "PIANO/04_GPiano_sus_C5_v2_rr1_Player.wav", 72, 1.0, 0),
    ("FLUTE", "FLUTE/00_LDFlute_susvib_C4_v1_1.wav", 60, 0.75, 0.3),
    ("FLUTE", "FLUTE/01_LDFlute_susvib_C5_v1_1.wav", 72, 0.75, 0.3),
    ("SAX", "SAX/01_BrettTenor_NV_Main_A#2_vl2_rr1.wav", 46, 0.75, 0.3),
    ("SAX", "SAX/02_BrettTenor_NV_Main_A#3_vl2_rr1.wav", 58, 0.75, 0.3),
]


def read_wav(path):
    w = wave.open(str(path))
    ch, sw, fr = w.getnchannels(), w.getsampwidth(), w.getframerate()
    raw = w.readframes(w.getnframes())
    n = len(raw) // (sw * ch)
    out = []
    for i in range(n):
        s = 0
        for c in range(ch):
            o = (i * ch + c) * sw
            if sw == 2:
                s += struct.unpack_from("<h", raw, o)[0] / 32768.0
            elif sw == 3:
                b = raw[o:o + 3]
                s += (int.from_bytes(b, "little", signed=True)) / 8388608.0
        out.append(s / ch)
    return out, fr


def resample(x, fr, rate):
    # a box low-pass over the ratio, then linear interpolation
    k = max(1, int(round(fr / rate)))
    if k > 1:
        acc, sm = 0.0, []
        for i, v in enumerate(x):
            acc += v
            if i >= k:
                acc -= x[i - k]
            sm.append(acc / k)
        x = sm
    n = int(len(x) * rate / fr)
    out = []
    for i in range(n):
        p = i * fr / rate
        j = int(p)
        f = p - j
        a = x[j] if j < len(x) else 0.0
        b = x[j + 1] if j + 1 < len(x) else a
        out.append(a + (b - a) * f)
    return out


def mulaw(v):
    v = max(-1.0, min(1.0, v))
    m = math.copysign(math.log1p(255 * abs(v)) / math.log1p(255), v)
    return int(round((m + 1) * 127.5)) & 0xFF


def trim(x):
    peak = max(abs(v) for v in x) or 1.0
    for i, v in enumerate(x):
        if abs(v) > peak * 0.02:
            return x[max(0, i - 8):]
    return x


def zero_near(x, i):
    for d in range(400):
        for j in (i - d, i + d):
            if 0 < j < len(x) and x[j - 1] <= 0 < x[j]:
                return j
    return i


def break_bar():
    """a bar of drums at 100 BPM: kick, snare, hats, a ghost or two"""
    n = int(RATE * 60 / 100 * 4)
    out = [0.0] * n
    st = n // 16
    rnd = 12345

    def noise():
        nonlocal rnd
        rnd = (rnd * 1103515245 + 12345) & 0x7FFFFFFF
        return rnd / 0x3FFFFFFF - 1.0

    def kick(at, amp):
        ph = 0.0
        for i in range(int(RATE * 0.35)):
            t = i / RATE
            f = 48 + 110 * math.exp(-t * 28)
            ph += 2 * math.pi * f / RATE
            if at + i < n:
                out[at + i] += amp * math.sin(ph) * math.exp(-t * 7)

    def snare(at, amp):
        lp = 0.0
        for i in range(int(RATE * 0.25)):
            t = i / RATE
            nz = noise()
            lp += (nz - lp) * 0.6
            tone = math.sin(2 * math.pi * 190 * t) * math.exp(-t * 25)
            if at + i < n:
                out[at + i] += amp * (0.6 * (nz - lp * 0.5) * math.exp(-t * 14) + 0.5 * tone)

    def hat(at, amp, dec):
        prev = 0.0
        for i in range(int(RATE * 0.12)):
            t = i / RATE
            nz = noise()
            hp = nz - prev
            prev = nz
            if at + i < n:
                out[at + i] += amp * 0.5 * hp * math.exp(-t * dec)

    for s in (0, 7, 10):
        kick(s * st, 0.9)
    for s in (4, 12):
        snare(s * st, 0.8)
    snare(15 * st, 0.25)
    for s in range(16):
        hat(s * st, 0.6 if s % 2 == 0 else 0.35, 60 if s != 14 else 12)
    peak = max(abs(v) for v in out) or 1.0
    return [v / peak * 0.95 for v in out]


def main():
    src = Path(sys.argv[1]) if len(sys.argv) > 1 else ROOT / "assets" / "samples-cc0"
    sets, data, zones = [], [], []
    for name, f, root, secs, loop in SOURCES:
        x, fr = read_wav(src / f)
        x = trim(x)
        x = resample(x, fr, RATE)[: int(secs * RATE)]
        peak = max(abs(v) for v in x) or 1.0
        x = [v / peak * 0.97 for v in x]
        n = len(x)
        if loop:
            ls = zero_near(x, n - int(loop * RATE))
            le = zero_near(x, n - 40)
            x = x[:le]
            xf = min(400, (le - ls) // 3)      # a crossfade into the loop's start
            for i in range(xf):
                a = i / xf
                x[le - xf + i] = x[le - xf + i] * (1 - a) + x[ls - xf + i] * a
            loop_start = ls
        else:
            fade = int(0.08 * RATE)
            for i in range(fade):
                x[n - fade + i] *= 1 - i / fade
            loop_start = -1
        if name not in sets:
            sets.append(name)
        zones.append((sets.index(name), root, len(data), len(x), loop_start))
        data += [mulaw(v) for v in x]
    br = break_bar()
    sets.append("BREAK")
    zones.append((len(sets) - 1, 60, len(data), len(br), -1))
    data += [mulaw(v) for v in br]
    lines = ["/* SPDX-License-Identifier: MPL-2.0 (the tables); the recordings are CC0 1.0: assets/samples-cc0 */",
             "/* generated by tools/gen_samples.py: the built-in samples, 16 kHz mu-law. Do not edit */",
             "#pragma once", "#include <stdint.h>", f"#define SMP_RATE {RATE}",
             f"#define SMP_NSETS {len(sets)}", f"#define SMP_NZONES {len(zones)}",
             "static const char *const SMP_SET_NAME[SMP_NSETS] = {" + ", ".join(f'"{s}"' for s in sets) + "};",
             "/* a zone: its set, the note it was recorded at, where it is, its length, its loop's start (-1 none) */",
             "static const struct { uint8_t set, root; uint32_t at, len; int32_t loop; } SMP_ZONE[SMP_NZONES] = {"]
    lines += [f"    {{{s}, {r}, {a}, {n}, {l}}}," for s, r, a, n, l in zones]
    lines += ["};", f"static const uint8_t SMP_DATA[{len(data)}] = {{"]
    for i in range(0, len(data), 24):
        lines.append("    " + ", ".join(str(v) for v in data[i:i + 24]) + ",")
    lines += ["};", ""]
    OUT.write_text("\n".join(lines))
    print(f"{OUT}: {len(sets)} sets, {len(zones)} zones, {len(data)} bytes")


main()
