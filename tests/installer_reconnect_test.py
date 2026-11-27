#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
# Copyright (C) 2026 nullwave contributors
"""The installer's reconnect patch (tools/publish_site.py RECONNECT, over fm1-base's web/fm1ota.js): an FM-1 back from
an update under a new USB identity, on a port the browser lists only in a new MIDIAccess, is not found by fm1-base's
wait as it is, and is found with the patch; the published installer page carries the same patched code, the 60 s
waits and the message. Run by tests/run_tests.sh (node: base/.toolchain/node, else the one on PATH).

    python3 tests/installer_reconnect_test.py
"""
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
import publish_site as P  # noqa: E402

fails = []


def check(what, ok):
    print(("  ok " if ok else "FAIL ") + what)
    if not ok:
        fails.append(what)


def node():
    tc = ROOT / "base" / ".toolchain" / "node" / "bin" / "node"
    return str(tc) if tc.exists() else shutil.which("node")


def run_back(js_dir):
    r = subprocess.run([node(), str(ROOT / "tests" / "web" / "installer_back.mjs"), str(js_dir)], capture_output=True,
                       text=True, timeout=60)
    return r.returncode, (r.stdout + r.stderr).strip().splitlines()[-1:]


def main():
    web = ROOT / "base" / "web"
    js = (web / "fm1ota.js").read_text(encoding="utf-8")
    with tempfile.TemporaryDirectory() as d:
        for name in ("as-is", "patched"):
            (Path(d) / name).mkdir()
            for f in web.glob("*.js"):
                shutil.copyfile(f, Path(d) / name / f.name)
        (Path(d) / "patched" / "fm1ota.js").write_text(P.reconnect_js(js), encoding="utf-8")
        rc, out = run_back(Path(d) / "as-is")
        check(f"fm1-base's wait as it is: the FM-1 back on a new device is not found ({' '.join(out)})", rc == 1)
        rc, out = run_back(Path(d) / "patched")
        check(f"with the patch: found, as FM-1_60000102 ({' '.join(out)})", rc == 0)
    patched = P.reconnect_js(js)
    block = patched[patched.index("  async waitFor(filter, ms) {"):patched.index("  async serve(")]
    page = ROOT / "app" / "build" / "site" / "installer" / "index.html"
    if page.exists():
        html = P.installer_reconnect(page.read_text(encoding="utf-8"))
        check("the published installer carries the same patched wait", block in html)
        check("its waits for the FM-1 back are 60 s, none 40 s", "!IS_OTA(id), 40000)" not in html
              and html.count("!IS_OTA(id), 60000)") + html.count("id.text === product, 60000)") == 5)
        check("its message says what to do", html.count("If its screen shows nullwave, it is installed") == 6)
    else:
        print("  (no app/build/site: the published page's checks skipped; ./build.sh --release and the site build)")
    try:
        P.installer_reconnect("class Updater\n" + js.replace("await sleep(1000);\n    }\n    return null;", "x"))
        check("a changed upstream wait stops the publish", False)
    except SystemExit as e:
        check("a changed upstream wait stops the publish (" + str(e)[:60] + "...)", "reconnect patch" in str(e))
    print(f"installer_reconnect_test: {len(fails)} failed")
    sys.exit(1 if fails else 0)


if __name__ == "__main__":
    main()
