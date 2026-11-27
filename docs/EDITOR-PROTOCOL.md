# nullwave fm1: the editor's protocol

A web editor shapes sounds on a computer and sends them to the FM-1. It talks to the FM-1 over USB MIDI with fm1-base's
companion services: SysEx in the update's framing. Those services are documented in
[base/docs/COMPANION.md](../base/docs/COMPANION.md), and their client is
[base/tools/fm1_companion.py](../base/tools/fm1_companion.py). This page is what nullwave puts in them.

The capability query (`0x60`) carries the tags `editor=nullwave/3` and
`editor_cmds=70:2,71:2,72:1,73:1,74:1,75:1,76:1`. The first is the protocol's version (3: the views 0x70..0x73 below,
with the follow 0x74..0x76). The second lists the editor's commands, each with the highest version it speaks. An editor
checks them first. The protocol's number goes up only when something below changes in a way an older editor would
misread; a command's number goes up when its layout changes in the middle (a field added at the end keeps it).

## What a sound is made of

Each of the six channels has:

- a **sound**: its engine, the engine's eight values (EDIT 1 and EDIT 2), the volume's envelope, the channel's filter,
  the LFO and the sends;
- its **modulation**: ENV 2 (a second envelope) and four mod slots;
- for FM6, its own six-operator patch.

### Oscillator types, by engine

The engine is parameter 1000 (0..13). Its oscillator, where it has a choice, is its first value (EDIT 1's WAVE).

| Engine | Id | Oscillator types (EDIT 1, WAVE) | Shape: what SHAPE / PWM moves |
|---|---|---|---|
| ANALOG | 0 | SAW, SQR, TRI, SIN, PWM, SYNC, SUB (two oscillators, DTN apart, MIX between) | PWM: the width; SYNC: the synced oscillator's pitch |
| FM6 | 1 | six-operator FM (ALG, its operators) | the modulation index |
| PHASE | 2 | phase distortion (WAVE, WAVE2, DCW) | DCW, the distortion |
| LOFI | 3 | chip waves (CHIP, WAVE, DUTY) | DUTY |
| SAMPLE | 4 | a sample (SET) | — |
| VOICE | 5 | a formant voice (VOWL, VOWL2) | — |
| TRIO | 6 | three oscillators (WAVE, INT2, INT3) | PW |
| WHEEL | 7 | a tonewheel organ (REG, SUB, BODY, TOP) | — |
| GRAIN | 8 | grains of a sample (SRC) | — |
| PHYS | 9 | physical models (MODEL) | — |
| NOISE | 10 | filtered noise (MODE, COLR) | — |
| SLICE | 11 | slices of a sample (SRC) | — |
| DRUM | 12 | a drum kit (KIT): eight drums | — |
| SUPER | 13 | **SAW, SQR, TRI** (stacks of seven, DTN apart, the outer six at MIX), **PULSE**, **SYNC**, **MORPH** (saw → square → triangle) | PULSE: the width; SYNC: the ratio, 1..4; MORPH: the position |

SUPER's EDIT 2 is CUT, RES (a 24 dB low-pass), DRIFT (each oscillator wanders a little) and SUB (a square an octave
down). It plays four voices at most: seven oscillators a voice cost about one and a half FM6 voices.

### Modulation: sources, targets, slots

