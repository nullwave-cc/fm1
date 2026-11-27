#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
# Copyright (C) 2026 nullwave contributors
"""One command for a release of nullwave fm1, its pages and the editor on nullwave.cc (docs/RELEASING.md):

    python3 tools/release.py VERSION [--notes FILE] [--editor patch|minor|major|X.Y.Z] [--site DIR]
                             [--include PATH ...] [--dry-run] [--json]
    python3 tools/release.py --editor-only [--editor ...] [--site DIR] [--dry-run] [--json]

1. preflight: VERSION is X.Y.Z and not a tag yet; a real run from a clean main, level with origin, with a CHANGELOG
   entry (--notes, or one already there) and signing set up
2. the tests (tests/run_tests.sh: with doc_check), the package (./build.sh --release VERSION), the web emulator
3. the pages (make_site, brand, quick_start, site_look), the leak check, publish_site into SITE/fm1/{installer,
   emulator,quick-start,manual}
4. the editor, when ../editor is there: its release script builds into SITE/fm1/editor (and SITE/fm1/versions.json)
5. a real run only: the signed version commit and tag, the push of main and the tag, the release workflow watched, the
   release's package checked against SITE's (SHA-256) and published as the latest; the editor's release published (its
   own commit, tag and push); one signed commit of SITE's fm1/ (and the --include paths), pushed to the site's main; the
   pages checked live. Other changes in SITE outside fm1/ stop it, named

--dry-run: 1-4 for real, into a copy of SITE in a temporary folder; nothing committed, tagged, pushed or published, and
a summary of what a real run would change and send. --editor-only: the fm1 steps left out, the firmware the site has
(SITE/fm1/versions.json, else its installer's package); the editor step and the site's commit only. --json: the
summary as one JSON line at the end. Stops at the first failure (exit 1). Never force-pushes; never builds or installs
a bootloader or the update loader."""
import argparse
import hashlib
import json
import os
import re
import shutil
import subprocess
import sys
import tempfile
import time
import urllib.request
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
BUILD = ROOT / "app" / "build"
BUILT = BUILD / "site"
EMU = BUILD / "emu"
EDITOR = ROOT.parent / "editor"
# the editor's release entry point (its repository's): the one place to change when it does
EDITOR_CMD = ["node", "scripts/release.mjs"]
PAGES = ("installer", "emulator", "quick-start", "manual")
LIVE = "https://nullwave.cc/fm1/"
# what the run writes in the site, staged on its own; any other tracked change there needs --include
WRITTEN = tuple(f"fm1/{p}" for p in PAGES) + ("fm1/editor", "fm1/versions.json")
VERSION_RE = re.compile(r"^[0-9]{1,2}\.[0-9]{1,2}\.[0-9]{1,2}$")   # (release.yml's rule)
ENV = dict(os.environ, DYLD_FALLBACK_LIBRARY_PATH="/opt/homebrew/lib")
GH_ENV = {k: v for k, v in ENV.items() if k not in ("GH_TOKEN", "GITHUB_TOKEN")}   # (gh as the keyring's account)
LOG = None


class Fail(Exception):
    pass


def step(n, what):
    print(f"\n== {n} {what}", flush=True)


def note(what):
    print(f"   {what}", flush=True)


def run(cmd, cwd=ROOT, env=ENV, quiet=True, check=True):
    """a command; its output to the log (the last of it shown on a failure); -> its stdout"""
    note("$ " + " ".join(str(c) for c in cmd))
    r = subprocess.run([str(c) for c in cmd], cwd=cwd, env=env, capture_output=True, text=True)
    if LOG:
        with open(LOG, "a", encoding="utf-8") as f:
            f.write(f"\n$ {' '.join(str(c) for c in cmd)}   (in {cwd})\n{r.stdout}{r.stderr}")
    if not quiet:
        print(r.stdout, end="")
    if check and r.returncode:
        tail = (r.stdout + r.stderr).strip().splitlines()[-25:]
        raise Fail(f"{' '.join(str(c) for c in cmd[:3])} failed (exit {r.returncode}):\n" + "\n".join(tail))
    return r.stdout


def git(*a, cwd=ROOT, check=True):
    return run(["git", *a], cwd=cwd, check=check).strip()


def sha256(p):
    return hashlib.sha256(Path(p).read_bytes()).hexdigest()


