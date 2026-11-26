#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
# Copyright (C) 2026 nullwave contributors
"""Every page, layer, graph and view of nullwave as a screenshot from the host simulator.

    python3 tools/page_shots.py OUT_DIR [--light] [--small]

Builds nothing: run ./build.sh --host first. Writes OUT_DIR/<NAME>.png for each entry of SHOTS and
OUT_DIR/pages.txt (the simulator script it ran). The names are what docs/GUIDE.md and the comparison page use.
--small: each PNG cut to 64 colours (needs Pillow), as docs/img/guide keeps them.
"""
import os
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
HOST = os.path.join(ROOT, "app", "build", "host", "nullwave_host")
SETTLE = "wait 1400"                     # the footer's message gone

# (name, steps before the shot); each starts from MAIN with nothing held
SHOTS = []


def page(name, *steps):
    SHOTS.append((name, list(steps)))


def walk(button, names, channel=None, shift=False):
    """a button's pages in order: tap it, then SELECT round"""
    pre = []
    if channel is not None:
        pre += ["channel %d" % channel]
    if shift:
        pre += ["press OCT-", "wait 30", "tap %s" % button, "wait 30", "release OCT-"]
    else:
        pre += ["tap %s" % button]
    for i, n in enumerate(names):
        page(n, *(pre + ["turn SELECT 1"] * i))


walk("ENV", ["ENV_ENV", "ENV_DEST"], channel=1)
walk("LFO", ["LFO_LFO", "LFO_LFO2", "LFO_DEST"], channel=1)
walk("FX", ["FX_FX", "FX_FILTER", "FX_SLICER", "FX_INSERT", "FX_INSERT2", "FX_DELAY", "FX_REVERB"], channel=1)
walk("SEL", ["SEL_KEY", "SEL_KEY2"], channel=1)
walk("EDIT", ["EDIT_EDIT1", "EDIT_EDIT2", "EDIT_FILTER", "EDIT_VOICE", "EDIT_VOICE2"], channel=1)
walk("EDIT", ["FM6_EDIT1", "FM6_EDIT2", "FM6_OPERATOR", "FM6_OPENV", "FM6_OPERATOR2"], channel=2)
walk("EDIT", ["DRUM_EDIT1", "DRUM_EDIT2", "DRUM_LANES", "DRUM_LANES2"], channel=0)
walk("ARP", ["ARP_ARP", "ARP_ARP2"], channel=1)
walk("SEQ", ["SEQ_PATTERN", "SEQ_NOTES"], channel=1)
walk("SEQ", ["DRUM_PATTERN", "SEQ_DRUMS"], channel=0)
walk("GLO", ["GLO_GLOBAL", "GLO_MASTER", "GLO_TEXTURE", "GLO_TEXTURE2", "GLO_DRUMS"], channel=0)
walk("SAVE", ["SAVE_SONG", "SAVE_SCENE", "SAVE_CHAIN", "SAVE_TOOLS"], channel=1)   # (a SAVE tap opens SONG)
page("SAVE_PRESETS", "tap SAVE", "turn SELECT -2")
page("SAVE_USER", "tap SAVE", "turn SELECT -1")
for i, n in enumerate(["SET_SCREEN", "SET_LIGHTS", "SET_AUDIO", "SET_SEQ", "SET_SYSTEM", "SET_MIDI1", "SET_MIDI2",
                       "SET_MIDI3", "SET_MIDI4", "SET_KEYS", "SET_BLUETOOTH"]):   # SETTINGS: HOME held
    page(n, *(["press HOME", "wait 800", "release HOME"] + ["turn SELECT 1"] * i))
for b in ["GLO", "SEL", "EDIT", "SEQ", "SAVE", "FX", "ARP"]:
    page("LAYER_" + b, "press %s" % b, "wait 700")
page("REC_READY", "tap REC")
page("MAIN", "channel 0")
page("MAIN_PLAYING", "channel 0", "wait 900")   # (the demo left it playing)


DEMO = [                                 # a beat on the kit (SEQ held, steps), a line on channel 2, so pages have notes
    "press SEQ", "wait 30",
    "key 0 down 100", "wait 20", "key 0 up", "key 7 down 100", "wait 20", "key 7 up",
    "key 14 down 100", "wait 20", "key 14 up", "key 21 down 100", "wait 20", "key 21 up",
    "key 3 down 100", "wait 20", "key 3 up", "key 12 down 100", "wait 20", "key 12 up",
    "release SEQ", "wait 30",
    "turn ALGO 1", "tap REC",
    "key 0 down 100", "wait 200", "key 0 up", "wait 300", "key 4 down 100", "wait 200", "key 4 up", "wait 300",
    "key 7 down 100", "wait 200", "key 7 up", "wait 600", "tap REC", "wait 100", "tap PLAY", "wait 100",
    "turn ALGO -1", "wait 50",
]


def script(light):
    out = ["wait 900"] + DEMO
    if light:                            # SETTINGS, SCREEN: THEME LIGHT
        out += ["press OCT-", "wait 30", "tap GLO", "wait 30", "release OCT-", "turn K1 1",
                "press HOME", "wait 800", "release HOME"]
    ch = 0
    for name, steps in SHOTS:
        real = []
        for st in steps:                 # "channel N": ALGO a detent at a time from where it is (it wraps round)
            if st.startswith("channel "):
                to = int(st.split()[1])
                real += ["turn ALGO %d" % (1 if to > ch else -1)] * abs(to - ch)
                ch = to
            else:
                real.append(st)
        steps = real
        out += steps + [SETTLE, "shot %s.png" % name]
        held = [s.split()[1] for s in steps if s.startswith("press ") and s.split()[1] != "HOME"]
        held = [b for b in held if "release %s" % b not in steps]
        out += ["release %s" % b for b in held]
        if "tap PLAY" in steps:
            out += ["tap PLAY"]
        if name == "REC_READY":
            out += ["tap REC", "wait 50"]
            continue                     # (that tap left REC READY for MAIN)
        if name.startswith("SET_"):      # (SETTINGS: HOME held again leaves)
            out += ["press HOME", "wait 800", "release HOME", "wait 50"]
        elif not name.startswith(("MAIN", "LAYER_")):   # (HOME on MAIN itself would turn the visualiser on)
            out += ["tap HOME", "wait 50"]
    return "\n".join(out) + "\n"


def main():
    if len(sys.argv) < 2:
        sys.exit(__doc__)
    out_dir = os.path.abspath(sys.argv[1])
    os.makedirs(out_dir, exist_ok=True)
    path = os.path.join(out_dir, "pages.txt")
    with open(path, "w") as f:
        f.write(script("--light" in sys.argv))
    subprocess.run([HOST, path, out_dir], check=True)
    if "--small" in sys.argv:
        from PIL import Image
        for f in os.listdir(out_dir):
            if f.endswith(".png"):
                im = Image.open(os.path.join(out_dir, f)).convert("RGB")
                im.quantize(colors=64, method=Image.Quantize.MEDIANCUT, dither=Image.Dither.NONE).save(
                    os.path.join(out_dir, f), optimize=True)


if __name__ == "__main__":
    main()
