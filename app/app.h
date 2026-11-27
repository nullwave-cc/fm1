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

/* the editor (docs/EDITOR-PROTOCOL.md): the selected channel's sound as a file of its own, "preset" (written: heard on
 * the channel at once, kept in a user slot if it asks; read: the channel's sound now), and its parameters, each in a
 * group (osc, filter, env, lfo, fx, mod) for a page to lay out */
#define APP_FILES
int app_file(uint32_t i, plat_file_info_t *f);
int app_file_read(uint32_t i, uint32_t off, uint8_t *dst, uint32_t n);
int app_file_begin(uint32_t i, uint32_t size);
int app_file_write(uint32_t i, uint32_t off, const uint8_t *src, uint32_t n);
int app_file_commit(uint32_t i, uint32_t size, uint32_t crc);
int app_file_delete(uint32_t i);
#define APP_COMPANION_EXTRAS "editor=nullwave/3;editor_cmds=70:2,71:2,72:1,73:1,74:1,75:1,76:1"   /* 3: the views 0x70..0x73
                                         * with the follow 0x74..0x76, each command:its version (docs/EDITOR-PROTOCOL.md) */
#define APP_USB_CMD                      /* app_usb_cmd: 0x74 FOCUS from the editor (the update's framing) */
void app_usb_cmd(const uint8_t *d, uint32_t n);
#define APP_SYSEX                        /* app_sysex: the same frames as the host simulator hands them; and
                                          * plat_sysex_send for 0x75 NAV, 0x76 HELLO-FOCUS */
int app_sysex(const uint8_t *p, uint32_t n);
#define APP_UPDATE_BEGIN                 /* app_update_begin: an install begins, nullwave stops and says so */
int app_update_begin(void);
#define APP_RESTART                      /* plat_restart: after a factory reset */
#define APP_BOOT                         /* app_boot: HOME held at power-on, the settings to their defaults (songs kept) */
void app_boot(void);
#define APP_PARAMS
#define APP_PARAM_MAX 80                /* (57 the channel's, 17 the FM-1's own: room for more) */
int app_param(uint32_t i, plat_param_t *p);
const char *app_param_group(uint32_t g);
int32_t app_param_get(uint32_t i);
int32_t app_param_set(uint32_t i, int32_t v);

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
