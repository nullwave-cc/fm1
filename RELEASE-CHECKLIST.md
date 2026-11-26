# Release checklist: nullwave fm1 0.1.0

Everything below is prepared locally and waits for Adro's approval. Nothing has been committed, tagged, pushed or
published.

## Before

- [ ] Install `nullwave-0.1.0.fwsc` from the local installer on the FM-1, and check that the songs and settings from
      the test builds are still there.
- [ ] Read [CHANGELOG.md](CHANGELOG.md), the [manual](app/docs/MANUAL.md) and the
      [quick start](docs/QUICK-START.md), and check the local pages: the installer, quick start and manual.
- [ ] `sh tests/run_tests.sh` passes. It runs the tests and the docs check.

## Approvals, in order

1. [ ] **Commit** the work on `device-build`, signed, from the Mac.
2. [ ] **Merge** it into the default branch, or open the pull request for it.
3. [ ] **Tag** `v0.1.0`, signed, on that commit.
4. [ ] **Push** the branch and the tag. The tag runs `release.yml`, which makes a draft release.
5. [ ] **GitHub release notes**: review the draft. It has the CHANGELOG entry and the three files. Then publish it.
6. [ ] **Publish to the site**: follow [docs/PUBLISH.md](docs/PUBLISH.md). It copies the pages into
       nullwave-cc/site's `fm1/` and the screens into `assets/screens/`, as a pull request. Review it and merge it.
7. [ ] Check nullwave.cc/fm1/installer/, /fm1/emulator/, /fm1/quick-start/ and /fm1/manual/ live, in dark and
       light, on a phone too.

## Not in the release

- The test builds' installers and packages (0.2-dev, 0.2.1-dev, 0.2.2-dev, 0.3-dev) stay local.