| Source (SRC) | | Target (DEST) | |
|---|---|---|---|
| 0 OFF | | 0 OFF | |
| 1 ENV | the volume's envelope | 1 CUT | the engine's cutoff |
| 2 ENV2 | ENV 2 | 2 PIT | the pitch (63: two octaves) |
| 3 LFO | the channel's LFO | 3 SHP | the engine's shape (PWM, the sync sweep, SUPER's morph) |
| 4 VEL | the note's velocity | 4 AMP | the level |
| 5 PRS | the key's pressure | 5 PAN | the pan |
| 6 RND | a random value each note | 6 LFO | the LFO's depth |

Each slot has a source, a target and an amount (−64..63). It adds source × amount to its target, on top of the fixed
routes the sound already has (ENV DEST: FLT, PIT, SHP; LFO DEST: PIT, FLT, SHP, AMP).

**Why four fixed slots rather than a knob per target.** A slot costs three bytes and one multiply-add a voice a block,
with nothing done for an empty slot. Four slots are 12 bytes a channel, and any source can reach any target. A knob per
pair would be 42 knobs for the same reach. Most of them would stay at zero, in RAM, in every song and every preset. The
FM-1 shows a slot as one page of three knobs (LFO → MOD 1..4), and an editor shows a small matrix.

The LFO's waves are SIN, TRI, SAW, SQR, S&H and RND (a smooth random). Its rate is free (RATE) or follows the tempo
(SYNC: 4 bars to 1/32).

## Parameters (0x62)

Each parameter has an id, a name, a type (a number, or one of labels), min, max, a default and a **group**. The group is
what an editor builds its panels from:

| Group | Index | Parameters |
|---|---|---|
| osc | 0 | the engine's EDIT 1 and EDIT 2 (EDIT 2 is in `filter` when it is the engine's filter: ANALOG, SUPER), VCE, GLD, ENGINE, WAVEFORM, SHAPE NOW |
| filter | 1 | FILTER, CUT, RES, FILT, TYPE |
| env | 2 | ATK, DEC, SUS, REL; FLT, PIT, SHP (ENV DEST); ENV 2's ATK, DEC, SUS, REL |
| lfo | 3 | RATE, WAVE, PHS, FADE, SYNC, TRIG, POL; PIT, FLT, SHP, AMP (LFO DEST) |
| fx | 4 | DST, CHO, DLY, REV, PAN |
| mod | 5 | each slot's SRC, DEST, AMT |
| key | 6 | the FM-1's own: ROOT, SCALE, KEYS, CHORD, TRANS, OCTAVE |
| midi | 7 | the FM-1's own: MIDI CH 1..6, MIDI IN, MIDI OUT |
| clock | 8 | the FM-1's own: CLOCK, CLOCK OUT, SCENE LAUNCH |

The parameters of groups 0..5 are the **selected channel's**. Their names, ranges and labels follow its engine, so an editor reads the
list again after it changes the engine (parameter 1000) or the channel.

Ids are synth.h's `SP_*` numbers, kept from one release to the next, with new ones added at the end. A few have ids of
their own:

| Id | Name | |
|---|---|---|
| 1000 | ENGINE | the engine (0..13), read and set |
| 1001 | WAVEFORM | read only: the oscillator type now, one of the waveform hints below |
| 1002 | SHAPE NOW | read only: the shape now (0..127, 64 the middle), with its modulation |

### Global parameters (0xF000 on)

