#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
# Copyright (C) 2026 nullwave contributors
"""The FM-1 pages in nullwave.cc's design, built to live under nullwave.cc/fm1/.

    python3 tools/site_look.py SITE_DIR VERSION PKG [BASE]

Run after tools/brand.py on the built site. BASE (default /fm1/) is where the pages are served. BASE itself is the
FM-1 page of the site repository, so make_site's front page is dropped and every link back to it points at BASE; the
emulator moves from emu/ to emulator/.

The design is the site's (its DESIGN.md): style.css (the tokens and the pieces), device.js (the FM-1 drawn in lines,
lighting named controls in red, .w the red "wave" of the name where it is big), its nav and its footer (the FM-1
page's, assets/web/footer.html). This repository keeps a copy in assets/web/, refreshed from
../site-redesign beside it when that is there (NW_SITE_KEEP=1: the copy as it is), so CI builds without it. The copy
is published at BASE/assets/. The installer, the emulator and the companion keep their own workings; their colours
and controls are mapped onto the design. The guide and the manual get the design's type in place of their own, and
drawings of the FM-1 where a combination of buttons is explained.
"""
import os
import re
import shutil
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
DESIGN = ROOT.parent / "site-redesign"
VENDOR = ROOT / "assets" / "web"
SHARED = {"style.css": "style.css", "device.js": "device.js"}
SHARED.update({f"{n}-{t}.svg": f"assets/{n}-{t}.svg" for n in ("mark", "wordmark", "lockup") for t in ("dark", "light")})
DOCS = ("guide", "manual")

# the drawings: (page, what they follow (a regex on the built page; "</main>": they end it), [(the parts lit, what to
# hold, what it does)], the screen shown). Part names are device.js's: MASTER SELECT PRESETS ALGORITHM KNOB1-4 SCREEN
# KEYS OCT- OCT+ FX SEL ENV LFO EDIT GLO HOME SAVE ARP SEQ, "PLAY STOP", REC
COMBOS = [
    ("guide", r"<p><strong>SHIFT\.</strong>.*?</p>\s*<ul>.*?</ul>",
     [("OCT-,GLO", "SHIFT + GLO", "Hold OCT− (SHIFT) and tap GLO: the SETTINGS screen."),
      ("OCT-,SAVE", "SHIFT + SAVE", "Hold OCT− and tap SAVE: name the selected channel.")], "MAIN_PLAYING"),
    ("guide", r"<p><strong>Names\.</strong>.*?</p>",
     [("OCT-,SAVE,KNOB1,KNOB2,KNOB3,KNOB4", "SHIFT + SAVE, then the knobs",
       "KNOB 1 the cursor, KNOB 2 the letter, KNOB 3 a ready name, KNOB 4 clear. SAVE keeps it, HOME leaves it.")],
     "NAME"),
    ("guide", r"<p><strong>GLO, the mix\.</strong>.*?</p>",
     [("GLO,KEYS,KNOB1,KNOB2,KNOB3,KNOB4", "Hold GLO",
       "The keys mute, solo and fill; KNOB 1–4 are the levels of channels 1–4.")], "LAYER_GLO"),
    ("guide", r"<p><strong>EDIT, the black keys\.</strong>.*?</p>\s*<ul>.*?</ul>",
     [("EDIT,KEYS", "EDIT + a black key", "OP1–OP6, PIT, GLO, MONO and POLY, by the panel's legends.")],
     "FM6_OPERATOR"),
    ("guide", r"<p><strong>SEQ\.</strong>.*?</p>",
     [("SEQ,KEYS,SELECT", "Hold SEQ", "The keys are the 16 steps of a bar; SELECT picks the bar.")], "SEQ_DRUMS"),
    ("manual", r"<h2[^>]*>Installing, and going back to stock</h2>\s*<p>.*?</p>",
     [("REC,SCREEN", "REC held at power-on", "The FM-1's update mode: the installer can write it again."),
      ("HOME,SCREEN", "HOME held at power-on", "Every setting back to how it was installed; the songs stay."),
      ("HOME,SELECT,KNOB1,PLAY STOP", "SETTINGS → SYSTEM 2, KNOB 1, hold PLAY",
       "FACTORY RESET: nullwave as just installed.")], "INSTALLED"),
    ("manual", r"<h2[^>]*>The screen</h2>\s*<ul>.*?</ul>",
     [("OCT-,HOME", "SHIFT + HOME", "MAIN as PLAY (the wave, the chord named) or LOOP (four channels, their steps)."),
      ("ALGORITHM", "ALGORITHM", "The channel PLAY shows and the keys play.")], "PLAY_FM6"),
    ("manual", r"<h2[^>]*>SHIFT combinations</h2>\s*<div class=\"table\"[^>]*>.*?</div>",
     [("OCT-,SAVE", "SHIFT + SAVE", "NAME: the selected channel's name."),
      ("OCT-,GLO", "SHIFT + GLO", "The SETTINGS screen."),
      ("OCT-,REC", "SHIFT + REC", "REC MODE: OVERDUB or REPLACE."),
      ("OCT-,HOME", "SHIFT + HOME", "MAIN as PLAY or LOOP."),
      ("OCT-,PLAY STOP", "SHIFT + PLAY", "HOLD: the keys latch their notes, for a drone."),
      ("OCT-,OCT+", "OCT− + OCT+", "Octave 0.")], "MAIN_PLAYING"),
    ("manual", r"<h2[^>]*>SETTINGS</h2>\s*<p>.*?</p>",
     [("HOME", "Hold HOME", "The SETTINGS screen; SELECT its pages, HOME tapped the next group, held again out."),
      ("HOME,SELECT,KNOB1,KNOB2,KNOB3,KNOB4", "SELECT, KNOB 1–4", "A page, its four rows: SYSTEM shows CPU and BATTERY.")],
     "SET_SYSTEM"),
    ("manual", r"<h2[^>]*>Keys, scales and chords</h2>\s*<p>.*?</p>",
     [("SEL,KEYS", "Hold SEL + a key", "The song's root."),
      ("SEL,KNOB1,KNOB2,KNOB3,KNOB4", "Hold SEL, KNOB 1–4", "CHORD, SCALE, KEYS, TRANSPOSE."),
      ("KEYS", "A white key", "With CHORD on, a chord of the scale, named under the wave.")], "PLAY_CHORD"),
    ("manual", r"<h2[^>]*>Punch-in effects \(FX held\)</h2>\s*<div class=\"table\"[^>]*>.*?</div>",
     [("FX,KEYS", "Hold FX + keys", "27 punch-in effects while held; several stack."),
      ("FX,OCT-,OCT+", "FX + OCT− / OCT+", "A reverb throw / an echo throw, while held.")], "LAYER_FX"),
    ("manual", r"<h2[^>]*>Sequencer</h2>\s*<ul>.*?</ul>",
     [("REC,KEYS", "REC, play", "Record on the channel while it plays; REC again stops."),
      ("OCT-,REC", "SHIFT + REC", "OVERDUB ↔ REPLACE."),
      ("SEQ,KEYS,SELECT", "Hold SEQ", "The keys are the bar's 16 steps; SELECT the bar."),
      ("OCT-,PLAY STOP,KEYS", "SHIFT + PLAY, then keys", "HOLD: each note stays on; held over the loop while recording, a drone."),
      ("EDIT,OCT-", "EDIT + OCT−", "Undo.")], "LOOP_MAIN"),
    ("manual", r"<h2[^>]*>Scenes and songs</h2>\s*<p>.*?</p>",
     [("SAVE,KEYS", "Hold SAVE + a key", "Launch that scene (held 1 s: what plays stored into it)."),
      ("SAVE,SELECT,KEYS", "SAVE → CHAIN, then keys", "The song in order: A1 A2 A2 A1 A3."),
      ("SAVE,KEYS,PLAY STOP", "SAVE + A#, play", "SONG REC: each scene you launch written into the order.")],
     "SAVE_CHAIN"),
    ("manual", r"<h2[^>]*>The buttons</h2>\s*<div class=\"table\"[^>]*>.*?</div>",
     [("GLO,KEYS", "Hold GLO", "The mix layer: mutes, solos, fills, MIDI LEARN, tap tempo."),
      ("EDIT,KEYS", "EDIT + the black keys", "OP1–OP6, PIT, GLO, MONO, POLY; on a kit, the drums' mutes."),
      ("SAVE,KEYS", "Hold SAVE", "The scene layer: launch, chain and copy scenes.")], "LAYER_GLO"),
]