def hello_tag(wasm):
    """the capability query's extras as this build has them (APP_COMPANION_EXTRAS, in the emulator's wasm)"""
    m = re.search(rb"editor=nullwave/[0-9]+[^\x00]*", Path(wasm).read_bytes())
    return m.group(0).decode() if m else None


# ------------------------------------------------------------------------------------------------- 1 preflight ---
def preflight(a):
    if not VERSION_RE.match(a.version or ""):
        raise Fail(f"VERSION {a.version!r}: X.Y.Z, as release.yml takes it (0.1.1)")
    tag = "v" + a.version
    if git("tag", "-l", tag):
        raise Fail(f"{tag} is already a tag here")
    if git("ls-remote", "--tags", "origin", "refs/tags/" + tag):
        raise Fail(f"{tag} is already a tag on origin")
    branch = git("rev-parse", "--abbrev-ref", "HEAD")
    dirty = git("status", "--porcelain")
    if a.dry_run:
        note(f"branch {branch}{', uncommitted changes (a dry run: allowed)' if dirty else ', clean'}")
    else:
        if branch != "main":
            raise Fail(f"on {branch}: a release is made from main")
        if dirty:
            raise Fail("the tree has uncommitted changes:\n" + dirty)
        git("fetch", "origin", "main")
        if git("rev-parse", "HEAD") != git("rev-parse", "origin/main"):
            raise Fail("main is not level with origin/main (pull or push first)")
        if not git("config", "user.signingkey", check=False):
            raise Fail("git has no user.signingkey: the release's commit and tag are signed")
        run(["gh", "auth", "status"], env=GH_ENV)
    return changelog_plan(a)


def changelog_plan(a):
    """the CHANGELOG's entry for VERSION (--notes: a file, or the text itself) and the README's version line:
    -> {path: new text} (written in a real run, shown in a dry run)"""
    out = {}
    cl = (ROOT / "CHANGELOG.md").read_text(encoding="utf-8")
    has = re.search(r"^## " + re.escape(a.version) + r"\s*$", cl, re.M)
    if a.notes:
        p = Path(a.notes)
        text = (p.read_text(encoding="utf-8") if p.is_file() else a.notes).strip()
        if has:
            raise Fail(f"CHANGELOG.md has a {a.version} entry already: leave --notes out, or take it out")
        head, sep, rest = cl.partition("\n## ")
        out[ROOT / "CHANGELOG.md"] = f"{head.rstrip()}\n\n## {a.version}\n\n{text}\n\n" + (("## " + rest) if sep else "")
    elif not has:
        if not a.dry_run:
            raise Fail(f"CHANGELOG.md has no {a.version} entry: give --notes FILE")
        note(f"CHANGELOG.md has no {a.version} entry (a real run needs --notes FILE)")
    rd = (ROOT / "README.md").read_text(encoding="utf-8")
    rd2 = re.sub(r"\*\*Version [0-9]+\.[0-9]+\.[0-9]+\*\*(, the first public release)?\.",
                 f"**Version {a.version}**, the current release.", rd, count=1)
    if rd2 != rd:
        out[ROOT / "README.md"] = rd2
    return out


# ---------------------------------------------------------------------------------------------------- 2 builds ---
def builds(a):
    run(["sh", "tests/run_tests.sh"])
    note("tests and doc_check: passed")
    run(["./build.sh", "--release", a.version])
    pkg = BUILD / f"nullwave-{a.version}.fwsc"
    if not pkg.exists():
        raise Fail(f"{pkg} is not there after the build")
    note(f"{pkg.relative_to(ROOT)}: {pkg.stat().st_size} bytes, sha256 {sha256(pkg)}")
    run(["./build.sh", "--web", "--release", a.version])
    wasm = (EMU / "app.wasm").read_bytes()
    if not re.search(rb"nullwave " + re.escape(a.version.encode()) + rb"(?![-.\w])", wasm):
        raise Fail(f"the emulator's build does not say nullwave {a.version}")
    tag = hello_tag(EMU / "app.wasm")
    if not tag:
        raise Fail("the emulator's build has no editor= capability tag")
    note(f"the emulator: {EMU.relative_to(ROOT)}, hello tag {tag}")
    return pkg, tag


