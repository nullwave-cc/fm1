/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Charles Vestal
 * Copyright (C) 2026 nullwave contributors */
/* nullwave: the app on fm1-base. Six channels of synths and drum kits (synth.c), patterns, scenes and
 * songs (seq.c).
 * This header is the app's half of the contract with the platform (device/unity.c, host/plat_host.c). */
#pragma once
#include <stdint.h>
#include "plat.h"

#define APP_NAME "nullwave"
#define APP_SPLASH_RGB RGB(229, 32, 46)
#define APP_BLOCK 256u                   /* frames per audio block: 5.8 ms at 44.1 kHz */
#define APP_TOUCH                        /* velocity and pressure from the keys (lib/touch.h) */
#define APP_BATTERY_RAW                  /* plat_battery_charging: the battery shows a bolt while charging */
#define APP_SPLASH                       /* app_splash: the nullwave logo at boot (art.h) */
#define APP_SYSEX_BYTE                   /* app_sysex_byte: DX7 SysEx for FM6 (a voice, a bank) */
void app_sysex_byte(uint8_t b);
#define APP_OVERLOAD                     /* app_overload: the render ran late (the screen says so) */
void app_overload(void);
void app_splash(void);
#define APP_BTDEV                        /* Bluetooth MIDI devices (lib/btdev.h) */
#define APP_BLE_AT_BOOT (!nullwave_ble_off())   /* Bluetooth at power-on: as the player left it (REC held) */
int nullwave_ble_off(void);

/* the store: the app's objects, the panel calibration (the platform's) and Bluetooth's address and pairing among
 * them, and their flash offsets */
enum { OBJ_SETTINGS, OBJ_PANEL, OBJ_BLE, OBJ_SOUND, OBJ_NOBJ };
#define APP_STORE_MAP {0xFC000u, 0xDC000u, 0xDE000u, 0xDA000u}

/* the objects a computer may keep as files (fm1.toml companion = true: web/companion, tools/fm1_companion.py): object,
 * kind, what it may do, the file's name. Never OBJ_PANEL (the unit's own) or OBJ_BLE (this FM-1's pairings) */
#define APP_FILE_OBJS {{OBJ_SETTINGS, PLAT_FK_SETTINGS, PLAT_FF_READ | PLAT_FF_WRITE, "settings"}}
int app_file_busy(void);                 /* 1: no file written now (playing) */
void app_file_changed(uint32_t obj);     /* the computer wrote obj: load it again */

/* the contract: the platform calls these */
void app_init(void);                     /* boot: the engine, then the saved settings (or defaults) */
void app_render(int32_t *lr, uint32_t n, uint32_t master_q12);   /* audio interrupt: n stereo frames, 24-bit */
void app_panic(void);                    /* silence now (before an update rewrites the flash) */
void app_ui_init(void);                  /* the first screen, after the splash */
void app_frame(void);                    /* the UI: input and a redraw, about 60 times a second */
void app_input_only(void);               /* between frames: input only */
void app_say(const char *a, const char *b);   /* a short message on the screen and to a screen reader */

/* the host simulator's checks (host/plat_host.c "expect"): octave, wave, note (-1 none), dirty, turnsN */
#define APP_HOST_EXPECT
int app_expect(const char *what, int *got);
