#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
# Copyright (C) 2026 nullwave contributors
"""The quick start page (nullwave.cc/fm1/quick-start/) from docs/QUICK-START.md: nine steps, each the FM-1 drawn in
lines with what to press lit (the site's device.js), the screen you get (docs/img/quick, HD: tools/hd_shots.py), two
lines. tools/site_look.py then gives it the site's nav, footer and stylesheet.

    python3 tools/quick_start.py SITE_DIR VERSION
"""
import html
import re
import shutil
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SRC = ROOT / "docs" / "QUICK-START.md"
IMG = ROOT / "docs" / "img" / "quick"

CSS = """
.qs-hero{padding:88px 0 40px}
.qs-steps{display:flex;flex-wrap:wrap;gap:8px;margin-top:28px;list-style:none;padding:0}
.qs-steps a{display:inline-flex;align-items:center;gap:8px;min-height:36px;padding:0 14px;border:1px solid var(--edge);
  border-radius:999px;font-size:14px;color:var(--text);background:none}
.qs-steps a:hover{border-color:var(--fg);color:var(--fg)}
.qs-steps .n{font:12px var(--mono);color:var(--red)}
.qs{padding:72px 0;border-top:1px solid var(--line)}
.qs .tour{grid-template-columns:1.25fr 1fr;gap:48px;margin-top:0;align-items:center}
.qs .stick{position:static}
.qs .fm1-3d{aspect-ratio:3/2}
.qs .feats [data-feature],.qs .feats [data-feature]:first-child{min-height:0;opacity:1;border-left:0;padding:0}
.qs .kicker{margin-bottom:14px}
.qs h2{font-size:clamp(26px,3vw,34px);margin-bottom:14px}
.qs p{max-width:30em}
.qs-press{display:flex;flex-wrap:wrap;align-items:center;gap:6px;margin:18px 0 0;padding:0;list-style:none;
  font:12px var(--mono);letter-spacing:.04em;color:var(--dim)}
.qs-press li{display:inline-flex;align-items:center;gap:6px}
.qs-press li+li::before{content:"→";color:var(--faint)}
.qs-press b{font-weight:400;color:var(--text);border:1px solid var(--edge);border-radius:6px;padding:2px 7px;
  transition:color .3s,border-color .3s}
.qs-press li[aria-current] b{color:var(--fg);border-color:var(--red)}
.qs-shot{margin-top:24px;max-width:240px}
.qs-shot img{width:100%;aspect-ratio:1;border:1px solid var(--edge);border-radius:12px;background:#000}
.qs-pn{display:flex;justify-content:space-between;gap:16px;margin-top:40px;font-size:14px}
.qs-end{padding:72px 0 96px;border-top:1px solid var(--line)}
@media(max-width:860px){
  .qs-hero{padding:48px 0 24px}
  .qs{padding:48px 0}
  .qs .tour{grid-template-columns:1fr;gap:20px}
  .qs .fm1-3d{aspect-ratio:16/10}
  .qs-shot{max-width:200px}
}
"""

QS_JS = """<script>
/* each drawing's step marked in its press list as it plays (device.js fires fm1step) */
document.querySelectorAll('.qs .fm1-3d').forEach(function (d) {
  var list = document.getElementById(d.getAttribute('aria-describedby'));
  d.addEventListener('fm1step', function (e) {
    var items = list ? list.children : [];
    for (var k = 0; k < items.length; k++)
      if (k === e.detail.index) items[k].setAttribute('aria-current', 'step'); else items[k].removeAttribute('aria-current');
  });
});
</script>"""

NAMES = {"OCT-": "OCT−", "OCT+": "OCT+", "PLAY STOP": "PLAY", "KNOB1,KNOB2,KNOB3,KNOB4": "KNOB 1–4", "KEYS": "keys",
         "SHIFT": "SHIFT", "SCREEN": "the screen"}
BUTTONS = {"FX", "SEL", "ENV", "LFO", "EDIT", "GLO", "HOME", "SAVE", "ARP", "SEQ", "PLAY STOP", "REC", "OCT-", "OCT+"}


def device_steps(s):
    """the press list as device.js steps (DESIGN.md): each group lit, its button pressed, the step's screen at the end
    (the splash before it on the first step)"""
    out = []
    for k, g in enumerate(s["press"]):
        parts = [p.strip() for p in g.split(",")]
        if "OCT-" in parts and len(parts) > 1:   # (OCT− held with another: SHIFT, as the drawing names it)
            parts = ["SHIFT" if p == "OCT-" else p for p in parts]
        st = {"on": ",".join(parts)}
        press = [p for p in parts if p in BUTTONS]
        if press:
            st["press"] = press[-1]
        st["screen"] = s["screen"] if k == len(s["press"]) - 1 or s["screen"] != "@splash" else "@splash"
        out.append(st)
    return out


