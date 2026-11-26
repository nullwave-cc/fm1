# nullwave fm1: quick start

Nine steps from switching on to a song. Each step: what to press (`press:`, in order, the FM-1's controls as the
site's drawing names them, device.js: the controls held together in a group, the groups in turn between `|`), the
screen you get (`screen:`, a name of the promo set, docs/img/promo; `@splash` the boot logo), and two short lines.
tools/quick_start.py builds the page (nullwave.cc/fm1/quick-start/); tools/doc_check.py checks it. The full reference
is the [manual](../app/docs/MANUAL.md).

## Switch on

press: SCREEN
screen: @splash

Switch the FM-1 on: the logo draws itself in, then PLAY shows channel 1. Press any key to skip the animation.

## Pick a channel and play

press: ALGORITHM | KEYS
screen: PLAY_FM6

Turn **ALGORITHM** to pick one of the six channels, then play the keys. PLAY draws the sound you play as a moving wave, and names the notes under it.

## Change the sound, then shape it

press: PRESETS | KNOB1,KNOB2,KNOB3,KNOB4
screen: PLAY_ANALOG

Turn **PRESETS** for another sound, from every instrument. KNOB 1–4 shape it as you play: the picture moves with them.

## Scales and chords

press: SEL,KEYS | SEL | KNOB2,KNOB3,KNOB4 | KEYS
screen: PLAY_CHORD

Hold **SEL** and press a key to set the root; tap SEL and turn SCALE, KEYS (WHITE) and CHORD. Now one finger plays a chord in key, named on the screen.

## Record a loop

press: REC | KEYS | REC | OCT-,REC
screen: LOOP_MAIN

Tap **REC**: the first note you play starts the loop, and REC again stops recording. SHIFT + REC switches OVERDUB (add) and REPLACE (a new take).

## Drums

press: ALGORITHM | SEQ,KEYS | PLAY STOP
screen: SEQ_DRUMS

Channel 6 is the drum kit. Hold **SEQ** and press keys to set steps, then PLAY.

## Scenes

press: SAVE,KEYS | SAVE,KEYS
screen: LAYER_SAVE

Hold **SAVE** and press a key to launch that scene at the next bar. Hold the key for a second to store what plays now into it.

## A song in order

press: SAVE | SELECT | KEYS
screen: SAVE_CHAIN

Tap **SAVE**, turn SELECT to CHAIN, and tap scene keys in the order the song goes: A1 A2 A2 A1 A3. PLAY plays it in that order.

## Settings

press: HOME | SELECT | KNOB1,KNOB2,KNOB3,KNOB4 | HOME
screen: SET_SCREEN

Hold **HOME** for SETTINGS: the screen, lights, audio, MIDI and more. Turn SELECT for the pages, and hold HOME again to go back.
