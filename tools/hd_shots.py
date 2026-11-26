#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
# Copyright (C) 2026 nullwave contributors
"""Every picture of the screen in the guide, the manual and the website's promo set, in HD: the host simulator built
with the HD render (app/hd.c, -DNW_HD through tools/cc_hd), the same scripts as the 1:1 pictures, each shot also taken
at 4x (960 x 960, full colour, the real Barlow faces).

    python3 tools/hd_shots.py [OUT_DIR]

Writes:
  app/docs/img/NAME.png                         480 x 480 (2x), full-colour PNG: what GitHub shows, the manual's
  app/docs/img/hd/NAME.webp                     960 x 960 (4x) WebP: the built manual's srcset
  docs/img/quick/NAME-dark.png / -light.png     the quick start's (docs/QUICK-START.md), 480, and .webp at 960
  docs/img/promo/NAME-dark.png / -light.png      960 x 960 full-colour PNG, and .webp beside each: the website's set,
                                                 its file names (assets/screens/)
The 1:1 pictures stay the tests' own (app/build/host). Needs Pillow; the HD faces come from tools/gen_hd_font.py.
"""
import os
import re
import shutil
import subprocess
import sys
from pathlib import Path

from PIL import Image

ROOT = Path(__file__).resolve().parents[1]
HD = ROOT / "app" / "build" / "hd"
MANUAL, QUICK = ROOT / "app" / "docs" / "MANUAL.md", ROOT / "docs" / "QUICK-START.md"
PROMO = ("MAIN_PLAYING", "SEQ_DRUMS", "LAYER_GLO", "LAYER_FX", "FM6_OPERATOR", "graph-env", "NAME", "PLAY_MAIN",
         "LOOP_MAIN", "PLAY_FM6", "PLAY_DRUM", "PLAY_CHORD", "PLAY_ANALOG", "LAYER_SAVE", "SET_SCREEN")
TESTS = ("graphs", "seqviews", "legends", "logo", "recmodes")   # (the guide's pictures some tests take)


def run(cmd, **kw):
    subprocess.run(cmd, check=True, cwd=ROOT, **kw)


def build():
    """the HD simulator, beside the normal one (which is built again after)"""
    HD.mkdir(parents=True, exist_ok=True)
    run([sys.executable, "tools/gen_hd_font.py", str(HD / "hd_font.h")])
    run(["./build.sh", "--host"], env=dict(os.environ, CC=str(ROOT / "tools" / "cc_hd")), stdout=subprocess.DEVNULL)
    (HD / "nullwave_hd").unlink(missing_ok=True)   # (a fresh file: macOS kills a binary copied over a running one's)
    shutil.copyfile(ROOT / "app" / "build" / "host" / "nullwave_host", HD / "nullwave_hd")
    os.chmod(HD / "nullwave_hd", 0o755)
    run(["./build.sh", "--host"], stdout=subprocess.DEVNULL)


def with_hd(script):
    return re.sub(r"^shot (\S+)\.png$", r"shot \1.png\nexpect hd:\1 1", script, flags=re.M)


def shots(out):
    env = dict(os.environ, NW_HOST=str(HD / "nullwave_hd"))
    for theme, flag in (("dark", []), ("light", ["--light"])):
        d = out / theme
        d.mkdir(parents=True, exist_ok=True)
        run([sys.executable, "tools/page_shots.py", str(d), "--hd", *flag], env=dict(env, NW_HD_DIR=str(d)),
            stdout=subprocess.DEVNULL)
    d = out / "tests"
    d.mkdir(parents=True, exist_ok=True)
    for t in TESTS + ("screens",):
        s = d / f"{t}.txt"
        s.write_text(with_hd((ROOT / "app" / "tests" / f"{t}.txt").read_text()))
        subprocess.run([str(HD / "nullwave_hd"), str(s), str(d)], cwd=ROOT, env=dict(os.environ, NW_HD_DIR=str(d)),
                       stdout=subprocess.DEVNULL)


def load(p):
    return Image.frombytes("RGB", (960, 960), p.read_bytes())


def save(im, png, webp=None, size=480):
    png.parent.mkdir(parents=True, exist_ok=True)
    (im if size == 960 else im.resize((size, size), Image.LANCZOS)).save(png, optimize=True)
    if webp:
        webp.parent.mkdir(parents=True, exist_ok=True)
        im.save(webp, quality=88, method=6)


