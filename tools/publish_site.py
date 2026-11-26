#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
# Copyright (C) 2026 nullwave contributors
"""The fm1 pages for nullwave.cc, as the site keeps them: four folders under fm1/ (installer, emulator, quick-start,
manual), each standing on its own, on the site's shared /style.css, /device.js and /assets/ (its logos, fonts and
screens; DESIGN.md), so nothing else of the site is touched. From the local build of the pages
(app/build/site: ./build.sh --release VERSION, then the site build, tools/site_look.py's look):

    python3 tools/publish_site.py VERSION OUT [--fw-url URL]

writes OUT/fm1/installer/, OUT/fm1/emulator/, OUT/fm1/quick-start/ and OUT/fm1/manual/ (OUT the site's checkout, or
any folder to look at first). What each folder needs and the site has not got goes into it: the installer the
companion's scripts and the firmware it installs, the emulator its fonts and words, the manual and the quick start the
docs' own script and style. --fw-url: the installer loads nullwave's package from there (a release's asset) instead of
its own copy. The screens the pages show are the site's /assets/screens/ (docs/img/promo's, by name): the list of
the ones it needs is printed, to check against the site. Nothing is written outside OUT/fm1/<the four>."""
import re
import shutil
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
BUILT = ROOT / "app" / "build" / "site"
PAGES = ("installer", "emulator", "quick-start", "manual")
SHARED = {"/fm1/assets/style.css": "/style.css", "/fm1/assets/device.js": "/device.js"}
LOGOS = ("mark", "wordmark", "lockup", "logotype")


def rewrite(html, page):
    for a, b in SHARED.items():
        html = html.replace(a, b)
    for n in LOGOS:                      # (the site's own logos: /assets/NAME-THEME.svg)
        for t in ("dark", "light"):
            html = html.replace(f"/fm1/assets/{n}-{t}.svg", f"/assets/{n}-{t}.svg")
            html = html.replace(f"../assets/{n}-{t}.svg", f"/assets/{n}-{t}.svg")
    html = html.replace("/fm1/assets/fm1-docs.css", "fm1-docs.css").replace("/fm1/assets/fm1-docs.js", "fm1-docs.js")
    if page == "installer":              # (the companion's scripts beside it)
        html = html.replace('"../companion/', '"./companion/')
    if page == "emulator":
        html = html.replace("../fonts/", "./fonts/").replace("../words/", "./words/")
    return html


def main():
    if len(sys.argv) < 3:
        sys.exit(__doc__)
    version, out = sys.argv[1], Path(sys.argv[2])
    fw_url = sys.argv[sys.argv.index("--fw-url") + 1] if "--fw-url" in sys.argv else ""
    pkg = f"nullwave-{version}.fwsc"
    if not (BUILT / "firmware" / pkg).exists():
        sys.exit(f"publish_site: {BUILT / 'firmware' / pkg} is not there: build the release and the site first")
    dest = out / "fm1"
    screens = set()
    for page in PAGES:
        src, dst = BUILT / page, dest / page
        if dst.exists():
            shutil.rmtree(dst)
        shutil.copytree(src, dst, ignore=shutil.ignore_patterns(".DS_Store"))
        for f in dst.rglob("*.html"):
            html = rewrite(f.read_text(encoding="utf-8"), page)
            screens.update(re.findall(r'data-screens?="([^"]*)"', html))
            screens.update(m for s in re.findall(r'&quot;screen&quot;: &quot;([^&]*)&quot;', html) for m in [s])
            f.write_text(html, encoding="utf-8")
        if page in ("manual", "quick-start"):
            for n in ("fm1-docs.css", "fm1-docs.js"):
                shutil.copyfile(BUILT / "assets" / n, dst / n)
        if page == "emulator":
            for d in ("fonts", "words"):
                if (BUILT / d).exists():
                    shutil.copytree(BUILT / d, dst / d, dirs_exist_ok=True)
        if page == "installer":
            shutil.copytree(BUILT / "companion", dst / "companion", dirs_exist_ok=True)
            shutil.copyfile(BUILT / "fm1ports.js", dst / "fm1ports.js")
            (dst / "firmware").mkdir(exist_ok=True)
            html = (dst / "index.html").read_text(encoding="utf-8")
            for fw in sorted(set(re.findall(r"(?:\.\./)?firmware/([A-Za-z0-9_.-]+\.fwsc)", html))):
                if fw == pkg and fw_url:
                    html = re.sub(r"(?:\.\./)?firmware/" + re.escape(fw), fw_url, html)
                elif (BUILT / "firmware" / fw).exists():
                    shutil.copyfile(BUILT / "firmware" / fw, dst / "firmware" / fw)
                    html = html.replace("../firmware/" + fw, "firmware/" + fw)
            (dst / "index.html").write_text(html, encoding="utf-8")
    names = sorted({n.strip() for s in screens for n in s.split(",") if n.strip() and not n.startswith("@")})
    print(f"publish_site: {', '.join(PAGES)} at {dest} ({version}{', the package from ' + fw_url if fw_url else ''})")
    print("publish_site: the site's /assets/screens/ needs: " + " ".join(names))


if __name__ == "__main__":
    main()
