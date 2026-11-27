#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
# Copyright (C) 2026 nullwave contributors
"""The editor's views of the FM-1 (docs/EDITOR-PROTOCOL.md 0x70 STATE with the transport, 0x71 STEPS, 0x72 SONG +
SCENES, 0x73 PRESETS and lanes; the FM-1's own settings as 0x62 parameters 0xF000 on) against the host simulator, with the request frames as captured vectors (their exact bytes on the wire). Run by
tests/run_tests.sh after ./build.sh --host.

    python3 tests/editor_views_test.py
"""
import struct
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "base" / "tools"))
import fm1_companion as C  # noqa: E402
import fm1_install as I  # noqa: E402

fails = []
# the captured vectors: a request's frame as it goes on the wire (F0 pack7(00 59 cmd len24 body ~sum8) F7)
VECTORS = {
    "STATE get": (0x70, bytes([1, 0]), "F0 00 32 41 13 00 00 40 00 00 7C 03 F7"),
    "STEPS read ch 1 bar 1": (0x71, bytes([1, 0, 0, 0]), "F0 00 32 45 23 00 00 40 00 00 00 00 70 0F F7"),
    "SONG scenes": (0x72, bytes([1, 0]), "F0 00 32 49 13 00 00 40 00 00 7C 03 F7"),
    "PRESETS factory SUPER": (0x73, bytes([1, 0, 13]), "F0 00 32 4D 1B 00 00 40 00 00 1A 44 07 F7"),
    "STATE PLAY": (0x70, bytes([1, 2, 1]), "F0 00 32 41 1B 00 00 40 00 02 02 6C 07 F7"),
    "STATE TEMPO 125.5": (0x70, bytes([1, 4, 0xE7, 0x04]), "F0 00 32 41 23 00 00 40 00 04 4E 13 78 00 F7"),
    "STATE LAUNCH scene 2": (0x70, bytes([1, 5, 1]), "F0 00 32 41 1B 00 00 40 00 05 02 60 07 F7"),
    "STEPS read version 2": (0x71, bytes([2, 0, 0, 0]), "F0 00 32 45 23 00 00 00 01 00 00 00 68 0F F7"),
    "PRESETS lanes channel 6": (0x73, bytes([1, 3, 5]), "F0 00 32 4D 1B 00 00 40 00 03 0A 58 07 F7"),
}


def check(what, ok):
    print(("  ok " if ok else "FAIL ") + what)
    if not ok:
        fails.append(what)


def hexs(b):
    return " ".join(f"{x:02X}" for x in b)


def pushed(link, cmd, op):
    link.run(60)
    out, keep = [], []
    for pk in link.q:
        a = C.decode(pk)
        if a and a[0] == cmd and a[1][2:3] == bytes([op]):
            out.append(a[1])
        else:
            keep.append(pk)
    link.q[:] = keep
    return out


def st(req, op, *a, ver=1):
    return req(0x70, bytes([ver, op] + list(a)))


def transport(req, link):
    """0x70 ops 2..6: the transport from the editor, as the FM-1's buttons and knobs move it"""
    s = st(req, 2, 1)
    check("PLAY from the editor: playing, the state back", s[:3] == bytes([1, 0, 2]) and s[3] & 1 and len(s) == 162)
    check("PLAY again: still playing", st(req, 2, 1)[3] & 1)
    s = st(req, 2, 0)
    check("STOP: stopped", s[1] == 0 and not s[3] & 1)
    s = st(req, 3, 1)
    check("REC arm, stopped: armed", s[1] == 0 and s[3] & 4 and s[17] == 1)
    check("REC arm again: still armed", st(req, 3, 1)[17] == 1)
    s = st(req, 3, 2)
    check("REC off", s[1] == 0 and s[17] == 0 and not s[3] & 6)
    check("REC: the button's toggle arms", st(req, 3, 0)[17] == 1 and st(req, 3, 0)[17] == 0)
    check("REC: an action past 2: bad request", st(req, 3, 7)[1] == 1)
    s = st(req, 4, 1255 & 0xFF, 1255 >> 8)
    check("TEMPO 125.5", s[1] == 0 and struct.unpack_from("<H", s, 4)[0] == 1255)
    s = st(req, 4, 0x2C, 0x01)
    check("TEMPO 30.0: out of range, the tempo kept", s[1] == 3 and struct.unpack_from("<H", s, 4)[0] == 1255)
    st(req, 4, 1200 & 0xFF, 1200 >> 8)
    s = st(req, 5, 3)
    check("LAUNCH stopped: scene 4 at once", s[1] == 0 and s[12] == 3 and s[13] == 0xFF)
    st(req, 2, 1)
    s = st(req, 5, 0)
    check("LAUNCH playing: scene 1 queued for the bar (SCENE LAUNCH BAR)", s[12] == 3 and s[13] == 0)
    link.run(2200)
    check("and it starts at the bar", st(req, 0)[12] == 0)
    st(req, 2, 0)
    check("LAUNCH scene 17: out of range", st(req, 5, 16)[1] == 3)
    s = st(req, 6, 1)
    check("MODE SONG", s[1] == 0 and s[3] & 16)
    s = st(req, 6, 0)
    check("MODE LOOP", s[1] == 0 and not s[3] & 16)
    check("an op past 6: bad request", st(req, 9)[1] == 1)


