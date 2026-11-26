# Publishing a release to nullwave.cc

nullwave.cc is the nullwave-cc/site repository. Its FM-1 product page is `fm1/index.html` there. This repository
makes the rest of `/fm1/`: the installer, the emulator, the quick start and the manual.

A version tag (`v0.1.0`) runs [release.yml](../.github/workflows/release.yml). It makes a **draft** GitHub release
with three files:

- `nullwave-0.1.0.fwsc`, the package.
- `nullwave-fm1-site-0.1.0.tar.gz`, the pages for `fm1/` on the site: `installer/`, `emulator/`, `quick-start/`,
  `manual/`, `companion/`, `firmware/`, `fonts/`, `words/`, `assets/` and `fm1ports.js`. It has no `index.html`:
  the site keeps its own product page.
- `nullwave-fm1-screens-0.1.0.tar.gz`, the screens for the site's `assets/screens/`. These are the 960 × 960 PNG and
  WebP pictures of [docs/img/promo](img/promo), with the site's file names. The site's FM-1 drawing loads them.

## Copying them into the site

In a checkout of nullwave-cc/site, on a new branch:

```bash
gh release download v0.1.0 --repo nullwave-cc/fm1 --pattern 'nullwave-fm1-*.tar.gz'
```

```bash
tar -xzf nullwave-fm1-site-0.1.0.tar.gz -C fm1
```

```bash
tar -xzf nullwave-fm1-screens-0.1.0.tar.gz -C assets/screens
```

Then check the result locally and open a pull request.

- The site's `fm1/index.html` must still be its own. The site archive has none, so it is not touched.
- The earlier test builds' installers (`installer-0.2-dev` and the like) are never in a release. If an older copy
  of them is in the site, remove it.

## Or by a workflow in the site

[publish/site-fm1.yml](publish/site-fm1.yml) does the same in nullwave-cc/site. Copy it there as
`.github/workflows/fm1.yml` and run it by hand with a version. It opens a pull request; it never pushes to `main`.
