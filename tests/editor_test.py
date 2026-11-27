#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
# Copyright (C) 2026 nullwave contributors
"""The editor's way in (docs/EDITOR-PROTOCOL.md), against the host simulator as a page would talk to an FM-1: the
parameters with their groups, a preset written as the "preset" file (heard on the selected channel, kept in a user
slot when it asks), and read back. Run by tests/run_tests.sh after ./build.sh --host.

    python3 tests/editor_test.py
"""
import struct
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "base" / "tools"))
import fm1_companion as C  # noqa: E402

SP = {}                                  # the parameter ids the test uses, from synth.h's enum
fails = []


def check(what, ok):
    print(("  ok " if ok else "FAIL ") + what)
    if not ok:
        fails.append(what)


def sp_ids():
    import re
    h = (ROOT / "app" / "synth.h").read_text()
    body = re.search(r"enum \{ SP_ATK,(.*?)SP_N \};", h, re.S).group(1)
    names = ["SP_ATK"] + [x.strip() for x in re.sub(r"/\*.*?\*/", "", body, flags=re.S).split(",") if x.strip()]
    n = 0
    for nm in names:
        if "=" in nm:
            nm, expr = (t.strip() for t in nm.split("="))
            base, off = (t.strip() for t in expr.split("+"))
            n = SP[base] + int(off)
        SP[nm] = n
        n += 1


def preset(engine, values, flags=1, slot=0, name=b"TEST"):
    b = struct.pack("<IBBBBB", 0x5250574E, 1, engine, flags, slot, len(name)) + name.ljust(16, b"\0")
    b += bytes([len(values)]) + b"".join(struct.pack("<Hh", i, v) for i, v in values) + b"\0"
    return b


def frames(link, cmd):
    """the frames of cmd the device sent since (0x75 NAV, 0x76 HELLO-FOCUS): their bodies"""
    link.run(50)
    out, keep = [], []
    for pk in link.q:
        a = C.decode(pk)
        (out if a and a[0] == cmd else keep).append(a[1] if a and a[0] == cmd else pk)
    link.q[:] = keep
    return out


def focus(c, ch, title, ids):
    body = bytes([1, ch, len(title)]) + title + bytes([len(ids)]) + b"".join(struct.pack("<H", i) for i in ids)
    return c.request(0x74, body)


def editor_focus(link, c):
    """0x74 FOCUS, 0x75 NAV, 0x76 HELLO-FOCUS (docs/EDITOR-PROTOCOL.md, editor=nullwave/3). In --serve the encoders
    are turned by role: 0 SELECT, 1 ALGORITHM, 2 PRESETS, 3..6 KNOB 1..4"""
    page = lambda: " ".join(link.cmd("expect page -1"))
    # channel 2, five parameters: DTN (E1), CUT, ENV 2 ATK, the engine, the waveform (read only)
    ids = [SP["SP_E1"], SP["SP_CUT"], SP["SP_E2_ATK"], 1000, 1001]
    check("FOCUS taken", focus(c, 1, b"Lead", ids) == b"\x00")
    check("the EDITOR page is open", "got 65" in page())
    check("the keys play the focused channel", "got 1" in " ".join(link.cmd("expect channel -1")))
    c.set(SP["SP_E1"], 0)
    link.cmd("turn 3 5")
    link.run(100)
    check("KNOB 1 sets the first parameter", c.get([SP["SP_E1"]]).get(SP["SP_E1"], 0) > 0)
    link.cmd("turn 0 1")
    link.run(50)
    w = c.get([1001]).get(1001)
    link.cmd("turn 3 2")
    link.run(50)
    check("SELECT: the next four (KNOB 1 the waveform, read only: unchanged)", c.get([1001]).get(1001) == w)
    frames(link, 0x75)
    link.cmd("turn 0 1")
    check("SELECT past the last page: NAV PAGE +1", bytes([2, 1]) in frames(link, 0x75))
    link.cmd("turn 2 -1")
    check("PRESETS: NAV PRESET -1", bytes([3, 0xFF]) in frames(link, 0x75))
    link.cmd("turn 1 1")
    check("ALGORITHM: NAV CH +1", bytes([1, 1]) in frames(link, 0x75))
    check("a bad channel is refused", focus(c, 9, b"X", [1]) == b"\x01")
    check("more than 8 parameters is refused", focus(c, 1, b"X", list(range(9))) == b"\x01")
    check("an empty list clears the focus", focus(c, 1, b"", []) == b"\x00")
    check("cleared: the EDITOR page left", "got 0" in page())
    focus(c, 1, b"Lead", ids)
    frames(link, 0x75)
    link.cmd("buttons " + str(1 << 6))   # (HOME down, then up: a tap)
    link.run(60)
    link.cmd("buttons 0")
    link.run(60)
    check("HOME leaves EDITOR: NAV EXIT", bytes([6, 0]) in frames(link, 0x75) and "got 0" in page())