def place(out):
    """each picture where the docs want it: the manual's (img/NAME.png), the quick start's (both themes), the promo
    set"""
    found = {}
    for theme in ("dark", "light", "tests"):
        for p in (out / theme).glob("*@4x.rgb"):
            found.setdefault((theme, p.name[:-7]), p)

    def shot(name, theme):               # (a page's, else a test's: "X" dark, "X-light" or "logo-light-X" light)
        if (theme, name) in found:
            return found[(theme, name)]
        if theme == "dark":
            return found.get(("tests", name))
        return found.get(("tests", name + "-light")) or found.get(("tests", name.replace("logo-", "logo-light-")))
    done = []
    img = ROOT / "app" / "docs" / "img"
    for n in sorted(set(re.findall(r"\(img/([\w-]+)\.png\)", MANUAL.read_text()))):
        src = shot(n, "dark")
        if src:
            save(load(src), img / f"{n}.png", img / "hd" / f"{n}.webp")
            done.append(f"manual {n}")
        else:
            print(f"hd_shots: no HD shot for the manual's {n}")
    q = ROOT / "docs" / "img" / "quick"
    for n in sorted({x if x != "@splash" else "logo-1100"
                     for x in re.findall(r"^screen: (@?[\w-]+)$", QUICK.read_text(), flags=re.M)}):
        for theme in ("dark", "light"):
            src = shot(n, theme)
            if src:
                save(load(src), q / f"{n}-{theme}.png", q / f"{n}-{theme}.webp")
                done.append(f"quick {n}-{theme}")
            else:
                print(f"hd_shots: no HD shot for the quick start's {n}-{theme}")
    pr = ROOT / "docs" / "img" / "promo"
    for f in pr.glob("*"):
        f.unlink()
    for n in PROMO:
        for theme in ("dark", "light"):
            src = shot(n, theme)
            if src:
                save(load(src), pr / f"{n}-{theme}.png", pr / f"{n}-{theme}.webp", size=960)
                done.append(f"promo {n}-{theme}")
            else:
                print(f"hd_shots: no HD shot for the promo's {n}-{theme}")
    return done


def system_screens():
    """fm1-base's own screens, the device draws them whatever the theme (device/main_fm1.c): UPDATE MODE (REC held at
    power-on) and SAFE MODE, at 960 x 960 for the promo set (the installer's drawings)"""
    from PIL import ImageDraw, ImageFont
    fonts = ROOT / "base" / "assets" / "fonts"
    big = ImageFont.truetype(str(fonts / "BarlowSemiCondensed-Bold.ttf"), 60)
    small = ImageFont.truetype(str(fonts / "BarlowSemiCondensed-SemiBold.ttf"), 52)
    hi, white = (255, 64, 64), (255, 255, 255)
    screens = {
        "UPDATE_MODE": [(440, "UPDATE MODE (REC)", small, white)],
        "SAFE_MODE": [(72, "nullwave SAFE MODE", big, hi), (208, "IT CRASHED TWICE", small, white),
                      (288, "WHILE STARTING.", small, white), (408, "NO SOUND. USB IS ON:", small, white),
                      (488, "REINSTALL FROM THE", small, white), (568, "WEB INSTALLER.", small, white),
                      (688, "PLAY: TRY AGAIN", small, hi), (760, "REC: UPDATE MODE", small, hi)],
    }
    pr = ROOT / "docs" / "img" / "promo"
    for name, lines in screens.items():
        im = Image.new("RGB", (960, 960), (0, 0, 0))
        d = ImageDraw.Draw(im)
        for y, t, f, c in lines:
            w = d.textlength(t, font=f)
            d.text(((960 - w) / 2, y), t, font=f, fill=c)
        for theme in ("dark", "light"):
            save(im, pr / f"{name}-{theme}.png", pr / f"{name}-{theme}.webp", size=960)


def install_screens(out):
    """nullwave's own screens around an install: INSTALLING (the moment an install begins, app_update_begin) and the
    first start of a new build, INSTALLED and its version under the logo (NW_FRESH: the host's first start)"""
    d = out / "install"
    d.mkdir(parents=True, exist_ok=True)
    s = d / "install.txt"
    s.write_text("wait 1250\nexpect hd:INSTALLED 1\nwait 1800\nexpect draw_update 1\nexpect hd:INSTALLING 1\n"
                 "expect draw_factory 1\nexpect hd:FACTORY_RESET 1\n"
                 "reboot\nwait 1800\npress HOME\nwait 900\nrelease HOME\nwait 50\nturn SELECT 5\nwait 400\n"
                 "expect hd:SET_SYSTEM 1\n"
                 "press HOME\nwait 900\nrelease HOME\nwait 50\ntap SAVE\nwait 30\nturn SELECT 2\nwait 30\n"
                 "tapkey 0 100\ntapkey 2 100\ntapkey 2 100\ntapkey 0 100\ntapkey 4 100\nwait 1600\n"
                 "expect hd:SAVE_CHAIN 1\n")
    subprocess.run([str(HD / "nullwave_hd"), str(s), str(d)], cwd=ROOT,
                   env=dict(os.environ, NW_HD_DIR=str(d), NW_FRESH="1"), stdout=subprocess.DEVNULL)
    pr = ROOT / "docs" / "img" / "promo"
    for name in ("INSTALLING", "INSTALLED", "FACTORY_RESET", "SET_SYSTEM", "SAVE_CHAIN"):
        im = load(d / f"{name}@4x.rgb")
        for theme in ("dark", "light"):   # (the theme the FM-1 had: drawn dark here, the device's first start)
            save(im, pr / f"{name}-{theme}.png", pr / f"{name}-{theme}.webp", size=960)


def main():
    out = Path(sys.argv[1]) if len(sys.argv) > 1 else HD / "shots"
    if out.exists():
        shutil.rmtree(out)
    build()
    shots(out)
    done = place(out)
    system_screens()
    install_screens(out)
    print(f"hd_shots: {len(done)} pictures")


if __name__ == "__main__":
    main()