def parse(text):
    steps = []
    for part in re.split(r"^## ", text, flags=re.M)[1:]:
        title, _, body = part.partition("\n")
        press = re.search(r"^press: (.*)$", body, re.M).group(1)
        screen = re.search(r"^screen: (.*)$", body, re.M).group(1).strip()
        words = [ln for ln in body.strip().splitlines() if ln and not ln.startswith(("press:", "screen:"))]
        steps.append({"title": title.strip(), "press": [g.strip() for g in press.split("|")], "screen": screen,
                      "text": " ".join(words)})
    return steps


def label(group):
    g = NAMES.get(group)
    if g:
        return g
    return " + ".join(NAMES.get(p, p if p != "KEYS" else "a key") for p in group.split(","))


def inline(t):                           # **bold** only
    return re.sub(r"\*\*([^*]+)\*\*", r"<strong>\1</strong>", html.escape(t, quote=False))


def page(steps, version):
    n = len(steps)
    nav = "".join(f'<li><a href="#step-{i + 1}"><span class="n">{i + 1:02d}</span>{html.escape(s["title"])}</a></li>'
                  for i, s in enumerate(steps))
    body = []
    import json
    for i, s in enumerate(steps):
        dsteps = html.escape(json.dumps(device_steps(s)), quote=True)
        press = "".join(f"<li><b>{html.escape(label(g))}</b></li>" for g in s["press"])
        shot = s["screen"] if s["screen"] != "@splash" else "logo-1100"
        pic = (f'<picture><source media="(prefers-color-scheme: dark)" type="image/webp" '
               f'srcset="img/{shot}-dark.png 480w, img/{shot}-dark.webp 960w" sizes="240px">'
               f'<source media="(prefers-color-scheme: dark)" srcset="img/{shot}-dark.png">'
               f'<source type="image/webp" srcset="img/{shot}-light.png 480w, img/{shot}-light.webp 960w" sizes="240px">'
               f'<img src="img/{shot}-light.png" width="240" height="240" loading="lazy" '
               f'alt="The FM-1\'s screen after this step"></picture>')
        prev = f'<a href="#step-{i}">← {html.escape(steps[i - 1]["title"])}</a>' if i else "<span></span>"
        nxt = (f'<a href="#step-{i + 2}">{html.escape(steps[i + 1]["title"])} →</a>' if i + 1 < n
               else '<a href="#done">What next →</a>')
        body.append(
            f'<section class="qs" id="step-{i + 1}"><div class="wrap"><div class="tour">'
            f'<div class="stick"><div class="fm1-3d" data-mode="still" data-view="three" data-steps="{dsteps}" '
            f'data-interval="1900" aria-describedby="press-{i + 1}"></div></div>'
            f'<div class="feats"><div>'
            f'<div class="kicker"><b>{i + 1:02d}</b> Step {i + 1} of {n}</div><h2>{html.escape(s["title"])}</h2>'
            f'<p>{inline(s["text"])}</p><ol class="qs-press" id="press-{i + 1}" aria-label="What to press, in order">{press}</ol>'
            f'<figure class="qs-shot">{pic}</figure></div></div></div>'
            f'<nav class="qs-pn" aria-label="Steps">{prev}{nxt}</nav></div></section>')
    return f"""<!doctype html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>Quick start · nullwave fm1</title>
<meta name="description" content="nullwave fm1 in nine steps: switch on, play, change the sound, scales and chords, record a loop, drums, scenes, a song in order, settings.">
<meta name="color-scheme" content="dark light">
<style>{CSS}</style>
</head>
<body>
<header class="wrap qs-hero"><div class="kicker"><b>●</b> Version {html.escape(version)}</div>
<h1>Quick start</h1>
<p class="lead">Nine steps, from switching on to a song in order. The drawing lights what to press; the screen shows
what you get.</p>
<ol class="qs-steps">{nav}</ol></header>
{"".join(body)}
<section class="qs-end" id="done"><div class="wrap"><div class="kicker">What next</div><h2>Everything else is in the
manual.</h2><p class="lead">Every button, page and setting, with the full walk through scales and chords.</p>
<div class="btns"><a class="btn solid" href="../manual/">Open the manual <span class="arr">→</span></a>
<a class="btn" href="../emulator/">Try it in the browser</a></div></div></section>
{QS_JS}
</body>
</html>
"""


def main(site, version):
    out = Path(site) / "quick-start"
    if out.exists():
        shutil.rmtree(out)
    (out / "img").mkdir(parents=True)
    steps = parse(SRC.read_text(encoding="utf-8"))
    for s in steps:
        for f in IMG.glob((s["screen"] if s["screen"] != "@splash" else "logo-1100") + "-*"):
            shutil.copyfile(f, out / "img" / f.name)
    (out / "index.html").write_text(page(steps, version), encoding="utf-8")
    print(f"quick start: {len(steps)} steps, {out}")


if __name__ == "__main__":
    main(*sys.argv[1:3])