CSS = """
/* the page between the site's nav and footer */
.nw-page{max-width:760px;margin:0 auto;padding:72px 32px 96px}
@media(max-width:860px){.nw-page{padding:48px 20px 72px}}
.skip{position:absolute;left:16px;top:-100px;z-index:20;background:var(--fg);color:var(--bg);padding:8px 14px;border-radius:0 0 8px 8px}
.skip:focus{top:0}
:focus-visible{outline:2px solid var(--red);outline-offset:3px}
/* the FM-1 drawn for a combination: the drawing, then what to hold (one after another as you scroll, if several) */
.nw-combo{margin:40px 0 48px;padding:32px 0;border-top:1px solid var(--line);border-bottom:1px solid var(--line)}
.tour.nw-combo{grid-template-columns:1.5fr 1fr;gap:40px}
.nw-combo .stick{top:96px}
.nw-combo .fm1-3d{aspect-ratio:3/2}
.nw-combo .feats [data-feature],.nw-combo .feats [data-feature]:first-child{min-height:0;padding:16px 0 16px 24px}
.nw-combo .feats.many [data-feature]{min-height:30vh}
.nw-combo .feats h3{font-size:18px;margin-bottom:6px}
.nw-combo .feats p{font-size:15px;margin:0}
@media(max-width:860px){.tour.nw-combo{grid-template-columns:1fr;gap:0}.nw-combo .stick{top:64px}
  .nw-combo .feats.many [data-feature]{min-height:36vh}}
"""

# the guide and the manual: the design's type in place of manual_html.py's own
DOC = """
body,h1,h2,h3,button,input,select,textarea{font-family:var(--sans)!important}   /* (brand.py's faces undone) */
code,pre,kbd,samp{font-family:var(--mono)!important}
.nw-doc>a.home{display:none}
.nw-doc h1{margin:0 0 28px}
.nw-doc>p:first-of-type{font-size:clamp(17px,1.6vw,19px)}
.nw-doc p{margin:0 0 16px}
.nw-doc h2{font-size:clamp(24px,2.8vw,30px);max-width:none;margin:80px 0 20px;padding-top:40px;border-top:1px solid var(--line)}
.nw-doc h2 .num{font:13px var(--mono);color:var(--red);margin-right:16px;vertical-align:.25em;letter-spacing:0}
.nw-doc h3{margin:36px 0 10px}
.nw-doc ul,.nw-doc ol{margin:0 0 18px;padding-left:1.25em}
.nw-doc li{margin:6px 0}
.nw-doc li::marker{color:var(--faint)}
.nw-doc strong{color:var(--fg);font-weight:500}
.nw-doc code{font:.86em var(--mono);color:var(--fg);border:1px solid var(--edge);border-radius:4px;padding:0 4px}
.nw-doc pre{border:1px solid var(--edge);border-radius:8px;padding:14px 16px;overflow-x:auto;margin:0 0 20px;line-height:1.5}
.nw-doc pre code{border:0;padding:0;font-size:13px}
.nw-doc hr{display:none}
.nw-doc .table{overflow-x:auto;margin:12px 0 32px;border-top:1px solid var(--edge)}
.nw-doc table{border-collapse:collapse;width:100%;font-size:14px;line-height:1.5}
.nw-doc th,.nw-doc td{text-align:left;vertical-align:top;padding:10px 16px 10px 0;border-bottom:1px solid var(--edge)}
.nw-doc th{font:12px var(--mono);letter-spacing:.06em;text-transform:uppercase;color:var(--faint);font-weight:400}
.nw-doc td:first-child{color:var(--fg);white-space:nowrap}
.nw-doc .figs{display:grid;grid-template-columns:repeat(auto-fill,minmax(160px,240px));gap:16px;margin:28px 0 36px}
.nw-doc figure{margin:28px 0 36px}
.nw-doc .figs figure{margin:0}
.nw-doc figure.shot img{width:100%;max-width:240px;aspect-ratio:1;border:1px solid var(--edge);border-radius:12px;image-rendering:pixelated;background:#000}
.nw-doc figcaption{font:12px var(--mono);letter-spacing:.06em;text-transform:uppercase;color:var(--faint);margin-top:12px}
.nw-doc figcaption .nb{text-transform:none}   /* (the name stays lowercase) */
.nw-doc nav.contents{position:static;z-index:auto;background:none;backdrop-filter:none;-webkit-backdrop-filter:none;border:0;border-top:1px solid var(--edge);border-bottom:1px solid var(--edge);margin:40px 0;padding:24px 0}
.nw-doc nav.contents h2{font:12px var(--mono);letter-spacing:.1em;text-transform:uppercase;color:var(--faint);margin:0 0 14px;padding:0;border:0}
.nw-doc nav.contents ol{columns:2;column-gap:40px;margin:0;font-size:15px}
@media(max-width:600px){.nw-doc nav.contents ol{columns:1}.nw-doc td:first-child{white-space:normal}}
"""

# the tools (installer, emulator, companion): their own variables on the design's tokens, their buttons the design's
# pills, lines in place of cards
def tokens(theme):
    """the site's colour tokens (style.css's :root, dark, or its light media query), as one declaration list"""
    css = (VENDOR / "style.css").read_text(encoding="utf-8")
    if theme == "light":
        m = re.search(r"prefers-color-scheme:\s*light\)\s*\{\s*:root\{([^}]*)\}", css)
    else:
        m = re.search(r":root\{([^}]*)\}", css)
    return ";".join(d.strip() for d in (m.group(1) if m else "").split(";") if d.strip().startswith("--"))


_ALIAS = ("--ground:var(--bg);--paper:var(--bg);--ink:var(--text);--muted:var(--dim);--rule:var(--edge);"
          "--card:transparent;--sheet:transparent;--accent:var(--red);--on-accent:#fff;--bad:var(--red);"
          "--hot:var(--red);--hot-ink:var(--red);--teal:var(--fg);--cool:var(--dim);--focus:var(--fg)")