# ----------------------------------------------------------------------------------------------------- 3 pages ---
def pages(a, site):
    if BUILT.exists():
        shutil.rmtree(BUILT)
    pkg = f"app/build/nullwave-{a.version}.fwsc"
    run(["python3", "base/tools/make_site.py", "app", pkg, a.version, BUILT])
    (BUILT / "install").rename(BUILT / "installer")
    run(["python3", "tools/brand.py", BUILT])
    run(["python3", "tools/quick_start.py", BUILT, a.version])
    run(["python3", "tools/site_look.py", BUILT, a.version, f"nullwave-{a.version}.fwsc"])
    out = run(["python3", "tools/publish_site.py", a.version, site])   # (it rewrites other projects' build paths)
    for line in out.strip().splitlines():
        note(line)
    for p in PAGES:                      # (the leak check, on what is published)
        run(["python3", "tools/publish_site.py", "--check", site / "fm1" / p])
    note("the leak check: no private paths in the published pages")
    for p in PAGES:
        if 'href="/fm1/editor/"' not in (site / "fm1" / p / "index.html").read_text(encoding="utf-8"):
            raise Fail(f"{p}'s footer has no link to the editor")
    return site / "fm1" / "installer" / "firmware" / f"nullwave-{a.version}.fwsc"


# ---------------------------------------------------------------------------------------------------- 4 editor ---
def editor(a, site, fw, emu, publish=False):
    """the editor's release script: -> the JSON of its last line (None: no editor beside this repository)"""
    if not EDITOR.is_dir():
        note(f"no {EDITOR}: the editor left as the site has it")
        return None
    if not (EDITOR / EDITOR_CMD[-1]).exists():
        if a.dry_run:                    # (a dry run goes on, and says so; a real run stops)
            note(f"{EDITOR / EDITOR_CMD[-1]} is not there yet: the editor step skipped (a real run stops here)")
            return {"skipped": f"no {EDITOR_CMD[-1]}"}
        raise Fail(f"{EDITOR / EDITOR_CMD[-1]} is not there: the editor's release entry point (EDITOR_CMD)")
    cmd = [*EDITOR_CMD, "--out", site / "fm1" / "editor", "--fw-version", fw, "--emu", emu]
    if a.editor:
        cmd += ["--bump", a.editor]
    if publish:
        cmd.append("--publish")
    note("$ " + " ".join(str(c) for c in cmd))
    r = subprocess.run([str(c) for c in cmd], cwd=EDITOR, env=ENV, capture_output=True, text=True)
    if LOG:
        with open(LOG, "a", encoding="utf-8") as f:
            f.write(f"\n$ {' '.join(str(c) for c in cmd)}   (in {EDITOR})\n{r.stdout}{r.stderr}")
    out = r.stdout.strip().splitlines()
    try:
        res = json.loads(out[-1])        # (its last line: ok, version, fwVersion, protocol, tag, commit, files, problems)
    except (IndexError, ValueError):
        res = None
    if r.returncode or not res or not res.get("ok"):
        why = (res or {}).get("error") or "; ".join((res or {}).get("problems") or []) \
            or "\n".join(r.stderr.strip().splitlines()[-20:])
        raise Fail(f"the editor's release failed (exit {r.returncode}): {why}")
    note(f"the editor: {res.get('version')} for fm1 {res.get('fwVersion')}, protocol {res.get('protocol')}, "
         f"{res.get('files')} file(s)")
    if not (site / "fm1" / "versions.json").exists():
        raise Fail("the editor's release wrote no fm1/versions.json")
    return res


def site_firmware(site):
    """--editor-only: the firmware version the site has"""
    vj = site / "fm1" / "versions.json"
    if vj.exists():
        d = json.loads(vj.read_text(encoding="utf-8"))
        for k in ("fm1", "firmware", "fw"):
            v = d.get(k)
            v = v.get("version") if isinstance(v, dict) else v
            if isinstance(v, str) and VERSION_RE.match(v):
                return v
    got = sorted(f.name[9:-5] for f in (site / "fm1" / "installer" / "firmware").glob("nullwave-*.fwsc"))
    got = [v for v in got if VERSION_RE.match(v)]
    if not got:
        raise Fail(f"no firmware version in {vj} nor a release package in the site's installer")
    return got[-1]