GLOBALS = {0xF000: ("ROOT", "key", 0, 11), 0xF001: ("SCALE", "key", 0, 15), 0xF002: ("KEYS", "key", 0, 2),
           0xF003: ("CHORD", "key", 0, 5), 0xF004: ("TRANS", "key", -24, 24), 0xF005: ("OCTAVE", "key", -2, 2),
           0xF010: ("MIDI CH 1", "midi", 0, 16), 0xF015: ("MIDI CH 6", "midi", 0, 16),
           0xF016: ("MIDI IN", "midi", 0, 17), 0xF017: ("MIDI OUT", "midi", 0, 1),
           0xF020: ("CLOCK", "clock", 0, 3), 0xF021: ("CLOCK OUT", "clock", 0, 1),
           0xF022: ("SCENE LAUNCH", "clock", 0, 2)}


def globals_(c):
    """the FM-1's own settings as 0x62 parameters, 0xF000 on, with their groups and ranges"""
    byid = {p["id"]: p for p in c.params()}
    for pid, (name, grp, lo, hi) in GLOBALS.items():
        p = byid.get(pid, {})
        check(f"0x62 {pid:04X} {name}: {grp}, {lo}..{hi}", p.get("name") == name and p.get("group") == grp
              and p.get("min") == lo and (p.get("max") == hi or (pid == 0xF020 and p.get("max") in (2, 3))))
    check("SCALE's labels: CHROM first", byid[0xF001]["labels"][:2] == ["CHROM", "MAJOR"])
    check("ROOT set to D", c.set(0xF000, 2) == 2 and c.get([0xF000])[0xF000] == 2)
    c.set(0xF000, 0)
    check("TRANS set to -24 (and past it: clamped)", c.set(0xF004, -30) == -24)
    c.set(0xF004, 0)
    check("OCTAVE +1", c.set(0xF005, 1) == 1)
    c.set(0xF005, 0)
    check("MIDI CH 1 to another channel's 2: refused, 1 kept", c.set(0xF010, 2) == 1)
    check("MIDI CH 1 to 9", c.set(0xF010, 9) == 9)
    c.set(0xF010, 1)
    check("CLOCK USB", c.set(0xF020, 1) == 1)
    return byid


def versions2(req):
    """version 2: the patterns' DIV, step and time in STATE's channel blocks and STEPS' header"""
    s = req(0x70, bytes([2, 0]))
    b = 18 + 5 * 28
    check("STATE version 2: 186 bytes, its version back", len(s) == 186 and s[:3] == bytes([2, 0, 0]))
    check("STATE version 2: channel 1 at DIV 1/16, 24 ticks a step, 4/4", s[18 + 7:18 + 12] == bytes([0, 24, 0, 4, 4]))
    check("STATE version 2: channel 6's name after its time", s[b + 12:b + 17] == b"DRUMS")
    s = req(0x70, bytes([2, 4, 0xB0, 0x04]))
    check("TEMPO while the clock is USB's: busy", s[1] == 2 and struct.unpack_from("<H", s, 4)[0] == 1200)
    r = req(0x71, bytes([2, 0, 0, 0]))
    check("STEPS version 2: ch, bar, bars, DIV, step u16, 4/4, then the notes", r[:3] == bytes([2, 0, 0])
          and r[3:11] == bytes([0, 0, 1, 0, 24, 0, 4, 4]) and r[11] == 3 and len(r) == 12 + 18)
    r1 = req(0x71, bytes([1, 0, 0, 0]))
    check("STEPS version 1 as before", r1[:3] == bytes([1, 0, 0]) and r1[7] == 3 and r1[8:] == r[12:])