APP = """
body{margin:0;padding:0;background:var(--bg);color:var(--text);font:400 16px/1.6 var(--sans)!important}
h1,h2,h3,button,input,select,textarea{font-family:var(--sans)!important}
main h1{font-size:clamp(36px,5vw,56px);line-height:1.02;letter-spacing:-.04em;font-weight:500;margin:0 0 24px}
main h2{font-size:22px;line-height:1.2;letter-spacing:-.02em;font-weight:500;margin:48px 0 14px;max-width:none}
main button,main label.pick{display:inline-flex;align-items:center;justify-content:center;gap:8px;min-height:40px;
  padding:0 20px;border-radius:999px;font:500 15px var(--sans);background:none;color:var(--fg);
  border:1px solid var(--edge);cursor:pointer;transition:border-color .25s,background .25s,color .25s}
main button:hover:not(:disabled),main label.pick:hover{border-color:var(--fg)}
main button:disabled{opacity:.45}
main .small,main .lede{color:var(--dim)}
main .card,main section.card{background:none;border:0;border-top:1px solid var(--line);border-radius:0;padding:28px 0 8px;margin:0}
main section{padding:0;border-top:0}
main .status{background:none;border:0;border-left:1px solid var(--red);border-radius:0;padding:4px 0 4px 16px}
main .row,dialog .row{display:flex;flex-wrap:wrap;align-items:center;gap:8px 12px;grid-template-columns:none;padding:0;border:0;position:static}
dialog .row{justify-content:flex-end}
progress{appearance:none;-webkit-appearance:none;height:2px;border:0;background:var(--edge);accent-color:var(--red)}
progress::-webkit-progress-bar{background:var(--edge)}
progress::-webkit-progress-value{background:var(--red)}
progress::-moz-progress-bar{background:var(--red)}
dialog{background:var(--bg);color:var(--fg);border:1px solid var(--edge);border-radius:16px;box-shadow:none}
dialog::backdrop{background:color-mix(in srgb,var(--bg) 70%,transparent)}
dialog button{display:inline-flex;align-items:center;min-height:40px;padding:0 20px;border-radius:999px;font:500 15px var(--sans);
  background:none;color:var(--fg);border:1px solid var(--edge);cursor:pointer}
dialog button:not(.plain){background:var(--fg);color:var(--btn-fg);border-color:var(--fg)}
"""


def app_tokens():                        # (brand.py's colours of the tools' own pages, the site's again)
    return (":root:root:root:root:root{" + tokens("dark") + ";" + _ALIAS + ";color-scheme:dark}\n"
            "@media (prefers-color-scheme: light){:root:root:root:root:root{" + tokens("light") + ";color-scheme:light}}\n")
PAGE_CSS = {
    "installer": """
main{max-width:1120px!important;margin:0 auto;padding:0 32px 72px!important}
@media(max-width:860px){main{padding:0 20px 56px!important}}
.in-hero{padding:72px 0 40px}
.in-hero .lang{margin-top:6px}
.in-steps{display:flex;flex-wrap:wrap;gap:8px;margin:28px 0 0;padding:0;list-style:none}
.in-steps a{display:inline-flex;align-items:center;gap:8px;min-height:36px;padding:0 14px;border:1px solid var(--edge);
  border-radius:999px;font-size:14px;color:var(--text);background:none}
.in-steps a span{font:12px var(--mono);color:var(--faint)}
.in-steps a[aria-current]{border-color:var(--red);color:var(--fg)}
.in-steps a[aria-current] span{color:var(--red)}
.in-step,.in-more{padding:64px 0;border-top:1px solid var(--line)}
.in-grid{display:grid;grid-template-columns:1fr 1.1fr;gap:56px;align-items:center}
.in-text{max-width:34em}
.in-text h2,.in-more h2{font-size:clamp(26px,3vw,34px);margin:0 0 16px}
.in-dev .fm1-3d{aspect-ratio:3/2}
.in-step[aria-current] .kicker b{color:var(--red)}
.in-demo{position:fixed;left:50%;bottom:16px;transform:translateX(-50%);z-index:30;display:flex;align-items:center;gap:12px;
  padding:8px 8px 8px 18px;border:1px solid var(--edge);border-radius:999px;background:var(--bg)}
.in-demo .kicker{margin:0;color:var(--red)}
.in-demo .lab{font-size:14px;color:var(--fg);min-width:6em}
.in-demo button{min-height:36px;padding:0 16px;border-radius:999px;border:1px solid var(--edge);background:none;
  color:var(--fg);font:500 14px var(--sans);cursor:pointer}
.in-more>.small,.in-more>p{max-width:40em}
@media(max-width:860px){.in-hero{padding:40px 0 24px}.in-step,.in-more{padding:40px 0}
  .in-grid{grid-template-columns:1fr;gap:20px}.in-dev{order:-1}.in-dev .fm1-3d{aspect-ratio:16/10}}
main #go{background:var(--fg);color:var(--btn-fg);border-color:var(--fg);min-height:46px;padding:0 28px}
main #go:hover:not(:disabled){background:var(--red);border-color:var(--red);color:#fff}
main .lang{float:right;min-height:0;border:0;padding:4px 0;color:var(--dim);font-size:13px;text-decoration:underline}
main .backup{border:0;border-left:1px solid var(--red);border-radius:0;padding:4px 0 4px 16px}
main .backup strong{color:var(--fg)}
main #log,main textarea{border:1px solid var(--edge);border-radius:8px}
.nw-install .trk{stroke:var(--edge)}
.nw-install .arc{stroke:var(--red)}

body>footer{max-width:none;padding:72px 0 40px}
.nw-credits{max-width:640px;margin:0 auto;padding:0 32px 64px;font-size:13px;color:var(--faint)}
.nw-credits p{color:var(--faint);margin:0 0 6px}
@media(max-width:860px){.nw-credits{padding:0 20px 48px}}
""",
    "emulator": """
body>header{display:none}
main{max-width:1120px!important;margin:0 auto;padding:0 32px 72px}
@media(max-width:860px){main{padding:0 20px 56px}}
.em-hero{padding:64px 0 32px}
.em-hero h1{margin-bottom:0}
main .devicebox{border:1px solid var(--edge);border-radius:20px;padding:16px;background:none}
main .bar{margin-top:20px}
@media(max-width:860px){.em-hero{padding:40px 0 20px}main .devicebox{padding:8px;border-radius:14px}}
main .bar button,main .speak button{min-height:34px;padding:0 14px;font-size:14px}
main .bar button[aria-pressed="true"]{background:var(--fg);color:var(--btn-fg);border-color:var(--fg)}
main .speak{background:none;border:0;border-top:1px solid var(--line);border-radius:0;padding:24px 0 0}
main .start button{background:var(--fg)!important;color:var(--btn-fg)!important;border:0;min-height:52px;padding:0 32px;font:500 17px var(--sans)!important;letter-spacing:0}
main .start button:hover{background:var(--red)!important;color:#fff!important}
main .help{border-top:1px solid var(--line);padding-top:28px}
main kbd{background:none;border:1px solid var(--edge)}
.speak summary h2,.help h2{font:500 18px var(--sans)!important;letter-spacing:-.01em}
""",
    "companion": """
main{max-width:760px!important;margin:0 auto;padding:64px 32px 72px}
@media(max-width:860px){main{padding:40px 20px 56px}}
main #connect{background:var(--fg);color:var(--btn-fg);border-color:var(--fg);min-height:46px;padding:0 28px}
main #connect:hover:not(:disabled){background:var(--red);border-color:var(--red);color:#fff}
main .chip{border-color:var(--edge)}
main td{border-bottom-color:var(--edge)}
""",
}


