#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
# Copyright (C) 2026 nullwave contributors
"""Every page, layer, graph and view of nullwave as a screenshot from the host simulator.

    python3 tools/page_shots.py OUT_DIR [--light] [--small] [--hd]

Builds nothing: run ./build.sh --host first. Writes OUT_DIR/<NAME>.png for each entry of SHOTS and
OUT_DIR/pages.txt (the simulator script it ran). The names are what tools/hd_shots.py and the docs' pictures use.
--small: each PNG cut to 64 colours (needs Pillow). --hd: the script also asks the HD render for each shot
("expect hd:NAME 1"), for tools/hd_shots.py, which runs it in the HD simulator.
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


# the default song: ANALOG, FM6, SAMPLE, WHEEL, PHASE on channels 1-5 (0-4 here), the kit on channel 6 (5)
walk("ENV", ["ENV_ENV", "ENV_DEST"], channel=0)
walk("LFO", ["LFO_LFO", "LFO_LFO2", "LFO_DEST"], channel=0)
walk("FX", ["FX_FX", "FX_FILTER", "FX_SLICER", "FX_INSERT", "FX_INSERT2", "FX_DELAY", "FX_REVERB"], channel=0)
walk("SEL", ["SEL_KEY", "SEL_KEY2"], channel=0)
walk("EDIT", ["EDIT_EDIT1", "EDIT_EDIT2", "EDIT_FILTER", "EDIT_VOICE", "EDIT_VOICE2"], channel=0)
walk("EDIT", ["FM6_EDIT1", "FM6_EDIT2", "FM6_OPERATOR", "FM6_OPENV", "FM6_OPERATOR2"], channel=1)
walk("EDIT", ["DRUM_EDIT1", "DRUM_EDIT2", "DRUM_LANES", "DRUM_LANES2"], channel=5)
walk("ARP", ["ARP_ARP", "ARP_ARP2"], channel=0)
walk("SEQ", ["SEQ_PATTERN", "SEQ_NOTES"], channel=0)
walk("SEQ", ["DRUM_PATTERN", "SEQ_DRUMS"], channel=5)
walk("GLO", ["GLO_GLOBAL", "GLO_MASTER", "GLO_TEXTURE", "GLO_TEXTURE2", "GLO_DRUMS"], channel=5)
walk("SAVE", ["SAVE_SONG", "SAVE_SCENE", "SAVE_CHAIN", "SAVE_TOOLS"], channel=0)   # (a SAVE tap opens SONG)
page("SAVE_PRESETS", "tap SAVE", "turn SELECT -2")
page("SAVE_USER", "tap SAVE", "turn SELECT -1")
for i, n in enumerate(["SET_SCREEN", "SET_SCREEN2", "SET_LIGHTS", "SET_AUDIO", "SET_SEQ", "SET_SYSTEM", "SET_MIDI1", "SET_MIDI2",
                       "SET_MIDI3", "SET_MIDI4", "SET_KEYS", "SET_BLUETOOTH"]):   # SETTINGS: HOME held
    page(n, *(["press HOME", "wait 800", "release HOME"] + ["turn SELECT 1"] * i))
for b in ["GLO", "SEL", "EDIT", "SEQ", "SAVE", "FX", "ARP"]:
    page("LAYER_" + b, "press %s" % b, "wait 700")
page("REC_READY", "tap REC")
SHIFT_HOME = ["press OCT-", "wait 30", "tap HOME", "wait 30", "release OCT-"]
page("LOOP_MAIN", "channel 0", *SHIFT_HOME, "wait 600")     # (the demo playing; SHIFT + HOME again after)
page("PLAY_MAIN", "channel 0", "wait 600")
page("PLAY_ANALOG", "channel 0", SETTLE, "key 0 down 110", "wait 140")
page("PLAY_FM6", "channel 1", SETTLE, "key 7 down 120", "key 11 down 100", "key 14 down 90", "key 18 down 80", "wait 260")
page("PLAY_DRUM", "channel 5", SETTLE, "key 0 down 120", "key 4 down 100", "key 7 down 90", "wait 70")
page("PLAY_CHORD", "channel 1", "tap SEL", "turn K2 2", "turn K3 2", "turn K4 1", "tap HOME", "wait 1500",
     "key 3 down 100", "wait 30", "key 7 down 110", "wait 220")
page("NAME", "channel 0", "press OCT-", "wait 30", "tap SAVE", "wait 30", "release OCT-")
page("MAIN", "channel 0")
page("MAIN_PLAYING", "channel 0", "wait 900")   # (the demo left it playing)


# channels 1-5: PRESETS turned so far, then a ready name (KNOB 3): 1 BASS (SQR BASS), 2 ARP (BELL), 3 LEAD (SAX),
# 4 KEYS (FULL ORGAN), 5 PAD (STRING); 6 is DRUMS already
NAMES = [(2, 2), (1, 7), (2, 4), (0, 3), (2, 5)]


def name_steps():
    out = []
    for pre, k in NAMES:                 # the sound, then SHIFT + SAVE, a ready name, SAVE; then the next channel
        out += ["turn PRESET 1"] * pre
        out += ["press OCT-", "wait 30", "tap SAVE", "wait 30", "release OCT-", "turn K3 %d" % k, "tap SAVE", "wait 30",
                "turn ALGO 1"]
    return out + ["turn ALGO 1"]         # (round to channel 1 again)


DEMO = [                                 # a beat on the kit (channel 6: SEQ held, steps), a line on channel 1, so pages
    "turn ALGO -1",                      # have notes
    "press SEQ", "wait 30",
    "key 0 down 100", "wait 20", "key 0 up", "key 7 down 100", "wait 20", "key 7 up",
    "key 14 down 100", "wait 20", "key 14 up", "key 21 down 100", "wait 20", "key 21 up",
    "key 3 down 100", "wait 20", "key 3 up", "key 12 down 100", "wait 20", "key 12 up",
    "release SEQ", "wait 30",
    "turn ALGO 1", "tap REC",
    "key 0 down 100", "wait 200", "key 0 up", "wait 300", "key 4 down 100", "wait 200", "key 4 up", "wait 300",
    "key 7 down 100", "wait 200", "key 7 up", "wait 600", "tap REC", "wait 100", "tap PLAY", "wait 100",
    "wait 50",
]


def script(light, hd=False):
    out = ["wait 1800"] + name_steps() + DEMO
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
        live = name.startswith("PLAY_") and any(st.startswith("key ") for st in steps)   # (a note's moment: no wait)
        out += steps + ([] if live else [SETTLE]) + ["shot %s.png" % name]
        if hd:
            out += ["expect hd:%s 1" % name]
        held = [s.split()[1] for s in steps if s.startswith("press ") and s.split()[1] != "HOME"]
        held = [b for b in held if "release %s" % b not in steps]
        out += ["release %s" % b for b in held]
        keys = [s.split()[1] for s in steps if s.startswith("key ") and s.endswith(tuple("0123456789")) and " down " in s]
        out += ["key %s up" % k for k in keys if "key %s up" % k not in steps]
        if name == "LOOP_MAIN":
            out += SHIFT_HOME + ["wait 50"]
        if name == "PLAY_CHORD":         # (the song's key as it was)
            out += ["tap SEL", "turn K2 -2", "turn K3 -2", "turn K4 -1", "tap HOME", "wait 50"]
            continue
        if "tap PLAY" in steps:
            out += ["tap PLAY"]
        if name == "NAME":               # (HOME: as it was)
            out += ["tap HOME", "wait 50"]
            continue
        if name == "REC_READY":
            out += ["tap REC", "wait 50"]
            continue                     # (that tap left REC READY for MAIN)
        if name.startswith("SET_"):      # (SETTINGS: HOME held again leaves)
            out += ["press HOME", "wait 800", "release HOME", "wait 50"]
        elif not name.startswith(("MAIN", "LAYER_", "PLAY_", "LOOP_")):   # (HOME on MAIN itself would turn the visualiser on)
            out += ["tap HOME", "wait 50"]
    return "\n".join(out) + "\n"


def main():
    if len(sys.argv) < 2:
        sys.exit(__doc__)
    out_dir = os.path.abspath(sys.argv[1])
    os.makedirs(out_dir, exist_ok=True)
    path = os.path.join(out_dir, "pages.txt")
    with open(path, "w") as f:
        f.write(script("--light" in sys.argv, "--hd" in sys.argv))
    host = os.environ.get("NW_HOST", HOST)
    subprocess.run([host, path, out_dir], check=True)
    if "--small" in sys.argv:
        from PIL import Image
        for f in os.listdir(out_dir):
            if f.endswith(".png"):
                im = Image.open(os.path.join(out_dir, f)).convert("RGB")
                im.quantize(colors=64, method=Image.Quantize.MEDIANCUT, dither=Image.Dither.NONE).save(
                    os.path.join(out_dir, f), optimize=True)


if __name__ == "__main__":
    main()
