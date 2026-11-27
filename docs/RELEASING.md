# Releasing nullwave fm1

One command makes a release of the firmware, its pages on nullwave.cc (the installer, the emulator, the quick start
and the manual) and the editor:

```bash
python3 tools/release.py VERSION --notes NOTES.md [--editor patch|minor|major|X.Y.Z] [--site DIR] \
    [--include PATH ...] [--dry-run] [--json]
```

It is the fm1 step of the site's `npm run release` too, which calls it with `--json` and reads its last line.

Always try it first with `--dry-run`: the same builds and checks, into a copy of the site in a temporary folder, with
nothing committed, tagged, pushed or published. It ends with what a real run would change and send.

## What it does

1. **Preflight.** VERSION is `X.Y.Z` and is not a tag yet, here or on GitHub. A real run also needs `main`, clean and
   level with `origin/main`, git's signing key and `gh` signed in. The CHANGELOG entry comes from `--notes` (a file,
   or the text itself), or one already in CHANGELOG.md. The README's version line is set to VERSION.
2. **Builds.** The tests (`tests/run_tests.sh`, with doc_check), the package (`./build.sh --release VERSION`:
   `app/build/nullwave-VERSION.fwsc`) and the web emulator (`./build.sh --web --release VERSION`: `app/build/emu`),
   its version and its hello tag checked.
3. **Pages.** The site build (make_site, brand, quick_start, site_look), then `tools/publish_site.py VERSION SITE`:
   `SITE/fm1/installer/`, `emulator/`, `quick-start/` and `manual/`, each footer with its link to the editor. Then the
   leak check on those four folders: no private paths in anything published.
4. **Editor.** When `../editor` is beside this repository, its release script runs there (`node scripts/release.mjs
   --out SITE/fm1/editor --fw-version VERSION --emu app/build/emu [--bump B]`, set once in `EDITOR_CMD` in
   tools/release.py). It tests the editor against this emulator, builds it into `SITE/fm1/editor/` and merges its
   keys into `SITE/fm1/versions.json` (release.py never writes that file). Its last line is JSON (`ok`, `version`,
   `fwVersion`, `protocol`, `tag`, `commit`, `files`, `problems`, `error`); a non-zero exit or `ok` false stops the
   release. `--editor` is its version's bump.
5. **The real run only:**
   - a signed commit "Release VERSION" (the CHANGELOG and the README), a signed tag `vVERSION`, the push of main and
     of the tag;
   - the release workflow watched to its end; the draft release's package checked against the site's copy (SHA-256:
     the builds are reproducible), then published as the latest;
   - the editor's release script again with `--publish` (its own commit, tag and push);
   - one signed commit in the site repository with what the run wrote and the `--include` paths (below), pushed to its
     main;
   - the pages and the package checked live on nullwave.cc, for up to ten minutes.

`--editor-only` leaves the fm1 steps out: the firmware is the one the site has (`fm1/versions.json`, else its
installer's package), the emulator the site's own (`fm1/emulator/`), and only the editor and the site's commit run.
An editor that needs a newer firmware than the site has stops there: release the firmware with it.

It stops at the first failure with the reason and exits 1. The whole output is in `app/build/release-VERSION.log`.
It never force-pushes, and never builds or installs a bootloader or the update loader. `gh` runs as the keyring's
account (GH_TOKEN left out of its environment).

What the run writes in the site is staged on its own: `fm1/installer/` (with `companion/`), `fm1/emulator/`,
`fm1/quick-start/`, `fm1/manual/`, `fm1/editor/` and `fm1/versions.json`. Any other tracked file changed in the site
must be named with `--include` (a file or a folder), or the run stops and names it. Untracked files the run does not
write (such as `assets/site.css`) are left alone and never staged. A dry run stages the commit in its copy of the site
and lists it.

The first release with the editor needs the site's own pages that link it (`git -C ../site status`: the editor links
in the front page, the 404 page, the sitemap and the FM-1 page):

```bash
python3 tools/release.py 0.1.1 --notes NOTES.md --include index.html 404.html sitemap.xml fm1/index.html
```

## `--json`

The last line is one JSON object:

| Key | |
|---|---|
| `version` | the firmware's version (null with `--editor-only`) |
| `fwsc`, `fwsc_sha256` | the package in the site's installer and its SHA-256 |
| `wasm_sha256`, `emu_dir` | the web emulator's build: its wasm's SHA-256, its folder |
| `hello_tag` | the capability query's extras, as the build has them (`editor=nullwave/3;...`) |
| `editor` | the editor script's JSON (null without the editor) |
| `files` | a dry run: each file of the site's `fm1/` added, changed or removed, with its size |
| `include` | the `--include` paths |
| `site_stage` | a dry run: the site commit's files as staged in the copy (status, path) |
| `release_url` | the published GitHub release (null in a dry run) |
| `would_commit`, `would_push` | a dry run: what a real run would commit and push |
| `error` | on a failure: why |

## Afterwards

Check nullwave.cc/fm1/installer/, /emulator/, /quick-start/, /manual/ and /editor/ in dark and light, on a phone too,
and install the release on an FM-1 from the live installer.