def nav(base, here):
    a = base + "assets/"
    items = ""
    for text, href, cls in (("Overview", base, "wide"), ("Install", base + "installer/", ""),
                            ("Quick start", base + "quick-start/", ""), ("Manual", base + "manual/", "")):
        items += (f'<li{" class=" + chr(34) + cls + chr(34) if cls else ""}><a href="{href}"'
                  f'{" aria-current=" + chr(34) + "page" + chr(34) if href == here else ""}>{text}</a></li>')
    return ('<!--nw:nav--><nav><div class="wrap">\n'
            '  <a class="brand" href="/" aria-label="nullwave home">'
            f'<picture><source media="(prefers-color-scheme: dark)" srcset="{a}mark-dark.svg">'
            f'<img src="{a}mark-light.svg" width="30" height="30" alt=""></picture>'
            f'<picture><source media="(prefers-color-scheme: dark)" srcset="{a}wordmark-dark.svg">'
            f'<img src="{a}wordmark-light.svg" height="24" style="width:auto" alt="nullwave"></picture></a>\n'
            f"  <ul>{items}</ul>\n</div></nav><!--/nw:nav-->")


def footer(base):
    """the site's footer, as its FM-1 page has it (the copy: assets/web/footer.html), its assets under BASE and the
    emulator beside the installer"""
    html = (VENDOR / "footer.html").read_text(encoding="utf-8").strip()
    html = html.replace('"/assets/', f'"{base}assets/').replace(' srcset="/assets/', f' srcset="{base}assets/')
    html = html.replace(f'<li><a href="{base}manual/">Quick start</a></li><li><a href="{base}guide/">Guide</a></li>',
                        f'<li><a href="{base}quick-start/">Quick start</a></li><li><a href="{base}manual/">Manual</a></li>')
    html = html.replace(f'<li><a href="{base}installer/">Installer</a></li>',
                        f'<li><a href="{base}installer/">Installer</a></li><li><a href="{base}emulator/">Emulator</a></li>', 1)
    return "<!--nw:foot-->" + html + "<!--/nw:foot-->"


def combo(items, screens):
    feats = "".join(f'<div data-feature data-on="{on}"><h3 class="nw-ft">{k}</h3><p>{p}</p></div>' for on, k, p in items)
    return ('<!--nw:combo--><div class="tour nw-combo"><div class="stick"><div class="fm1-3d" data-mode="tour" '
            f'data-screens="{screens}"></div></div><div class="feats{" many" if len(items) > 1 else ""}">{feats}'
            "</div></div><!--/nw:combo-->")


def links(html, base, backup=True):
    """the links between the pages, from their own relative form to the base; ../ alone is the FM-1 page. Without a
    backup page (fm1.toml has no backup = true) the installer's link to it goes: the installer hides it anyway"""
    if not backup:
        html = re.sub(r'\s*<a class="baklink"[^>]*>.*?</a>', "", html, flags=re.S)
    html = re.sub(r'href="(?:\.\./)+(emu|installer[^/"]*|guide|manual|companion|firmware)/',
                  lambda m: f'href="{base}{"emulator" if m.group(1) == "emu" else m.group(1)}/', html)
    html = re.sub(r'href="' + re.escape(base) + r'emu/', f'href="{base}emulator/', html)
    return re.sub(r'href="(?:\.\./)+(?:index\.html)?"', f'href="{base}"', html)


