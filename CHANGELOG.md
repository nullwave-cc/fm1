# Changelog

## 0.1.1

The nullwave editor (nullwave.cc/fm1/editor/) now works with the FM-1 over USB.

- The editor follows and controls the FM-1: the transport (PLAY, STOP, REC, the tempo, launching a scene, LOOP or
  SONG), a bar's notes, the scenes and the song's order, the presets, and the settings of SEL KEY, MIDI and the clock.
- SETTINGS → SYSTEM 2 → EDITOR opens the EDITOR page: the knobs set the parameters the editor shows, and the editor
  follows the channel, the preset, SAVE and HOME.
- The FM-1's own rules hold when the editor changes things: a scene launched while playing waits for its point, the
  tempo follows an outside clock, a bar being recorded can't be rewritten.
- The manual describes the editor's page and links the editor; the pages' footers link it too.
- The emulator was listed twice in the pages' footers: once now.
- The editor protocol is in docs/EDITOR-PROTOCOL.md (version 3).

## 0.1.0

First release.