The FM-1's own settings, the same whichever channel is selected, come after the channel's in the list, with ids from
0xF000. An editor builds their panel from the descriptions (name, group, range, labels) as it does a sound's. A set
does what the knob does on the FM-1: ROOT, SCALE, KEYS, CHORD, TRANS and OCTAVE let go of the notes the keys hold;
each is kept as the FM-1 keeps it (the song's key with the song, the rest with the settings).

| Id | Name | Group | Range | |
|---|---|---|---|---|
| 0xF000 | ROOT | key | 0..11 | C, C#, D .. B |
| 0xF001 | SCALE | key | 0..15 | CHROM, MAJOR, MINOR, DORIAN .. (SEL > KEY's list) |
| 0xF002 | KEYS | key | 0..2 | OFF, SNAP, WHITE: the keys in the scale |
| 0xF003 | CHORD | key | 0..5 | OFF, TRIAD, 7TH, 9TH, SUS4, POWER: a key plays a chord |
| 0xF004 | TRANS | key | −24..24 | semitones (unit `st`) |
| 0xF005 | OCTAVE | key | −2..2 | the keys' octave (OCT− / OCT+) |
| 0xF010..0xF015 | MIDI CH 1..6 | midi | 0..16 | each channel's MIDI channel, in and out (OFF, 1..16). Two channels never share one: a set to a channel another has is refused, the value kept |
| 0xF016 | MIDI IN | midi | 0..17 | NOTES, CLOCK (only), CH 1..16 (that channel to the selected one) |
| 0xF017 | MIDI OUT | midi | 0..1 | KEYS (what the keys play), SEQ (the sequencer too) |
| 0xF020 | CLOCK | clock | 0..3 | the clock source: INT, USB, TRS, BLE (BLE in a build with Bluetooth: max 2 without) |
| 0xF021 | CLOCK OUT | clock | 0..1 | OFF, ON: MIDI clock out while the clock is INT |
| 0xF022 | SCENE LAUNCH | clock | 0..2 | when a launched scene starts: BAR, LOOP, NOW |

The ids are kept from one release to the next; new ones are added, never renumbered. A watch (0x62 op 5) reports these
too, when the FM-1's own knobs change them.

### Waveform hints

WAVEFORM (1001) is an index into this list, so an editor can draw the live shape without knowing each engine:

`SAW, SQR, TRI, SIN, PWM, SYNC, SUB, SUPERSAW, SUPERSQR, SUPERTRI, PULSE, MORPH, FM, PD, CHIP, SAMPLE, FORMANT, TRIO,
ORGAN, GRAIN, MODAL, NOISE, SLICE, KIT`

To draw one cycle `x` (0..1), with `s` = SHAPE NOW / 127:

- **PWM, PULSE:** +1 while `x < 0.05 + 0.9 s`, else −1.
- **SYNC:** a saw of `1 + 3 s` times the note's frequency, restarted at each cycle.
- **MORPH:** a saw blended into a square as `s` goes 0 → 0.5, the square into a triangle as it goes 0.5 → 1.
- **SUPERSAW, SUPERSQR, SUPERTRI:** the plain wave, with the stack shown as six fainter copies offset by DTN.
- **Everything from FM on:** no fixed cycle. An editor shows the engine's name, or the live sound if it has it.

## Presets: the "preset" file (0x61)

The companion's file list has, after the settings, nullwave's own files:

| File | Kind | Does |
|---|---|---|
| `preset` | preset | the selected channel's sound: read and written (below) |
| `song-a` … `song-h` | project | each song slot as it is stored, read only (a backup); empty: size 0 |
| `user-presets` | preset | the 32 user slots (8 KiB), read only |
| `fm6-bank` | bank | the FM6 bank B1–B32 (4 KiB), read only |
| `factory-reset` | other | write only: the words `nullwave factory reset` erase every song, user slot, the FM6 bank and the settings, then the FM-1 restarts as a first start; any other bytes are refused |

`preset` (readable, writable, also while playing) is the selected channel's sound:

- **read:** the channel's sound now, as a preset;
- **write:** the preset plays on the channel at once (to audition it). If its flags ask, it is also kept in a user slot
  (SAVE → USER), but only while the sequencer is stopped. While playing, the FM-1 says `PRESET STOP FIRST`.

The format is version 1, all little-endian:

| Offset | Size | Field |
|---|---|---|
| 0 | 4 | magic `NWPR` |
| 4 | 1 | version: 1 |
| 5 | 1 | engine (0..13) |
| 6 | 1 | flags: bit 0 play it on the selected channel (always, for now), bit 1 keep it in user slot SLOT |
| 7 | 1 | SLOT, 0..31 (USER 1..32) |
| 8 | 1 | the name's length, 0..16 |
| 9 | 16 | the name, padded with 0 |
| 25 | 1 | N, the values that follow |
| 26 | 4 × N | each a u16 parameter id and an i16 value: the parameters of the table above, ENV 2 and the mod slots among them |
| 26 + 4N | 1 | 1: an FM6 patch follows, 0: none |
| 27 + 4N | 96 | the FM6 patch (FM6 only) |

**Versions.** A value is carried by its id, not by its place in a struct. A preset written by one firmware therefore
loads in the next, whatever the next one added:

- an id the FM-1 does not know is skipped;
- an id the preset leaves out keeps the engine's first preset's value;
- a later version only adds fields after these, so this firmware reads on.

User slots keep the sound, the FM6 patch, the modulation and the name. The FM-1 shows a name in capitals: small letters
become capitals, and any character other than a letter, a digit or `- . / + # &` becomes a space. A name with no
length keeps the engine preset's name.

## The preset library: JSON

A preset kept in the library, or exported and imported by the editor, is a JSON file in `presets/<ENGINE>/`, one per
sound:

```json
{
  "format": "nullwave-preset",
  "version": 1,
  "name": "WIDE PAD",
  "engine": "SUPER",
  "tags": ["pad"],
  "params": {"<id>": 0},
  "mods": [{"src": "ENV2", "dst": "CUT", "amt": 32}],
  "fm6": "<base64>"
}
```

- **format, version:** always `nullwave-preset`, then 1. A later version only adds keys.
- **name:** as in the device format, at most 16 characters.
- **engine:** the engine's name (`ANALOG` … `SUPER`), not its id.
- **tags:** free words, for the editor's and the site's lists. The FM-1 never sees them.
- **params:** the values, each keyed by its parameter id as a string (the ids of the table above). ENV 2's values are
  here too.
- **mods:** the mod slots in order, up to four, each with `src` and `dst` by name (the tables above) and `amt`
  (−64..63). A slot left out is off.
- **fm6:** FM6's 96-byte patch in base64, or no key at all.

Readers skip ids and keys they don't know. To send a JSON preset, the editor turns it into the device format above:
the engine by its index, each param as an id/value pair, then each mod as the ids of slot n's SRC, DEST and AMT.

## The firmware's path

1. The editor writes `preset`: begin (size, CRC-32), data (512 bytes at a time), commit. This is fm1-base's companion,
   `device/companion.c`.
2. `app_file_begin`, `app_file_write` and `app_file_commit` (app/app.c) gather it in a RAM buffer of 640 bytes. At
   commit, `preset_take` checks it: magic, version, engine, its length.
3. It sets the engine at its first preset, then each value by id (`synth_param_set`, ENV 2 and the slots too), and the
   FM6 patch.
4. With flag bit 1 and the sequencer stopped, it saves the slot (`user_save`).
5. The screen says `PRESET FROM THE EDITOR`, or `PRESET SAVED USER n`.

A page that sets single values as you turn them uses 0x62 set; the preset file is for sending a whole sound.

The factory reset's own test is [tests/factory_test.py](../tests/factory_test.py) (the backup read, other words
refused, the files empty after). Its host test is [tests/editor_test.py](../tests/editor_test.py), run by tests/run_tests.sh. Against the simulator, it:

- lists the parameters and their groups;
- writes a SUPER preset with ENV 2 and a mod slot, and checks every value took;
- reads it back, its name too (in capitals);
- keeps one in a user slot;
- checks that a file that is not a preset is refused.

## The editor's follow (0x74..0x76)

The FM-1 follows the editor: the editor tells it which parameters its page shows, and the FM-1's knobs set them. Same
framing as the companion's services: `F0 pack7(00 59 cmd len24 body ~sum8(body)) F7`, numbers little-endian.
`0x78`..`0x7F` are kept for nullwave's later commands.

### 0x74 FOCUS (editor → FM-1)

| Offset | Size | Field |
|---|---|---|
| 0 | 1 | version: 1 |
| 1 | 1 | channel, 0..5 |
| 2 | 1 | the title's length, 0..16 |
| 3 | L | the title (ASCII; shown in capitals) |
| 3 + L | 1 | N, the parameters, 0..8 |
| 4 + L | 2 × N | their ids (u16), in knob order: the first four on KNOB 1..4, the next four after SELECT |

The ids are the parameters' of 0x62 (synth.h's `SP_*`, 1000 ENGINE, 1001 WAVEFORM, 1002 SHAPE NOW). The FM-1 selects
the channel (the keys play it), opens its EDITOR page and answers `0x74` with one byte: 0 taken, 1 refused (another
version, a channel past 5, a title past 16, more than 8). **N = 0** clears the focus: the EDITOR page closes. A knob turn
sets its parameter as 0x62 set does, so a watch (0x62 op 5) reports it; a read-only one (WAVEFORM, SHAPE NOW) does not
move.