def installer_flow(html, version):
    """the installer as four steps (connect, back up, install, done), each with the FM-1 drawn doing it (device.js
    data-steps), then by hand, back to the official firmware, and reporting a problem; its own workings (fm1-base's
    installer: the ids, the texts it translates) moved, not changed"""
    m = re.search(r"<main>(.*)</main>", html, flags=re.S)
    if not m or "in-flow" in m.group(1):
        return html
    b = m.group(1)

    def cut(pat):
        nonlocal b
        x = re.search(pat, b, flags=re.S)
        if not x:
            return ""
        b = b[:x.start()] + b[x.end():]
        return x.group(0)
    ring = cut(r"<style>\s*\.nw-install.*?</script>")
    lang = cut(r'<button type="button" class="lang".*?</button>')
    cut(r"<h1>.*?</h1>")
    intro = cut(r'<p data-t="intro">.*?</p>')
    warn = cut(r'<p data-t="warn">.*?</p>')
    direct = cut(r'<p class="small" data-t="direct">.*?</p>')
    brick = cut(r'<p class="small"><span data-t="brick">.*?</p>')
    back = cut(r'<p class="small" data-t="back">.*?</p>')
    mvave = cut(r'<p class="small"><a href="https://www.m-vave.com/download".*?</p>')
    mboot = cut(r'<div id="mbootopt".*?</div>')
    bak = cut(r'<p class="backup">.*?</p>')
    go = cut(r'<p><button type="button" id="go".*?</p>')
    prog = cut(r'<label class="sr" for="bar".*?</progress>')
    status = cut(r'<p id="status".*?</p>')
    alert = cut(r'<p id="alert".*?</p>')
    log = cut(r'<pre id="log">.*?</pre>')
    pick = cut(r'<div id="portpick".*?</div>')
    stock = cut(r'<h2 data-t="stockh">.*?<p><button type="button" id="stockgo".*?</p>')
    report = cut(r'<h2 data-t="reporth">.*?</textarea>')
    home = cut(r'<p><a href="\.\./" data-t="manual">.*?</p>')
    rest = b.strip()

    def dev(steps, label):
        import json
        return (f'<div class="fm1-3d" data-mode="still" data-view="three" data-interval="2200" '
                f'data-steps="{html_escape(json.dumps(steps))}" role="img" aria-label="{label}"></div>')
    step = lambda n, k, title, body, d: (
        f'<section class="in-step" id="{k}" data-step="{n}"><div class="in-grid"><div class="in-text">'
        f'<div class="kicker"><b>{n:02d}</b> Step {n} of 4</div><h2>{title}</h2>{body}</div>'
        f'<div class="in-dev">{d}</div></div></section>')
    out = (
        '<div class="in-flow">'
        f'<header class="in-hero">{lang}<div class="kicker"><b>●</b> Version {version}</div>'
        f'<h1>Install nullwave</h1>{intro}'
        '<ol class="in-steps" aria-label="The steps"><li><a href="#connect"><span>01</span>Connect</a></li>'
        '<li><a href="#backup"><span>02</span>Back up</a></li><li><a href="#install"><span>03</span>Install</a></li>'
        '<li><a href="#done"><span>04</span>Done</a></li></ol></header>'
        + step(1, "connect", "Connect the FM-1.",
               '<p>Plug the FM-1 into this computer with a USB data cable and switch it on. Use Chrome or Edge; '
               'when the browser asks to use MIDI devices, allow it.</p>' + direct,
               dev([{"on": "SCREEN", "screen": "@splash"}, {"on": "SCREEN", "screen": "PLAY_MAIN"}],
                   "The FM-1 switched on, showing nullwave or its own firmware"))
        + step(2, "backup", "Back it up.",
               '<p>Installing can lose what is saved on the FM-1. Before the first install, keep a full backup '
               'of it.</p>' + bak + mboot,
               dev([{"on": "SAVE", "screen": "PLAY_MAIN"}, {"on": "SAVE", "press": "SAVE", "screen": "PLAY_MAIN"}],
                   "The FM-1, nothing to press: the backup is made on the computer"))
        + step(3, "install", "Install.",
               '<p>Press Install. The ring here fills as it writes. On the FM-1, nullwave says INSTALLING for a '
               'moment, then the FM-1\'s own updater takes the screen: a cream screen with UPDATING, DO NOT '
               'UNPLUG and a bar filling. That screen is the FM-1\'s, not nullwave\'s, and it means all is well '
               '(it may also stay dark). Leave the cable in until this page says Done.</p>'
               + warn + go + ring + prog + status + alert + log + pick,
               dev([{"on": "SCREEN", "screen": "INSTALLING"}], "nullwave handing over to the FM-1's updater"))
        + step(4, "done", "Done.",
               '<p>The FM-1 restarts into nullwave: the logo says INSTALLED and the version, then PLAY. Your songs '
               'and settings stay.</p>'
               '<div class="btns"><a class="btn" href="../quick-start/">Quick start <span class="arr">→</span></a>'
               '<a class="btn" href="../manual/">Manual</a></div>',
               dev([{"on": "SCREEN", "screen": "INSTALLED"}, {"on": "SCREEN,KEYS", "screen": "PLAY_FM6"}],
                   "The FM-1 running nullwave"))
        + '<section class="in-more" id="by-hand"><div class="kicker">If it goes wrong</div><h2>By hand.</h2>'
          '<div class="in-grid"><div class="in-text">'
          '<p><strong>The installer can\'t find the FM-1, or nullwave starts but USB doesn\'t answer:</strong> '
          'switch the FM-1 off, hold <b>REC</b> and switch it on. It shows UPDATE MODE (REC); press Install '
          'again.</p><p><strong>nullwave fails to start twice in a row:</strong> it opens SAFE MODE, USB on. '
          'Press Install here, or PLAY on the FM-1 to try again.</p>'
          '<p><strong>Nothing starts at all:</strong> restore the full backup from step 2.</p>' + brick + '</div>'
          '<div class="in-dev">'
        + dev([{"on": "REC", "screen": "@splash"}, {"on": "REC", "press": "REC", "screen": "UPDATE_MODE"},
               {"on": "PLAY STOP,REC", "screen": "SAFE_MODE"}], "REC held at power-on: update mode")
        + '</div></div></section>'
        + '<section class="in-more" id="factory"><div class="kicker">Start over</div><h2>Reset to factory.</h2>'
          '<div class="in-grid"><div class="in-text">'
          '<p>Puts nullwave back as it was just after installing. It is separate from installing, and it '
          '<strong>erases</strong>:</p><ul class="fr-list"><li>every song, with its scenes, patterns and channel '
          'names</li><li>the 32 user sound slots and the FM6 bank</li><li>every setting (SETTINGS, the octave, the '
          'channel the keys play)</li></ul><p>It keeps nullwave itself, the FM-1\'s own calibration and its '
          'Bluetooth pairings, and it never touches the FM-1\'s updater. On the FM-1 itself: SETTINGS → SYSTEM 2, '
          'turn RESET (KNOB 1), then hold PLAY for 3 seconds.</p>'
          '<div class="fr-box"><div class="btns"><button class="btn" id="fr-connect" type="button">Connect the FM-1'
          '</button><button class="btn" id="fr-backup" type="button" disabled>Download a backup first</button></div>'
          '<label class="fr-type">Type <b>RESET</b> to confirm <input id="fr-word" autocomplete="off" '
          'spellcheck="false" disabled></label><div class="btns"><button class="btn fr-go" id="fr-go" type="button" '
          'disabled>Erase and reset</button></div>'
          '<div class="fr-ring" id="fr-ring" hidden><svg viewBox="0 0 120 120" aria-hidden="true">'
          '<circle cx="60" cy="60" r="52" class="fr-track"/><circle cx="60" cy="60" r="52" class="fr-arc" '
          'id="fr-arc"/><circle cx="60" cy="60" r="30" class="fr-disc"/><line x1="36" y1="84" x2="84" y2="36" '
          'class="fr-slash"/></svg><span id="fr-pc">0%</span></div>'
          '<p class="fr-status" id="fr-status" role="status" aria-live="polite"></p></div></div>'
          '<div class="in-dev">'
        + dev([{"on": "SCREEN", "screen": "FACTORY_RESET"}, {"on": "SCREEN", "screen": "INSTALLED"}],
              "The FM-1 erasing nullwave's songs and settings, then starting as new")
        + '</div></div></section>'
        + f'<section class="in-more" id="official"><div class="kicker">Go back</div>'
          f'<h2>Back to the official firmware.</h2>{back}{mvave}{stock}</section>'
        + f'<section class="in-more" id="report">{report}</section>'
        + (f'<div class="in-rest">{rest}</div>' if rest else "") + home + "</div>" + FLOW_JS + DEMO_JS + FACTORY_JS)
    return html[:m.start()] + "<main>" + out + "</main>" + html[m.end():]