# --------------------------------------------------------------------------------------------- 5 the real run ---
def fm1_release(a, writes, site_pkg):
    tag = "v" + a.version
    for p in writes:
        git("add", p.relative_to(ROOT))
    git("commit", "-S", "-m", f"Release {a.version}")
    git("tag", "-s", tag, "-m", f"nullwave {a.version}")
    git("push", "origin", "main")
    git("push", "origin", tag)
    note("waiting for the release workflow")
    rid = None
    for _ in range(60):
        out = run(["gh", "run", "list", "--workflow", "release.yml", "--branch", tag, "--json", "databaseId",
                   "--limit", "1"], env=GH_ENV)
        runs = json.loads(out or "[]")
        if runs:
            rid = str(runs[0]["databaseId"])
            break
        time.sleep(10)
    if not rid:
        raise Fail(f"no release workflow run for {tag} after 10 minutes")
    run(["gh", "run", "watch", rid, "--exit-status", "--interval", "30"], env=GH_ENV)
    with tempfile.TemporaryDirectory() as d:
        run(["gh", "release", "download", tag, "-p", f"nullwave-{a.version}.fwsc", "-D", d], env=GH_ENV)
        got = sha256(Path(d) / f"nullwave-{a.version}.fwsc")
    if got != sha256(site_pkg):
        raise Fail(f"the release's package ({got}) is not the site's ({sha256(site_pkg)}): the draft left as it is")
    note(f"the release's package is the site's: {got}")
    run(["gh", "release", "edit", tag, "--draft=false", "--latest"], env=GH_ENV)
    url = run(["gh", "release", "view", tag, "--json", "url", "-q", ".url"], env=GH_ENV).strip()
    note(f"published: {url}")
    return url


def under(path, roots):
    return any(path == r.rstrip("/") or path.startswith(r.rstrip("/") + "/") for r in roots)


def site_dirty(site, include):
    """the site's tracked changes the run does not write (untracked files are left alone and never staged):
    -> (the ones --include takes, the ones it does not)"""
    out = run(["git", "status", "--porcelain", "--untracked-files=no", "--no-renames"], cwd=site).splitlines()
    paths = [ln[3:].strip('"') for ln in out if len(ln) > 3]   # (not stripped: "XY path", X may be a space)
    other = [p for p in paths if not under(p, WRITTEN)]
    return [p for p in other if under(p, include)], [p for p in other if not under(p, include)]


def site_release(site, what, include):
    if git("rev-parse", "--abbrev-ref", "HEAD", cwd=site) != "main":
        raise Fail(f"{site} is not on main")
    git("fetch", "origin", "main", cwd=site)
    if git("rev-parse", "HEAD", cwd=site) != git("rev-parse", "origin/main", cwd=site):
        raise Fail(f"{site}'s main is not level with origin/main")
    staged = stage(site, include)
    if not staged:
        note("the site is as committed: nothing to commit")
        return False
    git("commit", "-S", "-m", what, cwd=site)
    git("push", "origin", "main", cwd=site)
    return True


def stage(site, include):
    """the site commit's files staged: what the run wrote, and --include's tracked changes; -> [(status, path)]"""
    if git("diff", "--cached", "--name-only", cwd=site):
        raise Fail(f"{site} has changes staged already: commit or unstage them first")
    inc, left = site_dirty(site, include)
    if left:
        raise Fail("the site has changes the release does not make: " + ", ".join(left)
                   + " (--include them, or commit or undo them)")
    for p in WRITTEN:                    # (each, new files too; a path the run did not write: nothing to add)
        if (site / p).exists() or git("ls-files", "--", p, cwd=site):
            git("add", "-A", "--", p, cwd=site)
    for p in include:
        git("add", "-u", "--", p, cwd=site)   # (tracked files only: --include never brings in an untracked one)
    staged = [tuple(ln.split("\t", 1)) for ln in
              git("diff", "--cached", "--name-status", "--no-renames", cwd=site).splitlines() if ln]
    stray = [x for _, x in staged if not under(x, WRITTEN) and not under(x, include)]
    if stray:
        raise Fail("staged, and neither written by the release nor --include: " + ", ".join(stray))
    return staged


def live_check(version, pkg_sha):
    urls = [LIVE + p + "/" for p in (*PAGES, "editor")]
    fw = LIVE + f"installer/firmware/nullwave-{version}.fwsc" if version else None
    for attempt in range(20):            # (Pages takes a minute or a few)
        bad = []
        for u in urls:
            try:
                urllib.request.urlopen(urllib.request.Request(u, headers={"Cache-Control": "no-cache"}), timeout=20)
            except Exception as e:       # noqa: BLE001
                bad.append(f"{u}: {e}")
        if fw and pkg_sha:
            try:
                got = hashlib.sha256(urllib.request.urlopen(fw + f"?t={time.time():.0f}", timeout=60).read()).hexdigest()
                if got != pkg_sha:
                    bad.append(f"{fw}: sha256 {got}, not yet {pkg_sha}")
            except Exception as e:       # noqa: BLE001
                bad.append(f"{fw}: {e}")
        if not bad:
            note("live: the pages answer" + (", the package is the release's" if fw else ""))
            return True
        note(f"live, try {attempt + 1}: " + "; ".join(bad[:2]))
        time.sleep(30)
    raise Fail("not live after 10 minutes:\n" + "\n".join(bad))


