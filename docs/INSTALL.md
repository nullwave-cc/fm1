# Installing nullwave on an FM-1

nullwave is early development software. Install it at your own risk, and back up first.

## 1. Back up the FM-1

Make a full flash backup with [M-Flasher](https://github.com/jvitkauskas/m-flasher) before the first install. Skip
its bootloader option; the backup alone is enough. Keep the file: it puts the FM-1 back exactly as it was.

## 2. Get a package (only for the command line)

Either:

- **From CI:** every pull request and every push to `main` builds the package. Open the run under the repository's
  Actions tab and download the artifact named `nullwave-` and the commit. It holds
  `nullwave.fwsc`.
- **Build it yourself:** once, `./build.sh --setup` fetches the pinned JieLi toolchain and SDK files into
  `base/.toolchain`. Then `./build.sh` writes `app/build/nullwave.fwsc`. On macOS the JieLi tools run in a
  container, so Docker or podman has to be installed and running.

## 3. Install

### From the web (easiest)

Open [nullwave.cc/fm1/installer](https://nullwave.cc/fm1/installer/) in Chrome or Edge, connect the FM-1 by USB, and
follow the page. It installs the latest release, and its "Back to the stock firmware" button puts M-VAVE's firmware
back.

### From the command line

Connect the FM-1 by USB and switch it on. Then:

    python3 -m pip install --user mido python-rtmidi     # once
    python3 base/tools/fm1_install.py app/build/nullwave.fwsc

The installer finds the FM-1, shows what it will write and asks before writing. It works from M-VAVE's stock
firmware and from any firmware built on fm1-base or felucca-bsp. The FM-1 restarts into nullwave when it's done.

If the FM-1 runs M-VAVE's "Open" firmware, put the stock firmware back first: the installer says so.

## Going back

- **To M-VAVE's firmware:** M-VAVE's updater, M-UPGRADE, with their `FM-1.fwsc`, or the "Back to the stock
  firmware" button on fm1-base's web installer.
- **To exactly what you had:** restore the M-Flasher backup.
- **If nullwave starts but USB doesn't answer:** hold REC while switching on. The FM-1 goes into the chip's update
  mode, and the installer or M-UPGRADE can write it again. After two failed starts in a row nullwave also opens a
  safe mode on its own, where USB still works.
- **If the FM-1 doesn't start at all:** restore the M-Flasher backup (its README explains the chip's boot mode).
  Installing m-bootloader, which the web installer offers, adds a recovery menu that works even then: hold PLAY while
  switching on.