FACTORY_JS = """<style>
.fr-list{margin:.4em 0 1em 1.2em}.fr-box{border:1px solid var(--edge);border-radius:12px;padding:16px;margin-top:12px}
.fr-type{display:block;margin:14px 0 10px}.fr-type input{font:inherit;letter-spacing:.1em;width:9em;margin-left:.5em;
padding:6px 8px;border:1px solid var(--edge);border-radius:8px;background:transparent;color:inherit}
.fr-go:not([disabled]){background:#E5202E;border-color:#E5202E;color:#fff}
.fr-ring{display:flex;align-items:center;gap:14px;margin-top:14px}.fr-ring svg{width:72px;height:72px}
.fr-track{fill:none;stroke:var(--edge);stroke-width:6}.fr-arc{fill:none;stroke:#E5202E;stroke-width:6;
stroke-linecap:round;transform:rotate(-90deg);transform-origin:60px 60px;stroke-dasharray:326.7;stroke-dashoffset:326.7;
transition:stroke-dashoffset .2s}.fr-disc{fill:currentColor}.fr-slash{stroke:var(--bg,#000);stroke-width:5}
.fr-ring span{font-variant-numeric:tabular-nums}.fr-status{min-height:1.4em;margin:.6em 0 0}
</style>
<script type="module">
/* Reset to factory: the FM-1 found over the companion services (its files), a backup of what nullwave keeps (the songs,
   the user sounds, the FM6 bank, the settings) as a zip, RESET typed, then the "factory-reset" file written with its
   words: the FM-1 erases its songs and settings (its ring filling), restarts, and is found again. ?demo=1: walked
   through with nothing sent anywhere. */
import { Companion, findCompanions } from "../companion/companion.js";
import { zip } from "../companion/zip.js";
const $ = (id) => document.getElementById(id);
const demo = new URLSearchParams(location.search).get("demo");
const WORDS = new TextEncoder().encode("nullwave factory reset");
const BACKUP = ["song-a", "song-b", "song-c", "song-d", "song-e", "song-f", "song-g", "song-h", "user-presets",
  "fm6-bank", "settings"];
let c = null, files = [], access = null, port = null;
const say = (t) => { $("fr-status").textContent = t; };
const ring = (v) => { $("fr-ring").hidden = false; $("fr-arc").style.strokeDashoffset = 326.7 * (1 - v / 100);
  $("fr-pc").textContent = Math.round(v) + "%"; };
const wait = (ms) => new Promise((r) => setTimeout(r, ms));
function ready(on) { $("fr-backup").disabled = !on; $("fr-word").disabled = !on; check(); }
function check() { $("fr-go").disabled = !(c || demo) || $("fr-word").value.trim().toUpperCase() !== "RESET"; }
$("fr-word").addEventListener("input", check);
$("fr-connect").addEventListener("click", async () => {
  if (demo) { say("Found nullwave on the FM-1 (demo: nothing is sent)."); ready(true); return; }
  try {
    access = access || await navigator.requestMIDIAccess({ sysex: true });
    const found = (await findCompanions(access)).filter((f) => (f.hello.extras || {}).editor);
    if (!found.length) { say("No FM-1 running nullwave found. Check the USB cable, close other MIDI apps, try again."); return; }
    port = found[0];
    c = new Companion(port.input, port.output);
    files = await c.files();
    say("Found nullwave on the FM-1. Download a backup first, then type RESET.");
    ready(true);
  } catch (e) { say("The FM-1 could not be reached: " + (e.message || e)); }
});
$("fr-backup").addEventListener("click", async () => {
  const day = new Date().toISOString().slice(0, 10), out = [];
  if (demo) { say("Backup: nullwave-backup-" + day + ".zip (demo: not made)."); return; }
  try {
    for (const name of BACKUP) {
      const i = files.findIndex((f) => f.name === name);
      if (i < 0 || !files[i].size) continue;
      say("Backing up " + name + "...");
      out.push({ name: name + ".bin", data: await c.read(i) });
    }
    const a = document.createElement("a");
    a.href = URL.createObjectURL(new Blob([zip(out, new Date())], { type: "application/zip" }));
    a.download = "nullwave-backup-" + day + ".zip";
    a.click();
    say("Backup saved: " + a.download + " (" + out.length + " files). Now type RESET.");
  } catch (e) { say("The backup failed: " + (e.message || e) + ". Nothing was erased."); }
});
$("fr-go").addEventListener("click", async () => {
  if (!confirm("Erase every song, user sound and setting on this FM-1? This can't be undone.")) return;
  $("fr-go").disabled = $("fr-word").disabled = $("fr-backup").disabled = $("fr-connect").disabled = true;
  let v = 0;
  const tick = setInterval(() => { v = Math.min(95, v + 3); ring(v); }, 120);
  try {
    if (!demo) {
      const i = files.findIndex((f) => f.name === "factory-reset");
      if (i < 0) throw new Error("this nullwave has no factory reset: install the latest version first");
      say("Erasing on the FM-1. Don't unplug it.");
      await c.write(i, WORDS);
      c.close(); c = null;
      await wait(4500);
      for (let k = 0; k < 20 && !c; k++) {         /* (it restarts: found again) */
        const found = (await findCompanions(access)).filter((f) => (f.hello.extras || {}).editor);
        if (found.length) c = new Companion(found[0].input, found[0].output);
        else await wait(1000);
      }
    } else {
      say("Erasing on the FM-1 (demo). Don't unplug it.");
      await wait(4000);
    }
    clearInterval(tick); ring(100);
    say(c || demo ? "Done. nullwave starts as new: the FM-1 says INSTALLED, then PLAY." :
      "Done on the FM-1; it hasn't answered since its restart. Switch it off and on if its screen stays dark.");
  } catch (e) {
    clearInterval(tick); $("fr-ring").hidden = true;
    say("Not reset: " + (e.message || e) + ". Nothing was erased.");
    $("fr-connect").disabled = false; ready(!!c);
  }
});
</script>"""


FLOW_JS = """<script>
/* the step you're on, marked: back up while the dialog asks, install while it writes, done when it says so */
addEventListener('DOMContentLoaded', function () {   /* (the dialogs come after main) */
  var bar = document.getElementById('bar'), conf = document.getElementById('confirm'), st = document.getElementById('status');
  var chips = document.querySelectorAll('.in-steps a'), secs = document.querySelectorAll('.in-step');
  var DONE = /done|\u5b8c\u4e86/i;
  function now() {
    var k = 1;
    if (conf && conf.open) k = 2;
    if (bar && +bar.value > 0) k = 3;
    if (st && DONE.test(st.textContent) && +bar.value >= 100) k = 4;
    chips.forEach(function (a, i) { if (i === k - 1) a.setAttribute('aria-current', 'step'); else a.removeAttribute('aria-current'); });
    secs.forEach(function (s, i) { if (i === k - 1) s.setAttribute('aria-current', 'step'); else s.removeAttribute('aria-current'); });
  }
  now();
  setInterval(now, 400);
});
</script>"""


DEMO_JS = """<script>
/* ?demo=1: the install walked through with no FM-1 (to see it): connect, back up, install (the ring filling), done,
   and an error; ?demo=1&stage=N opens at a stage (0..4). Nothing is sent to any device. */
addEventListener('DOMContentLoaded', function () {
  var q = new URLSearchParams(location.search);
  if (!q.get('demo')) return;
  var bar = document.getElementById('bar'), st = document.getElementById('status'), al = document.getElementById('alert');
  var conf = document.getElementById('confirm'), go = document.getElementById('go'), timer = 0, k = 0;
  var S = [
    ['connect', 'Found the FM-1 (USB MIDI). Ready to install nullwave 0.1.0.', ''],
    ['backup', '', ''],
    ['install', 'Writing', ''],
    ['done', 'Done. The FM-1 restarts into nullwave.', ''],
    ['install', '', 'The FM-1 was not found. Check that the USB cable carries data, close other apps or tabs that use MIDI, then press Install again.']];
  function show(i, still) {
    k = (i + S.length) % S.length;
    clearInterval(timer);
    if (conf.open) conf.close();
    al.textContent = S[k][2];
    st.textContent = S[k][1];
    bar.value = k === 3 ? 100 : 0;
    go.disabled = k !== 0;
    if (k === 1) conf.showModal();
    if (k === 2) {
      var v = still ? 62 : 0;
      bar.value = v;
      st.textContent = 'Writing ' + v + '%';
      if (!still) timer = setInterval(function () {
        v = Math.min(100, v + 2); bar.value = v; st.textContent = 'Writing ' + v + '%';
        if (v >= 100) clearInterval(timer);
      }, 120);
    }
    var sec = document.getElementById(S[k][0]);
    if (still) {                         /* (a stage alone under the title, for a picture of it) */
      document.body.classList.add('in-only');
      document.querySelectorAll('.in-step, .in-more').forEach(function (x) { x.hidden = x !== sec; });
    } else if (sec) sec.scrollIntoView({behavior: 'smooth', block: 'start'});
    lab.textContent = ['Connect', 'Back up', 'Install', 'Done', 'An error'][k];
  }
  var box = document.createElement('div');
  box.className = 'in-demo';
  box.innerHTML = '<span class="kicker">Demo</span><span class="lab"></span>'
    + '<button type="button" class="prev">Back</button><button type="button" class="next">Next</button>';
  document.body.appendChild(box);
  var lab = box.querySelector('.lab');
  box.querySelector('.prev').onclick = function () { show(k - 1); };
  box.querySelector('.next').onclick = function () { show(k + 1); };
  setTimeout(function () { show(+(q.get('stage') || 0), q.has('stage')); }, 600);   /* (after the installer's own start) */
});
</script>"""