# ----------------------------------------------------------------------------------------------------- summary ---
def changes(src, dst):
    """what the run changed in the site's fm1/, against its checkout as it is (uncommitted edits too):
    [(status, path, bytes)]"""
    out = []
    for f in sorted((dst / "fm1").rglob("*")):
        if not f.is_file():
            continue
        rel = f.relative_to(dst)
        old = src / rel
        if not old.exists():
            out.append(("added", str(rel), f.stat().st_size))
        elif old.read_bytes() != f.read_bytes():
            out.append(("changed", str(rel), f.stat().st_size))
    for f in sorted((src / "fm1").rglob("*")):
        if f.is_file() and not (dst / f.relative_to(src)).exists():
            out.append(("removed", str(f.relative_to(src)), 0))
    return out


def main():
    global LOG
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0], formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("version", nargs="?", help="X.Y.Z (left out with --editor-only)")
    ap.add_argument("--notes", help="the CHANGELOG entry: a file, or the text")
    ap.add_argument("--editor", help="the editor's version: patch, minor, major or X.Y.Z (its script's --bump)")
    ap.add_argument("--editor-only", action="store_true", help="the editor and the site's commit only")
    ap.add_argument("--site", default=str(ROOT.parent / "site"), help="the site's checkout (nullwave-cc/site)")
    ap.add_argument("--include", nargs="+", default=[], metavar="PATH",
                    help="the site's other changed tracked files to commit with it (index.html 404.html sitemap.xml fm1/index.html)")
    ap.add_argument("--dry-run", action="store_true", help="into a copy of the site; nothing committed or sent")
    ap.add_argument("--json", action="store_true", help="the summary as JSON on the last line")
    a = ap.parse_args()
    real_site = Path(a.site).expanduser().resolve()
    if not (real_site / "fm1").is_dir():
        sys.exit(f"release: {real_site} is not the site's checkout (no fm1/)")
    for inc in a.include:
        if inc.startswith("/") or ".." in Path(inc).parts:
            sys.exit(f"release: --include {inc}: a path inside the site")
    if a.editor_only and a.version:
        sys.exit("release: --editor-only takes no VERSION (the site's firmware is used)")
    if not a.editor_only and not a.version:
        sys.exit("release: VERSION (X.Y.Z), or --editor-only")
    BUILD.mkdir(parents=True, exist_ok=True)
    LOG = BUILD / f"release-{a.version or 'editor'}.log"
    LOG.write_text("", encoding="utf-8")
    tmp = None
    summary = {"version": a.version, "dry_run": a.dry_run, "editor_only": a.editor_only, "fwsc": None,
               "fwsc_sha256": None, "wasm_sha256": None, "emu_dir": str(EMU), "hello_tag": None, "editor": None,
               "files": [], "include": a.include, "release_url": None, "would_commit": [], "would_push": [], "site": str(real_site),
               "log": str(LOG)}
    try:
        site = real_site
        if a.dry_run:
            tmp = Path(tempfile.mkdtemp(prefix="nullwave-release-"))
            site = tmp / "site"
            step(0, f"a dry run: the site copied to {site}")
            shutil.copytree(real_site, site, symlinks=True)
        writes, pkg, site_pkg = {}, None, None
        if not a.editor_only:
            step(1, f"preflight for {a.version}")
            writes = preflight(a)
            for p, text in writes.items():
                note(f"{'would write' if a.dry_run else 'writes'} {p.relative_to(ROOT)}")
                if not a.dry_run:
                    p.write_text(text, encoding="utf-8")
            step(2, "the tests, the package, the web emulator")
            pkg, summary["hello_tag"] = builds(a)
            step(3, f"the pages into {site / 'fm1'}")
            site_pkg = pages(a, site)
            fw = a.version
        else:
            step(1, "the editor only: the site's firmware")
            fw = site_firmware(site)
            note(f"the firmware: {fw}")
            emu = site / "fm1" / "emulator"   # (the site's own emulator: the firmware it has, not this tree's)
            summary["emu_dir"] = str(emu)
            if (emu / "app.wasm").exists():
                summary["hello_tag"] = hello_tag(emu / "app.wasm")
            site_pkg = site / "fm1" / "installer" / "firmware" / f"nullwave-{fw}.fwsc"
        summary["fwsc"] = str(site_pkg) if site_pkg and site_pkg.exists() else None
        summary["fwsc_sha256"] = sha256(site_pkg) if summary["fwsc"] else None
        emu = Path(summary["emu_dir"])
        summary["wasm_sha256"] = sha256(emu / "app.wasm") if (emu / "app.wasm").exists() else None
        step(4, "the editor")
        summary["editor"] = editor(a, site, fw, emu)
        summary["files"] = [{"status": s, "path": p, "bytes": n} for s, p, n in changes(real_site, site)] \
            if a.dry_run else []
        tag = f"v{a.version}"
        if a.dry_run:
            if not a.editor_only:
                summary["would_commit"].append(f"fm1: Release {a.version} (signed): "
                                               + ", ".join(str(p.relative_to(ROOT)) for p in writes))
                summary["would_push"] += [f"fm1: main, tag {tag} (signed)",
                                          f"fm1: publish the GitHub release {tag} as the latest, once its package's "
                                          f"sha256 is {summary['fwsc_sha256']}"]
            if summary["editor"] is not None:
                summary["would_push"].append("editor: its release script with --publish (its commit, tag, push)")
            step(5, "the site's commit, staged in the copy (not committed)")
            staged = stage(site, a.include)   # (the copy's git: what a real run's commit would hold, against HEAD)
            summary["site_stage"] = [{"status": st, "path": pth} for st, pth in staged]
            inc = sorted({pth for _, pth in staged if not under(pth, WRITTEN)})
            note(f"{len(staged)} file(s) staged; outside what the release writes: {', '.join(inc) or 'none'}")
            summary["would_commit"].append(f"site: {len(staged)} file(s) (signed): the release's {', '.join(WRITTEN)}"
                                           + (" and --include " + ", ".join(inc) if inc else ""))
            summary["would_push"].append("site: main")
        else:
            if not a.editor_only:
                step(5, f"fm1 {a.version}: the commit, the tag, the push, the release")
                summary["release_url"] = fm1_release(a, writes, site_pkg)
            if summary["editor"] is not None:
                step(6, "the editor's release, published")
                summary["editor"] = editor(a, site, fw, emu, publish=True)
            step(7, "the site's fm1/: one commit, pushed")
            ev = (summary["editor"] or {}).get("version") if isinstance(summary["editor"], dict) else None
            what = (f"fm1 {a.version}" if not a.editor_only else f"fm1 editor {ev or ''}".strip()) \
                + ": the installer, emulator, quick start, manual and editor"
            if site_release(site, what, a.include):
                step(8, "live on nullwave.cc")
                live_check(None if a.editor_only else a.version, summary["fwsc_sha256"])
    except Fail as e:
        print(f"\nrelease: FAILED: {e}\n(the whole output: {LOG})", file=sys.stderr)
        if a.json:
            summary["error"] = str(e)
            print(json.dumps(summary))
        sys.exit(1)
    finally:
        if tmp and not os.environ.get("NW_RELEASE_KEEP"):
            shutil.rmtree(tmp, ignore_errors=True)
    print("\n== summary")
    for k in ("version", "fwsc", "fwsc_sha256", "wasm_sha256", "emu_dir", "hello_tag", "release_url"):
        print(f"   {k}: {summary[k]}")
    if a.dry_run:
        tot = sum(f["bytes"] for f in summary["files"])
        print(f"   site fm1/ changes: {len(summary['files'])} file(s), {tot} bytes")
        for f in summary["files"][:40]:
            print(f"     {f['status']:8} {f['path']} ({f['bytes']} B)")
        if len(summary["files"]) > 40:
            print(f"     ... and {len(summary['files']) - 40} more")
        st = summary.get("site_stage") or []
        cnt = {}
        for x in st:
            cnt[x["status"]] = cnt.get(x["status"], 0) + 1
        print(f"   the site commit would stage {len(st)} file(s): "
              + ", ".join(f"{n} {k}" for k, n in sorted(cnt.items())))
        for x in st:
            if not under(x["path"], WRITTEN):
                print(f"     {x['status']} {x['path']} (--include)")
        for c in summary["would_commit"]:
            print(f"   would commit: {c}")
        for p in summary["would_push"]:
            print(f"   would push: {p}")
    if a.json:
        print(json.dumps(summary))


if __name__ == "__main__":
    main()