def lanes(req):
    """0x73 op 3: a kit's lanes, its note and name each"""
    k = req(0x73, bytes([1, 3, 5]))
    check("LANES channel 6 (DRUM): 8, lane 1 KICK on 36", k[:3] == bytes([1, 0, 3]) and k[3:5] == bytes([5, 8])
          and k[5:7] == bytes([0, 36]) and k[7:11] == b"KICK" and len(k) == 5 + 8 * 18)
    check("LANES: lane 8 BELL on 56", k[5 + 7 * 18:7 + 7 * 18] == bytes([7, 56]) and k[7 + 7 * 18:11 + 7 * 18] == b"BELL")
    k = req(0x73, bytes([1, 3, 0]))
    check("LANES of a channel not a kit: none", k[1] == 0 and k[4] == 0 and len(k) == 5)


def main():
    for name, (cmd, body, want) in VECTORS.items():
        check(f"vector {name}: {want}", hexs(I.frame(cmd, body)) == want)
    host = ROOT / "app" / "build" / "host" / "nullwave_host"
    link = C.ServeLink(f"{host} --serve")
    try:
        link.run(1800)
        c = C.Client(link)
        c.hello()
        req = lambda cmd, body: c.request(cmd, body, match=lambda b: len(b) > 2 and b[2] < 0x80)   # (not a push)
        # 0x70 STATE
        s = req(0x70, bytes([1, 0]))
        check("STATE: 162 bytes, version 1, ok, op 0", len(s) == 162 and s[:3] == bytes([1, 0, 0]))
        check("STATE: stopped, 120.0 BPM, channel 1, a bar of 1", s[3] == 0 and struct.unpack_from("<H", s, 4)[0] == 1200
              and s[6] == 0 and s[7] == 1 and s[8] == 0xFF)
        check("STATE: channel 6 a DRUM kit named DRUMS", s[18 + 5 * 24] == 12 and s[18 + 5 * 24 + 7:18 + 5 * 24 + 12] == b"DRUMS")
        check("STATE: subscribed", req(0x70, bytes([1, 1, 1]))[:3] == bytes([1, 0, 1]))
        link.cmd("buttons " + str(1 << 10))  # PLAY
        link.run(30)
        link.cmd("buttons 0")
        link.run(300)
        p = pushed(link, 0x70, 0x80)
        check("STATE pushed on change: playing", any(b[3] & 1 for b in p))
        req(0x70, bytes([1, 1, 0]))
        link.cmd("buttons " + str(1 << 10))
        link.run(30)
        link.cmd("buttons 0")
        link.run(100)
        # 0x71 STEPS
        notes = [(0, 0, 60, 100, 4, 0), (4, 0, 64, 90, 4, 0), (8, 2, 67, 80, 255, 32)]
        w = req(0x71, bytes([1, 1, 0, 0, len(notes)]) + b"".join(bytes(n) for n in notes))
        check("STEPS write: ok, the bar back", w[:3] == bytes([1, 0, 1]) and w[3:7] == bytes([0, 0, 1, 0]) and w[7] == 3)
        r = req(0x71, bytes([1, 0, 0, 0]))
        got = sorted(tuple(r[8 + 6 * i:14 + 6 * i]) for i in range(r[7]))
        check("STEPS read: the three notes, the drone flagged", got == sorted(notes))
        check("STEPS: a bar past the pattern: out of range", req(0x71, bytes([1, 0, 0, 5]))[1] == 3)
        check("STEPS: a pitch past 127: out of range, nothing changed",
              req(0x71, bytes([1, 1, 0, 0, 1, 0, 0, 200, 100, 4, 0]))[1] == 3
              and req(0x71, bytes([1, 0, 0, 0]))[7] == 3)
        check("STEPS playhead subscribed", req(0x71, bytes([1, 2, 0, 1]))[1] == 0)
        link.cmd("buttons " + str(1 << 10))
        link.run(30)
        link.cmd("buttons 0")
        heads = pushed(link, 0x71, 0x82)
        link.run(300)
        heads += pushed(link, 0x71, 0x82)
        check("STEPS playhead pushed as it moves", len({h[5] for h in heads}) >= 2)
        link.cmd("buttons " + str(1 << 11))  # REC while playing: recording channel 1
        link.run(30)
        link.cmd("buttons 0")
        link.run(50)
        check("STEPS write while it records that channel: busy", req(0x71, bytes([1, 1, 0, 0, 0]))[1] == 2)
        link.cmd("buttons " + str(1 << 10))
        link.run(30)
        link.cmd("buttons 0")
        link.run(100)
        req(0x71, bytes([1, 2, 0, 0]))
        # 0x72 SONG + SCENES
        sc = req(0x72, bytes([1, 0]))
        check("SCENES: 16, 16 bytes each", len(sc) == 4 + 16 * 16 and sc[3] == 16)
        w2 = req(0x72, bytes([1, 1, 2]) + bytes([1, 1, 1, 1, 1, 1]) + bytes([0b10, 0]) + bytes([90] * 6))
        sc = req(0x72, bytes([1, 0]))
        e = sc[4 + 2 * 16:4 + 3 * 16]
        check("a scene written answers with the whole scene", w2[3] == 2 and w2[4:20] == e and len(w2) == 20)
        check("a scene written: scene 3, patterns 2, channel 2 muted, levels 90", e[:6] == bytes([1] * 6) and e[6] == 2
              and e[8:14] == bytes([90] * 6))
        so = req(0x72, bytes([1, 3, 3, 1, 0, 2, 1, 1, 2, 2, 0, 1, 1]))
        check("the song's order written: A1 A2 A1, END STOP", so[:3] == bytes([1, 0, 3]) and so[3:6] == bytes([3, 1, 0])
              and so[6:15] == bytes([0, 2, 1, 1, 2, 2, 0, 1, 1]))
        so = req(0x72, bytes([1, 2]))
        check("the song read back", so[3] == 3 and so[4] == 1 and so[6:9] == bytes([0, 2, 1]))
        # 0x73 PRESETS
        pr = req(0x73, bytes([1, 0, 13]))
        check("PRESETS: SUPER's seven, SUPERSAW first", pr[3:5] == bytes([13, 7]) and pr[6:14] == b"SUPERSAW")
        check("PRESETS: an engine past the last: out of range", req(0x73, bytes([1, 0, 40]))[1] == 3)
        us = req(0x73, bytes([1, 1, 0, 16]))
        check("PRESETS: user slots 1-16, empty", us[3:5] == bytes([0, 16]) and us[6] == 0 and len(us) == 5 + 16 * 19)
        ld = req(0x73, bytes([1, 2, 0, 0, 1, 2]))
        check("PRESETS: FM6's third (FM BASS) on channel 1", ld[:3] == bytes([1, 0, 2]) and ld[3:6] == bytes([0, 1, 2])
              and ld[6:13] == b"FM BASS")
        check("PRESETS: an empty user slot: out of range", req(0x73, bytes([1, 2, 0, 1, 0, 5]))[1] == 3)
        check("a version this firmware does not read: refused", req(0x70, bytes([3, 0]))[1] == 1
              and req(0x72, bytes([2, 0]))[1] == 1)
        transport(req, link)
        globals_(c)
        versions2(req)
        lanes(req)
        check("bytes past what version 1 reads: ignored", req(0x70, bytes([1, 0, 9, 9, 9]))[1] == 0)
    finally:
        link.close()
    print(f"editor_views_test: {len(fails)} failed")
    sys.exit(1 if fails else 0)


if __name__ == "__main__":
    main()