def html_escape(t):
    return t.replace("&", "&amp;").replace('"', "&quot;").replace("<", "&lt;")


def slug(t):
    return re.sub(r"[^a-z0-9]+", "-", re.sub(r"<[^>]+>", "", t).lower()).strip("-") or "s"


def docnav(html):
    """the guide's and the manual's way round: a contents sidebar (a sheet on phones, behind the bar under the nav),
    an anchor on each heading, the previous and next section at each section's end (fm1-docs.css, fm1-docs.js)"""
    m = re.search(r'(<main id="content"[^>]*>)(.*?)(</main>)', html, flags=re.S)
    if not m:
        return html
    body, seen = m.group(2), set()

    def head(hm):
        tag, attrs, inner = hm.group(1), hm.group(2), hm.group(3)
        if 'id="contents"' in attrs or "nw-ft" in attrs:   # (the contents' own title, a drawing's caption)
            return hm.group(0)
        hid = re.search(r'id="([^"]+)"', attrs)
        hid = hid.group(1) if hid else slug(inner)
        while hid in seen:
            hid += "-2"
        seen.add(hid)
        title = re.sub(r'<span class="num">[^<]*</span>', "", inner)
        title = re.sub(r"<[^>]+>", "", title).strip()
        attrs = re.sub(r'\s*id="[^"]*"', "", attrs)
        return (f'<{tag}{attrs} id="{hid}" data-title="{title}">{inner}<a class="nw-anchor" href="#{hid}" '
                f'aria-label="Link to {title}">#</a></{tag}>')
    body = re.sub(r"<(h2|h3)([^>]*)>(.*?)</\1>", head, body, flags=re.S)
    heads = re.findall(r'<(h2|h3)[^>]*\bid="([^"]+)" data-title="([^"]*)">(.*?)<a class="nw-anchor"', body, flags=re.S)
    secs = [(i, t, re.search(r'<span class="num">([^<]*)</span>', x)) for h, i, t, x in heads if h == "h2"]
    toc, open_sub = "", False
    for h, i, t, x in heads:
        n = re.search(r'<span class="num">([^<]*)</span>', x)
        if h == "h2":
            toc += ("</ol></li>" if open_sub else "</li>" if toc else "")
            open_sub = False
            toc += f'<li><a href="#{i}">{f"<span class=n>{n.group(1)}</span>" if n else ""}{t}</a>'
        else:
            toc += ("" if open_sub else "<ol>") + f'<li><a href="#{i}">{t}</a></li>'
            open_sub = True
    toc += "</ol></li>" if open_sub else "</li>" if toc else ""
    nav_ = f'<nav class="nw-toc" aria-label="Contents"><div class="kicker">Contents</div><ol>{toc}</ol></nav>'

    def pn(k):                           # the end of section k: back to the one before, on to the next, the top
        prev = f'<a class="prev" href="#{secs[k - 1][0]}">← {secs[k - 1][1]}</a>' if k > 0 else "<span></span>"
        nxt = f'<a class="next" href="#{secs[k + 1][0]}">{secs[k + 1][1]} →</a>' if k + 1 < len(secs) else "<span></span>"
        return f'<nav class="nw-pn" aria-label="Sections">{prev}<a class="up" href="#top">Top ↑</a>{nxt}</nav>\n'
    for k in range(len(secs) - 1, -1, -1):
        if k + 1 < len(secs):
            at = body.index(f'id="{secs[k + 1][0]}"')
            at = body.rindex("<h2", 0, at)
        else:
            at = len(body)
        body = body[:at] + pn(k) + body[at:]
    bar = ('<div class="nw-bar"><span class="here" aria-live="polite"></span>'
           '<button type="button" aria-haspopup="dialog">Contents</button></div>')
    sheet = (f'<dialog class="nw-sheet" aria-label="Contents"><div class="top"><div class="kicker">Contents</div>'
             f'<button type="button" class="close">Close</button></div>{nav_}</dialog>')
    out = (f'<div class="nw-docwrap" id="top">{nav_.replace("<nav ", "<nav id=" + chr(34) + "toc" + chr(34) + " ", 1)}'
           f"{bar}{m.group(1)}{body}{m.group(3)}</div>{sheet}")
    return html[:m.start()] + out + html[m.end():]


def srcsets(html, f):
    """a screen's picture with its HD ones beside it (docs/img/.../hd/NAME.webp, tools/hd_shots.py): the sharp one for
    the screen it is on"""
    def one(m):
        src = m.group(1)
        hd = (f.parent / src).parent / "hd" / (Path(src).stem + ".webp")
        if not hd.exists():
            return m.group(0)
        h = str(Path(src).parent / "hd" / (Path(src).stem + ".webp"))
        return f'<img src="{src}" srcset="{src} 480w, {h} 960w" sizes="(max-width: 600px) 72vw, 240px"'
    return re.sub(r'<img src="([^"]+\.png)"', one, html)


def wave(html):
    """the name in the page's title (its h1): its "wave" red, as the site's .w (style.css), one word with no gap;
    everywhere else it stays the colour of its text (bold in running text: bold())"""
    def inner(m):
        parts = re.split(r"(<[^>]+>)", m.group(3))
        parts = [t if t.startswith("<") else re.sub(r"\bnull(wave)\b", r'null<span class="w">\1</span>', t)
                 for t in parts]
        return m.group(1) + "".join(parts) + m.group(4)
    return re.sub(r"(<(h1)\b[^>]*>)(.*?)(</\2>)", inner, html, flags=re.S)


SKIP = ("script", "style", "code", "pre", "textarea", "title", "option", "h1", "h2")


def bold(html):
    """the name in smaller text (running text, nav, footer, buttons, captions): bold, one word, the site's .nb
    (style.css); not in code, in scripts or in the big headings (those have the red wave)"""
    html = html.replace('<b class="nb">nullwave</b>', "nullwave")   # (from plain again: twice is once)
    out, skip = [], []
    for t in re.split(r"(<[^>]+>)", html):
        if t.startswith("<"):
            m = re.match(r"<(/?)([a-zA-Z0-9]+)", t)
            if m and m.group(2).lower() in SKIP and not t.endswith("/>"):
                if m.group(1):
                    if skip and skip[-1] == m.group(2).lower():
                        skip.pop()
                else:
                    skip.append(m.group(2).lower())
            out.append(t)
        elif skip:
            out.append(t)
        else:
            out.append(re.sub(r"(?<![\w/.-])nullwave(?![\w-]|\.[a-z0-9])", '<b class="nb">nullwave</b>', t))
    return "".join(out)


