# Publishing the fm1 pages to nullwave.cc

nullwave.cc is the nullwave-cc/site repository. Its FM-1 product page is `fm1/index.html` there, and the site keeps
the shared pieces: `/style.css`, `/device.js` and `/assets/` (logos, fonts, the FM-1's screens). This repository makes
four folders of `/fm1/`: the installer, the emulator, the quick start and the manual.

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

On a branch of nullwave-cc/site, commit only those four folders (and the screens, if any changed, as their own
change), then open a pull request for the site's review. `fm1/index.html`, `style.css`, `device.js` and the rest of
the site stay as they are.

A version tag (`vX.Y.Z`) also runs [release.yml](../.github/workflows/release.yml): a **draft** GitHub release with
the package, to be published by hand.
