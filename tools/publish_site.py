#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
# Copyright (C) 2026 nullwave contributors
"""The fm1 pages for nullwave.cc, as the site keeps them: four folders under fm1/ (installer, emulator, quick-start,
manual), each standing on its own, on the site's shared /style.css, /device.js and /assets/ (its logos, fonts and
screens; DESIGN.md), so nothing else of the site is touched. From the local build of the pages
(app/build/site: ./build.sh --release VERSION, then the site build, tools/site_look.py's look):

    python3 tools/publish_site.py VERSION OUT [--fw-url URL]
    python3 tools/publish_site.py --check DIR      (the release workflow: no private paths in a folder of built files)

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


TITLES = {"installer": "nullwave fm1 installer", "emulator": "nullwave fm1 emulator",
          "quick-start": "nullwave fm1 quick start", "manual": "nullwave fm1 manual"}
# what must never be in a published file: a build's private paths (a toolchain's __FILE__, a debug section)
LEAKS = re.compile(rb"/private/(?:tmp|var)/|/Users/|/Volumes/|/home/(?!web_user)[a-z]|/tmp/[A-Za-z0-9_.-]+-[0-9]+/")
BUILD_PATH = re.compile(rb"/(?:private/tmp|private/var|Users|Volumes|home/(?!web_user)[a-z][^/\x00]*)/[^\x00\s\"']*")


def scrub(path):
    """a built file another project made (fm1-reader's espeak.wasm keeps its build's path in __FILE__ strings): each such
    path rewritten in place, the same length (/build/./././...), so the file stays valid"""
    d = path.read_bytes()
    def same(m):
        n = len(m.group(0))
        s = b"/build" + b"/." * ((n - 6) // 2)
        return s + b"/" * (n - len(s))
    d2 = BUILD_PATH.sub(same, d)
    if d2 != d:
        path.write_bytes(d2)
        return True
    return False


def leaks(root):
    """the published files that still hold a private path (the check: none)"""
    bad = []
    for f in sorted(Path(root).rglob("*")):
        if f.is_file():
            m = LEAKS.search(f.read_bytes())
            if m:
                bad.append(f"{f.relative_to(root)}: {m.group(0).decode(errors='replace')}")
    return bad


# the installer as published: the credit line and the comments that name the projects it is built on out (the
# licence texts, the copyright lines and the code's own names, PORT_NAMES and LOADER_MARK, stay); its "recovering an
# FM-1 that won't start" points at the manual. fm1-base's installer.html itself is not changed: done here, each time
INSTALLER_SUBS = [
    (r"<b class=\"nb\">nullwave</b> is built on fm1-base and felucca-bsp, from Felucca by .*?<br>\s*", ""),
    (r"A firmware's installer page: Felucca's \(by way of X0X and FoMni-1\), English first\.",
     "A firmware's installer page, English first."),
    (r'href="https://github\.com/hugelton/felucca-bsp/tree/main/tools"', 'href="../manual/#installing-and-going-back-to-stock"'),
    (r'// "optimist", "sloop", "melodee", "loom-8" / "loom8", as tools/fm1ports\.py asks them too\.',
     "// the other firmwares' own names, as tools/fm1ports.py asks them too."),
    (r'e\.g\. "X0X 1\.1"', 'e.g. "NAME 1.1"'),
    (r"\(felucca-bsp chip/ota\.c ota_jl_enc\)", "(the chip's update protocol)"),
    (r"as felucca-bsp's tools/fm1_rescue\.py pins them", "as the rescue tool pins them"),
    (r"\(felucca-bsp tools/fm1_rescue\.py's STOCK\)", "(the rescue tool's STOCK)"),
    (r"\(felucca-bsp loader/ldr_core\.c, LDR_FAST_CHUNK\)", "(the update loader, LDR_FAST_CHUNK)"),
    (r"a package without a Felucca loader", "a package without such a loader"),
    (r"a firmware on felucca-bsp's chip/ota\.c", "a firmware on the chip's update protocol"),
    (r"\(felucca-bsp chip/ota\.c: a well-made writer", "(the chip's update protocol: a well-made writer"),
    (r", as felucca-bsp chip/ota\.c ota_crc16", ", as the chip's update protocol has it"),
    (r"brings a Felucca update loader", "brings its own update loader"),
    (r"any well-made writer, felucca-bsp chip/ota\.c\)", "any well-made writer)"),
]
NAMES_OK = ('const PORT_NAMES', 'const LOADER_MARK', 'Copyright (C)')   # (export const PORT_NAMES: the same)   # (lines whose names stay)


def installer_subs(html):
    for pat, rep in INSTALLER_SUBS:
        html = re.sub(pat, rep, html, flags=re.S)
    left = [l.strip()[:90] for l in html.split("\n")
            if re.search(r"(?i)felucca|sloop", l) and not any(k in l for k in NAMES_OK)]
    if left:
        sys.exit("publish_site: the installer still names them:\n" + "\n".join(left))
    return html


def rewrite(html, page):
    html = re.sub(r"<title>[^<]*</title>", f"<title>{TITLES[page]}</title>", html, count=1)
    for a, b in SHARED.items():
        html = html.replace(a, b)
    for n in LOGOS:                      # (the site's own logos: /assets/NAME-THEME.svg)
        for t in ("dark", "light"):
            html = html.replace(f"/fm1/assets/{n}-{t}.svg", f"/assets/{n}-{t}.svg")
            html = html.replace(f"../assets/{n}-{t}.svg", f"/assets/{n}-{t}.svg")
    html = html.replace("/fm1/assets/fm1-docs.css", "fm1-docs.css").replace("/fm1/assets/fm1-docs.js", "fm1-docs.js")
    if page == "installer":              # (the companion's scripts beside it)
        html = html.replace('"../companion/', '"./companion/')
        html = installer_subs(html)
    if page == "emulator":
        html = html.replace("../fonts/", "./fonts/").replace("../words/", "./words/")
    return html


def main():
    if len(sys.argv) == 3 and sys.argv[1] == "--check":   # (the release workflow: a folder of built files, no leaks)
        bad = leaks(Path(sys.argv[2]))
        print("\n".join(f"publish_site: private path in {b}" for b in bad) or "publish_site: no private paths")
        sys.exit(1 if bad else 0)
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
            for d in ("guide", "assets", "o2", "web"):   # (the retired guide, vendored copies, a build's object,
                if (dst / d).exists():                    # an empty folder: none of them loaded by the emulator)
                    shutil.rmtree(dst / d)
            for d in ("fonts", "words"):
                if (BUILT / d).exists():
                    shutil.copytree(BUILT / d, dst / d, dirs_exist_ok=True)
        if page == "installer":
            shutil.copytree(BUILT / "companion", dst / "companion", dirs_exist_ok=True)
            shutil.copyfile(BUILT / "fm1ports.js", dst / "fm1ports.js")
            for f in [dst / "fm1ports.js", *(dst / "companion").glob("*.*")]:   # (their comments, as the page's)
                if f.suffix in (".js", ".html"):
                    f.write_text(installer_subs(f.read_text(encoding="utf-8")), encoding="utf-8")
            (dst / "firmware").mkdir(exist_ok=True)
            html = (dst / "index.html").read_text(encoding="utf-8")
            for fw in sorted(set(re.findall(r"(?:\.\./)?firmware/([A-Za-z0-9_.-]+\.fwsc)", html))):
                if fw == pkg and fw_url:
                    html = re.sub(r"(?:\.\./)?firmware/" + re.escape(fw), fw_url, html)
                elif (BUILT / "firmware" / fw).exists():
                    shutil.copyfile(BUILT / "firmware" / fw, dst / "firmware" / fw)
                    html = html.replace("../firmware/" + fw, "firmware/" + fw)
            (dst / "index.html").write_text(html, encoding="utf-8")
            for lic in (BUILT / "firmware").glob("*.txt"):   # (the packages' licences beside them: m-bootloader's MIT,
                shutil.copyfile(lic, dst / "firmware" / lic.name)   # the SDK's and the own stack's parts)
    for f in dest.rglob("*"):            # (another project's build paths out, then the check: none left)
        if f.is_file() and f.suffix in (".wasm", ".js") and scrub(f):
            print(f"publish_site: a build path rewritten in {f.relative_to(dest)}")
    bad = [b for page in PAGES for b in leaks(dest / page)]
    if bad:
        sys.exit("publish_site: private paths left:\n" + "\n".join(bad))
    names = sorted({n.strip() for s in screens for n in s.split(",") if n.strip() and not n.startswith("@")})
    print(f"publish_site: {', '.join(PAGES)} at {dest} ({version}{', the package from ' + fw_url if fw_url else ''})")
    print("publish_site: the site's /assets/screens/ needs: " + " ".join(names))


if __name__ == "__main__":
    main()
