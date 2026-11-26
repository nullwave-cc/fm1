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


def main():
    sp_ids()
    host = ROOT / "app" / "build" / "host" / "nullwave_host"
    link = C.ServeLink(f"{host} --serve")
    try:
        link.run(1800)
        c = C.Client(link)
        hi = c.hello()
        check("the capability query names the editor", hi["extras"].get("editor") == "nullwave/1")
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
    finally:
        link.close()
    print(f"editor_test: {len(fails)} failed")
    sys.exit(1 if fails else 0)


if __name__ == "__main__":
    main()
