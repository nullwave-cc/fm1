#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
# Copyright (C) 2026 nullwave contributors
"""The factory reset over the companion's files, as the installer page does it, against the host simulator: the
backup first (the songs, user presets and FM6 bank read as files), a wrong file refused, then the "factory-reset"
file written with its words: everything nullwave keeps erased and the FM-1 restarted, its files empty after. Run by
tests/run_tests.sh after ./build.sh --host.

    python3 tests/factory_test.py
"""
import struct
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "base" / "tools"))
import fm1_companion as C  # noqa: E402

WORDS = b"nullwave factory reset"
fails = []


def check(what, ok):
    print(("  ok " if ok else "FAIL ") + what)
    if not ok:
        fails.append(what)


def main():
    host = ROOT / "app" / "build" / "host" / "nullwave_host"
    link = C.ServeLink(f"{host} --serve")
    try:
        link.run(1800)
        c = C.Client(link)
        files = c.files()
        names = [f["name"] for f in files]
        idx = {f["name"]: i for i, f in enumerate(files)}
        check("the files: songs, user presets, the FM6 bank, factory-reset",
              all(n in names for n in ("song-a", "song-h", "user-presets", "fm6-bank", "factory-reset")))
        # something kept: a sound in user slot 1 (the editor's preset, flags: play and keep)
        pre = struct.pack("<IBBBBB", 0x5250574E, 1, 0, 3, 0, 4) + b"KEEP".ljust(16, b"\0") + b"\0\0"
        c.write(idx["preset"], pre)
        files = c.files()
        check("a user preset kept: user-presets has its bytes", files[idx["user-presets"]]["size"] == 8192)
        back = c.read(idx["user-presets"])
        check("the backup: user-presets read whole", len(back) == 8192 and back[:4] == b"PUWN")
        try:
            c.write(idx["factory-reset"], b"PLEASE ERASE")
            check("other words are refused", False)
        except C.CompanionError:
            check("other words are refused", True)
        check("refused: nothing erased", c.files()[idx["user-presets"]]["size"] == 8192)
        c.write(idx["factory-reset"], WORDS)
        link.run(4000)                   # (the erase, a sector a frame, then the restart)
        c = C.Client(link)
        files = c.files()
        check("after: the user presets erased", files[idx["user-presets"]]["size"] == 0)
        check("after: no songs (A at most: a first start's new song, autosaved)",
              all(files[idx[f"song-{s}"]]["size"] == 0 for s in "bcdefgh"))
    finally:
        link.close()
    print(f"factory_test: {len(fails)} failed")
    sys.exit(1 if fails else 0)


if __name__ == "__main__":
    main()