def unstyle(html):
    """what an earlier run put in (this tool's, older ones' too): out, so a page kept from before can be done again"""
    html = re.sub(r"<!--nw:(look|nav|foot|hand)-->.*?<!--/nw:\1-->\n?", "", html, flags=re.S)
    html = re.sub(r"<!--nw:combo-->.*?<!--/nw:combo-->\n?", "", html, flags=re.S)
    html = re.sub(r'\n?<script src="[^"]*assets/(device|fm1-docs)\.js"></script>', "", html)
    html = re.sub(r"<!-- nullwave\.cc look \(tools/site_look\.py\) -->\n(<link[^>]*site\.css\">\n)?", "", html)
    html = re.sub(r'<nav><div class="wrap"><a class="brand".*?</nav>\n?', "", html, flags=re.S)
    return re.sub(r'<footer><div class="wrap foot">.*?</footer>\n?', "", html, flags=re.S)


def look(f, site, base, backup, version=""):
    html = f.read_text(encoding="utf-8")
    if "</head>" not in html:
        return False
    kind = f.parent.name if f.parent != site else ""
    kind = "installer" if kind.startswith("installer") else kind
    html = unstyle(html)
    for pat in (r'<header class="site">.*?</header>', r'<header class="nw-head">.*?</header>',
                r'<h1 class="nw-logo">.*?</h1>', r'<div class="page" style="padding-bottom:0"><header class="top">.*?</header></div>'):
        html = re.sub(pat, "", html, flags=re.S)   # (the bars and logos the templates and brand.py put in)
    html = re.sub(r"<footer>(.*?)</footer>", r'<div class="nw-credits">\1</div>', html, flags=re.S)   # (a page's
    #                                    own footer: its credits, above the site's)
    if kind in DOCS:                     # manual_html.py's own type out, the design's in
        html = re.sub(r"<style>.*?</style>\n?", "", html, count=1, flags=re.S)
        html = html.replace('<main id="content"', '<main id="content" class="nw-page nw-doc"', 1)
        if "nw-docwrap" in html:         # (a page done before: its way round out, put in again below)
            m = re.search(r'<div class="nw-docwrap" id="top">.*?(<main id="content".*?</main>)</div>'
                          r'<dialog class="nw-sheet".*?</dialog>', html, flags=re.S)
            if m:
                main_ = re.sub(r'<nav class="nw-pn".*?</nav>\n', "", m.group(1), flags=re.S)
                main_ = re.sub(r'<a class="nw-anchor"[^>]*>#</a>', "", main_)
                main_ = re.sub(r' data-title="[^"]*"', "", main_)
                html = html[:m.start()] + main_ + html[m.end():]
        css = CSS + DOC
    elif kind == "quick-start":          # (tools/quick_start.py's: the site's pieces, its own few)
        css = CSS
    else:
        css = CSS + app_tokens() + APP + PAGE_CSS.get(kind, "")
    head = ('<!--nw:look-->\n<link rel="icon" href="/favicon.svg" type="image/svg+xml">\n'
            f'<link rel="stylesheet" href="{base}assets/style.css">\n'
            + (f'<link rel="stylesheet" href="{base}assets/fm1-docs.css">\n' if kind in DOCS else "")
            + f"<style>{css}</style>\n<!--/nw:look-->\n")
    html = html.replace("</head>", head + "</head>", 1)
    here = {"installer": base + "installer/", "quick-start": base + "quick-start/", "manual": base + "manual/"}.get(kind, "")
    at = re.search(r"<body[^>]*>", html)
    html = html[:at.end()] + "\n" + nav(base, here) + html[at.end():]
    for page, pat, items, screens in COMBOS:
        if page != kind:
            continue
        m = re.search(pat, html, flags=re.S)
        if not m:
            print(f"site_look: {f.relative_to(site)}: no place for the drawing after {pat[:40]!r}")
            continue
        ins = combo(items, screens)
        if page == "installer":          # (after the installer: a section of the site's width)
            ins = ('<!--nw:hand--><section class="nw-hand"><div class="wrap"><div class="kicker">By hand</div>'
                   "<h2>When the installer can't reach the FM-1.</h2>" + ins + "</div></section><!--/nw:hand-->")
        html = html[:m.end()] + "\n" + ins + "\n" + html[m.end():]
    if kind in DOCS:
        html = srcsets(docnav(html), f)
    if kind == "installer" and f.parent.name == "installer":   # (the current one; the kept dev ones as they were)
        html = installer_flow(html, version)
    if kind == "emulator" and 'class="em-hero"' not in html:   # (a title over the panel, as the site's pages have)
        html = html.replace("<main>", '<main><div class="em-hero"><div class="kicker"><b>●</b> Version ' + version
                            + '</div><h1>Emulator</h1><p class="lead">nullwave fm1 running in the browser: the same '
                            'firmware, with its screen, knobs, buttons and keys. Click or tap the panel, or play it from '
                            'the computer\'s keyboard.</p></div>', 1)
    html = html.replace("</body>", footer(base) + f'\n<script src="{base}assets/device.js"></script>\n'
                        + (f'<script src="{base}assets/fm1-docs.js"></script>\n' if kind in DOCS else "") + "</body>", 1)
    head, body = html.split("<body", 1)
    f.write_text(links(head + "<body" + bold(wave(body)), base, backup), encoding="utf-8")
    return True


def main(site, version, pkg, base="/fm1/"):
    site = Path(site)
    VENDOR.mkdir(parents=True, exist_ok=True)
    fresh = DESIGN.exists() and not os.environ.get("NW_SITE_KEEP")
    if fresh:                            # the design beside this repository: this repository's copy refreshed
        for name, src in SHARED.items():
            shutil.copyfile(DESIGN / src, VENDOR / name)
        page = (DESIGN / "fm1" / "index.html").read_text(encoding="utf-8")   # (its footer, for ours)
        (VENDOR / "footer.html").write_text(page[page.index("<footer"):page.index("</footer>") + 9] + "\n",
                                            encoding="utf-8")
    (site / "assets").mkdir(exist_ok=True)
    for name in SHARED:
        shutil.copyfile(VENDOR / name, site / "assets" / name)
    for name in ("fm1-docs.css", "fm1-docs.js"):   # (fm1's own: the guide's and the manual's way round)
        shutil.copyfile(ROOT / "assets" / name, site / "assets" / name)
    (site / "index.html").unlink(missing_ok=True)     # (BASE is the site repository's FM-1 page)
    if (site / "emu").is_dir():
        if (site / "emulator").exists():
            shutil.rmtree(site / "emulator")
        (site / "emu").rename(site / "emulator")
    backup = (site / "backup").is_dir()
    n = sum(look(f, site, base, backup, version) for f in sorted(site.rglob("*.html")))
    src = DESIGN if fresh else VENDOR.relative_to(ROOT)
    print(f"site_look: the site's design on {n} page(s), at {base}, from {src}")


if __name__ == "__main__":
    if len(sys.argv) not in (4, 5):
        sys.exit(__doc__.strip().splitlines()[2].strip())
    main(*sys.argv[1:])
