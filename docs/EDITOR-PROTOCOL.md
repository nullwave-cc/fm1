# nullwave fm1: the editor's protocol

A web editor shapes sounds on a computer and sends them to the FM-1. It talks to the FM-1 over USB MIDI with fm1-base's
companion services: SysEx in the update's framing. Those services are documented in
[base/docs/COMPANION.md](../base/docs/COMPANION.md), and their client is
[base/tools/fm1_companion.py](../base/tools/fm1_companion.py). This page is what nullwave puts in them.

The capability query (`0x60`) carries the tag `editor=nullwave/1`. An editor checks it first. The number goes up only
when something below changes in a way an older editor would misread.

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

The parameters are the **selected channel's**. Their names, ranges and labels follow its engine, so an editor reads the
list again after it changes the engine (parameter 1000) or the channel.

Ids are synth.h's `SP_*` numbers, kept from one release to the next, with new ones added at the end. A few have ids of
their own:

| Id | Name | |
|---|---|---|
| 1000 | ENGINE | the engine (0..13), read and set |
| 1001 | WAVEFORM | read only: the oscillator type now, one of the waveform hints below |
| 1002 | SHAPE NOW | read only: the shape now (0..127, 64 the middle), with its modulation |

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