def main():
    sp_ids()
    host = ROOT / "app" / "build" / "host" / "nullwave_host"
    link = C.ServeLink(f"{host} --serve")
    try:
        link.run(1800)
        c = C.Client(link)
        hi = c.hello()
        check("the capability query names the editor, version 3", hi["extras"].get("editor") == "nullwave/3")
        check("and lists its commands 0x70..0x76 with their versions",
              hi["extras"].get("editor_cmds") == "70:2,71:2,72:1,73:1,74:1,75:1,76:1")
        params = c.params()
        groups = {p["group"] for p in params}
        check("the parameters carry their groups", {"osc", "filter", "env", "lfo", "fx", "mod"} <= groups)
        byid = {p["id"]: p for p in params}
        check("ENV 2 is there (env)", byid.get(SP["SP_E2_ATK"], {}).get("group") == "env")
        check("a mod slot's target lists CUT .. LFO", byid.get(SP["SP_M1_DST"], {}).get("labels", [])[:2] == ["OFF", "CUT"])
        check("the oscillator types are listed (WAVEFORM)", "SUPERSAW" in byid.get(1001, {}).get("labels", []))
        files = c.files()
        idx = next(i for i, f in enumerate(files) if f["name"] == "preset")
        # SUPER (engine 13) with DTN 100, ENV 2 attack 30, slot 2: LFO -> SHP, amount 40
        data = preset(13, [(SP["SP_E1"], 100), (SP["SP_E2_ATK"], 30), (SP["SP_M2_SRC"], 3), (SP["SP_M2_DST"], 3),
                           (SP["SP_M2_AMT"], 40), (9999, 5)])
        c.write(idx, data)
        got = c.get([1000, SP["SP_E1"], SP["SP_E2_ATK"], SP["SP_M2_SRC"], SP["SP_M2_DST"], SP["SP_M2_AMT"], 1001])
        check("the preset plays on the channel: SUPER", got.get(1000) == 13)
        check("its values: DTN 100, ENV 2 ATK 30", got.get(SP["SP_E1"]) == 100 and got.get(SP["SP_E2_ATK"]) == 30)
        check("its mod slot: LFO -> SHP 40", (got.get(SP["SP_M2_SRC"]), got.get(SP["SP_M2_DST"]),
                                             got.get(SP["SP_M2_AMT"])) == (3, 3, 40))
        check("the waveform hint: SUPERSAW", got.get(1001) == byid[1001]["labels"].index("SUPERSAW"))
        back = c.read(idx)
        check("read back: a preset of version 1, SUPER", back[:4] == b"NWPR" and back[4] == 1 and back[5] == 13)
        check("its name on the channel: TEST", back[9:9 + back[8]] == b"TEST")
        # kept in user slot 5, then the channel changed and the slot loaded again
        c.write(idx, preset(13, [(SP["SP_E1"], 77)], flags=3, slot=4, name=b"Wide pad 2"))
        back = c.read(idx)
        check("a name in small letters: in capitals", back[9:9 + back[8]] == b"WIDE PAD 2")
        check("kept in a user slot", c.get([SP["SP_E1"]]).get(SP["SP_E1"]) == 77)
        c.set(SP["SP_E1"], 10)
        check("a parameter set by the page", c.get([SP["SP_E1"]]).get(SP["SP_E1"]) == 10)
        bad = preset(13, [])[:-3]
        try:
            c.write(idx, b"XXXX" + bad[4:])
            check("a file that is not a preset is refused", False)
        except C.CompanionError:
            check("a file that is not a preset is refused", True)
        editor_focus(link, c)
    finally:
        link.close()
    print(f"editor_test: {len(fails)} failed")
    sys.exit(1 if fails else 0)


if __name__ == "__main__":
    main()
