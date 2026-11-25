# nullwave

nullwave is a six-channel groovebox firmware for the M-VAVE FM-1. Every channel is equal: make it a synth, a
drum kit, a sampler or a MIDI output, and all six share one pool of voices.

This is version {{VERSION}}, an early build. You install it at your own risk, and the installer can put M-VAVE's
own firmware back.

- [Try it in the browser](emu/).
- [Install](install/) it from Chrome or Edge, with the FM-1 connected by USB.
- The [Manual](manual/).
- [Download {{PKG}}](firmware/{{PKG}}) for the command-line installer: `python3 tools/fm1_install.py {{PKG}}`.
- The [Source](https://github.com/nullwave-cc/fm1) is under MPL-2.0. It's built on fm1-base and felucca-bsp, which
  come from Felucca by Leo Kuroshita, Hügelton Instruments, and uses engines from Felucca-Engines.

M-VAVE and FM-1 are trademarks of their owners. nullwave is not affiliated with M-VAVE.
