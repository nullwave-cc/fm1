#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
"""Give fm1-base's generated pages (the emulator, the installer, the landing page, the manual, the companion)
nullwave's look, as on nullwave.cc:
- the colours: black and white with the red accent, dark by default and light when the system asks for it.
  fm1-base's templates name their colours differently page by page, so every name they use is mapped here; the
  rule is appended to each page's <head> and wins over the template's own :root rules
- the type: the system sans-serif, monospace for code
- the logotype (assets/logotype-*.svg) in place of a heading or the site bar's link that is only the name, else
  heading the page. The files are copied to DIR/assets/ and every page links them relatively, so the pages work from
  any folder
  tools/brand.py DIR ...      every .html under each directory
"""
import os
import re
import shutil
import sys
from pathlib import Path

LOGOS = Path(__file__).resolve().parents[1] / "assets"
LOGO_DARK, LOGO_LIGHT = "logotype-dark.svg", "logotype-light.svg"     # white "null" for dark pages, black for light
RED = "#E5202E"
DARK = {"bg": "#000000", "fg": "#FFFFFF", "dim": "#8A8A8A", "line": "#333333", "card": "#111111",
        "tint": "#3A0B0F", "scheme": "dark"}
LIGHT = {"bg": "#FFFFFF", "fg": "#000000", "dim": "#5C5C5C", "line": "#D0D0D0", "card": "#F2F2F2",
         "tint": "#FBD3D6", "scheme": "light"}
MARK = "<!-- nullwave look (tools/brand.py) -->"
SANS = 'system-ui, -apple-system, "Segoe UI", Roboto, sans-serif'
MONO = "ui-monospace, SFMono-Regular, Menlo, monospace"


def tokens(t):
    """every colour name fm1-base's pages use, set from one scheme"""
    v = {
        # page and text
        "bg": t["bg"], "ground": t["bg"], "paper": t["bg"],
        "fg": t["fg"], "ink": t["fg"],
        "dim": t["dim"], "muted": t["dim"],
        "rule": t["line"],
        "card": t["card"], "sheet": t["card"],
        # accents: the red, and the template's second colours as plain text colours
        "accent": RED, "on-accent": "#FFFFFF", "bad": RED, "focus": t["fg"],
        "hot": RED, "hot-ink": RED, "hot-t": t["tint"],
        "teal": t["fg"], "cool": t["dim"], "cool-t": t["card"],
    }
    return "".join(f"--{k}: {c}; " for k, c in v.items()) + f"color-scheme: {t['scheme']};"


# the emulator's FM-1: its lights and its start button, which fm1-base draws in its own coral, teal and blue
EMU = (":root:root:root { --panel-lit: #E5202E; --panel-lit-ink: #FFFFFF; --panel-lit-white: #E5202E; "
       "--panel-lit-black: #E5202E; --panel-dim: #5A0D12; --panel-dim-white: #F4A6AB; --panel-dim-black: #5A0D12; }\n"
       ".start button { background: #E5202E !important; color: #FFFFFF !important; }\n"
       ".held .btnface, .held .keyface { stroke: #E5202E !important; }\n"
       ".latched .btnface, .latched .keyface { stroke: #FFFFFF !important; }\n")

TYPE = (f"body {{ font-family: {SANS} !important; -webkit-font-smoothing: antialiased; }}\n"
        f"h1, h2, h3, button, input, select, textarea {{ font-family: {SANS} !important; }}\n"
        "h1, h2, h3 { letter-spacing: -0.01em; }\n"
        f"code, pre, kbd, samp {{ font-family: {MONO} !important; }}\n"
        f"a {{ text-decoration-color: {RED}; text-underline-offset: 3px; }}\n"
        ".nw-head { margin: 0 0 1.75rem; }\n"
        ".nw-head a, h1.nw-logo a { display: inline-block; }\n"
        "a.home { text-decoration: none; line-height: 0; }\n"
        "a.home img { height: 36px; width: auto; display: block; }\n"
        "a.home[aria-current=page] { visibility: hidden; }\n"      # the front page has it large
        ".nw-head img { height: 48px; width: auto; display: block; }\n"
        "h1.nw-logo { margin-top: 0; line-height: 0; }\n"
        "h1.nw-logo img { height: 72px; width: auto; display: block; }\n")


def style(emu):
    r = ":root:root:root"            # above the templates' :root, :root:not([data-theme]) and :root[data-theme]
    return (f"{MARK}\n<style>\n"
            f"{r} {{ {tokens(DARK)} }}\n"
            f"@media (prefers-color-scheme: light) {{ {r}:not([data-theme=\"dark\"]) {{ {tokens(LIGHT)} }} }}\n"
            f"{r}[data-theme=\"light\"] {{ {tokens(LIGHT)} }}\n"
            f"{r}[data-theme=\"dark\"] {{ {tokens(DARK)} }}\n"
            + TYPE + (EMU if emu else "") +
            "</style>\n")


def logo(assets, height):
    return (f'<picture><source srcset="{assets}/{LOGO_LIGHT}" media="(prefers-color-scheme: light)">'
            f'<img src="{assets}/{LOGO_DARK}" width="{round(height * 562 / 200)}" height="{height}" alt="nullwave">'
            "</picture>")


def brand(path, root):
    html = path.read_text(encoding="utf-8")
    if MARK in html or "</head>" not in html or "<body" not in html:
        return False
    assets = Path(os.path.relpath(root / "assets", path.parent)).as_posix()
    home = Path(os.path.relpath(root, path.parent)).as_posix() + "/"
    html = html.replace("</head>", style("PANEL_CSS" in html) + "</head>", 1)
    h1 = re.search(r"<h1>\s*nullwave\s*</h1>", html)
    if h1:                                      # a heading that is only the name: the logotype in its place
        html = html[:h1.start()] + f'<h1 class="nw-logo">{logo(assets, 72)}</h1>' + html[h1.end():]
    home_link = re.search(r'(<a class="home"[^>]*>)\s*nullwave\s*(</a>)', html)
    if home_link:                               # the site's bar: the logotype for the name
        html = (html[:home_link.start()] + home_link.group(1) + logo(assets, 36) + home_link.group(2)
                + html[home_link.end():])
    elif not h1:                                # else the logotype heads the page's column, back to the front page
        at = re.search(r"<main[^>]*>", html) or re.search(r"<body[^>]*>", html)
        head = f'\n<header class="nw-head"><a href="{home}">{logo(assets, 48)}</a></header>'
        html = html[:at.end()] + head + html[at.end():]
    path.write_text(html, encoding="utf-8")
    return True


def main(args):
    if not args:
        sys.exit(__doc__.strip().splitlines()[-1].strip())
    done = 0
    for root in map(Path, args):
        if not root.is_dir():
            sys.exit(f"brand: {root} is not a directory")
        (root / "assets").mkdir(exist_ok=True)
        for f in (LOGO_DARK, LOGO_LIGHT):
            shutil.copyfile(LOGOS / f, root / "assets" / f)
        for f in sorted(root.rglob("*.html")):
            done += brand(f, root)
    print(f"brand: nullwave's look on {done} page(s)")


if __name__ == "__main__":
    main(sys.argv[1:])
