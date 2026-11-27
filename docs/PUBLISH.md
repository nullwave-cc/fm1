# Publishing the fm1 pages to nullwave.cc

nullwave.cc is the nullwave-cc/site repository. Its FM-1 product page is `fm1/index.html` there, and the site keeps
the shared pieces: `/style.css`, `/device.js` and `/assets/` (logos, fonts, the FM-1's screens). This repository makes
four folders of `/fm1/`: the installer, the emulator, the quick start and the manual.

A release does all of this with one command, `python3 tools/release.py VERSION`
([RELEASING.md](RELEASING.md)): it builds the pages, publishes them into the site's `fm1/` with the editor, commits
that to the site's **main** (signed) and pushes it, with no pull request. What follows is what it runs, for building
or checking the pages by hand.

## 1. Build the pages

```bash
./build.sh --release VERSION
```

Then the site build (the pages at `app/build/site`, in the site's look by tools/site_look.py), and:

```bash
python3 tools/publish_site.py VERSION OUT
```

`OUT` is a checkout of nullwave-cc/site (or any folder, to look at first). It writes `OUT/fm1/installer/`,
`OUT/fm1/emulator/`, `OUT/fm1/quick-start/` and `OUT/fm1/manual/`, and nothing else:

- the pages use the site's `/style.css`, `/device.js` and `/assets/` logos;
- each folder carries what only it needs: the installer the companion's scripts and the packages it installs, the
  emulator its fonts and words, the quick start and the manual the docs' script and style;
- `--fw-url URL` makes the installer load nullwave's package from a release asset instead of its own copy (the
  host must allow the browser to fetch it: a GitHub release asset does not, so the default is the copy beside it).

It prints the screens the pages show. They are the site's `/assets/screens/` (this repository's
[docs/img/promo](img/promo), by name): copy any that are new or changed there.

## 2. Into the site

`tools/release.py` commits what it wrote in the site (the four folders, `fm1/editor/` and `fm1/versions.json`) to the
site's main in one signed commit and pushes it; `--include PATH ...` adds the site's own tracked files that changed
with it (`index.html`, `404.html`, `sitemap.xml`, `fm1/index.html`), and any other tracked change stops it. `fm1/index.html`, `style.css`,
`device.js` and the rest of the site are otherwise left as they are. Screens that are new or changed go into the
site's `assets/screens/` (the list `publish_site.py` prints) before the release, as their own change.

A version tag (`vX.Y.Z`) also runs [release.yml](../.github/workflows/release.yml): a **draft** GitHub release with
the package. `tools/release.py` checks that its package is the site's (SHA-256) and then publishes it.