### 0x75 NAV (FM-1 → editor)

| Offset | Size | Field |
|---|---|---|
| 0 | 1 | event |
| 1 | 1 | its argument (i8) |

| Event | Argument | Sent when, on the EDITOR page |
|---|---|---|
| 1 CH | −1 / +1 | ALGORITHM turned (the FM-1's channel moved too) |
| 2 PAGE | −1 / +1 | SELECT turned past the first or the last four |
| 3 PRESET | −1 / +1 | PRESETS turned (the FM-1's own sound does not change) |
| 4 SAVE | 0 | SAVE tapped |
| 5 SCENE | 0..15 | SAVE + a scene key (the scene launches too) |
| 6 EXIT | 0 | HOME: the page left |

Not answered. Off the EDITOR page, nothing is sent and every control keeps its meaning.

### 0x76 HELLO-FOCUS (FM-1 → editor)

| Offset | Size | Field |
|---|---|---|
| 0 | 1 | version: 1 |
| 1 | 1 | the FM-1's channel |
| 2 | 1 | reason: 0 a computer connected (a new USB session), 1 EDITOR opened on the FM-1 (SETTINGS → SYSTEM 2 → EDITOR) |

The editor answers with a 0x74 FOCUS (for that channel, or its own). Not answered by the FM-1.

Tests: tests/editor_test.py (FOCUS, knobs, NAV, refusals, clearing, HOME) and app/tests/editor.txt (SETTINGS → EDITOR,
HELLO-FOCUS, FOCUS as SysEx, NAV EXIT).

## The editor's views (0x70..0x73)

What the editor shows of the FM-1 and changes on it. The same framing (`F0 pack7(00 59 cmd len24 body ~sum8(body))
F7`), numbers little-endian. Every request starts with a version byte and an op; every answer with the version
asked, a **status** and the op, then its data. Bytes past what a version reads are ignored. An op of 0x80 or more is
a push the FM-1 sends by itself (a subscription): an editor waiting for an answer skips those.

Versions: STATE and STEPS speak 1 and 2 (2 adds each pattern's DIV, step and time inside the layout), SONG and
PRESETS 1. A request in a version the command does not speak is refused (status 1), its answer marked version 1.

| Status | |
|---|---|
| 0 | ok |
| 1 | a bad request (another version, an unknown op, a short body, a channel past 5) |
| 2 | busy: not now (recording that channel, SONG REC, a clock from outside sets the tempo, a factory reset under way) |
| 3 | out of range (a bar past the pattern, an engine or a slot that is not there, a note that does not fit) |

Captured request frames (tests/editor_views_test.py checks them byte for byte):

| Request | Body | Frame |
|---|---|---|
| STATE get | `01 00` | `F0 00 32 41 13 00 00 40 00 00 7C 03 F7` |
| STEPS read, channel 1, bar 1 | `01 00 00 00` | `F0 00 32 45 23 00 00 40 00 00 00 00 70 0F F7` |
| SONG: the scenes | `01 00` | `F0 00 32 49 13 00 00 40 00 00 7C 03 F7` |
| PRESETS: SUPER's | `01 00 0D` | `F0 00 32 4D 1B 00 00 40 00 00 1A 44 07 F7` |
| STATE PLAY | `01 02 01` | `F0 00 32 41 1B 00 00 40 00 02 02 6C 07 F7` |
| STATE TEMPO 125.5 | `01 04 E7 04` | `F0 00 32 41 23 00 00 40 00 04 4E 13 78 00 F7` |
| STATE LAUNCH scene 2 | `01 05 01` | `F0 00 32 41 1B 00 00 40 00 05 02 60 07 F7` |
| STEPS read, version 2 | `02 00 00 00` | `F0 00 32 45 23 00 00 00 01 00 00 00 68 0F F7` |
| PRESETS: channel 6's lanes | `01 03 05` | `F0 00 32 4D 1B 00 00 40 00 03 0A 58 07 F7` |

Tests: tests/editor_views_test.py (the views, the transport, the global parameters, versions 1 and 2, the lanes) and
app/tests/editor_push.txt (the pushes owed while the computer does not read, sent when it reads again).

### 0x70 STATE

| Op | Request (after version, op) | |
|---|---|---|
| 0 get | — | |
| 1 subscribe | 1 on, 0 off | pushes (op 0x80) in the version of this request |
| 2 PLAY | 1 play, 0 stop | as PLAY: play when stopped; stop drops a count-in or a free take, as PLAY does there |
| 3 REC | 0 the REC button, 1 arm, 2 off | on the selected channel. 0 does what tapping REC does. 1, with REC off: stopped, armed (recording starts as REC START says); playing, recording at once, as on the FM-1; already on, nothing. 2: REC off from any state (a free take closed, kept) |
| 4 TEMPO | BPM × 10, u16 (400..2400) | out of range (3) past it; busy (2) while CLOCK is not INT (the clock sets the tempo) |
| 5 LAUNCH | scene, 0..15 | stopped: at once; playing: queued, starting at SCENE LAUNCH's point (BAR, LOOP, NOW), as SAVE + a key |
| 6 MODE | 0 LOOP, 1 SONG | |

Every op answers with the state (as it is after the op; when refused, as it was), status first. A bad request (1)
answers without it. Subscribed, the FM-1 pushes the state (op 0x80) when it changes, 10 times a second at most, from
its main loop (never the sound's time).

A push goes out only when the SysEx ring has room for its whole frame, so the main loop never waits on a computer that
is slow to read. One that finds no room is owed: the FM-1 tries again on each pass of its loop and sends the state as it
is then, the newest. A change is late at worst, never lost. The same holds for the playhead push (0x71 op 0x82).

The state (version 1: 162 bytes; version 2: 186, answer and push):

| Offset | Size | Field |
|---|---|---|
| 0 | 3 | version 1, status, op |
| 3 | 1 | transport: bit 0 playing, 1 recording, 2 REC armed, 3 counting in, 4 SONG mode, 5 SONG REC, 6 REC MODE REPLACE |
| 4 | 2 | tempo, BPM × 10 |
| 6 | 1 | the selected channel, 0..5 |
| 7 | 1 | its pattern's bars, 1..8 |
| 8 | 1 | the bar playing, 0.. (0xFF stopped) |
| 9 | 1 | the step in that bar, at the pattern's DIV (0xFF stopped) |
| 10 | 1 | battery, % (0xFF: on USB power, or not read yet) |
| 11 | 1 | power: bit 0 USB powers it, bit 1 charging |
| 12 | 1 | the scene playing, 0..15 |
| 13 | 1 | the scene queued (0xFF none) |
| 14 | 1 | the song's sections |
| 15 | 1 | the section playing (0xFF none) |
| 16 | 1 | that section's scene (0xFF none) |
| 17 | 1 | REC: 0 off, 1 armed, 2 counting in, 3 a free take, 4 recording |
| 18 | 6 × 24 | version 1, each channel: engine, preset, level (0..127), flags (bit 0 muted, 1 soloed, 2 heard, 3 HOLD, 4 drones sounding), its pattern (0..15), its bars, voices sounding, name (16, 0-padded: the channel's or its engine's), 1 kept (0) |
| 18 | 6 × 28 | version 2, each channel: engine, preset, level, flags, pattern, bars, voices (as version 1), then **the pattern's time** (5: DIV, step in ticks u16, beats, beat unit), then name (16) |

The pattern's time, here and in STEPS version 2: DIV is the pattern's step (0 1/16, 1 1/8, 2 1/32, 3 1/16T, 4 1/4,
5 1/8T, 6 1/2, 7 a bar, 8 two bars), the step its length in ticks (96 a quarter: 24, 48, 12, 16, 96, 32, 192, 384,
768), then the time signature as beats and beat unit. Every pattern in this firmware is in 4/4 (a bar is 384 ticks), so
those two read 4 and 4; they are there so an editor draws bars from them rather than assuming it.

### 0x71 STEPS

A note is 6 bytes: step (at the pattern's DIV, within the bar), offset from that step (ticks, i8; 96 ticks a quarter),
note (0..127), velocity (1..127), length (6 ticks a unit, 1..255), flags (bits 0..1 ratchet − 1, 8 FILL only, 16 NO
FILL, 32 DRONE: on till the transport stops). Parameter locks are not notes: a write keeps them.

| Op | Request (after version, op) | Answer (after version, status, op) |
|---|---|---|
| 0 read | channel, bar (0..) | version 1: channel, bar, the pattern's bars, its DIV, N, N notes. Version 2: channel, bar, bars, the pattern's time (5: DIV, step u16, beats, beat unit), N, N notes |
| 1 write | channel, bar, N, N notes: the bar's notes replaced (undone with EDIT + OCT−) | as read: the bar as it is now |
| 2 playhead | channel, 1 on / 0 off | — |
| 0x82 push | — | channel, bar, step: each time the playhead moves a step |

A write is refused busy (2) while that channel records; out of range (3) for a bar past the pattern or a note past it.

### 0x72 SONG + SCENES

| Op | Request (after version, op) | Answer (after version, status, op) |
|---|---|---|
| 0 scenes | — | 16, then each scene 16 bytes: pattern × 6, mute bits, solo bits, level × 6, its own sounds (0/1), used (0/1) |
| 1 a scene | scene, pattern × 6, mute bits, solo bits, level × 6 | the scene's number, then the scene as it is now (16 bytes, as in op 0) |
| 2 the song | — | N, END (0 LOOP, 1 STOP), mode (0 LOOP, 1 SONG), N × (scene, bars, repeats) |
| 3 the song written | N (0..64), END, N × (scene, bars 0..64, repeats 1..16) | as 2 |

Writing the song is refused busy (2) during SONG REC.

### 0x73 PRESETS

| Op | Request (after version, op) | Answer (after version, status, op) |
|---|---|---|
| 0 factory | engine (0..13) | engine, N, N × (index, name 16) |
| 1 user | first slot (0..31), count (≤ 16) | first, count, each (slot, used, engine or 0xFF, name 16) |
| 2 load | channel, kind (0 factory, 1 user), engine (factory), index (or user slot) | channel, engine, preset, name 16 |
| 3 lanes | channel | channel, N, N × (lane, note, name 16): the kit's lanes, the MIDI note each plays and its name. N is 8 on a DRUM channel (KICK 36, SNARE 38, CLAP 39, HATCL 42, HATOP 46, TOM 45, RIM 37, BELL 56), 0 on any other |

A load is the same as turning PRESETS on the FM-1: allowed while playing. An empty user slot or an index past the
engine's sounds: out of range (3).
