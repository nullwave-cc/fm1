/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 nullwave contributors */
/* nullwave's controls and screen (in app.c's unit, after ui_draw.c). The controls follow the FM-1 firmwares players
 * know: a tap of a function button opens its pages (the footer's four knobs; SELECT turns the pages), a hold turns
 * the 16 white keys and KNOB 1..4 into its layer (GLO: mute, solo, levels, tap tempo; SEL: the key; EDIT: erase,
 * shift, length; SEQ: the steps; SAVE: the scenes), a hold of a layer and a tap of HOME keeps it open. ALGORITHM picks
 * the channel, PRESETS its sound. OCT- / OCT+ held are SHIFT (SHIFT + GLO: nullwave's settings); a press alone moves
 * the octave on release. HOME held: the menu. app/docs/MANUAL.md has it all.
 *
 * The screen: the top bar (song and scene, the bars, the tempo, the transport, the battery), the channels' rows (or a
 * layer's 16 tiles), the footer (the page's title and its four knobs). Each part is drawn in its
 * own band and only when what it shows has changed. */

/* --------------------------------------------------------------- pages --- */
enum { PG_HOME, PG_ENV, PG_FX, PG_FILTER, PG_DELAY, PG_REVERB, PG_KEY, PG_KEY2, PG_EDIT1, PG_EDIT2, PG_VOICE,
       PG_PATTERN, PG_GLOBAL, PG_MIDI, PG_MIDI2, PG_MIDI3, PG_SONG, PG_SCENE, PG_CHAIN, PG_REC,
       PG_MASTER, PG_SYSTEM, PG_DRUMS, PG_MIDI56,                           /* GLO's */
       PG_C_KEYS, PG_C_SCREEN, PG_C_BT, PG_C_SCENE,                         /* SHIFT + GLO: nullwave's settings */
       PG_M_SCREEN, PG_M_LIGHTS, PG_M_SYSTEM,
       PG_ENVDEST, PG_LFO, PG_LFO2, PG_LFODEST, PG_SLICER, PG_INSERT, PG_INSERT2, PG_AFILTER, PG_VOICE2,
       PG_FM_OP, PG_FM_ENV, PG_FM_OP2, PG_LANES, PG_LANES2, PG_ARP, PG_ARP2, PG_TEXTURE, PG_TEXTURE2,
       PG_PRESETS, PG_USER, PG_TOOLS, PG_M_AUDIO, PG_M_SEQ, PG_NOTES, PG_DGRID, PG_PITCH,
       PG_NAME, PG_M_SCREEN2, PG_ENV2, PG_MOD1, PG_MOD2, PG_MOD3, PG_MOD4, PG_M_SYSTEM2, NPG };   /* ENV 2, the mod slots */   /* NAME: SHIFT + SAVE, the channel's name */                        /* HOME held: the menu */
/* the page groups that are no button's own: nullwave's settings, the menu */
enum { G_CONF = NB, G_MENU, NGROUP };

/* what a knob does: a sound's parameter (KN_SP + SP_*), an effect's, the pattern's, the song's, a setting */
enum {
    KN_NONE,
    KN_SP,                               /* + SP_* */
    KN_EK = KN_SP + SP_N,                /* + 0..3: the engine's own four (EDIT 1) */
    KN_EP = KN_EK + 4,                   /* + 0..7: the engine's eight */
    KN_FX = KN_EP + 8,                   /* + FX_* */
    KN_SWING = KN_FX + FX_N, KN_LEVEL, KN_BARS, KN_DIV, KN_PATIDX, KN_BPM, KN_CLICK, KN_LAUNCH, KN_MODE,
    KN_ROOT, KN_SCALE, KN_KEYS, KN_CHORD, KN_TRANS, KN_STRUM, KN_VLEAD,
    KN_MIDI, KN_SYNC = KN_MIDI + NCH, KN_CLKOUT, KN_SEQOUT,
    KN_SLOT, KN_LOAD, KN_SAVE, KN_NEW,
    KN_SCN, KN_SCN_CH, KN_SCN_PAT, KN_SCN_SND,
    KN_CS, KN_CS_SCENE, KN_CS_BARS, KN_CS_REPS,
    KN_RMODE, KN_RBARS, KN_RSTART, KN_RQUANT,
    KN_SNOTE, KN_ESHIFT, KN_ELEN, KN_ETRANS,
    KN_LVL,                              /* + 0..3: GLO's layer, the levels of channels 1..4 */
    KN_STNOTE = KN_LVL + 4, KN_STVEL, KN_STRAT, KN_STNUDGE, KN_LOCKP, KN_LOCKV,
    KN_VSENS, KN_LEARN, KN_FIXVEL, KN_PRESS, KN_DISPLAY, KN_BT, KN_ACCEL, KN_LIGHTS, KN_SCALEKEYS, KN_AUTOSAVE,
    KN_ABOUT, KN_BTDEV, KN_BTGO, KN_GRAPHS,
    KN_GSWING, KN_TUNE, KN_DUST, KN_DUCK, KN_MFILT, KN_ROLL, KN_IN, KN_OTHER, KN_BEND, KN_CPU, KN_FXCH, KN_DRMAP,
    KN_REMOTE, KN_DR_CH, KN_DR_LVL, KN_DR_REV, KN_DR_DLY,
    KN_MST,                              /* + MST_* */
    KN_FM_OP = KN_MST + MST_N, KN_FM_RATIO, KN_FM_FINE, KN_FM_LEVEL, KN_FM_STAGE, KN_FM_RATE, KN_FM_ELEVEL,
    KN_FM_MODE, KN_FM_DTUN, KN_FM_VEL, KN_VMUTE, KN_PFADE,
    KN_PRE_SOUND, KN_PRE_ENGINE, KN_PRE_FAV, KN_USLOT, KN_ULOAD, KN_UERASE, KN_USAVE, KN_CLRSQ, KN_INIT,
    KN_LOWCUT, KN_USBA, KN_RESO, KN_STEPADV, KN_DRPREV, KN_RREPL,
    KN_NSEL, KN_NPITCH, KN_NMOVE, KN_NLEN, KN_DLANE, KN_DSTEP, KN_DHIT, KN_DVEL,
    KN_NMCUR, KN_NMCHR, KN_NMPRE, KN_NMCLR,
    KN_HOMEVIEW, KN_AUTOLOOP, KN_FACTORY, KN_BATT,             /* SETTINGS SCREEN 2: MAIN's view at power-on; REC shows LOOP */   /* NAME: the cursor, its character, a ready name, clear */
    KN_N
};

static const char *const PAGE_TITLE[NPG] = {"MAIN", "ENV", "FX", "FILTER", "DELAY", "REVERB", "KEY", "KEY 2",
                                            "EDIT 1", "EDIT 2", "VOICE", "PATTERN", "GLOBAL", "MIDI", "MIDI 2",
                                            "MIDI 3", "SONG", "SCENE", "CHAIN", "REC", "MASTER", "SYSTEM", "DRUMS",
                                            "MIDI", "KEYS", "SCREEN", "BT", "SCENE", "SCREEN", "LIGHTS", "SYSTEM",
                                            "ENV DEST", "LFO", "LFO 2", "LFO DEST", "SLICER", "INSERT", "INSERT 2",
                                            "FILTER", "VOICE 2", "OPERATOR", "OP ENV", "OPERATOR 2", "LANES",
                                            "LANES 2", "ARP", "ARP 2", "TEXTURE", "TEXTURE 2", "PRESETS", "USER",
                                            "TOOLS", "AUDIO", "SEQ", "NOTES", "DRUMS", "PITCH", "NAME", "SCREEN 2", "ENV 2", "MOD 1", "MOD 2", "MOD 3", "MOD 4", "SYSTEM 2"};
static const uint16_t PAGE_KN[NPG][4] = {
    [PG_HOME] = {KN_SWING, KN_LEVEL, KN_BARS, KN_SP + SP_PAN},
    [PG_ENV] = {KN_SP + SP_ATK, KN_SP + SP_DEC, KN_SP + SP_SUS, KN_SP + SP_REL},
    [PG_FX] = {KN_SP + SP_DST, KN_SP + SP_CHO, KN_SP + SP_DLY, KN_SP + SP_REV},
    [PG_FILTER] = {KN_SP + SP_TFILT, KN_SP + SP_FLT, KN_SP + SP_CUT, KN_SP + SP_RES},
    [PG_DELAY] = {KN_FX + FX_DTIME, KN_FX + FX_DFB, KN_FX + FX_DTONE, KN_FX + FX_DLVL},
    [PG_REVERB] = {KN_FX + FX_RSIZE, KN_FX + FX_RDAMP, KN_MST + MST_CRT, KN_MST + MST_CDP},
    [PG_KEY] = {KN_ROOT, KN_SCALE, KN_KEYS, KN_CHORD},
    [PG_KEY2] = {KN_TRANS, KN_STRUM, KN_VLEAD, KN_NONE},
    [PG_EDIT1] = {KN_EP + 0, KN_EP + 1, KN_EP + 2, KN_EP + 3},
    [PG_EDIT2] = {KN_EP + 4, KN_EP + 5, KN_EP + 6, KN_EP + 7},
    [PG_VOICE] = {KN_SP + SP_MONO, KN_SP + SP_GLIDE, KN_SP + SP_GLMOD, KN_SP + SP_PRIO},
    [PG_PATTERN] = {KN_BARS, KN_DIV, KN_SWING, KN_PATIDX},
    [PG_GLOBAL] = {KN_BPM, KN_GSWING, KN_CLICK, KN_TUNE},
    [PG_MASTER] = {KN_MST + MST_DUST, KN_MST + MST_DUCK, KN_MST + MST_FILT, KN_MST + MST_ROLL},
    [PG_ENVDEST] = {KN_SP + SP_FENV, KN_SP + SP_PENV, KN_SP + SP_SENV, KN_NONE},
    [PG_LFO] = {KN_SP + SP_LFO_RATE, KN_SP + SP_LFO_WAVE, KN_SP + SP_LFO_PHASE, KN_SP + SP_LFO_FADE},
    [PG_LFO2] = {KN_SP + SP_LFO_SYNC, KN_SP + SP_LFO_TRIG, KN_SP + SP_LFO_POL, KN_NONE},
    [PG_LFODEST] = {KN_SP + SP_LFO_PIT, KN_SP + SP_LFO_FLT, KN_SP + SP_LFO_SHP, KN_SP + SP_LFO_AMP},
    [PG_SLICER] = {KN_SP + SP_SLC, KN_SP + SP_SLC_PAT, KN_SP + SP_SLC_RATE, KN_SP + SP_SLC_DEPTH},
    [PG_INSERT] = {KN_SP + SP_INS, KN_SP + SP_INS_A, KN_SP + SP_INS_B, KN_SP + SP_INS_C},
    [PG_INSERT2] = {KN_SP + SP_INS_MIX, KN_NONE, KN_NONE, KN_NONE},
    [PG_AFILTER] = {KN_SP + SP_FTYPE, KN_SP + SP_E4, KN_SP + SP_E5, KN_SP + SP_E7},
    [PG_VOICE2] = {KN_SP + SP_ALLOC, KN_SP + SP_DTUNE, KN_SP + SP_PAN, KN_VMUTE},
    [PG_FM_OP] = {KN_FM_OP, KN_FM_RATIO, KN_FM_FINE, KN_FM_LEVEL},
    [PG_FM_ENV] = {KN_FM_OP, KN_FM_STAGE, KN_FM_RATE, KN_FM_ELEVEL},
    [PG_FM_OP2] = {KN_FM_OP, KN_FM_MODE, KN_FM_DTUN, KN_FM_VEL},
    [PG_LANES] = {KN_SP + SP_X0 + 0, KN_SP + SP_X0 + 1, KN_SP + SP_X0 + 2, KN_SP + SP_X0 + 3},
    [PG_LANES2] = {KN_SP + SP_X0 + 4, KN_SP + SP_X0 + 5, KN_SP + SP_X0 + 6, KN_SP + SP_X0 + 7},
    [PG_ARP] = {KN_SP + SP_ARP, KN_SP + SP_ARP_RATE, KN_SP + SP_ARP_OCT, KN_SP + SP_ARP_GATE},
    [PG_ARP2] = {KN_SP + SP_ARP_SWING, KN_SP + SP_ARP_PROB, KN_SP + SP_ARP_HOLD, KN_SP + SP_ARP_ORDER},
    [PG_TEXTURE] = {KN_MST + MST_TSRC, KN_MST + MST_TLEVEL, KN_MST + MST_TTONE, KN_MST + MST_TMOVE},
    [PG_TEXTURE2] = {KN_MST + MST_TRATE, KN_MST + MST_TWHEN, KN_MST + MST_TDUCK, KN_NONE},
    [PG_PRESETS] = {KN_PRE_SOUND, KN_PRE_ENGINE, KN_NONE, KN_PRE_FAV},
    [PG_USER] = {KN_USLOT, KN_ULOAD, KN_UERASE, KN_USAVE},
    [PG_TOOLS] = {KN_CLRSQ, KN_INIT, KN_NONE, KN_NEW},
    [PG_M_AUDIO] = {KN_LOWCUT, KN_USBA, KN_RESO, KN_NONE},
    [PG_M_SEQ] = {KN_STEPADV, KN_DRPREV, KN_LAUNCH, KN_RREPL},
    [PG_NOTES] = {KN_NSEL, KN_NPITCH, KN_NMOVE, KN_NLEN},
    [PG_DGRID] = {KN_DLANE, KN_DSTEP, KN_DHIT, KN_DVEL},
    [PG_PITCH] = {KN_SP + SP_PENV, KN_SP + SP_LFO_PIT, KN_TRANS, KN_SP + SP_GLIDE},
    [PG_NAME] = {KN_NMCUR, KN_NMCHR, KN_NMPRE, KN_NMCLR},
    [PG_M_SCREEN2] = {KN_HOMEVIEW, KN_AUTOLOOP, KN_NONE, KN_NONE},
    [PG_ENV2] = {KN_SP + SP_E2_ATK, KN_SP + SP_E2_DEC, KN_SP + SP_E2_SUS, KN_SP + SP_E2_REL},
    [PG_MOD1] = {KN_SP + SP_M1_SRC, KN_SP + SP_M1_DST, KN_SP + SP_M1_AMT, KN_NONE},
    [PG_MOD2] = {KN_SP + SP_M2_SRC, KN_SP + SP_M2_DST, KN_SP + SP_M2_AMT, KN_NONE},
    [PG_MOD3] = {KN_SP + SP_M3_SRC, KN_SP + SP_M3_DST, KN_SP + SP_M3_AMT, KN_NONE},
    [PG_MOD4] = {KN_SP + SP_M4_SRC, KN_SP + SP_M4_DST, KN_SP + SP_M4_AMT, KN_NONE},
    [PG_SYSTEM] = {KN_NONE, KN_NONE, KN_NONE, KN_NONE},
    [PG_DRUMS] = {KN_DR_LVL, KN_DR_REV, KN_DR_DLY, KN_NONE},
    [PG_MIDI] = {KN_MIDI + 0, KN_MIDI + 1, KN_MIDI + 2, KN_MIDI + 3},
    [PG_MIDI56] = {KN_MIDI + 4, KN_MIDI + 5, KN_IN, KN_OTHER},
    [PG_MIDI2] = {KN_SEQOUT, KN_BEND, KN_DRMAP, KN_FXCH},
    [PG_MIDI3] = {KN_REMOTE, KN_CLKOUT, KN_SYNC, KN_NONE},
    [PG_C_KEYS] = {KN_VSENS, KN_LEARN, KN_FIXVEL, KN_PRESS},
    [PG_C_SCREEN] = {KN_NONE, KN_NONE, KN_NONE, KN_NONE},
    [PG_C_BT] = {KN_BT, KN_BTDEV, KN_BTGO, KN_NONE},
    [PG_C_SCENE] = {KN_NONE, KN_NONE, KN_NONE, KN_NONE},
    [PG_M_SCREEN] = {KN_DISPLAY, KN_ACCEL, KN_GRAPHS, KN_NONE},
    [PG_M_LIGHTS] = {KN_LIGHTS, KN_SCALEKEYS, KN_NONE, KN_NONE},
    [PG_M_SYSTEM] = {KN_ABOUT, KN_AUTOSAVE, KN_CPU, KN_BATT},
    [PG_M_SYSTEM2] = {KN_FACTORY, KN_NONE, KN_NONE, KN_NONE},
    [PG_SONG] = {KN_SLOT, KN_LOAD, KN_SAVE, KN_NEW},
    [PG_SCENE] = {KN_SCN, KN_SCN_CH, KN_SCN_PAT, KN_SCN_SND},
    [PG_CHAIN] = {KN_CS, KN_CS_SCENE, KN_CS_BARS, KN_CS_REPS},
    [PG_REC] = {KN_RMODE, KN_RBARS, KN_RSTART, KN_RQUANT},
};
/* each button's pages, in the order SELECT and a second tap turn them (-1: the end) */
#define GMAX 14
static const int8_t GROUP[NGROUP][GMAX] = {
    [B_FX] = {PG_FX, PG_FILTER, PG_SLICER, PG_INSERT, PG_INSERT2, PG_DELAY, PG_REVERB, -1},
    [B_SEL] = {PG_KEY, PG_KEY2, -1},
    [B_ENV] = {PG_ENV, PG_ENVDEST, PG_ENV2, -1},
    [B_LFO] = {PG_LFO, PG_LFO2, PG_LFODEST, PG_MOD1, PG_MOD2, PG_MOD3, PG_MOD4, -1},
    [B_EDIT] = {PG_EDIT1, PG_EDIT2, PG_AFILTER, PG_FM_OP, PG_FM_ENV, PG_FM_OP2, PG_LANES, PG_LANES2, PG_VOICE,
                PG_VOICE2, PG_PITCH, -1},
    [B_GLO] = {PG_GLOBAL, PG_MASTER, PG_TEXTURE, PG_TEXTURE2, PG_DRUMS, -1},   /* the song's */
    [B_HOME] = {PG_HOME, -1},
    [B_SAVE] = {PG_PRESETS, PG_USER, PG_SONG, PG_SCENE, PG_CHAIN, PG_TOOLS, -1},
    [B_ARP] = {PG_ARP, PG_ARP2, -1},     [B_SEQ] = {PG_PATTERN, PG_NOTES, PG_DGRID, -1},
    [B_PLAY] = {-1}, [B_REC] = {PG_REC, -1}, [B_OCTDN] = {-1}, [B_OCTUP] = {-1},
    [G_CONF] = {-1},
    /* SETTINGS (HOME held, SHIFT + GLO): the FM-1's own, each once, in groups of pages */
    /* (SCREEN, LIGHTS, AUDIO, SEQ, SYSTEM, then MIDI, KEYS, BLUETOOTH) */
    [G_MENU] = {PG_M_SCREEN, PG_M_SCREEN2, PG_M_LIGHTS, PG_M_AUDIO, PG_M_SEQ, PG_M_SYSTEM, PG_M_SYSTEM2, PG_MIDI, PG_MIDI56, PG_MIDI2, PG_MIDI3,
                PG_C_KEYS, PG_C_BT, -1},
};
static const char *set_group(uint32_t pg)   /* a SETTINGS page's group */
{
    switch (pg) {
    case PG_C_KEYS: return "KEYS";
    case PG_M_SCREEN: case PG_M_SCREEN2: return "SCREEN";
    case PG_M_LIGHTS: return "LIGHTS";
    case PG_M_AUDIO: return "AUDIO";
    case PG_MIDI: case PG_MIDI56: case PG_MIDI2: case PG_MIDI3: return "MIDI";
    case PG_C_BT: return "BLUETOOTH";
    case PG_M_SEQ: return "SEQ";
    default: return "SYSTEM";
    }
}
/* the pages an engine has or not: ANALOG's FILTER, FM6's operators, DRUM's lanes */
static int page_shown(int pg)
{
    uint32_t e = synth_sound(set.channel)->engine;
    if (pg == PG_AFILTER)
        return e == 0u;
    if (pg == PG_FM_OP || pg == PG_FM_ENV || pg == PG_FM_OP2)
        return e == 1u;
    if (pg == PG_LANES || pg == PG_LANES2 || pg == PG_DGRID)
        return e == 12u;
    if (pg == PG_NOTES)
        return e != 12u;
    return 1;
}
static int has_layer(uint32_t b)
{
    return b == B_GLO || b == B_SEL || b == B_EDIT || b == B_SEQ || b == B_SAVE || b == B_FX || b == B_ARP;
}
static const uint16_t LAYER_KN[NB][4] = {
    [B_GLO] = {KN_LVL + 0, KN_LVL + 1, KN_LVL + 2, KN_LVL + 3},
    [B_SEL] = {KN_CHORD, KN_SCALE, KN_KEYS, KN_TRANS},
    [B_EDIT] = {KN_ESHIFT, KN_ELEN, KN_ETRANS, KN_NONE},
    [B_SEQ] = {KN_SNOTE, KN_DIV, KN_SWING, KN_BARS},
    [B_FX] = {KN_MST + MST_FILT, KN_MST + MST_DUST, KN_MST + MST_DUCK, KN_SP + SP_TFILT},
    [B_ARP] = {KN_MST + MST_ROLL, KN_NONE, KN_NONE, KN_NONE},
};
static const uint16_t STEP_KN[4] = {KN_STNOTE, KN_STVEL, KN_STRAT, KN_STNUDGE};

#define TAP_MS 500u                      /* a press shorter than this, with nothing else done: a tap */
#define LAYER_SHOW_MS 350u               /* a held layer shows after this (or at once when used) */
#define HOME_HOLD_MS 700u                /* HOME held: the menu */
#define REC_HOLD_MS 2000u                /* REC held: the channel cleared */
#define GO_MS 1500u                      /* LOAD, SAVE, NEW: a second turn within this */

static struct {
    char msg[2][24];                     /* a short message in the footer's title */
    uint32_t msg_until;
    uint32_t keys, btn;                  /* held at the last input */
    uint8_t vel[NKEYS], prs[NKEYS];      /* each key's last velocity, and its pressure while held */
    int8_t key_note[NKEYS][5];           /* the notes each held key started (-1 none) */
    uint8_t key_ch[NKEYS];
    int32_t last_key;
    uint8_t page, group_btn;             /* the page shown, the button whose pages they are */
    int8_t layer;                        /* the layer held or kept (a button), -1 none */
    uint8_t layer_lock, layer_used;
    uint32_t down_t[NB];                 /* when each button went down */
    uint8_t used[NB];                    /* something else was done while it was held */
    uint8_t oct_used;
    uint32_t fr_arm, fr_hold;            /* FACTORY RESET: armed (when), PLAY held since (0 not) */
    uint8_t fm_op, fm_stage, uslot;      /* FM6's operator and stage on its pages; SAVE USER's slot */
    uint8_t vl_center;                   /* VLEAD: the last chord's middle */
    uint8_t learn;                       /* MIDI LEARN: 1 waiting for a knob, 2 for a CC */
    uint16_t learn_kn;
    uint8_t nsel, dlane, dstep;
    uint32_t lit;                        /* the buttons lit (the tests) */
    uint8_t logo_on;                     /* the boot animation playing (any key, button or knob ends it) */
    uint32_t logo_t0, logo_drawn;
    int8_t save_key;
    int8_t edit_key;                     /* EDIT held: the black key down (its legend's job on release), since edit_t */
    uint32_t edit_t;                     /* SAVE held: the scene key down (-1 none), since save_t */
    uint32_t save_t;
    char name[SONG_NAME + 1];            /* NAME: the name being edited, space-padded, for channel name_ch */
    uint8_t loop, loop_auto;             /* MAIN's channels: 0 PLAY, 1 LOOP (SHIFT + HOME); LOOP put up by PLAY / REC */
    uint8_t loop_user;                   /* SHIFT + HOME while playing: the view as chosen till the transport stops */
    uint8_t pv_top;                      /* PLAY and LOOP: the first of the four channels shown, 0..2 */
    int16_t pv_y;                        /* where the list is drawn from, in 1/8 px (it slides to pv_top) */
    uint32_t pulse_t[NCH];               /* PLAY: when each channel last struck a note (its dot flashes) */
    uint8_t name_ch, name_cur, name_pre;  /* its channel, the cursor, the last ready name picked (0 none) */          /* NOTES: the note picked (in time order); DRUMS: the lane and step */
    uint8_t step_page;                   /* SEQ: the steps the keys show (16 a page) */
    int16_t held_step;                   /* SEQ: the step held, -1 none */
    uint8_t step_new, step_turned;       /* it was set by this press; a knob moved it */
    uint8_t lock_id;                     /* the parameter a held step's PRESETS locks */
    uint8_t entry[NCH];                  /* SEQ: the note (a kit: the sound) a new step gets */
    uint8_t copy_mode, save_keys;        /* the SAVE layer: COPY picked; scene keys pressed this hold */
    uint8_t scn, scn_ch, cs;             /* the SCENE and CHAIN pages' cursors */
    uint16_t go_id;
    uint32_t go_t;
    int32_t turns[NE];
    uint32_t input_t;                    /* the last input (the autosave waits for quiet) */
    uint8_t lvl[NCH];                    /* each channel's meter, in pixels, falling */
    uint32_t sig_top, sig_row[NCH], sig_foot;
    uint8_t theme_drawn;
    uint8_t mode_drawn;                  /* what the rows' band showed: 0 rows, 1 tiles, 2 LIST, 3 a graph */
} ui;

static void ui_redraw_all(void) { ui.theme_drawn = 0xFF; }

/* ------------------------------------------------------------------ text --- */
static void text_cat(char *d, const char *s)
{
    while (*d)
        d++;
    while (*s)
        *d++ = *s++;
    *d = 0;
}

static void int_text(int32_t v, char *b)     /* signed, no padding */
{
    char t[12];
    int n = 0;
    uint32_t u = (uint32_t)(v < 0 ? -v : v);
    do
        t[n++] = (char)('0' + u % 10u);
    while ((u /= 10u) && n < 11);
    if (v < 0)
        *b++ = '-';
    while (n)
        *b++ = t[--n];
    *b = 0;
}

static void signed_text(int32_t v, char *b)  /* +1, 0, -1 */
{
    if (v > 0)
        *b++ = '+';
    int_text(v, b);
}

static void copy_text(char *d, const char *s, uint32_t n)
{
    uint32_t i;
    for (i = 0; i + 1u < n && s[i]; i++)
        d[i] = s[i];
    d[i] = 0;
}

static void batt_text(char *out)         /* SYSTEM's BATTERY: "80%", USB, CHARGING */
{
    int32_t p = batt_pct();
#ifdef APP_BATTERY_RAW
    if (p < 0) {
        copy_text(out, plat_battery_charging() == 2 ? "CHARGING" : "USB", 9);
        return;
    }
#else
    if (p < 0) {
        copy_text(out, "USB", 9);
        return;
    }
#endif
    int_text(p, out);
    text_cat(out, "%");
}


static const char *const NOTE_NAME[12] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
static void note_text(int32_t note, char *b)
{
    char o[8];
    b[0] = 0;
    text_cat(b, NOTE_NAME[note % 12]);
    int_text(note / 12 - 1, o);
    text_cat(b, o);
}

void app_say(const char *a, const char *b)
{
    uint32_t i;
    char t[48];
    for (i = 0; i < 23u && a && a[i]; i++)
        ui.msg[0][i] = a[i];
    ui.msg[0][i] = 0;
    for (i = 0; i < 23u && b && b[i]; i++)
        ui.msg[1][i] = b[i];
    ui.msg[1][i] = 0;
    ui.msg_until = plat_ms() + 1300u;
    if (ann_on()) {                          /* and to a screen reader (announce.c, fm1-reader) */
        t[0] = 0;
        text_cat(t, ui.msg[0]);
        if (ui.msg[1][0]) {
            text_cat(t, " ");
            text_cat(t, ui.msg[1]);
        }
        ann_say(t, 0, ANN_NOW);
    }
}

static void channel_text(uint32_t ch, char *t)   /* "CH 1" */
{
    t[0] = 'C';
    t[1] = 'H';
    t[2] = ' ';
    t[3] = (char)('1' + ch);
    t[4] = 0;
}

/* a channel's name: the song's (SHIFT + SAVE), trailing spaces off; "" none */
static const char *chan_given(uint32_t ch)
{
    static char t[NCH][SONG_NAME + 1];
    uint32_t i, n = 0;
    ch %= NCH;
    for (i = 0; i < SONG_NAME && song.name[ch][i]; i++)
        if ((t[ch][i] = song.name[ch][i]) != ' ')
            n = i + 1u;
    t[ch][n] = 0;
    return t[ch];
}
/* what names a channel on the screen: its name, else its engine's */
static const char *chan_name(uint32_t ch)
{
    const char *n = chan_given(ch);
    return n[0] ? n : synth_engine_name(synth_sound(ch % NCH)->engine);
}

/* a screen reader that has just started listening (or asks) hears the whole screen */
static void speak(void)
{
    char t[112], o[8];
    if (ann_on() && ann_where()) {
        t[0] = 0;
        text_cat(t, APP_NAME ". CHANNEL ");
        o[0] = (char)('1' + set.channel);
        o[1] = 0;
        text_cat(t, o);
        text_cat(t, ". ");
        if (chan_given(set.channel)[0]) {
            text_cat(t, chan_given(set.channel));
            text_cat(t, ". ");
        }
        text_cat(t, synth_engine_name(synth_sound(set.channel)->engine));
        text_cat(t, ". ");
        text_cat(t, synth_preset_name(set.channel));
        if (seq_mute() >> set.channel & 1u)
            text_cat(t, ". MUTED");
        if (seq_solo() >> set.channel & 1u)
            text_cat(t, ". SOLO");
        text_cat(t, ". OCTAVE ");
        signed_text(set.octave, o);
        text_cat(t, o);
        text_cat(t, seq_playing() ? ". PLAYING" : "");
        ann_say(t, 0, ANN_NOW);
    }
    ann_tick(plat_ms());
}

/* ------------------------------------------------------------ the keys --- */
static const uint16_t SCALE_MASK[16] = {0xFFF, 0xAB5, 0x5AD, 0x6AD, 0x5AB, 0xAD5, 0x6B5, 0x56B,
                                        0x9AD, 0xAAD, 0x295, 0x4A9, 0x4E9, 0x555, 0x18D, 0xB6D};
static const char *const SCALE_NAME[16] = {"CHROM", "MAJOR", "MINOR", "DORIAN", "PHRYG", "LYDIAN", "MIXO", "LOCR",
                                           "HARM", "MELOD", "PENT", "MPENT", "BLUES", "WHOLE", "HIRAJ", "DIM"};
static const char *const KEYS_NAME[3] = {"OFF", "SNAP", "WHITE"};
static const char *const CHORD_NAME[6] = {"OFF", "TRIAD", "7TH", "9TH", "SUS4", "POWER"};
static const uint8_t WHITE_KEYS[16] = PLAT_WHITE_KEYS, BLACK_KEYS[11] = PLAT_BLACK_KEYS;

static int white_index(uint32_t k)       /* the white key's number 0..15, -1 a black key */
{
    uint32_t i;
    for (i = 0; i < 16u; i++)
        if (WHITE_KEYS[i] == k)
            return (int)i;
    return -1;
}
static int black_index(uint32_t k)
{
    uint32_t i;
    for (i = 0; i < 11u; i++)
        if (BLACK_KEYS[i] == k)
            return (int)i;
    return -1;
}

static int in_scale(int32_t note)
{
    return SCALE_MASK[song.scale & 15u] >> (uint32_t)((note - song.root + 120) % 12) & 1u;
}

/* the scale's degree d (any number) from the root at base */
static int32_t degree_note(int32_t base, int32_t d)
{
    uint32_t m = SCALE_MASK[song.scale & 15u], cnt = 0, i;
    int32_t iv[12], oct;
    if (m == 0xFFFu)
        m = 0xAB5u;                      /* (chords on the chromatic scale: major's) */
    for (i = 0; i < 12u; i++)
        if (m >> i & 1u)
            iv[cnt++] = (int32_t)i;
    oct = d >= 0 ? d / (int32_t)cnt : -((-d + (int32_t)cnt - 1) / (int32_t)cnt);
    return base + 12 * oct + iv[d - oct * (int32_t)cnt];
}
static int32_t note_degree(int32_t base, int32_t note)   /* the nearest degree at or below the note */
{
    int32_t d = (note - base) * 7 / 12 - 2;
    while (degree_note(base, d + 1) <= note)
        d++;
    while (degree_note(base, d) > note)
        d--;
    return d;
}

/* an engine that maps the keys itself (DRUM: the lanes, SLICE: the slices): no scale, no transpose */
static int maps_keys(uint32_t ch) { return synth_key_note(ch, 0) >= 0; }
static void ch_chord(uint32_t ch, char *out);
static int drum_lane_of(int32_t note)    /* DRUM: the lane a note plays, -1 none */
{
    uint32_t i;
    for (i = 0; i < 8u; i++)
        if ((int32_t)synth_drum_note(i) == note)
            return (int)i;
    return -1;
}
static void note_label(uint32_t ch, int32_t note, char *out)   /* a note as the channel plays it */
{
    if (synth_is_kit(ch)) {
        int l = drum_lane_of(note);
        copy_text(out, l >= 0 ? synth_kit_sound((uint32_t)l) : "-", 8);
    } else if (maps_keys(ch)) {
        copy_text(out, "SLICE ", 8);
        int_text(note - 35, out + 6);
    } else {
        note_text(note, out);
    }
}

/* the notes a key plays on the selected channel: the kit's sound, or the note as the scale and chord shape it */
static uint32_t key_notes(uint32_t k, int8_t *out)
{
    static const int8_t CH_DEG[6][5] = {{0}, {0, 2, 4}, {0, 2, 4, 6}, {0, 2, 4, 6, 8}, {0, 3, 4}, {0}};
    static const uint8_t CH_N[6] = {1, 3, 4, 5, 3, 2};
    int32_t note, base = 48 + song.root + 12 * set.octave, d, i, w = white_index(k);
    uint32_t n, chord = song.chord % 6u;
    if (maps_keys(set.channel)) {        /* DRUM, SLICE: the engine's own map */
        int32_t kn = synth_key_note(set.channel, k);
        if (kn < 0)
            return 0;
        out[0] = (int8_t)kn;
        return 1;
    }
    note = 53 + (int32_t)k + 12 * set.octave;
    if (song.keys == 2u) {               /* WHITE: the white keys walk the scale from the root */
        if (w < 0 && chord)
            return 0;                    /* (a chord on: the black keys are CHORD+'s) */
        if (w < 0)                       /* a black key: the note of the white key below it, as SNAP plays a key off the
                                          * scale (always in key) */
            w = white_index(k ? k - 1u : 0u);
        note = SCALE_MASK[song.scale & 15u] == 0xFFFu ? base + 12 + w - 4   /* (CHROM: semitones) */
             : degree_note(base + 12, w - 4);   /* (C4 plays ROOT, the white keys walk up and down from it) */
    } else if (song.keys == 1u) {        /* SNAP: a key off the scale plays the one below */
        while (!in_scale(note))
            note--;
    }
    if (!chord) {
        out[0] = (int8_t)clamp(note, 0, 127);
        return 1;
    }
    if (w < 0)                           /* a chord on: the black keys change it (CHORD+), they play nothing */
        return 0;
    note = degree_note(48 + song.root + 12 * set.octave + 12, w - 4);   /* the white keys walk the scale from C4 */
    if (chord == 5u) {                   /* POWER: the root, the fifth, the octave */
        out[0] = (int8_t)clamp(note, 0, 127);
        out[1] = (int8_t)clamp(note + 7, 0, 127);
        out[2] = (int8_t)clamp(note + 12, 0, 127);
        return 3;
    }
    {
        /* CHORD+: the black keys held: F# major <-> minor, G# the 7th, A# sus4, C# the 9th, D# an inversion */
        uint32_t mods = 0, j;
        static const uint8_t BK[11] = PLAT_BLACK_KEYS;
        for (j = 0; j < 11u; j++)
            if (ui.keys >> BK[j] & 1u)
                mods |= 1u << (j % 5u);
        d = note_degree(48 + song.root + 12 * set.octave + 12, note);
        n = CH_N[chord];
        for (i = 0; i < (int32_t)n; i++) {
            int32_t deg = CH_DEG[chord][i];
            if (chord == 3u && i == 2)   /* 9TH: 1-3-7-9 */
                deg = 6;
            if (chord == 3u && i == 3)
                deg = 8;
            if ((mods & 4u) && deg == 2)  /* sus4 */
                deg = 3;
            out[i] = (int8_t)clamp(i ? degree_note(48 + song.root + 12 * set.octave + 12, d + deg) : note, 0, 127);
        }
        if (chord == 3u)
            n = 4;
        if ((mods & 1u) && n > 1u)       /* major <-> minor: the third a semitone */
            out[1] = (int8_t)(out[1] + ((out[1] - out[0] + 120) % 12 == 4 ? -1 : 1));
        if ((mods & 2u) && n < 5u && chord != 2u)   /* the 7th */
            out[n++] = (int8_t)clamp(degree_note(48 + song.root + 12 * set.octave + 12, d + 6), 0, 127);
        if ((mods & 8u) && n < 5u)       /* the 9th */
            out[n++] = (int8_t)clamp(degree_note(48 + song.root + 12 * set.octave + 12, d + 8), 0, 127);
        if (mods & 16u)                  /* an inversion: the root an octave up */
            out[0] = (int8_t)clamp(out[0] + 12, 0, 127);
    }
    if (song.vlead && n > 1u) {          /* VLEAD: each note an octave up or down to lie nearest the last chord */
        int32_t c = 0;
        for (i = 0; i < (int32_t)n; i++) {
            int32_t x = out[i];
            if (ui.vl_center) {
                while (x - ui.vl_center > 6 && x >= 12)
                    x -= 12;
                while (ui.vl_center - x > 6 && x <= 115)
                    x += 12;
            }
            out[i] = (int8_t)x;
            c += x;
        }
        ui.vl_center = (uint8_t)(c / (int32_t)n);
    }
    return n;
}

static void dirty(void) { seq_touch(); }

static void octave_step(int32_t d)
{
    int32_t o = d ? clamp(set.octave + d, -2, 2) : 0;
    char t[4];
    if (o == set.octave)
        return;
    set.octave = (int8_t)o;
    app_mark();
    signed_text(o, t);
    app_say("OCTAVE", t);
}

static void say_sound(void)
{
    char t[8];
    channel_text(set.channel, t);
    app_say(t, synth_preset_name(set.channel));
}

/* Bluetooth off or on (kept with the settings, so it stays so at the next power-on) */
static void ble_set(int off)
{
    if (!FM1B_BLE || !!off == !!set.ble_off)
        return;
    set.ble_off = (uint8_t)!!off;
    app_mark();
    plat_ble_enable(!set.ble_off);
    app_say("BLUETOOTH", set.ble_off ? "OFF" : "ON");
}

/* ------------------------------------------------------------ the knobs --- */
static const char *const N_ROLL[] = {"1/8", "1/16", "1/32", "32T", "1/64"};
static const char *const N_TSRC[] = {"OFF", "VINYL", "TAPE", "RAIN", "WIND", "RADIO", "ROOM"};
static const char *const N_TRATE[] = {"1BAR", "2BAR", "4BAR", "8BAR", "16BAR"};
static const char *const N_TWHEN[] = {"PLAY", "ALWAYS"};
static const char *const N_OFFON2[] = {"OFF", "ON"};
static const struct { const char *label; int16_t lo, hi; const char *const *names; } MSTD[MST_N] = {
    [MST_DUST] = {"DUST", 0, 127, 0}, [MST_DUCK] = {"DUCK", 0, 127, 0}, [MST_FILT] = {"FILT", -64, 63, 0},
    [MST_ROLL] = {"ROLL", 0, 4, N_ROLL}, [MST_TSRC] = {"SRC", 0, 6, N_TSRC}, [MST_TLEVEL] = {"LEVEL", 0, 127, 0},
    [MST_TTONE] = {"TONE", -64, 63, 0}, [MST_TMOVE] = {"MOVE", 0, 127, 0}, [MST_TRATE] = {"RATE", 0, 4, N_TRATE},
    [MST_TWHEN] = {"WHEN", 0, 1, N_TWHEN}, [MST_TDUCK] = {"DUCK", 0, 1, N_OFFON2}, [MST_CRT] = {"CRT", 0, 127, 0},
    [MST_CDP] = {"CDP", 0, 127, 0}, [MST_RESO] = {"RESO", 0, 10, 0},
};
static void mst_set(uint32_t id, int32_t v)   /* a master value: the synth's, the song's (RESO: the FM-1's) */
{
    v = clamp(v, MSTD[id].lo, MSTD[id].hi);
    synth_master_set(id, v);
    if (id == MST_RESO) {
        prefs.reso = (uint8_t)v;
        app_mark();
    } else {
        song.mst[id] = (int16_t)v;
        seq_touch();
    }
}
static uint8_t slot_pick;                /* the SONG page's song */
static int ui_is_fav(uint32_t ch);
static void ui_fav(uint32_t ch, int on);
static int32_t lvl_q(uint32_t ch) { return (int32_t)synth_get_level(ch); }
static int dr_ch(void)                   /* GLO DRUMS: the selected channel if it is a kit, else the first kit, -1 */
{
    uint32_t i;
    if (synth_is_kit(set.channel))
        return (int)set.channel;
    for (i = 0; i < NCH; i++)
        if (synth_is_kit(i))
            return (int)i;
    return -1;
}

/* NAME (SHIFT + SAVE): a channel's name, A-Z 0-9 space and a few signs, or one of the ready ones */
static const char NAME_CS[] = " ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-./+#&!";
static const char *const NAME_READY[] = {"DRUMS", "BASS", "KEYS", "LEAD", "PAD", "CHORD", "ARP", "FX", "VOX", "GTR"};
#define NREADY (sizeof NAME_READY / sizeof NAME_READY[0])

static void name_open(void)              /* the selected channel's name to edit */
{
    uint32_t i;
    const char *n = chan_given(set.channel);
    ui.name_ch = set.channel;
    for (i = 0; i < SONG_NAME; i++)
        ui.name[i] = ' ';
    ui.name[SONG_NAME] = 0;
    for (i = 0; n[i]; i++)
        ui.name[i] = n[i];
    ui.name_cur = 0;
    ui.name_pre = 0;
    ui.page = PG_NAME;
    ui.group_btn = B_SAVE;
    ui.layer = -1;
    ui.layer_lock = 0;
    app_say("NAME", n[0] ? n : "NONE");
}
static void name_close(int keep)         /* SAVE again: kept (all spaces: none); HOME: as it was */
{
    uint32_t i, ch = ui.name_ch % NCH;
    char t[8];
    if (keep) {
        uint32_t n = 0;
        for (i = 0; i < SONG_NAME; i++)
            if (ui.name[i] != ' ')
                n = i + 1u;
        memset(song.name[ch], 0, SONG_NAME);
        for (i = 0; i < n; i++)
            song.name[ch][i] = ui.name[i];
        dirty();
    }
    channel_text(ch, t);
    ui.page = PG_HOME;
    ui.group_btn = B_HOME;
    app_say(t, keep ? chan_name(ch) : "NAME AS IT WAS");
}

static const char *kn_label(uint32_t id)
{
    static const char *const L[] = {
        [KN_SWING] = "SWING", [KN_LEVEL] = "LEVEL", [KN_BARS] = "BARS", [KN_DIV] = "DIV", [KN_PATIDX] = "PATTERN",
        [KN_BPM] = "BPM", [KN_CLICK] = "CLICK", [KN_LAUNCH] = "LAUNCH", [KN_MODE] = "PLAY", [KN_ROOT] = "ROOT",
        [KN_SCALE] = "SCALE", [KN_KEYS] = "KEYS", [KN_CHORD] = "CHORD", [KN_TRANS] = "TRANSP",
        [KN_SYNC] = "SYNC", [KN_CLKOUT] = "CLOCK", [KN_SEQOUT] = "OUT", [KN_SLOT] = "SONG", [KN_LOAD] = "LOAD",
        [KN_SAVE] = "SAVE", [KN_NEW] = "NEW", [KN_SCN] = "SCENE", [KN_SCN_CH] = "CHANNEL", [KN_SCN_PAT] = "PATTERN",
        [KN_SCN_SND] = "SOUND", [KN_CS] = "STEP", [KN_CS_SCENE] = "SCENE", [KN_CS_BARS] = "BARS",
        [KN_CS_REPS] = "REPEAT", [KN_RMODE] = "MODE", [KN_RBARS] = "BARS", [KN_RSTART] = "START",
        [KN_RQUANT] = "QUANT", [KN_RREPL] = "REC", [KN_SNOTE] = "NOTE", [KN_ESHIFT] = "SHIFT", [KN_ELEN] = "LENGTH",
        [KN_ETRANS] = "TRANSP", [KN_STNOTE] = "NOTE", [KN_STVEL] = "VEL", [KN_STRAT] = "RATCH",
        [KN_STNUDGE] = "NUDGE", [KN_LOCKP] = "LOCK", [KN_LOCKV] = "VALUE", [KN_VSENS] = "SENS",
        [KN_LEARN] = "LEARN", [KN_FIXVEL] = "FIXVEL", [KN_PRESS] = "PRESS", [KN_DISPLAY] = "THEME",
        [KN_BT] = "BLUETOOTH", [KN_GRAPHS] = "GRAPHS", [KN_ACCEL] = "ACCEL", [KN_LIGHTS] = "BRIGHT", [KN_SCALEKEYS] = "KEYS",
        [KN_AUTOSAVE] = "AUTOSV", [KN_ABOUT] = "ABOUT", [KN_BTDEV] = "DEVICE", [KN_BTGO] = "CONNECT",
        [KN_GSWING] = "SWING", [KN_TUNE] = "TUNE", [KN_DUST] = "DUST", [KN_DUCK] = "DUCK", [KN_MFILT] = "FILT",
        [KN_ROLL] = "ROLL", [KN_IN] = "IN", [KN_OTHER] = "OTHER", [KN_BEND] = "BEND", [KN_CPU] = "CPU",
        [KN_FXCH] = "FX CH", [KN_DRMAP] = "DR MAP", [KN_REMOTE] = "REMOTE", [KN_DR_CH] = "CH", [KN_DR_LVL] = "LVL",
        [KN_DR_REV] = "REV", [KN_DR_DLY] = "DLY",
        [KN_STRUM] = "STRUM", [KN_VLEAD] = "VLEAD", [KN_FM_OP] = "OP", [KN_FM_RATIO] = "RATIO", [KN_FM_FINE] = "FINE",
        [KN_FM_LEVEL] = "LEVEL", [KN_FM_STAGE] = "STAGE", [KN_FM_RATE] = "RATE", [KN_FM_ELEVEL] = "LEVEL",
        [KN_FM_MODE] = "MODE", [KN_FM_DTUN] = "DTUN", [KN_FM_VEL] = "VEL", [KN_VMUTE] = "MUTE", [KN_PFADE] = "FADE",
        [KN_PRE_SOUND] = "SOUND", [KN_PRE_ENGINE] = "ENGINE", [KN_PRE_FAV] = "FAV", [KN_USLOT] = "SLOT",
        [KN_ULOAD] = "LOAD", [KN_UERASE] = "ERASE", [KN_USAVE] = "SAVE", [KN_CLRSQ] = "CLRSQ", [KN_INIT] = "INIT",
        [KN_NMCUR] = "CURSOR", [KN_NMCHR] = "CHAR", [KN_NMPRE] = "NAME", [KN_NMCLR] = "CLEAR",
        [KN_HOMEVIEW] = "HOME", [KN_AUTOLOOP] = "AUTOLP", [KN_FACTORY] = "RESET", [KN_BATT] = "BATTERY",
        [KN_LOWCUT] = "LOWCUT", [KN_USBA] = "USB", [KN_RESO] = "RESO", [KN_STEPADV] = "ADVANCE", [KN_NSEL] = "NOTE#", [KN_NPITCH] = "PITCH",
        [KN_NMOVE] = "MOVE", [KN_NLEN] = "LENGTH", [KN_DLANE] = "LANE", [KN_DSTEP] = "STEP", [KN_DHIT] = "HIT",
        [KN_DVEL] = "VEL",
        [KN_DRPREV] = "PREVIEW",
    };
    static char t[8];
    if (ui.page == PG_PITCH && id == KN_SP + SP_PENV)
        return "ENV";                    /* (PITCH: the envelope's and the LFO's pitch apart) */
    if (ui.page == PG_PITCH && id == KN_SP + SP_LFO_PIT)
        return "VIB";
    if (id >= KN_SP && id < KN_SP + SP_N)
        return synth_param_label(set.channel, id - KN_SP);
    if (id >= KN_EK && id < KN_EK + 4u)
        return synth_param_label(set.channel, synth_knob_param(set.channel, id - KN_EK));
    if (id >= KN_EP && id < KN_EP + 8u)
        return synth_param_label(set.channel, SP_E0 + id - KN_EP);
    if (id >= KN_FX && id < KN_FX + FX_N)
        return synth_fx_label(id - KN_FX);
    if (id >= KN_MST && id < KN_MST + MST_N)
        return MSTD[id - KN_MST].label;
    if (id >= KN_MIDI && id < KN_MIDI + NCH) {
        copy_text(t, "CH  ", sizeof t);
        t[3] = (char)('1' + id - KN_MIDI);
        return t;
    }
    if (id >= KN_LVL && id < KN_LVL + 4u) {
        copy_text(t, "LVL  ", sizeof t);
        t[4] = (char)('1' + id - KN_LVL);
        return t;
    }
    return id < sizeof L / sizeof L[0] && L[id] ? L[id] : "";
}

static void on_off(int v, char *out) { copy_text(out, v ? "ON" : "OFF", 8); }

/* the value as text, and where it is in its range (0..255) */
static uint32_t kn_value(uint32_t id, char *out)
{
    uint32_t ch = set.channel;
    const pat_t *p = seq_pat(ch);
    int32_t v = 0, lo = 0, hi = 1;
    out[0] = 0;
    if ((id >= KN_SP && id < KN_SP + SP_N) || (id >= KN_EK && id < KN_EK + 4u) || (id >= KN_EP && id < KN_EP + 8u)) {
        uint32_t sp = id >= KN_EP ? SP_E0 + id - KN_EP : id >= KN_EK ? synth_knob_param(ch, id - KN_EK) : id - KN_SP;
        v = synth_param(ch, sp);
        synth_param_range(ch, sp, &lo, &hi);
        synth_param_text(ch, sp, v, out);
    } else if (id >= KN_FX && id < KN_FX + FX_N) {
        synth_fx_text(id - KN_FX, out);
        return (uint32_t)synth_fx_pos(id - KN_FX);
    } else if (id >= KN_MST && id < KN_MST + MST_N) {
        uint32_t m = id - KN_MST;
        v = synth_master(m);
        lo = MSTD[m].lo;
        hi = MSTD[m].hi;
        if (MSTD[m].names)
            copy_text(out, MSTD[m].names[clamp(v, lo, hi) - lo], 8);
        else if (m == MST_FILT || m == MST_TTONE) {
            if (!v)
                copy_text(out, "OFF", 8);
            else {
                copy_text(out, v < 0 ? "LP " : "HP ", 8);
                int_text(v < 0 ? -v : v, out + 3);
            }
        } else
            int_text(v, out);
    } else if (id >= KN_MIDI && id < KN_MIDI + NCH) {
        v = prefs.midi_ch[id - KN_MIDI];
        hi = 16;
        if (v)
            int_text(v, out);
        else
            copy_text(out, "OFF", 8);
    } else if (id >= KN_LVL && id < KN_LVL + 4u) {
        v = lvl_q(id - KN_LVL);
        hi = 127;
        int_text(v, out);
    } else {
        switch (id) {
        case KN_SWING: v = p->swing; hi = 100; int_text(v, out); break;
        case KN_LEVEL: v = lvl_q(ch); hi = 127; int_text(v, out); break;
        case KN_BARS: v = p->bars; lo = 1; hi = 8; int_text(v, out); break;
        case KN_DIV: {
            static const char *const D[NDIV] = {"1/16", "1/8", "1/32", "1/16T", "1/4", "1/8T", "1/2", "1 BAR", "2 BAR"};
            static const uint8_t POS[NDIV] = {2, 4, 0, 1, 5, 3, 6, 7, 8};   /* (the knob: fine to coarse) */
            v = POS[p->div % NDIV]; hi = NDIV - 1; copy_text(out, D[p->div % NDIV], 8); break;
        }
        case KN_PATIDX: v = (int32_t)seq_pat_index(ch); hi = NPAT - 1; int_text(v + 1, out); break;
        case KN_BPM: {
            uint32_t b = seq_bpm10();
            char d[4];
            v = (int32_t)b; lo = 400; hi = 2400;
            int_text((int32_t)(b / 10u), out);
            d[0] = '.'; d[1] = (char)('0' + b % 10u); d[2] = 0;
            text_cat(out, d);
            break;
        }
        case KN_CLICK: {
            static const char *const C[3] = {"OFF", "REC", "ON"};
            v = seq_cfg.click; hi = 2; copy_text(out, C[v % 3], 8); break;
        }
        case KN_LAUNCH: {
            static const char *const C[3] = {"BAR", "LOOP", "NOW"};
            v = seq_cfg.launch; hi = 2; copy_text(out, C[v % 3], 8); break;
        }
        case KN_MODE: v = song.mode; copy_text(out, v ? "SONG" : "LOOP", 8); break;
        case KN_ROOT: v = song.root; hi = 11; copy_text(out, NOTE_NAME[v % 12], 8); break;
        case KN_SCALE: v = song.scale; hi = 15; copy_text(out, SCALE_NAME[v & 15], 8); break;
        case KN_KEYS: v = song.keys; hi = 2; copy_text(out, KEYS_NAME[v % 3], 8); break;
        case KN_CHORD: v = song.chord; hi = 5; copy_text(out, CHORD_NAME[v % 6], 8); break;
        case KN_TRANS: v = song.transpose; lo = -24; hi = 24; signed_text(v, out); break;
        case KN_SYNC: {
            static const char *const S[4] = {"INT", "USB", "TRS", "BLE"};
            v = seq_cfg.sync; hi = 3; copy_text(out, S[v & 3], 8); break;
        }
        case KN_CLKOUT: v = seq_cfg.clock_out; on_off(v, out); break;
        case KN_SEQOUT: v = seq_cfg.seq_out; copy_text(out, v ? "SEQ" : "KEYS", 8); break;
        case KN_SLOT: {
            v = slot_pick;
            hi = NSONG - 1;
            out[0] = (char)('A' + v);
            out[1] = 0;
            if (!seq_slot_used((uint32_t)v))
                text_cat(out, " -");
            break;
        }
        case KN_LOAD: case KN_SAVE: case KN_NEW: copy_text(out, "GO", 8); return 0;
        case KN_SCN: v = ui.scn; hi = NSCENE - 1; int_text(v + 1, out); break;
        case KN_SCN_CH: v = ui.scn_ch; hi = NCH - 1; int_text(v + 1, out); break;
        case KN_SCN_PAT: v = song.scene[ui.scn].pat[ui.scn_ch]; hi = NPAT - 1; int_text(v + 1, out); break;
        case KN_SCN_SND: v = song.scene[ui.scn].has_sound; on_off(v, out); break;
        case KN_CS: v = ui.cs; hi = NCHAIN - 1; int_text(v + 1, out); break;
        case KN_CS_SCENE:
            if (ui.cs >= song.nchain) {
                copy_text(out, "END", 8);
                return 0;
            }
            v = song.chain[ui.cs].scene; hi = NSCENE - 1; int_text(v + 1, out); break;
        case KN_CS_BARS:
            if (ui.cs >= song.nchain)
                return 0;
            v = song.chain[ui.cs].bars; hi = 64;
            if (v)
                int_text(v, out);
            else
                copy_text(out, "CUE", 8);
            break;
        case KN_CS_REPS:
            if (ui.cs >= song.nchain)
                return 0;
            v = song.chain[ui.cs].reps; lo = 1; hi = 16; int_text(v, out); break;
        case KN_RMODE: v = seq_cfg.rec_mode; copy_text(out, v == RM_FREE ? "FREE" : "TEMPO", 8); break;
        case KN_RBARS: v = seq_cfg.rec_bars; lo = 1; hi = 8; int_text(v, out); break;
        case KN_RSTART: v = seq_cfg.rec_start; copy_text(out, v ? "COUNT" : "NOTE", 8); break;
        case KN_RQUANT: v = seq_cfg.rec_quant; on_off(v, out); break;
        case KN_RREPL: v = seq_cfg.rec_replace; copy_text(out, v ? "REPLACE" : "OVERDUB", 8); break;
        case KN_SNOTE:
            v = ui.entry[ch]; hi = 127;
            note_label(ch, v, out);
            break;
        case KN_ESHIFT: case KN_ELEN: case KN_ETRANS: copy_text(out, "<  >", 8); return 128;
        case KN_STNOTE: case KN_STVEL: case KN_STRAT: case KN_STNUDGE: {
            int e = ui.held_step >= 0 ? seq_step_find(ch, (uint32_t)ui.held_step) : -1;
            const ev_t *ev = e >= 0 ? &p->ev[e] : 0;
            if (!ev) {
                copy_text(out, "-", 8);
                return 0;
            }
            if (id == KN_STNOTE) {
                v = ev->note; hi = 127;
                note_label(ch, v, out);
            } else if (id == KN_STVEL) {
                v = ev->vel; hi = 127; int_text(v, out);
            } else if (id == KN_STRAT) {
                v = ev->fl & EV_RATCH; hi = 3; out[0] = 'x'; out[1] = (char)('1' + v); out[2] = 0;
            } else {
                uint32_t st = seq_step_ticks(p);
                v = (int32_t)ev->pos - (int32_t)((uint32_t)ui.held_step * st);
                lo = -(int32_t)st / 2; hi = (int32_t)st / 2;
                signed_text(v, out);
            }
            break;
        }
        case KN_LOCKP: copy_text(out, synth_param_label(ch, ui.lock_id), 8); return 128;
        case KN_LOCKV: {
            int32_t lv = ui.held_step >= 0 ? seq_step_lock(ch, (uint32_t)ui.held_step, ui.lock_id) : SEQ_NOLOCK;
            if (lv == SEQ_NOLOCK) {
                copy_text(out, "-", 8);
                return 0;
            }
            synth_param_range(ch, ui.lock_id, &lo, &hi);
            v = lv;
            synth_param_text(ch, ui.lock_id, lv, out);
            break;
        }
        case KN_VSENS: v = set.vsens; lo = -5; hi = 5; signed_text(v, out); break;
        case KN_LEARN: v = prefs.key_learn; on_off(v, out); break;
        case KN_FIXVEL: v = prefs.fixed_vel; hi = 127; if (v) int_text(v, out); else copy_text(out, "OFF", 8); break;
        case KN_PRESS: v = prefs.prs_mode; hi = PRS_NMODES - 1; copy_text(out, synth_pressure_name(v), 8); break;
        case KN_DISPLAY: v = prefs.display; copy_text(out, v ? "LIGHT" : "DARK", 8); break;
        case KN_BT: v = FM1B_BLE && !set.ble_off; copy_text(out, !FM1B_BLE ? "NONE" : v ? "ON" : "OFF", 8); break;
        case KN_ACCEL: v = prefs.knob_accel; on_off(v, out); break;
        case KN_GRAPHS: v = prefs.graphs; on_off(v, out); break;
        case KN_HOMEVIEW: v = prefs.home_view; copy_text(out, v ? "LOOP" : "PLAY", 8); break;
        case KN_AUTOLOOP: v = !prefs.loop_manual; on_off(v, out); break;
        case KN_FACTORY: copy_text(out, ui.fr_arm ? "PLAY 3S" : "TURN", 8); return 0;
        case KN_BATT: batt_text(out); return 0;
        case KN_LIGHTS: {
            static const char *const L3[3] = {"OFF", "LOW", "HIGH"};
            v = prefs.lights; hi = 2; copy_text(out, L3[v % 3], 8); break;
        }
        case KN_SCALEKEYS: v = prefs.scale_keys; on_off(v, out); break;
        case KN_AUTOSAVE: v = prefs.autosave; on_off(v, out); break;
        case KN_ABOUT: copy_text(out, APP_VERSION, 8); return 0;
        case KN_GSWING: v = song.swing; hi = 100; int_text(v, out); break;
        case KN_STRUM: v = song.strum; lo = -60; hi = 60; if (v) signed_text(v, out); else copy_text(out, "OFF", 8); break;
        case KN_VLEAD: v = song.vlead; on_off(v, out); break;
        case KN_FM_OP: v = ui.fm_op; hi = 5; int_text(v + 1, out); break;
        case KN_FM_STAGE: v = ui.fm_stage; hi = 3; int_text(v + 1, out); break;
        case KN_FM_RATIO: case KN_FM_FINE: case KN_FM_LEVEL: case KN_FM_RATE: case KN_FM_ELEVEL: case KN_FM_MODE:
        case KN_FM_DTUN: case KN_FM_VEL: {
            static const uint8_t F[] = {FM6_RATIO, FM6_FINE, FM6_LEVEL, 0, FM6_RATE, FM6_ELEVEL, FM6_MODE, FM6_DTUN,
                                        FM6_VEL};
            static const int16_t HI[] = {31, 99, 99, 0, 99, 99, 1, 7, 7};
            uint32_t f = F[id - KN_FM_RATIO];
            v = synth_fm6_op(ch, ui.fm_op, f, ui.fm_stage);
            lo = f == FM6_DTUN ? -7 : 0;
            hi = HI[id - KN_FM_RATIO];
            if (f == FM6_MODE)
                copy_text(out, v ? "FIXED" : "RATIO", 8);
            else if (f == FM6_RATIO && v == 0)
                copy_text(out, "0.5", 8);
            else if (f == FM6_DTUN)
                signed_text(v, out);
            else
                int_text(v, out);
            break;
        }
        case KN_VMUTE: v = (int32_t)(seq_mute() >> ch & 1u); on_off(v, out); break;
        case KN_PFADE: v = prefs.pfade; hi = 15; int_text(v, out); break;
        case KN_PRE_SOUND: {
            char t2[24];
            v = (int32_t)synth_sound_index(ch); hi = (int32_t)synth_nsounds() - 1;
            copy_text(t2, synth_preset_name(ch), 24);
            copy_text(out, t2, 8);
            break;
        }
        case KN_PRE_ENGINE: v = synth_sound(ch)->engine; hi = (int32_t)synth_nengines() - 1;
            copy_text(out, synth_engine_name((uint32_t)v), 8); break;
        case KN_PRE_FAV: v = ui_is_fav(ch); copy_text(out, v ? "FAV" : "-", 8); break;
        case KN_USLOT: v = ui.uslot; hi = 31; int_text(v + 1, out);
            if (!user_used(ui.uslot)) text_cat(out, " -");
            break;
        case KN_ULOAD: case KN_UERASE: case KN_USAVE: case KN_CLRSQ: case KN_INIT: copy_text(out, "GO", 8); return 0;
        case KN_NMCUR: v = ui.name_cur; hi = SONG_NAME - 1; int_text(v + 1, out); text_cat(out, "/8"); break;
        case KN_NMCHR: {
            char c = ui.name[ui.name_cur % SONG_NAME];
            uint32_t i = 0;
            while (NAME_CS[i] && NAME_CS[i] != c)
                i++;
            v = (int32_t)i; hi = (int32_t)sizeof NAME_CS - 2;
            if (c == ' ')
                copy_text(out, "SPACE", 8);
            else {
                out[0] = c;
                out[1] = 0;
            }
            break;
        }
        case KN_NMPRE: v = ui.name_pre; hi = NREADY;
            copy_text(out, v ? NAME_READY[v - 1] : "-", 8); break;
        case KN_NMCLR: copy_text(out, "GO", 8); return 0;
        case KN_LOWCUT: v = prefs.lowcut; on_off(v, out); break;
        case KN_USBA: v = prefs.usb_full; copy_text(out, v ? "FULL" : "MASTER", 8); break;
        case KN_STEPADV: v = prefs.step_adv; on_off(v, out); break;
        case KN_DRPREV: v = prefs.drum_prev; on_off(v, out); break;
        case KN_NSEL: case KN_NPITCH: case KN_NMOVE: case KN_NLEN: {
            uint8_t ix[PAT_EV];
            uint32_t n = seq_ev_list(ch, ix, PAT_EV);
            const ev_t *e = n ? seq_ev(ch, ix[ui.nsel < n ? ui.nsel : n - 1u]) : 0;
            if (!e) {
                copy_text(out, "-", 8);
                return 0;
            }
            if (id == KN_NSEL) { v = ui.nsel < n ? ui.nsel : (int32_t)n - 1; hi = (int32_t)n - 1; int_text(v + 1, out); }
            else if (id == KN_NPITCH) { v = e->note; hi = 127; note_text(v, out); }
            else if (id == KN_NMOVE) {
                uint32_t st = seq_step_ticks(p);
                v = e->pos; hi = (int32_t)(p->bars * TBAR);
                int_text((int32_t)(e->pos / st) + 1, out);
            } else { v = e->len; hi = 64; int_text((e->len * 6 + 12) / 24, out); text_cat(out, "/16"); }
            break;
        }
        case KN_DLANE: v = ui.dlane; hi = 7; copy_text(out, synth_kit_sound(ui.dlane), 8); break;
        case KN_DSTEP: v = ui.dstep; hi = (int32_t)seq_steps(p) - 1; int_text(v + 1, out); break;
        case KN_DHIT: case KN_DVEL: {
            int i = seq_hit_find(ch, ui.dstep, synth_drum_note(ui.dlane));
            if (id == KN_DHIT) { v = i >= 0; on_off(v, out); }
            else if (i >= 0) { v = seq_ev(ch, (uint32_t)i)->vel; hi = 127; int_text(v, out); }
            else { copy_text(out, "-", 8); return 0; }
            break;
        }
        case KN_TUNE: v = prefs.tune; lo = -50; hi = 50; signed_text(v, out); break;
        case KN_DUST: case KN_DUCK: case KN_MFILT: case KN_ROLL:
            copy_text(out, "-", 8);              /* (the master effects: next build) */
            return 0;
        case KN_IN:
            v = prefs.in_mode; hi = 17;
            if (v == 0)
                copy_text(out, "NOTES", 8);
            else if (v == 1)
                copy_text(out, "CLOCK", 8);
            else {
                copy_text(out, "CH ", 8);
                int_text(v - 1, out + 3);
            }
            break;
        case KN_OTHER: v = prefs.other; copy_text(out, v ? "OFF" : "SEL", 8); break;
        case KN_BEND: v = prefs.bend; lo = 1; hi = 12; int_text(v, out); break;
        case KN_CPU: v = (int32_t)(nw_cpu / 16u); hi = 100; int_text(v, out); text_cat(out, "%"); break;
        case KN_FXCH: v = prefs.fxch; hi = 16; if (v) int_text(v, out); else copy_text(out, "OFF", 8); break;
        case KN_DRMAP: v = prefs.drmap; copy_text(out, v ? "1-16" : "GM", 8); break;
        case KN_REMOTE: v = prefs.remote; hi = 16; if (v) int_text(v, out); else copy_text(out, "OFF", 8); break;
        case KN_DR_CH: case KN_DR_LVL: case KN_DR_REV: case KN_DR_DLY: {
            int dc = dr_ch();
            if (dc < 0) {
                copy_text(out, "NO KIT", 8);
                return 0;
            }
            if (id == KN_DR_CH) {
                v = prefs.midi_ch[dc]; hi = 16;
                if (v) int_text(v, out); else copy_text(out, "OFF", 8);
            } else if (id == KN_DR_LVL) {
                v = lvl_q((uint32_t)dc); hi = 127; int_text(v, out);
            } else {
                v = synth_param((uint32_t)dc, id == KN_DR_REV ? SP_REV : SP_DLY); hi = 127; int_text(v, out);
            }
            break;
        }
        case KN_BTDEV: {                 /* a Bluetooth MIDI device: the one connected, or the one picked */
            char nm[21];
            if (!FM1B_BLE_CENTRAL || set.ble_off)
                copy_text(out, "NONE", 8);
            else if (btdev_busy())
                copy_text(out, btdev_name(), 8);
            else if (btdev_found(btdev_pick(), nm, 0))
                copy_text(out, nm, 8);
            else
                copy_text(out, "SCAN..", 8);
            return 0;
        }
        case KN_BTGO:
            copy_text(out, FM1B_BLE_CENTRAL && btdev_busy() ? (btdev_ready() ? "DROP" : "WAIT") : "GO", 8);
            return 0;
        default: return 0;
        }
    }
    return hi > lo ? (uint32_t)((v - lo) * 255 / (hi - lo)) : 0u;
}

/* favourites (SAVE PRESETS FAV): bit i of prefs.fav, sound i of the list; PRESETS goes through them first, then
 * every factory sound, then the user presets */
static int ui_is_fav(uint32_t ch)
{
    uint32_t i = synth_sound_index(ch);
    return i < 128u && (prefs.fav[i / 8u] >> (i % 8u) & 1u);
}
static void ui_fav(uint32_t ch, int on)
{
    uint32_t i = synth_sound_index(ch);
    if (i >= 128u)
        return;
    if (on)
        prefs.fav[i / 8u] |= (uint8_t)(1u << (i % 8u));
    else
        prefs.fav[i / 8u] &= (uint8_t)~(1u << (i % 8u));
    app_mark();
    app_say("FAV", on ? "ADDED" : "OFF");
}
static int8_t user_cur[NCH] = {-1, -1, -1, -1, -1, -1};   /* the user preset a channel plays, -1 a factory sound */
static void presets_step(uint32_t ch, int32_t d)   /* PRESETS: the next sound in that order */
{
    uint32_t n = synth_nsounds(), total = n + 32u, pos, i, k, list[128 + 32], cnt = 0;
    for (i = 0; i < n && i < 128u; i++)          /* the list: favourites, the rest, the user presets used */
        if (prefs.fav[i / 8u] >> (i % 8u) & 1u)
            list[cnt++] = i;
    for (i = 0; i < n; i++)
        if (i >= 128u || !(prefs.fav[i / 8u] >> (i % 8u) & 1u))
            list[cnt++] = i;
    for (i = 0; i < 32u; i++)
        if (user_used(i))
            list[cnt++] = n + i;
    (void)total;
    pos = 0;
    k = user_cur[ch] >= 0 ? n + (uint32_t)user_cur[ch] : synth_sound_index(ch);
    for (i = 0; i < cnt; i++)
        if (list[i] == k)
            pos = i;
    pos = (uint32_t)(((int32_t)pos + d % (int32_t)cnt + (int32_t)cnt) % (int32_t)cnt);
    if (list[pos] >= n) {
        user_load(list[pos] - n, ch);
        user_cur[ch] = (int8_t)(list[pos] - n);
    } else {
        synth_sound_pick(ch, list[pos]);
        user_cur[ch] = -1;
    }
}

/* LOAD, SAVE, NEW: the first turn asks AGAIN, a second within 1.5 s does it */

static void song_saved(int r, uint32_t s)
{
    char t[8] = "SONG  ";
    t[5] = (char)('A' + s);
    app_say(r == 0 ? "SAVED" : r == -1 ? "SONG FULL" : "SAVE FAILED", r == 0 ? t : "");
}

static int go(uint32_t id, const char *word)
{
    uint32_t now = plat_ms();
    if (ui.go_id == id && now - ui.go_t < GO_MS) {
        ui.go_id = 0;
        return 1;
    }
    ui.go_id = (uint8_t)id;
    ui.go_t = now;
    app_say("AGAIN:", word);
    return 0;
}

static void do_save(uint32_t s)
{
    if (seq_playing()) {
        app_say("STOP", "TO SAVE");
        return;
    }
    if (go(KN_SAVE, "SAVE")) {
        int r = seq_save(s);
        if (r == 0) {
            prefs.slot = (uint8_t)s;
            app_mark();
        }
        song_saved(r, s);
    }
}

static int32_t accel(int32_t e)
{
    int32_t a = e < 0 ? -e : e;
    return prefs.knob_accel && a > 1 ? e * (a > 4 ? 4 : a) : e;
}

/* a note heard for a moment (NOTES, DRUMS: what is picked), not recorded */
static struct { int32_t note; uint32_t ch, until; } pv = {-1, 0, 0};
static void preview_end(void)
{
    uint32_t f;
    if (pv.note < 0)
        return;
    f = plat_lock();
    synth_note_off(pv.ch, (uint32_t)pv.note);
    plat_unlock(f);
    pv.note = -1;
}
static void preview_note(uint32_t ch, int32_t note, uint32_t vel)
{
    uint32_t f;
    preview_end();
    f = plat_lock();
    synth_note_on(ch, (uint32_t)note, vel);
    plat_unlock(f);
    pv.note = note;
    pv.ch = ch;
    pv.until = plat_ms() + 180u;
}

/* MIDI: the CCs and program changes the render queued (app.c midi_in), here where the sounds are changed. The map:
 * 1 the LFO's pitch depth, 5 GLIDE, 7 and 11 the level, 10 PAN, 71 RES, 72 REL, 73 ATK, 74 CUT, 75 DEC, 91 REV,
 * 93 CHO, 94 DLY; 121 all back; and the CCs MIDI LEARN gave a knob. A CC sets its parameter across its range. */
static void cc_param(uint32_t ch, uint32_t sp, uint32_t v)
{
    int32_t lo, hi;
    synth_param_range(ch, sp, &lo, &hi);
    synth_param_set(ch, sp, lo + ((hi - lo) * (int32_t)v + 63) / 127);
    dirty();
}
static void cc_poll(void)
{
    static const uint8_t MAP[][2] = {{1, SP_LFO_PIT}, {5, SP_GLIDE}, {10, SP_PAN}, {71, SP_RES}, {72, SP_REL},
                                     {73, SP_ATK}, {74, SP_CUT}, {75, SP_DEC}, {91, SP_REV}, {93, SP_CHO},
                                     {94, SP_DLY}};
    while (ccq_r != ccq_w) {
        uint32_t e = ccq[ccq_r++ % CCQ_N], ch = e >> 16 & 0xFFu, cc = e >> 8 & 0xFFu, v = e & 0x7Fu, mc = e >> 24, i;
        int done = 0;
        if (cc == 128u) {                /* a program: the engine's preset v (DRUM: 0-8 its KIT) */
            if (synth_sound(ch)->engine == 12u && v < 9u) {
                synth_param_set(ch, SP_E0, (int32_t)v);
                dirty();
            } else if (v < synth_npresets(ch)) {
                synth_preset(ch, v);
                dirty();
            }
            continue;
        }
        if (ui.learn == 2u && cc != 64u && cc != 121u) {   /* LEARN: this CC to the knob turned */
            for (i = 0; i < 8u && prefs.learn[i][0] && !(prefs.learn[i][0] == cc && prefs.learn[i][1] == mc); i++)
                ;
            if (i == 8u) {               /* (full: the oldest goes) */
                uint32_t r, c;
                for (r = 0; r < 7u; r++)
                    for (c = 0; c < 3u; c++)
                        prefs.learn[r][c] = prefs.learn[r + 1u][c];
                i = 7u;
            }
            prefs.learn[i][0] = (uint8_t)cc;
            prefs.learn[i][1] = (uint8_t)mc;
            prefs.learn[i][2] = (uint8_t)ui.learn_kn;
            app_mark();
            ui.learn = 0;
            {
                char t[8];
                int_text((int32_t)cc, t);
                app_say("LEARNED CC", t);
            }
            continue;
        }
        for (i = 0; i < 8u; i++)         /* the learned first */
            if (prefs.learn[i][0] == cc && prefs.learn[i][1] == mc) {
                cc_param(ch, prefs.learn[i][2], v);
                done = 1;
            }
        if (done)
            continue;
        if (cc == 7u || cc == 11u) {
            synth_set_level(ch, v);
            continue;
        }
        if (cc == 121u) {                /* all back: the pitch, the LFO's depth */
            synth_param_set(ch, SP_LFO_PIT, 0);
            continue;
        }
        for (i = 0; i < sizeof MAP / sizeof MAP[0]; i++)
            if (MAP[i][0] == cc)
                cc_param(ch, MAP[i][1], v);
    }
}

static void kn_turn(uint32_t id, int32_t e)
{
    uint32_t ch = set.channel, sp;
    int32_t d = accel(e), s = e > 0 ? 1 : -1;
    if (!e)
        return;
    if ((id >= KN_SP && id < KN_SP + SP_N) || (id >= KN_EK && id < KN_EK + 4u) || (id >= KN_EP && id < KN_EP + 8u)) {
        sp = id >= KN_EP ? SP_E0 + id - KN_EP : id >= KN_EK ? synth_knob_param(ch, id - KN_EK) : id - KN_SP;
        if (ui.learn == 1u) {            /* MIDI LEARN: this knob, then a CC */
            ui.learn = 2;
            ui.learn_kn = (uint16_t)sp;
            app_say("LEARN:", "SEND A CC");
            return;
        }
        synth_param_turn(ch, sp, d);
        dirty();
        return;
    }
    if (id >= KN_FX && id < KN_FX + FX_N) {
        synth_fx_turn(id - KN_FX, id == KN_FX + FX_DTIME ? s : d);
        song.fx[id - KN_FX] = (int16_t)synth_fx(id - KN_FX);
        dirty();
        return;
    }
    if (id >= KN_MIDI && id < KN_MIDI + NCH) {   /* two channels never share a MIDI channel: the next free one */
        uint32_t c = id - KN_MIDI, i;
        int32_t v = prefs.midi_ch[c];
        int clash;
        do {
            v = clamp(v + s, 0, 16);
            clash = 0;
            for (i = 0; i < NCH && v; i++)
                clash |= i != c && prefs.midi_ch[i] == v;
        } while (clash && v > 0 && v < 16);
        if (!clash)
            prefs.midi_ch[c] = (uint8_t)v;
        app_mark();
        return;
    }
    if (id >= KN_MST && id < KN_MST + MST_N) {
        uint32_t m = id - KN_MST;
        mst_set(m, synth_master(m) + (MSTD[m].names || MSTD[m].hi <= 10 ? s : d));
        return;
    }
    if (id >= KN_LVL && id < KN_LVL + 4u) {
        seq_set_level(id - KN_LVL, (uint32_t)clamp(lvl_q(id - KN_LVL) + d, 0, 127));
        return;
    }
    switch (id) {
    case KN_SWING: seq_pat_swing(ch, d); break;
    case KN_LEVEL: seq_set_level(ch, (uint32_t)clamp(lvl_q(ch) + d, 0, 127)); break;
    case KN_BARS: seq_pat_set_bars(ch, (uint32_t)clamp(seq_pat(ch)->bars + s, 1, 8)); break;
    case KN_DIV: seq_pat_div(ch, s); break;
    case KN_PATIDX: seq_scene_pat(seq_scene(), ch, (uint32_t)clamp((int32_t)seq_pat_index(ch) + s, 0, NPAT - 1)); break;
    case KN_BPM: seq_set_bpm10((uint32_t)clamp((int32_t)seq_bpm10() + d * 10, 400, 2400)); break;
    case KN_CLICK: seq_cfg.click = (uint8_t)clamp(seq_cfg.click + s, 0, 2); app_mark(); break;
    case KN_LAUNCH: seq_cfg.launch = (uint8_t)clamp(seq_cfg.launch + s, 0, 2); app_mark(); break;
    case KN_MODE: seq_mode(s > 0); break;
    case KN_ROOT: song.root = (uint8_t)((song.root + 12 + s) % 12); dirty(); break;
    case KN_SCALE: song.scale = (uint8_t)clamp(song.scale + s, 0, 15); dirty(); break;
    case KN_KEYS: song.keys = (uint8_t)clamp(song.keys + s, 0, 2); dirty(); break;
    case KN_CHORD: song.chord = (uint8_t)clamp(song.chord + s, 0, 5); dirty(); break;
    case KN_TRANS: song.transpose = (int8_t)clamp(song.transpose + s, -24, 24); dirty(); break;
    case KN_SYNC: seq_cfg.sync = (uint8_t)clamp(seq_cfg.sync + s, 0, FM1B_BLE ? 3 : 2); app_mark(); break;
    case KN_CLKOUT: seq_cfg.clock_out = s > 0; app_mark(); break;
    case KN_SEQOUT: seq_cfg.seq_out = s > 0; app_mark(); break;
    case KN_SLOT: slot_pick = (uint8_t)clamp(slot_pick + s, 0, NSONG - 1); break;
    case KN_LOAD:
        if (s > 0 && go(KN_LOAD, "LOAD")) {
            if (seq_load(slot_pick) == 0) {
                prefs.slot = slot_pick;
                app_mark();
                app_say("LOADED", "");
            } else {
                app_say("EMPTY", "SLOT");
            }
        }
        break;
    case KN_SAVE:
        if (s > 0)
            do_save(slot_pick);
        break;
    case KN_NEW:
        if (s > 0 && go(KN_NEW, "NEW")) {
            seq_new();
            app_say("NEW", "SONG");
        }
        break;
    case KN_SCN: ui.scn = (uint8_t)clamp(ui.scn + s, 0, NSCENE - 1); break;
    case KN_SCN_CH: ui.scn_ch = (uint8_t)clamp(ui.scn_ch + s, 0, NCH - 1); break;
    case KN_SCN_PAT: seq_scene_pat(ui.scn, ui.scn_ch, (uint32_t)clamp(song.scene[ui.scn].pat[ui.scn_ch] + s, 0, NPAT - 1)); break;
    case KN_SCN_SND: seq_scene_sound(ui.scn, s > 0); break;
    case KN_CS: ui.cs = (uint8_t)clamp(ui.cs + s, 0, song.nchain < NCHAIN ? song.nchain : NCHAIN - 1); break;
    case KN_CS_SCENE:
        if (ui.cs >= song.nchain && s > 0 && song.nchain < NCHAIN) {   /* past the end: a new section */
            song.chain[song.nchain].scene = (uint8_t)seq_scene();
            song.chain[song.nchain].bars = 4;
            song.chain[song.nchain].reps = 1;
            song.nchain++;
        } else if (ui.cs < song.nchain) {
            int32_t v = song.chain[ui.cs].scene + s;
            if (v < 0)                   /* before the first scene: END, the chain stops here */
                song.nchain = ui.cs;
            else
                song.chain[ui.cs].scene = (uint8_t)clamp(v, 0, NSCENE - 1);
        }
        dirty();
        break;
    case KN_CS_BARS:
        if (ui.cs < song.nchain)
            song.chain[ui.cs].bars = (uint8_t)clamp(song.chain[ui.cs].bars + s, 0, 64);
        dirty();
        break;
    case KN_CS_REPS:
        if (ui.cs < song.nchain)
            song.chain[ui.cs].reps = (uint8_t)clamp(song.chain[ui.cs].reps + s, 1, 16);
        dirty();
        break;
    case KN_RMODE: seq_cfg.rec_mode = s > 0 ? RM_TEMPO : RM_FREE; app_mark(); break;
    case KN_RBARS: {
        static const uint8_t B[4] = {1, 2, 4, 8};
        uint32_t i = 0;
        while (i < 3u && B[i] < seq_cfg.rec_bars)
            i++;
        seq_cfg.rec_bars = B[clamp((int32_t)i + s, 0, 3)];
        app_mark();
        break;
    }
    case KN_RSTART: seq_cfg.rec_start = s > 0 ? RS_COUNT : RS_NOTE; app_mark(); break;
    case KN_RQUANT: seq_cfg.rec_quant = s > 0; app_mark(); break;
    case KN_RREPL: seq_cfg.rec_replace = s > 0; seq_replace_changed(); app_mark(); break;
    case KN_NMCUR: ui.name_cur = (uint8_t)clamp(ui.name_cur + s, 0, SONG_NAME - 1); break;
    case KN_NMCHR: {                     /* the character at the cursor, round the set */
        uint32_t i = 0, n = sizeof NAME_CS - 1u;
        while (NAME_CS[i] && NAME_CS[i] != ui.name[ui.name_cur % SONG_NAME])
            i++;
        ui.name[ui.name_cur % SONG_NAME] = NAME_CS[((int32_t)(i % n) + d % (int32_t)n + (int32_t)n) % (int32_t)n];
        break;
    }
    case KN_NMPRE: {                     /* a ready name, the cursor after it */
        uint32_t i;
        const char *r;
        ui.name_pre = (uint8_t)(((int32_t)ui.name_pre - 1 + s + (int32_t)NREADY) % (int32_t)NREADY + 1);
        r = NAME_READY[ui.name_pre - 1u];
        for (i = 0; i < SONG_NAME; i++)
            ui.name[i] = ' ';
        for (i = 0; r[i]; i++)
            ui.name[i] = r[i];
        ui.name_cur = (uint8_t)(i < SONG_NAME ? i : SONG_NAME - 1u);
        break;
    }
    case KN_NMCLR: {
        uint32_t i;
        for (i = 0; i < SONG_NAME; i++)
            ui.name[i] = ' ';
        ui.name_cur = 0;
        ui.name_pre = 0;
        break;
    }
    case KN_SNOTE:
        if (synth_is_kit(ch)) {          /* DRUM: the lanes in turn */
            int l = drum_lane_of(ui.entry[ch]);
            ui.entry[ch] = (uint8_t)synth_drum_note((uint32_t)clamp((l < 0 ? 0 : l) + s, 0, 7));
        } else {
            ui.entry[ch] = (uint8_t)clamp(ui.entry[ch] + s, maps_keys(ch) ? 36 : 0, maps_keys(ch) ? 51 : 127);
        }
        break;
    case KN_ESHIFT: seq_pat_shift(ch, s); app_say("SHIFT", s > 0 ? "LATER" : "EARLIER"); break;
    case KN_ELEN: {
        char t[8];
        seq_pat_bars(ch, s);
        int_text(seq_pat(ch)->bars, t);
        text_cat(t, " BAR");
        app_say("LENGTH", t);
        break;
    }
    case KN_ETRANS: seq_pat_transpose(ch, s); break;
    case KN_STNOTE: case KN_STVEL: case KN_STRAT: case KN_STNUDGE:
        if (ui.held_step >= 0) {
            uint32_t st = (uint32_t)ui.held_step;
            ui.step_turned = 1;
            if (id == KN_STNOTE)
                seq_step_note(ch, st, s);
            else if (id == KN_STVEL)
                seq_step_vel(ch, st, d);
            else if (id == KN_STRAT)
                seq_step_ratchet(ch, st, s);
            else
                seq_step_nudge(ch, st, s);
        }
        break;
    case KN_VSENS:
        set.vsens = (int8_t)clamp(set.vsens + e, -5, 5);
        synth_vel_sens(set.vsens);
        app_mark();
        break;
    case KN_LEARN: prefs.key_learn = s > 0; keys_config(); app_mark(); break;
    case KN_FIXVEL: prefs.fixed_vel = (uint8_t)clamp(prefs.fixed_vel + e * 4, 0, 127); app_mark(); break;
    case KN_PRESS: prefs.prs_mode = (uint8_t)clamp(prefs.prs_mode + s, 0, PRS_NMODES - 1); keys_config(); app_mark(); break;
    case KN_DISPLAY: prefs.display = s > 0; app_mark(); break;
    case KN_BT: ble_set(s < 0); break;
    case KN_ACCEL: prefs.knob_accel = s > 0; app_mark(); break;
    case KN_GRAPHS: prefs.graphs = s > 0; prefs.graph_set = 0; app_mark(); break;   /* (every page to the default) */
    case KN_HOMEVIEW: prefs.home_view = s > 0; ui.loop = prefs.home_view; ui.loop_auto = 0; app_mark(); break;
    case KN_AUTOLOOP: prefs.loop_manual = s < 0; app_mark(); break;
    case KN_FACTORY:                     /* armed for 10 s: PLAY held 3 s erases everything nullwave keeps */
        ui.fr_arm = plat_ms() | 1u;
        app_say("ERASES ALL SONGS", "HOLD PLAY 3 S");
        ui.msg_until = plat_ms() + 10000u;
        break;
    case KN_LIGHTS: prefs.lights = (uint8_t)clamp(prefs.lights + s, 0, 2); app_mark(); break;
    case KN_SCALEKEYS: prefs.scale_keys = s > 0; app_mark(); break;
    case KN_AUTOSAVE: prefs.autosave = s > 0; app_mark(); break;
    case KN_GSWING: song.swing = (uint8_t)clamp(song.swing + d * 2, 0, 100); dirty(); break;
    case KN_STRUM: song.strum = (int8_t)clamp(song.strum + d, -60, 60); dirty(); break;
    case KN_VLEAD: song.vlead = s > 0; dirty(); break;
    case KN_FM_OP: ui.fm_op = (uint8_t)clamp(ui.fm_op + s, 0, 5); break;
    case KN_FM_STAGE: ui.fm_stage = (uint8_t)clamp(ui.fm_stage + s, 0, 3); break;
    case KN_FM_RATIO: synth_fm6_op_turn(ch, ui.fm_op, FM6_RATIO, 0, s); dirty(); break;
    case KN_FM_FINE: synth_fm6_op_turn(ch, ui.fm_op, FM6_FINE, 0, d); dirty(); break;
    case KN_FM_LEVEL: synth_fm6_op_turn(ch, ui.fm_op, FM6_LEVEL, 0, d); dirty(); break;
    case KN_FM_RATE: synth_fm6_op_turn(ch, ui.fm_op, FM6_RATE, ui.fm_stage, d); dirty(); break;
    case KN_FM_ELEVEL: synth_fm6_op_turn(ch, ui.fm_op, FM6_ELEVEL, ui.fm_stage, d); dirty(); break;
    case KN_FM_MODE: synth_fm6_op_turn(ch, ui.fm_op, FM6_MODE, 0, s); dirty(); break;
    case KN_FM_DTUN: synth_fm6_op_turn(ch, ui.fm_op, FM6_DTUN, 0, s); dirty(); break;
    case KN_FM_VEL: synth_fm6_op_turn(ch, ui.fm_op, FM6_VEL, 0, s); dirty(); break;
    case KN_VMUTE: seq_set_mute(s > 0 ? seq_mute() | 1u << ch : seq_mute() & ~(1u << ch)); break;
    case KN_PFADE: prefs.pfade = (uint8_t)clamp(prefs.pfade + s, 0, 15); synth_punch_fade(prefs.pfade); app_mark(); break;
    case KN_PRE_SOUND: {
        uint32_t nn = synth_nsounds();
        synth_sound_pick(ch, (uint32_t)(((int32_t)synth_sound_index(ch) + e % (int32_t)nn + (int32_t)nn) % (int32_t)nn));
        dirty();
        break;
    }
    case KN_PRE_ENGINE:
        synth_engine(ch, (uint32_t)((synth_sound(ch)->engine + synth_nengines() + (uint32_t)s) % synth_nengines()));
        dirty();
        break;
    case KN_PRE_FAV: ui_fav(ch, s > 0); break;
    case KN_USLOT: {                     /* (the slot's name said as it turns) */
        char t[8] = "USER ";
        uint32_t k = 5;
        ui.uslot = (uint8_t)clamp(ui.uslot + s, 0, 31);
        if (ui.uslot >= 9u)
            t[k++] = (char)('0' + (ui.uslot + 1u) / 10u);
        t[k++] = (char)('0' + (ui.uslot + 1u) % 10u);
        t[k] = 0;
        app_say(t, user_name(ui.uslot));
        break;
    }
    case KN_ULOAD:
        if (s > 0 && go(KN_ULOAD, "LOAD")) {
            if (user_load(ui.uslot, ch))
                app_say("LOADED", "USER");
            else
                app_say("EMPTY", "SLOT");
        }
        break;
    case KN_UERASE:
        if (s > 0 && go(KN_UERASE, "ERASE"))
            app_say(user_erase(ui.uslot) ? "ERASED" : "STOP FIRST", "");
        break;
    case KN_USAVE:
        if (s > 0 && go(KN_USAVE, "SAVE"))
            app_say(user_save(ui.uslot, ch) ? "SAVED" : "STOP FIRST", "USER");
        break;
    case KN_CLRSQ:
        if (s > 0 && go(KN_CLRSQ, "CLEAR")) {
            seq_clear(ch);
            app_say("CLEARED", "");
        }
        break;
    case KN_INIT:
        if (s > 0 && go(KN_INIT, "INIT")) {
            synth_engine(ch, synth_sound(ch)->engine);
            dirty();
            app_say("INIT", synth_engine_name(synth_sound(ch)->engine));
        }
        break;
    case KN_LOWCUT: prefs.lowcut = s > 0; app_mark(); break;
    case KN_USBA: prefs.usb_full = s > 0; app_mark(); break;
    case KN_STEPADV: prefs.step_adv = s > 0; app_mark(); break;
    case KN_DRPREV: prefs.drum_prev = s > 0; app_mark(); break;
    case KN_NSEL: case KN_NPITCH: case KN_NMOVE: case KN_NLEN: {
        uint8_t ix[PAT_EV];
        uint32_t n = seq_ev_list(ch, ix, PAT_EV), at;
        const ev_t *e;
        if (!n)
            break;
        if (ui.nsel >= n)
            ui.nsel = (uint8_t)(n - 1u);
        if (id == KN_NSEL) {
            ui.nsel = (uint8_t)clamp(ui.nsel + s, 0, (int32_t)n - 1);
            e = seq_ev(ch, ix[ui.nsel]);
            if (!seq_playing())          /* (heard as it is picked) */
                preview_note(ch, e->note, e->vel);
            break;
        }
        at = ix[ui.nsel];
        e = seq_ev(ch, at);
        if (id == KN_NPITCH) {
            seq_ev_edit(ch, at, d, 0, 0);
            if (!seq_playing())
                preview_note(ch, seq_ev(ch, at)->note, e->vel);
        } else if (id == KN_NMOVE) {
            uint16_t was = e->pos;
            uint8_t note = e->note;
            uint32_t k;
            seq_ev_edit(ch, at, 0, s * (int32_t)seq_step_ticks(seq_pat(ch)), 0);
            n = seq_ev_list(ch, ix, PAT_EV);   /* (its place in time order has moved: follow it) */
            for (k = 0; k < n; k++)
                if (ix[k] == at)
                    ui.nsel = (uint8_t)k;
            (void)was;
            (void)note;
        } else
            seq_ev_edit(ch, at, 0, 0, s * (seq_ev(ch, at)->len >= 8u ? 2 : 1));
        break;
    }
    case KN_DLANE:
        ui.dlane = (uint8_t)clamp(ui.dlane + s, 0, 7);
        if (prefs.drum_prev && !seq_playing())
            preview_note(ch, (int32_t)synth_drum_note(ui.dlane), 100);
        break;
    case KN_DSTEP: ui.dstep = (uint8_t)clamp(ui.dstep + s, 0, (int32_t)seq_steps(seq_pat(ch)) - 1); break;
    case KN_DHIT:
        seq_hit_set(ch, ui.dstep, synth_drum_note(ui.dlane), s > 0 ? 100u : 0u);
        if (s > 0 && prefs.drum_prev && !seq_playing())
            preview_note(ch, (int32_t)synth_drum_note(ui.dlane), 100);
        if (s > 0 && prefs.step_adv)     /* ADVANCE: on to the next step */
            ui.dstep = (uint8_t)((ui.dstep + 1u) % seq_steps(seq_pat(ch)));
        break;
    case KN_DVEL: {
        int i = seq_hit_find(ch, ui.dstep, synth_drum_note(ui.dlane));
        if (i >= 0)
            seq_hit_set(ch, ui.dstep, synth_drum_note(ui.dlane), (uint32_t)clamp(seq_ev(ch, (uint32_t)i)->vel + d * 2, 1, 127));
        break;
    }
    case KN_TUNE: prefs.tune = (int8_t)clamp(prefs.tune + d, -50, 50); synth_tune(prefs.tune); app_mark(); break;
    case KN_IN: prefs.in_mode = (uint8_t)clamp(prefs.in_mode + s, 0, 17); app_mark(); break;
    case KN_OTHER: prefs.other = s > 0; app_mark(); break;
    case KN_BEND: prefs.bend = (uint8_t)clamp(prefs.bend + s, 1, 12); app_mark(); break;
    case KN_DRMAP: prefs.drmap = s > 0; app_mark(); break;
    case KN_REMOTE: prefs.remote = (uint8_t)clamp(prefs.remote + s, 0, 16); app_mark(); break;
    case KN_FXCH: prefs.fxch = (uint8_t)clamp(prefs.fxch + s, 0, 16); app_mark(); break;
    case KN_DR_CH: case KN_DR_LVL: case KN_DR_REV: case KN_DR_DLY: {
        int dc = dr_ch();
        if (dc < 0)
            break;
        if (id == KN_DR_CH) {
            prefs.midi_ch[dc] = (uint8_t)clamp(prefs.midi_ch[dc] + s, 0, 16);
            app_mark();
        } else if (id == KN_DR_LVL) {
            seq_set_level((uint32_t)dc, (uint32_t)clamp(lvl_q((uint32_t)dc) + d, 0, 127));
        } else {
            synth_param_turn((uint32_t)dc, id == KN_DR_REV ? SP_REV : SP_DLY, d);
            dirty();
        }
        break;
    }
    case KN_BTDEV:
        if (FM1B_BLE_CENTRAL && !btdev_busy())
            btdev_choose(btdev_pick() + s);
        break;
    case KN_BTGO:
        if (FM1B_BLE_CENTRAL && !set.ble_off && s > 0 && go(KN_BTGO, btdev_busy() ? "DROP" : "CONNECT")) {
            int ev = btdev_save();
            app_say(ev == BTDEV_CONNECTING ? btdev_word(ev) : "BLE", ev == BTDEV_CONNECTING ? btdev_name() : btdev_word(ev));
        }
        break;
    default: break;
    }
}

/* ---------------------------------------------------------- the context --- */
static int layer_on(void) { return ui.layer >= 0; }
/* the layer as the screen and the lights show it: once held LAYER_SHOW_MS, used, or kept (a tap never shows it, so a
 * tap is one thing on the screen: the button's pages) */
static int layer_shown(void)
{
    return ui.layer >= 0 && (ui.layer_lock || ui.layer_used || plat_ms() - ui.down_t[ui.layer] >= LAYER_SHOW_MS
                             || !(ui.btn >> ui.layer & 1u));
}
static int shift_held(void) { return (ui.btn >> B_OCTDN & 1u) || (ui.btn >> B_OCTUP & 1u); }
static const uint16_t *knobs_now(void)    /* the four knobs' jobs now */
{
    static uint16_t lk[4];
    if (ui.layer == B_SEQ && ui.held_step >= 0)
        return STEP_KN;
    if (layer_shown()) {
        if (ui.layer == B_SAVE) {
            lk[0] = lk[1] = lk[2] = lk[3] = KN_NONE;
            return lk;
        }
        return LAYER_KN[ui.layer];
    }
    if (seq_rec() == REC_ARMED && ui.page == PG_HOME)
        return PAGE_KN[PG_REC];
    if (ui.page == PG_HOME && !ui.loop)  /* PLAY: the engine's EDIT 1, to play with */
        return PAGE_KN[PG_EDIT1];
    return PAGE_KN[ui.page];
}

static int group_list(uint32_t b, int8_t *out)   /* the group's pages shown now, their number */
{
    int i, n = 0;
    const int8_t *g = GROUP[b];
    for (i = 0; i < GMAX && g[i] >= 0; i++)
        if (page_shown(g[i]))
            out[n++] = g[i];
    return n;
}
static int page_index(void)              /* the page's place in its group, 0 none */
{
    int8_t l[GMAX];
    int i, n = group_list(ui.group_btn, l);
    for (i = 0; i < n; i++)
        if (l[i] == ui.page)
            return i;
    return 0;
}

static void page_open(uint32_t b)        /* a function button tapped: its first page, or its next */
{
    int8_t l[GMAX];
    int i, n = group_list(b, l), cur = -1;
    if (!n) {
        static const char *const BTN_NAME[NB] = {"FX", "SEL", "ENV", "LFO", "EDIT", "GLO", "HOME", "SAVE", "ARP",
                                                 "SEQ", "PLAY", "REC", "OCT-", "OCT+"};
        app_say(BTN_NAME[b], "NO PAGES");
        return;
    }
    for (i = 0; i < n; i++)
        if (l[i] == ui.page)
            cur = i;
    ui.page = (uint8_t)l[cur >= 0 && ui.group_btn == b ? (cur + 1) % n : 0];
    ui.group_btn = (uint8_t)b;
    ui.layer = -1;
    ui.layer_lock = 0;
}

static void page_turn(int32_t d)         /* SELECT on a page: the next or the one before in its group */
{
    int8_t l[GMAX];
    int n = group_list(ui.group_btn, l), cur = page_index();
    if (!n)
        return;
    cur = clamp(cur + (d > 0 ? 1 : -1), 0, n - 1);   /* (SELECT stops at the ends) */
    ui.page = (uint8_t)l[cur];
}

static int group_pages(void)
{
    int8_t l[GMAX];
    return group_list(ui.group_btn, l);
}

static void go_home(void)
{
    ui.page = PG_HOME;
    ui.group_btn = B_HOME;
    ui.layer = -1;
    ui.layer_lock = 0;
    ui.held_step = -1;
}

/* ------------------------------------------------------------- the input --- */
static void play_key(uint32_t k, uint32_t vel)
{
    int8_t n[5];
    uint32_t cnt = key_notes(k, n), i, mc = nw_midi_ch(set.channel);
    for (i = 0; i < 5u; i++)
        ui.key_note[k][i] = (int8_t)(i < cnt ? n[i] : -1);
    ui.key_ch[k] = set.channel;
    for (i = 0; i < cnt; i++) {
        int32_t pn = maps_keys(set.channel) ? n[i] : clamp(n[i] + song.transpose, 0, 127);
        uint32_t f;
        if (seq_arp_on(set.channel)) {   /* the arp plays them */
            seq_arp_key(set.channel, (uint32_t)pn, vel, 1);
            continue;
        }
        f = plat_lock();
        if (song.strum && cnt > 1u && i) {   /* STRUM: the chord's notes a few ms apart (+ low to high) */
            uint32_t ord = song.strum > 0 ? i : cnt - 1u - i;
            plat_unlock(f);
            seq_strum(set.channel, (uint32_t)pn, vel, ord * (uint32_t)(song.strum < 0 ? -song.strum : song.strum));
            seq_note_on(set.channel, (uint32_t)n[i], vel);
            continue;
        }
        synth_note_on(set.channel, (uint32_t)pn, vel);
        plat_unlock(f);
        seq_note_on(set.channel, (uint32_t)n[i], vel);
        last_note = pn;
        last_ch = set.channel;
        if (mc)
            plat_midi_send(0x09u | (0x8Fu + mc) << 8 | (uint32_t)pn << 16 | vel << 24, TO_USB | TO_BLE);
    }
    if (cnt)
        ui.entry[set.channel] = (uint8_t)n[0];
}

static void release_key(uint32_t k)
{
    uint32_t i, ch = ui.key_ch[k], mc = nw_midi_ch(ch);
    for (i = 0; i < 5u; i++) {
        int32_t n = ui.key_note[k][i], pn;
        if (n < 0)
            continue;
        pn = maps_keys(ch) ? n : clamp(n + song.transpose, 0, 127);
        if (seq_arp_on(ch)) {
            seq_arp_key(ch, (uint32_t)pn, 0, 0);
            ui.key_note[k][i] = -1;
            continue;
        }
        {
            uint32_t f = plat_lock();
            synth_note_off(ch, (uint32_t)pn);
            plat_unlock(f);
        }
        seq_note_off(ch, (uint32_t)n);
        if (last_note == pn)
            last_note = -1;
        if (mc)
            plat_midi_send(0x08u | (0x7Fu + mc) << 8 | (uint32_t)pn << 16, TO_USB | TO_BLE);
        ui.key_note[k][i] = -1;
    }
}

/* EDIT held, the black keys by the legends printed on the panel: OP1..OP6 (FM6: that operator's pages; held 0.5 s:
 * the operator muted or back, as the stock firmware's long press), PIT the PITCH page, GLO the glide on or off, MONO,
 * POLY the channel's voice mode. On other engines OP1 and OP2 open EDIT 1 and EDIT 2 */
static void edit_legend(uint32_t b, int held)
{
    uint32_t ch = set.channel, fm6 = synth_sound(ch)->engine == 1u;
    char t[8] = "OP ";
    if (b < 6u) {
        t[2] = (char)('1' + b);
        t[3] = 0;
        if (fm6 && held) {
            app_say(t, synth_fm6_mute(ch, b) ? "MUTED" : "ON");
            dirty();
        } else if (fm6) {
            ui.fm_op = (uint8_t)b;
            ui.page = PG_FM_OP;
            ui.group_btn = B_EDIT;
            app_say(t, "EDIT");
        } else if (b < 2u) {
            ui.page = b ? PG_EDIT2 : PG_EDIT1;
            ui.group_btn = B_EDIT;
        } else {
            app_say(t, "FM6 ONLY");
        }
    } else if (b == 6u) {                /* PIT */
        ui.page = PG_PITCH;
        ui.group_btn = B_EDIT;
    } else if (b == 7u) {                /* GLO: the glide on (a medium time) or off; VOICE shows it */
        int32_t g = synth_param(ch, SP_GLIDE);
        synth_param_set(ch, SP_GLIDE, g ? 0 : 40);
        dirty();
        ui.page = PG_PITCH;
        ui.group_btn = B_EDIT;
        app_say("GLIDE", g ? "OFF" : "ON");
    } else if (b == 8u || b == 9u) {     /* MONO, POLY */
        synth_param_set(ch, SP_MONO, b == 8u ? 1 : 0);
        dirty();
        app_say("VOICE", b == 8u ? "MONO" : "POLY");
    }
}

static void layer_key(uint32_t k, int down)
{
    int w = white_index(k), b = black_index(k);
    uint32_t ch = set.channel;
    char t[12];
    ui.layer_used = 1;
    ui.used[ui.layer] = 1;
    switch (ui.layer) {
    case B_GLO:                          /* D4 MIDI LEARN, E4 FILL while held; around them: F4-D5 the
                                          * mutes of channels 1-6, the black keys F#4-F#5 their solos, C4 the next
                                          * bar a fill, G5 tap tempo */
        if (w == 6) {
            seq_fill_hold(down);
            break;
        }
        if (!down)
            break;
        if (w == 5) {
            ui.learn = ui.learn ? 0 : 1;
            app_say(ui.learn ? "LEARN:" : "LEARN", ui.learn ? "TURN A KNOB" : "DONE");
        } else if (w == 4) {
            seq_fill_bar();
        } else if (w >= 7 && w <= 12) {
            seq_set_mute(seq_mute() ^ (1u << (w - 7)));
            channel_text((uint32_t)(w - 7), t);
            app_say(t, seq_mute() >> (w - 7) & 1u ? "MUTED" : "ON");
        } else if (b >= 5 && b <= 10) {
            seq_set_solo(seq_solo() ^ (1u << (b - 5)));
            channel_text((uint32_t)(b - 5), t);
            app_say(t, seq_solo() >> (b - 5) & 1u ? "SOLO" : "SOLO OFF");
        } else if (w == 15) {
            seq_tap();
        }
        break;
    case B_SEL:
        if (!down)
            break;
        song.root = (uint8_t)((53u + k) % 12u);
        dirty();
        app_say("KEY", NOTE_NAME[song.root]);
        break;
    case B_EDIT: {
        int8_t n[5];
        uint32_t cnt;
        if (!down) {
            seq_erase_hold(ch, -1);
            if (b >= 0 && ui.edit_key == (int8_t)b)
                edit_legend((uint32_t)b, plat_ms() - ui.edit_t >= 500u);
            ui.edit_key = -1;
            break;
        }
        if (b >= 0 && b < 8 && synth_sound(ch)->engine == 12u) {   /* DRUM: black keys 1-8 mute its lanes */
            app_say(synth_kit_sound((uint32_t)b), synth_lane_mute(ch, (uint32_t)b) ? "MUTED" : "ON");
            break;
        }
        if (b >= 0) {                    /* the panel's legends: OP1-OP6, PIT, GLO, MONO, POLY (let go: edit_key) */
            ui.edit_key = (int8_t)b;
            ui.edit_t = plat_ms();
            break;
        }
        cnt = key_notes(k, n);
        if (!cnt)
            break;
        if (seq_playing()) {
            seq_erase_hold(ch, n[0]);
        } else {
            seq_erase(ch, n[0]);
            app_say("ERASED", "");
        }
        break;
    }
    case B_SEQ:
        if (w >= 0) {
            uint32_t s = (uint32_t)ui.step_page * 16u + (uint32_t)w;
            if (s >= seq_steps(seq_pat(ch)))
                break;
            if (down) {
                ui.held_step = (int16_t)s;
                ui.step_turned = 0;
                ui.step_new = 0;
                if (seq_step_find(ch, s) < 0) {   /* empty: set now (with the note, or the kit's sound) */
                    seq_step_set(ch, s, ui.entry[ch], 100);
                    ui.step_new = 1;
                }
            } else if (ui.held_step == (int16_t)s) {
                if (!ui.step_new && !ui.step_turned)
                    seq_step_clear(ch, s);
                ui.held_step = -1;
            }
        } else if (b >= 0 && b < 8 && down && synth_is_kit(ch)) {   /* a kit: the black keys pick the lane */
            ui.entry[ch] = (uint8_t)synth_drum_note((uint32_t)b);
            app_say("LANE", synth_kit_sound((uint32_t)b));
        }
        break;
    case B_FX:                           /* the punch-in effects, while held */
        if (w >= 0)
            synth_punch((uint32_t)w, down);
        else if (b >= 0)
            synth_punch(16u + (uint32_t)b, down);
        break;
    case B_ARP: {                        /* note repeat at ROLL (OCT- / OCT+ held: ghost, hard) */
        int8_t n[5];
        uint32_t vel = (ui.btn >> B_OCTDN & 1u) ? 40u : (ui.btn >> B_OCTUP & 1u) ? 127u : ui.vel[k] ? ui.vel[k] : 100u;
        if (key_notes(k, n))
            seq_roll(ch, (uint32_t)(maps_keys(ch) ? n[0] : clamp(n[0] + song.transpose, 0, 127)), vel, down);
        break;
    }
    case B_SAVE:
        if (w >= 0 && down) {            /* a scene's key: what it does when let go (held 1 s: the scene stored) */
            ui.save_key = (int8_t)w;
            ui.save_t = plat_ms();
            break;
        }
        if (!down && w >= 0 && ui.save_key != (int8_t)w)
            break;                       /* (stored while held, or another key's) */
        if (!down && w < 0)
            break;
        if (w >= 0) {
            char s[4];
            ui.save_key = -1;
            int_text(w + 1, s);
            if (ui.copy_mode) {
                seq_scene_copy(seq_scene(), (uint32_t)w);
                ui.copy_mode = 0;
                app_say("COPIED TO", s);
            } else if (ui.save_keys++ == 0) {
                seq_launch((uint32_t)w);
                app_say("SCENE", s);
            } else {
                seq_qchain_add((uint32_t)w);
                app_say("CHAIN +", s);
            }
        } else if (b == 0) {
            ui.copy_mode = (uint8_t)!ui.copy_mode;
            app_say("COPY", ui.copy_mode ? "PICK A SCENE" : "OFF");
        } else if (b == 1) {
            seq_mode(song.mode == MODE_LOOP);
            app_say("PLAY", song.mode ? "SONG" : "LOOP");
        } else if (b == 2) {
            seq_song_rec(!seq_song_rec_on());
            app_say("SONG REC", seq_song_rec_on() ? "ON" : "OFF");
        }
        break;
    default:
        break;
    }
}

/* the keys: releases first (pass 0: a layer let go in the same frame still sees them), then presses (pass 1) */
static void keys_input(uint32_t keys, int pass)
{
    uint32_t k;
    for (k = 0; k < NKEYS; k++) {            /* key 0 is F3 (MIDI 53) */
        uint32_t bit = 1u << k, v;
        if ((pass == 0) != (!(keys & bit) && (ui.keys & bit)))
            continue;
        if ((keys & bit) && !(ui.keys & bit)) {
            v = touch_vel(k);
            ui.vel[k] = (uint8_t)v;              /* (as measured) */
            if (prefs.fixed_vel)
                v = prefs.fixed_vel;
            ui.prs[k] = 0;
            ui.last_key = (int32_t)k;
            if (ui.btn >> B_OCTDN & 1u || ui.btn >> B_OCTUP & 1u)
                ui.oct_used = 1;
            if (layer_on() && ui.layer != B_SEL + 100) {
                layer_key(k, 1);
                ui.key_note[k][0] = -2;          /* (taken by the layer: no note-off) */
                continue;
            }
            play_key(k, v);
        } else if (!(keys & bit) && (ui.keys & bit)) {
            ui.prs[k] = 0;
            if (ui.key_note[k][0] == -2) {
                ui.key_note[k][0] = -1;
                if (layer_on())
                    layer_key(k, 0);
                else if (ui.held_step >= 0) {    /* SEQ let go first: the step's tap still counts */
                    if (!ui.step_new && !ui.step_turned)
                        seq_step_clear(set.channel, (uint32_t)ui.held_step);
                    ui.held_step = -1;
                }
                seq_erase_hold(set.channel, -1);
                continue;
            }
            release_key(k);
        } else if ((keys & bit) && ui.key_note[k][0] >= 0 && touch_pressure(k) != ui.prs[k]) {
            uint32_t i, mc = nw_midi_ch(ui.key_ch[k]);
            ui.prs[k] = touch_pressure(k);
            for (i = 0; i < 5u && ui.key_note[k][i] >= 0; i++) {
                uint32_t pn = (uint32_t)(maps_keys(ui.key_ch[k]) ? ui.key_note[k][i]
                                                                       : clamp(ui.key_note[k][i] + song.transpose, 0, 127));
                pressure(ui.key_ch[k], pn, ui.prs[k]);
                if (mc)
                    plat_midi_send(0x0Au | (0x9Fu + mc) << 8 | pn << 16 | (uint32_t)ui.prs[k] << 24, TO_USB | TO_BLE);
            }
        }
    }
}

/* ------------------------------------------------------- the graph view --- */
/* SHIFT + a page's own button: a picture of what its knobs do, above the footer (y 26..193, three bands of 56 px),
 * redrawn as a knob turns and, where it moves, as it plays. Remembered per kind of page (GRAPHS: the default). */
enum { GR_NONE, GR_ENV, GR_EDEST, GR_LFO, GR_FILTER, GR_AFILTER, GR_SCOPE, GR_DLY, GR_SLICER, GR_ARP, GR_SEL,
       GR_INSERT, GR_N };
static const char *const GR_NAME[GR_N] = {"", "ENV", "ENV DEST", "LFO", "FILTER", "FILTER", "SCOPE", "DELAY",
                                          "SLICER", "ARP", "SCALE", "INSERT"};
static uint32_t graph_kind_of(uint32_t pg)
{
    switch (pg) {
    case PG_ENV: return GR_ENV;
    case PG_ENVDEST: return GR_EDEST;
    case PG_LFO: case PG_LFO2: case PG_LFODEST: return GR_LFO;
    case PG_FILTER: return GR_FILTER;
    case PG_AFILTER: return GR_AFILTER;
    case PG_EDIT1: case PG_EDIT2: return GR_SCOPE;
    case PG_DELAY: return GR_DLY;
    case PG_SLICER: return GR_SLICER;
    case PG_ARP: case PG_ARP2: return GR_ARP;
    case PG_KEY: case PG_KEY2: return GR_SEL;
    case PG_INSERT: case PG_INSERT2: return GR_INSERT;
    default: return GR_NONE;
    }
}
static int graph_wanted(uint32_t kind)
{
    return prefs.graph_set >> kind & 1u ? (int)(prefs.graph_on >> kind & 1u) : prefs.graphs;
}
static uint32_t graph_now(void)          /* the graph shown, GR_NONE none */
{
    uint32_t kind = graph_kind_of(ui.page);
    if (layer_shown() || ui.group_btn >= NB || kind == GR_NONE || !graph_wanted(kind))
        return GR_NONE;
    return kind;
}
static int group_has_graph(uint32_t k)
{
    uint32_t i;
    for (i = 0; i < GMAX && GROUP[k][i] >= 0; i++)
        if (graph_kind_of((uint32_t)GROUP[k][i]) != GR_NONE)
            return 1;
    return 0;
}
static void graph_toggle(uint32_t kind, int on)
{
    prefs.graph_set = (uint16_t)(prefs.graph_set | 1u << kind);
    prefs.graph_on = (uint16_t)(on ? prefs.graph_on | 1u << kind : prefs.graph_on & ~(1u << kind));
    app_mark();
    app_say("GRAPH", on ? "ON" : "OFF");
}

#define GX0 12
#define GX1 228
#define GY0 18
#define GY1 150
#define G_FS 44100.0f
static uint16_t g_col;                   /* the channel's colour */

/* a line with smoothed edges, hw its half width in pixels: along its longer axis, each step lights the pixels across it
 * by how near their centres are to the line (as the graphs' curves are thin, near-vertical in places) */
static void aa_line(float x0, float y0, float x1, float y1, float hw, uint16_t c)
{
#ifdef NW_HD
    hd_line(x0, y0, x1, y1, hw, c);
    hd_quiet++;
#endif
    float dx = x1 - x0, dy = y1 - y0, adx = dx < 0 ? -dx : dx, ady = dy < 0 ? -dy : dy, g, w;
    int steep = ady > adx, i0, i1, i;
    if (steep) {
        float t = x0; x0 = y0; y0 = t;
        t = x1; x1 = y1; y1 = t;
        t = dx; dx = dy; dy = t;
    }
    if (x0 > x1) {
        float t = x0; x0 = x1; x1 = t;
        t = y0; y0 = y1; y1 = t;
    }
    g = dx != 0.0f ? (y1 - y0) / (x1 - x0) : 0.0f;
    w = hw * fm_sqrtf(1.0f + g * g);     /* (the width across, measured along the minor axis) */
    i0 = (int)(x0 + 0.5f);
    i1 = (int)(x1 + 0.5f);
    for (i = i0; i <= i1; i++) {
        float y = y0 + g * ((float)i - x0);
        int j, j0 = (int)(y - w - 1.0f), j1 = (int)(y + w + 1.0f);
        for (j = j0; j <= j1; j++) {
            float d = (float)j - y, a;
            d = d < 0 ? -d : d;
            a = w + 0.5f - d;
            if (a <= 0.0f)
                continue;
            a = a > 1.0f ? 15.0f : a * 15.0f;
            if (steep)
                px_blend(j, i, c, (uint32_t)a);
            else
                px_blend(i, j, c, (uint32_t)a);
        }
    }
#ifdef NW_HD
    hd_quiet--;
#endif
}
static void g_line(int32_t x0, int32_t y0, int32_t x1, int32_t y1, uint16_t c)   /* a curve's segment, ~2 px */
{
    aa_line((float)x0, (float)y0, (float)x1, (float)y1, 0.75f, c);
}
static void g_thin(int32_t x0, int32_t y0, int32_t x1, int32_t y1, uint16_t c)   /* ~1 px */
{
    aa_line((float)x0, (float)y0, (float)x1, (float)y1, 0.35f, c);
}
static void g_text(int32_t x, int32_t y, const char *t, uint16_t c) { cv_text(x, at_base(&FONT_T, y), &FONT_T, t, c); }
static void g_dot(int32_t x, int32_t y, uint16_t c)
{
    circle(2 * x + 1, 2 * y + 1, 4.5f, 0, TC(BG));
    circle(2 * x + 1, 2 * y + 1, 3.5f, 0, c);
}
static void g_frame(void)                /* the box's rule above and below, a title */
{
    cv_rect(GX0, GY0, GX1 - GX0, 1, TC(LINE));
    cv_rect(GX0, GY1, GX1 - GX0, 1, TC(LINE));
}
static int32_t g_y(float v)              /* 0..1 up the box */
{
    return GY1 - (int32_t)(fm_clampf(v, -0.05f, 1.05f) * (float)(GY1 - GY0));
}

/* the ADSR's shape: segment widths (the square roots of their times, as they feel), the level at x */
typedef struct { float a, d, h, r, sus; } env_shape_t;
static float env_ms(int32_t p)
{
    float x = (float)clamp(p, 0, 127) / 127.0f;
    return 1.0f + 4000.0f * x * x * x;
}
static env_shape_t env_shape(uint32_t ch)
{
    env_shape_t e;
    float a = fm_sqrtf(env_ms(synth_param(ch, SP_ATK))), d = fm_sqrtf(env_ms(synth_param(ch, SP_DEC)));
    float r = fm_sqrtf(env_ms(synth_param(ch, SP_REL))), h = (a + d + r) * 0.3f + 2.0f, w = a + d + h + r;
    float px = (float)(GX1 - GX0);
    e.a = a / w * px;
    e.d = d / w * px;
    e.h = h / w * px;
    e.r = r / w * px;
    e.sus = (float)clamp(synth_param(ch, SP_SUS), 0, 127) / 127.0f;
    return e;
}
static float env_at(const env_shape_t *e, float x)   /* x pixels from the box's left */
{
    if (x < e->a)
        return x / fm_maxf(e->a, 1.0f);
    x -= e->a;
    if (x < e->d)
        return e->sus + (1.0f - e->sus) * fm_exp2f(-6.6f * x / fm_maxf(e->d, 1.0f));
    x -= e->d;
    if (x < e->h)
        return e->sus;
    x -= e->h;
    return e->sus * fm_exp2f(-6.6f * fm_minf(x, e->r) / fm_maxf(e->r, 1.0f));
}
static float env_x(const env_shape_t *e, uint32_t stage, float lvl)   /* where the voice is on the shape */
{
    if (stage == 1u)
        return lvl * e->a;
    if (stage == 2u) {
        float top = 1.0f - e->sus, over = lvl - e->sus;
        if (top > 0.01f && over > 0.01f * top)
            return e->a + e->d * fm_log2f(over / top) / -6.6f;
        return e->a + e->d + e->h * 0.5f;
    }
    if (e->sus > 0.001f && lvl < e->sus)
        return e->a + e->d + e->h + e->r * fm_log2f(fm_maxf(lvl, 0.0001f) / e->sus) / -6.6f;
    return e->a + e->d + e->h;
}

static void graph_env(uint32_t ch)
{
    env_shape_t e = env_shape(ch);
    int32_t x, py = g_y(env_at(&e, 0));
    float b[3] = {e.a, e.a + e.d, e.a + e.d + e.h};
    static const char *const L[4] = {"A", "D", "S", "R"};
    uint32_t i, stage;
    int32_t lvl;
    g_frame();
    for (x = 0; x <= GX1 - GX0; x++) {   /* the area under the curve, then the curve */
        int32_t y = g_y(env_at(&e, (float)x));
        cv_rect(GX0 + x, y, 1, GY1 - y, th.ch_fill[ch]);
    }
    for (i = 0; i < 3u; i++)
        cv_rect(GX0 + (int32_t)b[i], GY0 + 1, 1, GY1 - GY0 - 1, TC(LINE));
    for (x = 1; x <= GX1 - GX0; x++) {
        int32_t y = g_y(env_at(&e, (float)x));
        g_line(GX0 + x - 1, py, GX0 + x, y, g_col);
        py = y;
    }
    for (i = 0; i < 4u; i++) {
        float x0 = i ? b[i - 1] : 0.0f, x1 = i < 3u ? b[i] : (float)(GX1 - GX0);
        g_text(GX0 + (int32_t)((x0 + x1) * 0.5f) - 2, GY1 + 14, L[i], TC(SUB));
    }
    lvl = synth_env_now(ch, &stage);
    if (stage)
        g_dot(GX0 + (int32_t)env_x(&e, stage, (float)lvl / 32768.0f), g_y((float)lvl / 32768.0f), TC(FG));
}

static void graph_edest(uint32_t ch)
{
    env_shape_t e = env_shape(ch);
    static const uint8_t ID[3] = {SP_FENV, SP_PENV, SP_SENV};
    static const char *const L[3] = {"FLT", "PIT", "SHP"};
    uint16_t col[3];
    int32_t mid = (GY0 + GY1) / 2, x, k;
    col[0] = g_col;
    col[1] = TC(FG);
    col[2] = TC(SUB);
    g_frame();
    cv_rect(GX0, mid, GX1 - GX0, 1, TC(LINE));
    for (k = 2; k >= 0; k--) {
        float amt = (float)synth_param(ch, ID[k]) / 64.0f;
        int32_t py = mid - (int32_t)(env_at(&e, 0) * amt * (float)(mid - GY0));
        for (x = 1; x <= GX1 - GX0; x++) {
            int32_t y = mid - (int32_t)(env_at(&e, (float)x) * amt * (float)(mid - GY0));
            (k ? g_thin : g_line)(GX0 + x - 1, py, GX0 + x, y, col[k]);
            py = y;
        }
    }
    for (k = 0; k < 3; k++) {
        cv_rect(GX0 + 60 * k, GY1 + 9, 8, 2, col[k]);
        g_text(GX0 + 60 * k + 12, GY1 + 14, L[k], TC(SUB));
    }
}

static float lfo_wave(uint32_t w, float ph)
{
    ph -= (float)(int32_t)ph;
    switch (w) {
    case 1: return ph < 0.5f ? ph * 4.0f - 1.0f : 3.0f - ph * 4.0f;
    case 2: return ph * 2.0f - 1.0f;
    case 3: return ph < 0.5f ? 1.0f : -1.0f;
    case 4: return 0.0f;
    default: return fm_sin_turns(ph);
    }
}
static void graph_lfo(uint32_t ch)
{
    static const float SYNC_Q[11] = {0, 16, 8, 4, 2, 1, 0.5f, 1.0f / 3.0f, 0.25f, 1.0f / 6.0f, 0.125f};
    const sound_t *s = synth_sound(ch);
    float hz, fade_s, ph0 = (float)s->lfo_phase / 128.0f, depth, now, w = (float)(GX1 - GX0);
    int32_t x, py = 0, mid = (GY0 + GY1) / 2, half = (GY1 - GY0) / 2;
    uint32_t wave = s->lfo_wave;
    char t[16];
    if (s->lfo_sync && s->lfo_sync <= 10u)
        hz = (float)seq_bpm10() / 600.0f / SYNC_Q[s->lfo_sync];
    else
        hz = 0.05f * fm_exp2f((float)s->lfo_rate / 14.0f);
    fade_s = (1.0f + 4000.0f * fm_powf((float)s->lfo_fade / 127.0f, 3.0f)) / 1000.0f;
    g_frame();
    cv_rect(GX0, s->lfo_pol ? GY1 : mid, GX1 - GX0, 1, TC(LINE));
    for (x = 0; x <= GX1 - GX0; x++) {
        float u = (float)x / w, d = s->lfo_fade ? fm_minf(1.0f, u / hz / fade_s) : 1.0f, v;
        int32_t y;
        if (wave == 4u) {                /* S&H: a level a cycle (eight shown, as it might fall) */
            uint32_t q = (uint32_t)(u * 8.0f) * 2654435761u + 77u;
            v = (float)(int32_t)((q >> 16 & 0xFFFu) - 2048) / 2048.0f;
        } else
            v = lfo_wave(wave, ph0 + u);
        if (s->lfo_pol)
            v = (v + 1.0f) * 0.5f;
        v *= d;
        y = s->lfo_pol ? GY1 - (int32_t)(v * (float)(GY1 - GY0)) : mid - (int32_t)(v * (float)half);
        if (s->lfo_fade && d < 1.0f) {   /* the fade's ramp */
            cv_pset(GX0 + x, s->lfo_pol ? GY1 - (int32_t)(d * (float)(GY1 - GY0)) : mid - (int32_t)(d * (float)half),
                    TC(DIM));
            if (!s->lfo_pol)
                cv_pset(GX0 + x, mid + (int32_t)(d * (float)half), TC(DIM));
        }
        if (x)
            g_line(GX0 + x - 1, py, GX0 + x, y, g_col);
        py = y;
    }
    now = synth_lfo_now(ch, &depth) - ph0;
    now -= (float)(int32_t)now;
    if (now < 0.0f)
        now += 1.0f;
    if (s->lfo_pit || s->lfo_flt || s->lfo_shp || s->lfo_amp) {
        float v = wave == 4u ? 0.0f : lfo_wave(wave, ph0 + now);
        if (s->lfo_pol)
            v = (v + 1.0f) * 0.5f;
        v *= depth;
        g_dot(GX0 + (int32_t)(now * w), s->lfo_pol ? GY1 - (int32_t)(v * (float)(GY1 - GY0)) : mid - (int32_t)(v * (float)half),
              TC(FG));
    } else
        g_text(GX0, GY1 + 14, "NO DESTINATION", TC(DIM));
    if (s->lfo_sync && s->lfo_sync <= 10u) {
        copy_text(t, "SYNC ", 16);
        synth_param_text(ch, SP_LFO_SYNC, s->lfo_sync, t + 5);
    } else {
        int32_t c = (int32_t)(hz * 100.0f + 0.5f);
        char d[4] = {'.', (char)('0' + c / 10 % 10), (char)('0' + c % 10), 0};
        int_text(c / 100, t);
        text_cat(t, d);
        text_cat(t, " HZ");
    }
    g_text(GX1 - text_w(&FONT_T, t), GY1 + 14, t, TC(SUB));
}

/* a two-pole state-variable filter's level in dB at f: type 1 LP, 2 HP, 3 BP (the channel's numbering); g Q12, k damping */
static float svf_db(uint32_t type, float f, int32_t g, float k)
{
    float w = fm_tanf(3.14159265f * fm_minf(f, G_FS * 0.49f) / G_FS) / ((float)g / 4096.0f), w2 = w * w;
    float den = (1.0f - w2) * (1.0f - w2) + k * k * w2, num = type == 2u ? w2 * w2 : type == 3u ? k * k * w2 : 1.0f;
    return 10.0f * fm_log2f(fm_maxf(num / den, 1e-9f)) * 0.30103f;
}
static void graph_curve(uint32_t type, int32_t cut, float k, uint16_t c, int thick)
{
    int32_t x, py = 0, g = synth_svf_g((uint32_t)clamp(cut, 0, 127));
    for (x = 0; x <= GX1 - GX0; x++) {   /* 20 Hz .. 20 kHz; +18 .. -42 dB */
        float f = 20.0f * fm_exp2f((float)x / (float)(GX1 - GX0) * 9.9658f), db = svf_db(type, f, g, k);
        int32_t y = g_y((db + 42.0f) / 60.0f);
        if (x)
            (thick ? g_line : g_thin)(GX0 + x - 1, py, GX0 + x, y, c);
        py = y;
    }
}
static void graph_freq_grid(void)
{
    static const float F[3] = {100.0f, 1000.0f, 10000.0f};
    static const char *const L[3] = {"100", "1K", "10K"};
    int32_t i, y0 = g_y(42.0f / 60.0f);
    g_frame();
    cv_rect(GX0, y0, GX1 - GX0, 1, TC(LINE));
    for (i = 0; i < 3; i++) {
        int32_t x = GX0 + (int32_t)(fm_log2f(F[i] / 20.0f) / 9.9658f * (float)(GX1 - GX0));
        cv_rect(x, GY0 + 1, 1, GY1 - GY0 - 1, TC(LINE));
        g_text(x - text_w(&FONT_T, L[i]) / 2, GY1 + 14, L[i], TC(SUB));
    }
    g_text(GX0, GY1 + 14, "HZ", TC(DIM));
    g_text(GX1 - text_w(&FONT_T, "0 DB"), y0 - 3, "0 DB", TC(DIM));
}
static void graph_filter(uint32_t ch)
{
    const sound_t *s = synth_sound(ch);
    graph_freq_grid();
    if (s->tfilt) {                      /* FILT, the one knob: the low-pass left, the high-pass right */
        int32_t t = s->tfilt;
        graph_curve(t < 0 ? 1u : 2u, t < 0 ? 127 + 2 * t : 2 * t, (float)(8192 - (int32_t)prefs.reso * 700) / 4096.0f,
                    s->flt ? TC(SUB) : g_col, !s->flt);
    }
    if (s->flt)
        graph_curve(s->flt, s->cut, (float)(8192 - s->res * 7600 / 127) / 4096.0f, g_col, 1);
    if (!s->flt && !s->tfilt)
        graph_curve(1u, 127, 2.0f, TC(DIM), 0);
}
static void graph_afilter(uint32_t ch)   /* the engine's own filter (ANALOG): TYPE LP BP HP, CUT E4, RES E5 */
{
    static const uint8_t T[3] = {1, 3, 2};
    int32_t res = synth_param(ch, SP_E5);
    int32_t cut = synth_param(ch, SP_E4), mod = synth_voices(ch) ? synth_cut_mod(ch) >> 8 : 0;
    graph_freq_grid();
    if (mod)                             /* moved by the modulation now (ENV, the LFO, the slots): where it is, live */
        graph_curve(T[clamp(synth_param(ch, SP_FTYPE), 0, 2)], cut, (float)(8192 - res * 7600 / 127) / 4096.0f,
                    TC(DIM), 0);
    graph_curve(T[clamp(synth_param(ch, SP_FTYPE), 0, 2)], clamp(cut + mod, 0, 127),
                (float)(8192 - res * 7600 / 127) / 4096.0f, g_col, 1);
}

static void graph_scope(void)
{
    int16_t b[GX1 - GX0 + 1];
    int32_t x, py = 0, mid = (GY0 + GY1) / 2, half = (GY1 - GY0) / 2;
    uint32_t n = synth_scope(b, (uint32_t)(GX1 - GX0 + 1));
    int32_t pk = 3000;                   /* (scaled to fill the box: a quiet sound still shows its shape) */
    for (x = 0; x < (int32_t)n; x++)
        pk = b[x] > pk ? b[x] : -b[x] > pk ? -b[x] : pk;
    g_frame();
    cv_rect(GX0, mid, GX1 - GX0, 1, TC(LINE));
    for (x = 0; x < (int32_t)n; x++) {
        int32_t y = mid - (int32_t)b[x] * half * 9 / (pk * 10);
        y = clamp(y, GY0, GY1);
        if (x)
            g_line(GX0 + x - 1, py, GX0 + x, y, g_col);
        py = y;
    }
    g_text(GX0, GY1 + 14, "SCOPE", TC(SUB));
    g_text(GX1 - text_w(&FONT_T, "10 MS"), GY1 + 14, "10 MS", TC(DIM));
}

static void graph_delay(uint32_t ch)
{
    float fb, beats = synth_dly_beats(&fb), span = fm_maxf(4.0f, beats * 6.0f), a = 1.0f;
    float lvl = (float)synth_fx(FX_DLVL) / 100.0f;
    int32_t i, nb = (int32_t)span;
    char t[8];
    (void)ch;
    g_frame();
    for (i = 1; i <= nb; i++) {          /* the beats */
        int32_t x = GX0 + (int32_t)((float)i / span * (float)(GX1 - GX0));
        if (nb <= 8 || i % 2 == 0) {
            cv_rect(x, GY0 + 1, 1, GY1 - GY0 - 1, TC(LINE));
            int_text(i, t);
            g_text(x - text_w(&FONT_T, t) / 2, GY1 + 14, t, TC(SUB));
        }
    }
    cv_rect(GX0, g_y(1.0f), 3, GY1 - g_y(1.0f), TC(FG));   /* the note */
    for (i = 1; i < 64 && a > 0.02f; i++) {   /* the echoes, each fb of the last */
        int32_t x = GX0 + (int32_t)((float)i * beats / span * (float)(GX1 - GX0)), y = g_y(a * fm_minf(lvl, 1.0f));
        if (x > GX1)
            break;
        cv_rect(x - 1, y, 3, GY1 - y, g_col);
        a *= fb;
    }
}

static void graph_slicer(uint32_t ch)
{
    const sound_t *s = synth_sound(ch);
    int32_t step, i;
    uint32_t pat = synth_slicer(ch, &step);
    float depth = (float)s->slc_depth / 127.0f;
    char t[4];
    g_frame();
    for (i = 0; i < 16; i++) {
        int32_t x0 = GX0 + i * (GX1 - GX0) / 16 + 1, x1 = GX0 + (i + 1) * (GX1 - GX0) / 16 - 1, on = (int32_t)(pat >> i & 1u);
        uint16_t c = i == step ? TC(FG) : g_col;
        if (!s->slc)
            c = TC(DIM);
        if (on && s->slc == 2u) {        /* STUT: each step struck again, dying */
            int32_t x;
            for (x = x0; x < x1; x++) {
                int32_t y = g_y(fm_expf(-(float)(x - x0) / (float)(x1 - x0) * 6.0f));
                cv_rect(x, y, 1, GY1 - y, c);
            }
        } else {
            int32_t y = g_y(on ? 1.0f : 1.0f - depth);
            rrect(x0, y, x1 - x0, GY1 - y, 1, on ? c : i == step ? TC(SUB) : TC(PANEL));
        }
        if (i % 4 == 0) {
            int_text(i + 1, t);
            g_text(x0, GY1 + 14, t, TC(SUB));
        }
    }
}

static void graph_arp(uint32_t ch)
{
    const sound_t *s = synth_sound(ch);
    uint8_t n[16];
    int32_t i, lo = 127, hi = 0, px = 0, py = 0, oct;
    char t[8];
    seq_arp_preview(ch, n, 16);
    for (i = 0; i < 16; i++) {
        lo = n[i] < lo ? n[i] : lo;
        hi = n[i] > hi ? n[i] : hi;
    }
    if (hi - lo < 12)
        hi = lo + 12;
    g_frame();
    for (oct = lo - lo % 12; oct <= hi; oct += 12)   /* the octaves' Cs */
        if (oct >= lo) {
            int32_t y = GY1 - 8 - (oct - lo) * (GY1 - GY0 - 16) / (hi - lo);
            cv_rect(GX0, y, GX1 - GX0, 1, TC(LINE));
            note_text(oct, t);
            g_text(GX1 - text_w(&FONT_T, t), y - 2, t, TC(DIM));
        }
    for (i = 0; i < 16; i++) {
        int32_t x0 = GX0 + i * (GX1 - GX0) / 16, x1 = GX0 + (i + 1) * (GX1 - GX0) / 16 - 2;
        int32_t y = GY1 - 8 - (n[i] - lo) * (GY1 - GY0 - 16) / (hi - lo);
        if (i)
            g_thin(px, py, x0 + (x1 - x0) / 2, y, TC(DIM));
        rrect(x0 + 1, y - 3, x1 - x0, 7, 2, s->arp ? g_col : TC(SUB));
        px = x0 + (x1 - x0) / 2;
        py = y;
    }
    note_text(lo, t);
    g_text(GX0, GY1 + 14, s->arp ? t : "ARP OFF", s->arp ? TC(SUB) : TC(DIM));
}

static void graph_scale(uint32_t ch)
{
    static const uint8_t WN[7] = {0, 2, 4, 5, 7, 9, 11}, BN[5] = {1, 3, 6, 8, 10}, BX[5] = {1, 2, 4, 5, 6};
    int32_t o, i, kw = (GX1 - GX0) / 14, y0 = GY0 + 10, h = GY1 - GY0 - 20;
    char t[16];
    for (o = 0; o < 2; o++)              /* two octaves from C: the white keys, then the black over them */
        for (i = 0; i < 7; i++) {
            int32_t note = 60 + 12 * o + WN[i], x = GX0 + (7 * o + i) * kw;
            uint16_t c = note % 12 == song.root ? TC(FG) : in_scale(note) ? g_col : TC(PANEL);
            rrect(x + 1, y0, kw - 2, h, 2, c);
        }
    for (o = 0; o < 2; o++)
        for (i = 0; i < 5; i++) {
            int32_t note = 60 + 12 * o + BN[i], x = GX0 + (7 * o + BX[i]) * kw - kw / 3;
            uint16_t c = note % 12 == song.root ? TC(FG) : in_scale(note) ? th.ch_line[ch] : TC(BG);
            rrect(x - 1, y0 - 1, 2 * kw / 3 + 2, h * 6 / 10 + 2, 2, TC(BG));
            rrect(x, y0, 2 * kw / 3, h * 6 / 10, 2, c == TC(BG) ? TC(LINE) : c);
        }
    t[0] = 0;
    text_cat(t, NOTE_NAME[song.root % 12]);
    text_cat(t, " ");
    {
        uint32_t l = 0;
        while (t[l])
            l++;
        kn_value(KN_SCALE, t + l);
    }
    g_text(GX0, GY1 + 14, t, TC(SUB));
}

static float ins_curve(const sound_t *s, float x)   /* the INSERT's drive, as channel_post shapes a level */
{
    float a = (float)s->ins_a / 127.0f, c = (float)s->ins_c / 127.0f, mix = (float)s->ins_mix / 127.0f, y = x;
    switch (s->ins) {
    case 1: y = fm_tanhf(x * (1.0f + a * 10.0f)); break;
    case 2: y = fm_clampf(x * (1.0f + a * 10.0f), -0.8f, 0.8f); break;
    case 3: { float v = x * (1.0f + a * 6.0f); while (v > 1.0f || v < -1.0f) v = v > 1.0f ? 2.0f - v : -2.0f - v; y = v; break; }
    case 4: y = fm_tanhf(x * (1.0f + a * 20.0f) + 0.3f * a) - 0.3f * a; break;
    case 5: { float q = fm_exp2f(1.0f + (1.0f - a) * 6.0f); y = (float)(int32_t)(x * q) / q; break; }
    default: break;
    }
    if (s->ins <= 4u && s->ins)
        y *= 0.3f + c;
    return x * (1.0f - mix) + y * mix;
}
static void graph_insert(uint32_t ch)
{
    const sound_t *s = synth_sound(ch);
    int32_t side = GY1 - GY0, x0 = 120 - side / 2, cx = 120, cy = (GY0 + GY1) / 2, x, py = 0;
    char t[12];
    cv_rect(x0, cy, side, 1, TC(LINE));  /* the axes: in across, out up */
    cv_rect(cx, GY0, 1, side, TC(LINE));
    g_thin(x0, GY1, x0 + side, GY0, TC(PANEL));
    for (x = 0; x <= side; x++) {
        float in = (float)x / (float)side * 2.0f - 1.0f;
        int32_t y = cy - (int32_t)(fm_clampf(ins_curve(s, in), -1.0f, 1.0f) * (float)side * 0.5f);
        if (x)
            g_line(x0 + x - 1, py, x0 + x, y, s->ins && s->ins_mix ? g_col : TC(DIM));
        py = y;
    }
    synth_param_text(ch, SP_INS, s->ins, t);
    g_text(GX0, GY0 + 6, t, TC(SUB));
    g_text(GX0, GY1 + 14, "IN", TC(DIM));
    g_text(GX1 - text_w(&FONT_T, "OUT"), GY0 + 6, "OUT", TC(DIM));
    if (s->ins >= 6u)
        g_text(GX1 - text_w(&FONT_T, "MOVES IN TIME"), GY1 + 14, "MOVES IN TIME", TC(DIM));
}

static void draw_graph(uint32_t band, uint32_t kind)   /* band 0..1: 84 rows each */
{
    uint32_t ch = set.channel;
    g_col = th.ch[ch];
    cv_begin(240, 84, TC(BG));
    cv_oy = -84 * (int32_t)band;         /* (draw over the three bands' 168 rows, keep this band's) */
    cv_y0 = 0;
    cv_y1 = 84;
    g_text(GX0, GY0 - 6, GR_NAME[kind], TC(SUB));
    switch (kind) {
    case GR_ENV: graph_env(ch); break;
    case GR_EDEST: graph_edest(ch); break;
    case GR_LFO: graph_lfo(ch); break;
    case GR_FILTER: graph_filter(ch); break;
    case GR_AFILTER: graph_afilter(ch); break;
    case GR_SCOPE: graph_scope(); break;
    case GR_DLY: graph_delay(ch); break;
    case GR_SLICER: graph_slicer(ch); break;
    case GR_ARP: graph_arp(ch); break;
    case GR_SEL: graph_scale(ch); break;
    case GR_INSERT: graph_insert(ch); break;
    default: break;
    }
    cv_oy = 0;
    cv_blit(0, 26 + 84 * band);
}
static uint32_t graph_sig(uint32_t kind, uint32_t now)   /* what the graph shows: its knobs and what moves */
{
    uint32_t h = sig_add(2166136261u, kind << 8 | set.channel), i, stage;
    const sound_t *s = synth_sound(set.channel);
    h = sig_add(h, (uint32_t)prefs.display);
    for (i = 0; i < sizeof *s; i++)
        h = sig_add(h, ((const uint8_t *)s)[i]);
    h = sig_add(h, (uint32_t)synth_fx(FX_DTIME) << 16 | (uint32_t)synth_fx(FX_DFB) << 8 | (uint32_t)synth_fx(FX_DLVL));
    h = sig_add(h, (uint32_t)song.root << 8 | song.scale);
    h = sig_add(h, seq_bpm10() << 4 | prefs.reso);
    switch (kind) {
    case GR_ENV: h = sig_add(h, (uint32_t)(synth_env_now(set.channel, &stage) >> 9) << 2 | stage); break;
    case GR_LFO: { float d; h = sig_add(h, (uint32_t)(synth_lfo_now(set.channel, &d) * 108.0f) << 4 | (uint32_t)(d * 15.0f)); break; }
    case GR_SCOPE: h = sig_add(h, now / 40u); break;   /* (25 a second) */
    case GR_AFILTER: h = sig_add(h, (uint32_t)(synth_voices(set.channel) ? synth_cut_mod(set.channel) >> 8 : 0)); break;
    case GR_SLICER: { int32_t st; synth_slicer(set.channel, &st); h = sig_add(h, (uint32_t)(st + 1)); break; }
    case GR_ARP: { uint8_t n[16]; seq_arp_preview(set.channel, n, 16); for (i = 0; i < 16u; i++) h = sig_add(h, n[i]); break; }
    default: break;
    }
    return h;
}

static void button_down(uint32_t k, uint32_t now)
{
    uint32_t b;
    ui.down_t[k] = now ? now : 1u;   /* (never now | 1: one ms in the future, a hold seen at once) */
    ui.used[k] = 0;
    for (b = 0; b < NB; b++)                 /* another button held meanwhile: it was used */
        if (b != k && (ui.btn >> b & 1u))
            ui.used[b] = 1;
    switch (k) {
    case B_PLAY:
        if (ui.fr_arm && ui.page == PG_M_SYSTEM2 && now - ui.fr_arm < 10000u) {   /* FACTORY RESET armed: held 3 s */
            ui.fr_hold = now | 1u;
            return;
        }
        if (seq_playing() && seq_rec() != REC_COUNT)
            seq_stop();
        else
            seq_play();
        return;
    case B_REC:
        if (shift_held()) {                  /* SHIFT + REC: OVERDUB <-> REPLACE, at once, recording or not */
            seq_cfg.rec_replace = (uint8_t)!seq_cfg.rec_replace;
            seq_replace_changed();
            app_mark();
            app_say("REC", seq_cfg.rec_replace ? "REPLACE" : "OVERDUB");
            ui.oct_used = 1;
            ui.used[k] = 1;
            return;
        }
        if (ui.layer == B_EDIT) {            /* EDIT + REC: the channel's pattern cleared at once (EDIT + OCT-: back) */
            char t[8];
            seq_clear(set.channel);
            ui.used[k] = 1;
            ui.layer_used = 1;
            channel_text(set.channel, t);
            app_say(t, "CLEARED");
            return;
        }
        return;                              /* (on release; held, the channel cleared) */
    case B_OCTDN:
    case B_OCTUP: {
        uint32_t other = k == B_OCTDN ? B_OCTUP : B_OCTDN;
        ui.oct_used = 0;
        if (ui.btn >> other & 1u) {          /* both: octave 0 */
            octave_step(0);
            ui.oct_used = 1;
            ui.used[other] = 1;
        } else if (ui.layer == B_EDIT) {     /* EDIT + OCT: undo, redo */
            int r = k == B_OCTDN ? seq_undo() : seq_redo();
            app_say(k == B_OCTDN ? "UNDO" : "REDO", r ? "" : "NOTHING");
            ui.oct_used = 1;
        } else if (ui.layer == B_FX) {       /* FX + OCT-: the reverb throw; OCT+: the echo throw, while held */
            synth_throw(k == B_OCTDN || (ui.btn >> B_OCTDN & 1u), k == B_OCTUP || (ui.btn >> B_OCTUP & 1u));
            ui.oct_used = 1;
        } else if (ui.layer == B_SEQ) {      /* SEQ + OCT: the step page; a step held: OCT- clears its extras */
            if (ui.held_step >= 0) {
                if (k == B_OCTDN) {
                    seq_step_unlock(set.channel, (uint32_t)ui.held_step);
                    app_say("STEP", "PLAIN");
                    ui.step_turned = 1;
                } else {                 /* OCT+: its fill condition, round */
                    static const char *const FL[3] = {"ALWAYS", "FILL ONLY", "NO FILL"};
                    seq_step_flag_turn(set.channel, (uint32_t)ui.held_step);
                    app_say("STEP", FL[seq_step_flag(set.channel, (uint32_t)ui.held_step) % 3u]);
                    ui.step_turned = 1;
                }
            } else {
                uint32_t np = (seq_steps(seq_pat(set.channel)) + 15u) / 16u;
                ui.step_page = (uint8_t)clamp(ui.step_page + (k == B_OCTUP ? 1 : -1), 0, (int32_t)np - 1);
                {
                    char t[8];
                    int_text(ui.step_page + 1, t);
                    app_say("STEPS PAGE", t);
                }
            }
            ui.oct_used = 1;
        }
        return;
    }
    case B_HOME:
        if (shift_held()) {                  /* SHIFT + HOME: MAIN's channels as PLAY or as LOOP, and MAIN */
            ui.oct_used = 1;
            ui.used[k] = 1;
            ui.loop = (uint8_t)!ui.loop;
            ui.loop_auto = 0;
            ui.loop_user = (uint8_t)(seq_playing() || seq_rec() != REC_OFF);   /* (kept so till it stops) */
            go_home();
            app_say("VIEW", ui.loop ? "LOOP" : "PLAY");
            return;
        }
        return;                              /* (on release: lock a layer held, or home; held: the menu) */
    case B_SAVE:
        if (shift_held() || ui.page == PG_NAME) {   /* SHIFT + SAVE: the channel's name; SAVE again: kept */
            if (shift_held())
                ui.oct_used = 1;
            ui.used[k] = 1;
            if (ui.page == PG_NAME)
                name_close(1);
            else
                name_open();
            return;
        }
        break;
    case B_GLO:
        if (shift_held()) {                  /* SHIFT + GLO: the SETTINGS screen (as HOME held) */
            ui.oct_used = 1;
            ui.used[k] = 1;
            ui.group_btn = B_HOME;           /* (from its first group, SCREEN, open or not) */
            page_open(G_MENU);
            return;
        }
        break;
    default:
        break;
    }
    if (shift_held() && k < NB && group_has_graph(k) && !layer_on()) {   /* SHIFT + a page's button: its graph */
        uint32_t kind;
        ui.oct_used = 1;
        ui.used[k] = 1;
        if (ui.group_btn != k)
            page_open(k);
        for (kind = 0; kind < GMAX && graph_kind_of(ui.page) == GR_NONE; kind++)
            page_open(k);                /* (a page with none, as FX's first: on to one with a graph) */
        kind = graph_kind_of(ui.page);
        if (kind != GR_NONE)
            graph_toggle(kind, !graph_wanted(kind));
        else
            app_say("GRAPH", "NONE ON THIS PAGE");
        return;
    }
    if (ui.layer_lock && ui.layer == (int8_t)k) {   /* the kept layer's own button: it closes, nothing else */
        ui.layer = -1;
        ui.layer_lock = 0;
        ui.used[k] = 1;
        return;
    }
    if (ui.layer_lock && ui.layer != (int8_t)k) {   /* another button lets a kept layer go */
        ui.layer = -1;
        ui.layer_lock = 0;
    }
    if (has_layer(k)) {
        ui.layer = (int8_t)k;                /* its layer while held */
        ui.layer_used = 0;
        ui.save_keys = 0;
        ui.save_key = -1;
        ui.held_step = -1;
        if (k == B_SEQ) {                    /* the steps of the bar playing */
            int32_t pos = seq_pos(set.channel);
            const pat_t *p = seq_pat(set.channel);
            uint32_t np = (seq_steps(p) + 15u) / 16u;
            ui.step_page = (uint8_t)(pos >= 0 ? (uint32_t)pos / seq_step_ticks(p) / 16u
                                              : ui.step_page < np ? ui.step_page : 0u);   /* (stopped: the bar picked) */
        }
    } else {
        page_open(k);                        /* no layer: the pages at once */
    }
}

static void button_up(uint32_t k, uint32_t now)
{
    uint32_t held = now - ui.down_t[k], b;
    ui.down_t[k] = 0;
    switch (k) {
    case B_REC:
        if (held < REC_HOLD_MS && !ui.used[k]) {
            seq_rec_button(set.channel);
            if (seq_rec() == REC_ARMED)
                app_say("REC", "READY");
        }
        return;
    case B_OCTDN:
    case B_OCTUP:
        synth_throw((ui.btn >> B_OCTDN & 1u) && ui.layer == B_FX, (ui.btn >> B_OCTUP & 1u) && ui.layer == B_FX);
        if (!ui.oct_used && !ui.used[k])
            octave_step(k == B_OCTDN ? -1 : 1);
        return;
    case B_HOME:
        if (held >= HOME_HOLD_MS || ui.used[k])
            return;                          /* (the menu opened, or closed, while held; SHIFT + HOME done) */
        for (b = 0; b < NB; b++)             /* a layer held: kept open */
            if (has_layer(b) && (ui.btn >> b & 1u) && ui.layer == (int8_t)b) {
                ui.layer_lock = 1;
                ui.used[b] = 1;
                app_say("LAYER", "KEPT");
                return;
            }
        if (ui.page == PG_NAME && !layer_on()) {   /* NAME: left as it was */
            name_close(0);
            return;
        }
        if (ui.group_btn == G_MENU && !layer_on()) {   /* SETTINGS: the next group's first page, round */
            int8_t l[GMAX];
            int i, n = group_list(G_MENU, l), cur = page_index();
            const char *g = set_group(ui.page);
            for (i = 1; i < n; i++)
                if (set_group((uint32_t)l[(cur + i) % n]) != g) {
                    ui.page = (uint8_t)l[(cur + i) % n];
                    break;
                }
            if (i == n)
                ui.page = (uint8_t)l[0];
            return;
        }
        go_home();
        say_sound();
        return;
    default:
        break;
    }
    if (has_layer(k) && ui.layer == (int8_t)k && !ui.layer_lock) {
        ui.layer = -1;                       /* the layer goes with the button (a step still held: at its key's release) */
        seq_erase_hold(set.channel, -1);
        ui.copy_mode = 0;
    }
    if (has_layer(k) && !ui.used[k] && !ui.layer_used && held < TAP_MS) {   /* a tap: the pages */
        if (k == B_SAVE && ui.page == PG_SONG)
            do_save(seq_slot());             /* SAVE on the SONG page: the song saved (twice: AGAIN) */
        else if (k == B_SAVE && ui.group_btn != B_SAVE) {
            slot_pick = (uint8_t)seq_slot();  /* SAVE elsewhere: the SONG page */
            ui.page = PG_SONG;
            ui.group_btn = B_SAVE;
            ui.layer = -1;
            ui.layer_lock = 0;
                } else {
            page_open(k);                    /* (on its other pages: the next one) */
        }
    }
}

/* the buttons debounced: a button's new state counts once it has held DEBOUNCE_MS, on both edges, so a contact that
 * chatters makes one press and one release, never two */
#define DEBOUNCE_MS 15u
static uint32_t debounce(uint32_t raw)
{
    static uint32_t stable, last, since[NB];
    uint32_t k, now = plat_ms();
    for (k = 0; k < NB; k++) {
        uint32_t bit = 1u << k;
        if ((raw ^ last) & bit)
            since[k] = now;              /* (changed: the clock starts again) */
        else if (((raw ^ stable) & bit) && now - since[k] >= DEBOUNCE_MS)
            stable = (stable & ~bit) | (raw & bit);
    }
    last = raw;
    return stable;
}

static void buttons_input(uint32_t btn)
{
    uint32_t k, now = plat_ms();
    for (k = 0; k < NB; k++) {
        uint32_t bit = 1u << k;
        if ((btn & bit) && !(ui.btn & bit)) {
            ui.btn |= bit;                   /* (seen by the combinations at once) */
            button_down(k, now);
        } else if (!(btn & bit) && (ui.btn & bit)) {
            ui.btn &= ~bit;
            button_up(k, now);
        }
    }
    ui.btn = btn;
    if ((btn >> B_HOME & 1u) && ui.down_t[B_HOME] && !ui.used[B_HOME] && now - ui.down_t[B_HOME] >= HOME_HOLD_MS) {
        ui.used[B_HOME] = 1;                 /* HOME held: the menu (held again: out) */
        if (ui.group_btn == G_MENU)
            go_home();
        else
            page_open(G_MENU);
    }
    if ((btn >> B_REC & 1u) && ui.down_t[B_REC] && !ui.used[B_REC] && now - ui.down_t[B_REC] >= REC_HOLD_MS) {
        char t[8];
        ui.used[B_REC] = 1;                  /* REC held two seconds: the channel's pattern cleared */
        seq_clear(set.channel);
        channel_text(set.channel, t);
        app_say("CLEARED", t);
    }
}

static void encoders_input(void)
{
    uint32_t k, ch = set.channel;
    int32_t e;
    const uint16_t *kn = knobs_now();
    for (k = 0; k < NE; k++) {
        if ((e = plat_enc((int)k)) == 0)
            continue;
        ui.turns[k] += e;
        ui.input_t = plat_ms();
        if (layer_on()) {
            ui.layer_used = 1;
            ui.used[ui.layer] = 1;
            if (k == EN_SELECT && ui.layer == B_FX) {   /* FX + SELECT: the punch-ins' fade in and out */
                char t[8];
                kn_turn(KN_PFADE, e);
                int_text(prefs.pfade, t);
                app_say("PUNCH FADE", t);
            } else if (k == EN_SELECT && ui.layer == B_SEQ && ui.held_step < 0) {   /* SEQ + SELECT: the bar */
                uint32_t np = (seq_steps(seq_pat(ch)) + 15u) / 16u;
                char t[12];
                ui.step_page = (uint8_t)clamp(ui.step_page + (e > 0 ? 1 : -1), 0, (int32_t)np - 1);
                ui.dstep = (uint8_t)(ui.step_page * 16u + ui.dstep % 16u);   /* (DRUMS: the same bar) */
                int_text(ui.step_page + 1, t);
                text_cat(t, "/");
                {
                    uint32_t l = 0;
                    while (t[l])
                        l++;
                    int_text((int32_t)np, t + l);
                }
                app_say("BAR", t);
            } else if (k == EN_SELECT) {
                kn_turn(KN_BPM, e);
            } else if (ui.layer == B_SEQ && ui.held_step >= 0 && k == EN_ALGO) {   /* the lock's parameter */
                int32_t id = ui.lock_id;
                do
                    id = (id + (e > 0 ? 1 : -1) + SP_N) % SP_N;
                while (!SP_LOCKABLE((uint32_t)id));
                ui.lock_id = (uint8_t)id;
                app_say("LOCK", synth_param_label(ch, ui.lock_id));
            } else if (ui.layer == B_SEQ && ui.held_step >= 0 && k == EN_PRESET) {   /* its value */
                int32_t lv = seq_step_lock(ch, (uint32_t)ui.held_step, ui.lock_id), lo, hi;
                char t[8];
                synth_param_range(ch, ui.lock_id, &lo, &hi);
                lv = lv == SEQ_NOLOCK ? synth_param(ch, ui.lock_id) + (e > 0 ? 1 : -1) : lv + accel(e);
                lv = clamp(lv, lo, hi);
                seq_step_lock_set(ch, (uint32_t)ui.held_step, ui.lock_id, lv);
                ui.step_turned = 1;
                synth_param_text(ch, ui.lock_id, lv, t);
                app_say(synth_param_label(ch, ui.lock_id), t);
            } else if (k >= EN_K1 && kn) {
                kn_turn(kn[k - EN_K1], e);
            }
            continue;                        /* (ALGORITHM and PRESETS wait in a layer) */
        }
        if (ui.page == PG_NAME && k < EN_K1)   /* NAME: the channel and its sound stay, SELECT waits */
            continue;
        if (k == EN_SELECT) {
            if (ui.page != PG_HOME && group_pages() > 1)
                page_turn(e);
            else
                kn_turn(KN_BPM, e);
        } else if (k == EN_ALGO) {           /* the channel (a REPLACE pass stops: its channel keeps its last take) */
            if (seq_rec() == REC_ON && seq_cfg.rec_replace)
                seq_rec_off();
            set.channel = (uint8_t)(((int32_t)set.channel + e % NCH + NCH) % NCH);
            app_mark();
            say_sound();
        } else if (k == EN_PRESET) {         /* its sound: favourites, every engine's, the user presets */
            presets_step(ch, e);
            dirty();
            app_say(synth_engine_name(synth_sound(ch)->engine), synth_preset_name(ch));
            if (synth_is_kit(ch) && drum_lane_of(ui.entry[ch]) < 0)
                ui.entry[ch] = 36;
            else if (maps_keys(ch) && !synth_is_kit(ch) && (ui.entry[ch] < 36u || ui.entry[ch] > 51u))
                ui.entry[ch] = 36;
        } else if (kn) {
            char t[12];
            uint32_t id = kn[k - EN_K1];
            kn_turn(id, e);
            if (id != KN_LOAD && id != KN_SAVE && id != KN_NEW && id != KN_NONE && id != KN_BTGO && id != KN_BT && id != KN_ULOAD
                && id != KN_UERASE && id != KN_USAVE && id != KN_CLRSQ && id != KN_INIT) {
                kn_value(id, t);
                app_say(kn_label(id), t);
            }
        }
    }
}

static void input(void)
{
    uint32_t keys, raw = plat_buttons() | remote_btn, btn;   /* (REMOTE: the buttons held over MIDI) */
    boot_ignore &= raw;                  /* (a button held at power-on: nothing till it is let go) */
    btn = debounce(raw & ~boot_ignore);
    touch_poll();                            /* the sensed keys' presses, velocities and pressure */
    keys = touch_keys();
    if (keys != ui.keys || btn != ui.btn)
        ui.input_t = plat_ms();
    keys_input(keys, 0);
    buttons_input(btn);
    keys_input(keys, 1);
    encoders_input();
    ui.keys = keys;
}

void app_input_only(void) { input(); }

/* ------------------------------------------------------------ the lights --- */
static void lights(uint32_t now)
{
    uint32_t lit = 0, mid = 0, klit = ui.keys, kmid = 0, kdim = 0, i, blink = now / 250u & 1u, ch = set.channel;
    if (seq_playing())
        lit |= 1u << B_PLAY | LED_PLAY_GREEN;
    if (seq_rec() == REC_ON || (seq_rec() != REC_OFF && blink))
        lit |= 1u << B_REC;
    if (layer_shown())
        lit |= 1u << (uint32_t)ui.layer;
    else if (ui.page != PG_HOME)
        lit |= 1u << (ui.group_btn == G_MENU ? B_HOME : ui.group_btn);
    else
        lit |= 1u << B_HOME;                 /* (MAIN: HOME lit, as a button lights its own pages) */
    if (seq_dirty())
        mid |= 1u << B_SAVE;
    if (set.octave)
        mid |= 1u << (set.octave < 0 ? B_OCTDN : B_OCTUP);
    kdim = (1u << NKEYS) - 1u;               /* every key glows (BUTTON LIGHTS), whatever is on: to play in the dark */
    if (layer_shown()) {                     /* the layer's keys */
        if (ui.layer == B_EDIT) {            /* (the black keys' legends: OP1..OP6, PIT, GLO, MONO, POLY; a drum
                                              * kit's lanes; lit: a muted lane or operator, the voice mode) */
            uint32_t e = synth_sound(set.channel)->engine, m = e == 12u ? synth_lane_mutes(set.channel)
                       : e == 1u ? synth_fm6_mutes(set.channel) : 0u, vm = (uint32_t)synth_param(set.channel, SP_MONO);
            for (i = 0; i < (e == 12u ? 8u : 10u); i++)
                if ((m >> i & 1u) || (i == 8u && vm == 1u) || (i == 9u && vm == 0u) || (i == 7u && synth_param(set.channel, SP_GLIDE)))
                    klit |= 1u << BLACK_KEYS[i];
                else
                    kmid |= 1u << BLACK_KEYS[i];
        }
        if (ui.layer == B_GLO) {             /* (the solos: the black keys F#4..F#5) */
            for (i = 5; i < 11u; i++) {
                if (seq_solo() >> (i - 5u) & 1u)
                    klit |= 1u << BLACK_KEYS[i];
                else
                    kmid |= 1u << BLACK_KEYS[i];
            }
        }
        for (i = 0; i < 16u; i++) {
            uint32_t kb = 1u << WHITE_KEYS[i];
            kdim |= kb;
            if (ui.layer == B_GLO) {
                if (i >= 7u && i <= 12u && (seq_mute() >> (i - 7u) & 1u))
                    klit |= kb;
                else if ((i >= 4u && i <= 12u) || i == 15u)
                    kmid |= kb;
            } else if (ui.layer == B_SEQ) {
                uint32_t s = ui.step_page * 16u + i;
                int32_t pos = seq_pos(ch);
                const pat_t *p = seq_pat(ch);
                if (s >= seq_steps(p))
                    continue;
                if (pos >= 0 && (uint32_t)pos / seq_step_ticks(p) == s)
                    klit |= kb;
                else if (seq_step_find(ch, s) >= 0)
                    kmid |= kb;
            } else if (ui.layer == B_SAVE) {
                if (i == seq_scene() || ((int32_t)i == seq_queued() && blink))
                    klit |= kb;
                else if (seq_scene_used(i))
                    kmid |= kb;
            } else if (ui.layer == B_SEL) {
                int32_t n = 53 + WHITE_KEYS[i];
                if (n % 12 == song.root)
                    klit |= kb;
                else if (in_scale(n))
                    kmid |= kb;
            }
        }
    } else if (prefs.scale_keys && !maps_keys(ch)) {   /* the scale on the keys: root lit, the scale brighter */
        for (i = 0; i < NKEYS; i++) {
            int32_t n = 53 + (int32_t)i;
            kdim |= 1u << i;
            if (song.scale == 0u && song.keys == 0u)
                continue;
            if ((n - song.root + 120) % 12 == 0)
                klit |= 1u << i;
            else if (in_scale(n))
                kmid |= 1u << i;
        }
    }
    ui.lit = lit;
    plat_lights(ui.btn | lit, klit, ~0u & ((1u << NB) - 1u) & ~(ui.btn | lit), prefs.lights ? kdim : 0u,
                prefs.lights);
    plat_lights_mid(mid & ~lit, kmid & ~klit);
}

/* ------------------------------------------------------------- the screen --- */
static uint8_t meter_px(int32_t peak)    /* a channel's peak as the meter's pixels, 0..101 */
{
    float f;
    if (peak <= 0)
        return 0;
    f = fm_sqrtf((float)peak / 24000.f) * 101.f;
    return (uint8_t)(f > 101.f ? 101 : f);
}

static uint32_t sel_bar(uint32_t ch)     /* the bar the row shows: the playhead's, or the SEQ layer's page */
{
    int32_t pos = seq_pos(ch);
    const pat_t *p = seq_pat(ch);
    if (ui.layer == B_SEQ && ch == set.channel)
        return ui.step_page * 16u * seq_step_ticks(p) / TBAR;
    return pos >= 0 ? (uint32_t)pos / TBAR : 0u;
}

/* the top bar, y 0..24: song and scene, the bars, the tempo, play and record, the battery */
static void draw_top(int bat, uint32_t now)
{
    int32_t i, y, nb;
    char t[12];
    const pat_t *p = seq_pat(set.channel);
    uint32_t bar = sel_bar(set.channel), blink = now / 250u & 1u, r = seq_rec();
    cv_begin(240, 25, TC(BG));
    rrect(4, 4, 25, 17, 3, TC(FG));
    t[0] = (char)('A' + seq_slot());
    int_text((int32_t)seq_scene() + 1, t + 1);
    cv_text(4 + (25 - text_w(t[2] ? &FONT_XS : &FONT_B, t)) / 2, at_mid(t[2] ? &FONT_XS : &FONT_B, 12),
            t[2] ? &FONT_XS : &FONT_B, t, TC(BG));
    nb = p->bars;
    for (i = 0; i < nb && seq_queued() < 0; i++) {   /* the pattern's bars: the one shown lit (a scene queued:
                                                  * its name there instead) */
        if (nb <= 4)
            cv_rect(34 + 9 * i, 9, 7, 6, (uint32_t)i == bar ? TC(FG) : TC(DIM));
        else
            cv_rect(34 + 4 * i, 9, 3, 6, (uint32_t)i == bar ? TC(FG) : TC(DIM));
    }
    if (seq_queued() >= 0) {                 /* a scene queued: it, blinking */
        t[0] = '>';
        t[1] = (char)('A' + seq_slot());
        int_text(seq_queued() + 1, t + 2);
        if (blink)
            cv_text(34, at_mid(&FONT_XS, 12), &FONT_XS, t, TC(REC));   /* (where the bars were: clear of the tempo) */
    } else if (seq_count()) {
        int_text(seq_count(), t);
        cv_text(72, at_mid(&FONT_XS, 12), &FONT_XS, t, TC(REC));
    } else {
        int_text((int32_t)bar + 1, t);
        text_cat(t, "/");
        {
            uint32_t l = 0;
            while (t[l])
                l++;
            int_text(nb, t + l);
        }
        cv_text(72, at_mid(&FONT_XS, 12), &FONT_XS, t, TC(SUB));
    }
    {
        uint32_t b = seq_bpm10();
        char d[4] = {'.', (char)('0' + b % 10u), 0, 0};
        int_text((int32_t)(b / 10u), t);
        text_cat(t, d);
    }
    y = at_base(&FONT_B, 12 + 6);
    cv_text(128 - text_w(&FONT_B, t), y, &FONT_B, t, TC(FG));
    cv_text(131, at_base(&FONT_T, 18), &FONT_T, song.mode ? "SNG" : "BPM", TC(SUB));
#ifdef NW_HD
    hd_tri(160.0f, 5.0f, 173.0f, 12.5f, 160.0f, 20.0f, seq_playing() ? TC(FG) : TC(DIM));
    hd_quiet++;
#endif
    for (i = 0; i < 15; i++) {               /* play: a triangle 13 x 15, lit while playing */
        int32_t w = (13 * (15 - 2 * (i < 7 ? 7 - i : i - 7)) + 7) / 15;
        cv_rect(160, 5 + i, w, 1, seq_playing() ? TC(FG) : TC(DIM));
    }
#ifdef NW_HD
    hd_quiet--;
#endif
    if (r == REC_ON || ((r == REC_COUNT || r == REC_TAKE) && blink))
        circle(2 * 184 + 1, 2 * 12 + 1, 5.5f, 0, TC(REC));
    else
        circle(2 * 184 + 1, 2 * 12 + 1, 5.5f, 1.2f, r == REC_ARMED ? TC(REC) : TC(DIM));
    if (seq_cfg.rec_replace) {           /* REPLACE: an R in the record dot (OVERDUB: none) */
        int filled = r == REC_ON || ((r == REC_COUNT || r == REC_TAKE) && blink);
        cv_text(184 - text_w(&FONT_T, "R") / 2 + 1, at_mid(&FONT_T, 12), &FONT_T, "R",
                filled ? TC(BG) : r == REC_OFF ? TC(SUB) : TC(REC));
    }
    draw_battery(bat);
    cv_rect(0, 24, 240, 1, TC(LINE));
    cv_blit(0, 0);
}

/* MAIN's channels, four at a time (the selected one among them, the list sliding to it): PLAY, a row of 36 px each
 * (the name big, the sound, a dot that flashes as it strikes, the level) over a scope of the selected channel; LOOP,
 * a row of 42 px each (the steps of the bar playing, the level, M and S). Both show the same four. */
#define PLAY_RH 36
#define LOOP_RH 42
static int32_t chans_off(int32_t rh) { return (int32_t)ui.pv_y * rh / (8 * LOOP_RH); }

static void loop_row(uint32_t i, int32_t y)
{
    int muted = !synth_audible(i), sel = i == set.channel, rec = seq_rec() != REC_OFF && seq_rec_ch() == i;
    uint16_t c = muted ? th.ch_mute[i] : th.ch[i];
    char n[2] = {(char)('1' + i), 0};
    uint32_t k, m = seq_mute() >> i & 1u, so = seq_solo() >> i & 1u, bar = sel_bar(i), cells = seq_cells(i, bar);
    int32_t pos = seq_pos(i), ph = pos >= 0 && (uint32_t)pos / TBAR == bar ? (int32_t)((uint32_t)pos % TBAR / TPS) : -1;
    if (sel)
        rrect_line(2, y + 2, 230, 38, 4, rec ? TC(REC) : th.ch_line[i], th.ch_fill[i]);
    else if (rec)
        rrect_line(2, y + 2, 230, 38, 4, TC(REC), TC(BG));
    rrect(5, y + 7, 5, 28, 2, c);
    cv_text(19 - text_w(&FONT_B, n) / 2, at_mid(&FONT_B, y + 21), &FONT_B, n, c);
    text_fit(28, y + 6, &FONT_B, chan_name(i), muted ? TC(SUB) : TC(FG), 62);   /* (its name, else its engine) */
    {                                    /* (a chord playing: its name in place of the sound's) */
        char cn[16];
        ch_chord(i, cn);
        text_fit(28, y + 22, &FONT_T, cn[0] ? cn : synth_preset_name(i), muted ? TC(DIM) : cn[0] ? TC(FG) : TC(SUB), 62);
    }
    for (k = 0; k < 16u; k++) {
        uint16_t cc = (int32_t)k == ph ? TC(FG) : cells >> k & 1u ? c : TC(PANEL);
        rrect(92 + 6 * (int32_t)k + 2 * (int32_t)(k / 4u), y + 9, 5, 13, 1, cc);
    }
    cv_rect(92, y + 27, 101, 5, TC(PANEL));
    cv_rect(92, y + 27, ui.lvl[i], 5, c);
    rrect(196, y + 12, 17, 17, 3, m ? TC(MUTE) : TC(PANEL));
    cv_text(196 + (17 - text_w(&FONT_XS, "M") + 1) / 2, at_mid(&FONT_XS, y + 20), &FONT_XS, "M", m ? TC(BG) : TC(SUB));
    rrect(214, y + 12, 17, 17, 3, so ? TC(FG) : TC(PANEL));
    cv_text(214 + (17 - text_w(&FONT_XS, "S") + 1) / 2, at_mid(&FONT_XS, y + 20), &FONT_XS, "S", so ? TC(BG) : TC(SUB));
}

#include "chords.c"                       /* chord_spell: the notes held named as a chord, in the song's key */
static void ch_chord(uint32_t ch, char *out)   /* LOOP: the chord a channel plays (three notes or more), else "" */
{
    synth_vview_t vv[SYN_VMAX];
    uint32_t n = synth_voice_view(ch, vv), i, nn = 0, pcs = 0, c = 0;
    int32_t held[SYN_VMAX];
    char det[8];
    out[0] = 0;
    if (maps_keys(ch))
        return;
    for (i = 0; i < n; i++)
        if (vv[i].gate) {
            held[nn++] = vv[i].note;
            pcs |= 1u << (vv[i].note % 12u);
        }
    for (i = 0; i < 12u; i++)
        c += pcs >> i & 1u;
    if (c >= 3u)
        chord_spell(held, nn, song.root % 12u, song.scale, out, det, 16);
}

static uint32_t chans_sig(uint32_t band)   /* what a band of LOOP's four channels shows */
{
    int32_t rh = LOOP_RH, bh = 2 * rh, off = chans_off(rh);
    uint32_t i, h = sig_add(2166136261u, set.channel << 16 | (uint16_t)ui.pv_y);
    h = sig_add(h, (uint32_t)(seq_rec() != REC_OFF) << 8 | seq_rec_ch());
    for (i = 0; i < NCH; i++) {
        int32_t y = rh * (int32_t)i - off, pos = seq_pos(i);
        const sound_t *s = synth_sound(i);
        if (y + rh <= bh * (int32_t)band || y >= bh * (int32_t)(band + 1u))
            continue;
        h = sig_add(h, i << 24 | (uint32_t)synth_audible(i) << 2 | (seq_mute() >> i & 1u) << 1 | (seq_solo() >> i & 1u));
        h = sig_str(sig_add(h, (uint32_t)s->engine << 8 | s->preset), chan_given(i));
        h = sig_add(h, ui.lvl[i]);
        h = sig_add(h, seq_cells(i, sel_bar(i)));
        h = sig_add(h, pos >= 0 ? (uint32_t)pos / TPS : 0xFFFFu);
        {
            char cn[16];
            ch_chord(i, cn);
            h = sig_str(h, cn);
        }
    }
    return h;
}

static void draw_chans(uint32_t band)    /* LOOP, band 0..1: two rows each */
{
    int32_t rh = LOOP_RH, bh = 2 * rh, off = chans_off(rh);
    uint32_t i;
    cv_begin(240, (uint32_t)bh, TC(BG));
    cv_oy = -bh * (int32_t)band;
    cv_y0 = 0;
    cv_y1 = bh;
    for (i = 0; i < NCH; i++) {
        int32_t y = rh * (int32_t)i - off;
        if (y + rh > bh * (int32_t)band && y < bh * (int32_t)(band + 1u))
            loop_row(i, y);
    }
    for (i = 0; i < NCH; i++) {          /* where the four are among the six: a dot each, the selected in its colour */
        int32_t y = bh - 3 * 9 + 9 * (int32_t)i + 2;
        uint16_t c = i == set.channel ? th.ch[i] : i >= ui.pv_top && i < ui.pv_top + 4u ? TC(SUB) : TC(PANEL);
        rrect(235, y, 4, 5, 1, c);
    }
    cv_oy = 0;
    cv_blit(0, 26 + (uint32_t)bh * band);
}

/* PLAY: a line naming the selected channel (and six dots, the channels, each flashing as it plays), then the channel
 * drawn alive (anim.c) with the notes it plays named under it, the chord, the key (or on a kit, the drums) */
#define PLAY_HEAD 24
#define PLAY_ANIM 144                    /* (two bands of 72) */
#define PULSE_MS 360u
#include "anim.c"

static void draw_play_head(uint32_t now)
{
    uint32_t ch = set.channel, i;
    uint16_t c = th.ch[ch];
    char n[2] = {(char)('1' + ch), 0};
    int32_t x;
    cv_begin(240, PLAY_HEAD, TC(BG));
    rrect(4, 4, 5, 16, 2, c);
    cv_text(14, at_mid(&FONT_B, 12), &FONT_B, n, c);
    x = text_fit(28, at_mid(&FONT_B, 12), &FONT_B, chan_name(ch), TC(FG), 80);
    text_fit(x + 8, at_mid(&FONT_T, 12), &FONT_T, synth_preset_name(ch), TC(SUB), 168 - x);
    for (i = 0; i < NCH; i++) {          /* the six: the selected ringed, each swelling as it strikes */
        uint32_t age = ui.pulse_t[i] ? now - ui.pulse_t[i] : PULSE_MS;
        float r = synth_audible(i) && age < PULSE_MS ? 2.5f + 2.0f * (float)(PULSE_MS - age) / (float)PULSE_MS : 2.5f;
        int32_t cx = 184 + 9 * (int32_t)i;
        circle(2 * cx, 2 * 12, r, 0.0f, synth_audible(i) ? th.ch[i] : th.ch_mute[i]);
        if (i == ch)
            circle(2 * cx, 2 * 12, 4.5f, 1.0f, TC(FG));
    }
    cv_rect(0, PLAY_HEAD - 1, 240, 1, TC(LINE));
    cv_blit(0, 26);
}

static uint32_t play_keys_state(uint32_t *lit, char *t1, char *t2);
static void play_notes(char *out);
static char play_det[8];                 /* the interval of two notes (chord_spell's detail), shown beside the name */
/* why the channel makes no sound, where nothing else on PLAY shows it: muted, another channel soloed, its level at 0,
 * the master filter closed, FM6's operators all muted; 0 none */
static const char *silent_why(uint32_t ch)
{
    int32_t f = synth_master(MST_FILT);
    if (seq_mute() >> ch & 1u)
        return "MUTED";
    if (seq_solo() && !(seq_solo() >> ch & 1u))
        return "SOLO ELSEWHERE";
    if (lvl_q(ch) == 0)
        return "LEVEL 0";
    if (f <= -56 || f >= 56)
        return "MASTER FILTER";
    if (synth_sound(ch)->engine == 1u && (synth_fm6_mutes(ch) & 0x3Fu) == 0x3Fu)
        return "OPS MUTED";
    if (!maps_keys(ch)) {                /* (C4 with the octave and TRANS: out of the speaker's reach) */
        int32_t c4 = 60 + 12 * set.octave + song.transpose;
        if (c4 < 30)
            return "TOO LOW: OCT, TRANS";
        if (c4 > 100)
            return "TOO HIGH: OCT, TRANS";
    }
    return 0;
}
static void draw_play_anim(uint32_t band, uint32_t now)
{
    cv_begin(240, PLAY_ANIM / 2, TC(BG));
    cv_oy = -(PLAY_ANIM / 2) * (int32_t)band;
    cv_y0 = 0;
    cv_y1 = PLAY_ANIM / 2;
    anim_draw(set.channel, now);
    {                                    /* under it: the notes sounding, and the chord and the key (a kit: its drums) */
        uint32_t lit;
        char t1[24], t2[24], nt[24];
        play_keys_state(&lit, t1, t2);
        play_notes(nt);
        {                                /* (one note: the chord's place names it already) */
            uint32_t q = 0, sp = 0;
            while (nt[q])
                sp |= nt[q++] == ' ';
            if (!sp)
                nt[0] = 0;
        }
        cv_rect(0, PLAY_ANIM - 32, 240, 1, TC(LINE));
        {                                /* the chord, large (two notes: their interval beside it, after a dot) */
            int32_t x = text_fit(8, PLAY_ANIM - 28, &FONT_M, t1[0] ? t1 : "-", t1[0] ? TC(FG) : TC(DIM), 140);
            if (play_det[0] && t1[0]) {
                cv_rect(x + 5, PLAY_ANIM - 15, 2, 2, TC(SUB));
                text_fit(x + 11, PLAY_ANIM - 20, &FONT_B, play_det, TC(SUB), 40);
            }
        }
        {                                /* the right: the key (silent: why, in red, instead), the notes under it */
            const char *why = silent_why(set.channel), *k = why ? why : t2;
            int32_t w = text_w(&FONT_T, k);
            cv_text(232 - (w < 150 ? w : 150), PLAY_ANIM - 28, &FONT_T, k, why ? TC(REC) : TC(SUB));
            w = text_w(&FONT_T, nt);
            cv_text(232 - (w < 90 ? w : 90), PLAY_ANIM - 15, &FONT_T, nt, TC(DIM));
        }
    }
    cv_oy = 0;
    cv_blit(0, 26 + PLAY_HEAD + (PLAY_ANIM / 2) * band);
}

static const char *const NOTE_NM[12] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};

static uint32_t play_keys_state(uint32_t *lit, char *t1, char *t2)   /* the keys sounding, the two texts */
{
    synth_vview_t vv[SYN_VMAX];
    uint32_t ch = set.channel, n = synth_voice_view(ch, vv), i, pcs = 0, k, drums = 0, nn = 0;
    int32_t base = 53 + 12 * set.octave, low = -1, held[SYN_VMAX];
    *lit = 0;
    for (i = 0; i < n; i++) {
        int32_t note = vv[i].note;
        if (!vv[i].amp && !vv[i].gate)
            continue;
        if (maps_keys(ch)) {             /* a kit: the keys by its own map, the drums named */
            for (k = 0; k < NKEYS; k++)
                if (synth_key_note(ch, k) == note)
                    *lit |= 1u << k;
            if (synth_is_kit(ch) && synth_drum_lane((uint32_t)note) >= 0)
                drums |= 1u << synth_drum_lane((uint32_t)note);
            continue;
        }
        if (note - base >= 0 && note - base < (int32_t)NKEYS)
            *lit |= 1u << (note - base);
        if (vv[i].gate) {
            pcs |= 1u << (note % 12);
            low = low < 0 || note < low ? note : low;
            if (nn < SYN_VMAX)
                held[nn++] = note;
        }
    }
    t1[0] = t2[0] = 0;
    play_det[0] = 0;
    if (synth_is_kit(ch)) {
        for (i = 0, k = 0; i < 8u; i++)   /* (the first drum hit on the first line, the next ones under it) */
            if (drums >> i & 1u) {
                char *t = k++ ? t2 : t1;
                if (t[0])
                    text_cat(t, " ");
                text_cat(t, synth_param_label(ch, SP_X0 + i));
            }
        return drums;
    }
    chord_spell(held, nn, song.root % 12u, song.scale, t1, play_det, 24);
    if (song.scale || song.keys) {
        char sc[12];
        copy_text(t2, chord_pc((int32_t)song.root, chord_key_spell((int32_t)song.root, song.scale)), 8);
        text_cat(t2, " ");
        kn_value(KN_SCALE, sc);
        text_cat(t2, sc);
    }
    return pcs;
}

static void play_notes(char *out)      /* the notes sounding, by name, lowest first (a kit: none, its drums say it) */
{
    synth_vview_t vv[SYN_VMAX];
    uint32_t n = synth_voice_view(set.channel, vv), i, j, c = 0;
    int32_t got[SYN_VMAX];
    out[0] = 0;
    if (maps_keys(set.channel))
        return;
    for (i = 0; i < n; i++)
        if (vv[i].gate)
            got[c++] = vv[i].note;
    for (i = 0; i < c; i++)              /* (lowest first) */
        for (j = i + 1; j < c; j++)
            if (got[j] < got[i]) {
                int32_t x = got[i];
                got[i] = got[j];
                got[j] = x;
            }
    for (i = 0; i < c && i < 4u; i++) {
        char b[6];
        const char *nm = chord_pc(got[i], chord_key_spell((int32_t)song.root, song.scale));   /* (in the key's names) */
        copy_text(b, nm, 4);
        int_text(got[i] / 12 - 1, b + (nm[1] ? 2 : 1));
        if (i)
            text_cat(out, " ");
        text_cat(out, b);
    }
}


/* a layer's 16 tiles, four rows of four over the rows' band (two rows of tiles a band) */
static void tile_info(uint32_t w, char *label, int *state)   /* state: 0 off, 1 on, 2 lit, 3 dim, 4 queued */
{
    uint32_t ch = set.channel;
    label[0] = 0;
    *state = 0;
    switch (ui.layer) {
    case B_GLO:
        if (w >= 7u && w <= 12u) {       /* a channel: lit, off when muted, white when soloed (its black key) */
            label[0] = 'C';              /* (its name if it has one) */
            label[1] = 'H';
            label[2] = (char)('1' + w - 7u);
            label[3] = 0;
            if (chan_given(w - 7u)[0])
                copy_text(label, chan_given(w - 7u), 9);
            *state = seq_solo() >> (w - 7u) & 1u ? 1 : seq_mute() >> (w - 7u) & 1u ? 0 : 2;
        } else if (w == 5u) {
            copy_text(label, "LEARN", 8);
            *state = ui.learn ? 4 : 0;
        } else if (w == 6u) {
            copy_text(label, "FILL", 8);
            *state = seq_fill() ? 1 : 0;
        } else if (w == 4u) {
            copy_text(label, "FILL >", 8);
        } else if (w == 15u) {
            copy_text(label, "TAP", 8);
        } else {
            *state = 3;
        }
        break;
    case B_FX: {
        static const char *const PN[16] = {"LOOP 1/4", "LOOP 1/8", "LOOP 16", "LOOP 32", "STUTTER", "REVERSE",
                                           "TAPE STOP", "HALF", "LP SWEEP", "HP SWEEP", "PHONE", "CRUSH", "ALIAS",
                                           "GATE", "ECHO", "WOBBLE"};
        copy_text(label, PN[w], 10);
        *state = synth_punches() >> w & 1u ? 1 : 0;
        break;
    }
    case B_ARP:
        note_label(ch, synth_key_note(ch, WHITE_KEYS[w]) >= 0 ? synth_key_note(ch, WHITE_KEYS[w])
                                                              : 53 + WHITE_KEYS[w] + 12 * set.octave, label);
        *state = ui.keys >> WHITE_KEYS[w] & 1u ? 2 : 0;
        break;
    case B_SEL: {
        int32_t note = 53 + WHITE_KEYS[w];
        copy_text(label, NOTE_NAME[note % 12], 8);
        *state = note % 12 == song.root ? 1 : in_scale(note) ? 0 : 3;
        break;
    }
    case B_EDIT:
        if (maps_keys(ch))
            note_label(ch, synth_key_note(ch, WHITE_KEYS[w]), label);
        else
            note_text(53 + WHITE_KEYS[w] + 12 * set.octave, label);
        *state = ui.keys >> WHITE_KEYS[w] & 1u ? 2 : 0;
        break;
    case B_SEQ: {
        uint32_t s = ui.step_page * 16u + w;
        const pat_t *p = seq_pat(ch);
        int32_t pos = seq_pos(ch);
        int_text((int32_t)s + 1, label);
        if (s >= seq_steps(p))
            *state = 3;
        else if ((int16_t)s == ui.held_step)
            *state = 1;
        else if (pos >= 0 && (uint32_t)pos / seq_step_ticks(p) == s)
            *state = 4;
        else if (seq_step_find(ch, s) >= 0)
            *state = 2;
        break;
    }
    case B_SAVE:
        int_text((int32_t)w + 1, label);
        *state = w == seq_scene() ? 1 : (int32_t)w == seq_queued() ? 4 : seq_scene_used(w) ? 2 : 0;
        break;
    default:
        break;
    }
}

static void draw_tiles(uint32_t band)    /* band 0..2: 56 px each, the tiles' rows 0..3 spread over them */
{
    uint32_t r, col;
    uint16_t c = th.ch[set.channel];
    cv_begin(240, 56, TC(BG));
    for (r = 0; r < 4u; r++) {
        int32_t y = 4 + 42 * (int32_t)r - 56 * (int32_t)band;
        if (y + 38 < 0 || y >= 56)
            continue;
        for (col = 0; col < 4u; col++) {
            char label[12];
            int state;
            int32_t x = 3 + 59 * (int32_t)col;
            uint16_t fill, ink;
            tile_info(r * 4u + col, label, &state);
            fill = state == 1 ? TC(FG) : state == 2 ? (ui.layer == B_GLO && r * 4u + col >= 7u && r * 4u + col <= 12u
                                                          ? th.ch[r * 4u + col - 7u] : c)
                 : state == 4 ? TC(REC) : TC(PANEL);
            ink = state == 1 || state == 2 || state == 4 ? TC(BG) : state == 3 ? TC(DIM) : TC(SUB);
            rrect(x, y, 56, 38, 4, fill);
            text_fit(x + (56 - text_w(&FONT_XS, label)) / 2, y + 19 - (int32_t)FONT_XS.h / 2, &FONT_XS, label, ink, 52);
        }
    }
    cv_blit(0, 26 + 56 * band);
}

static const char *set_label(uint32_t id)   /* a setting's name in full, as the SETTINGS rows have room for */
{
    static char t[12];
    switch (id) {
    case KN_VSENS: return "VELOCITY";
    case KN_LEARN: return "KEY LEARN";
    case KN_FIXVEL: return "FIXED VEL";
    case KN_PRESS: return "PRESSURE";
    case KN_ACCEL: return "KNOB ACCEL";
    case KN_LIGHTS: return "BUTTON LIGHTS";
    case KN_SCALEKEYS: return "SCALE ON KEYS";
    case KN_LOWCUT: return "SPEAKER LOWCUT";
    case KN_USBA: return "USB AUDIO";
    case KN_RESO: return "FILTER RESO";
    case KN_IN: return "MIDI IN";
    case KN_OTHER: return "OTHER CH";
    case KN_SEQOUT: return "MIDI OUT";
    case KN_BEND: return "BEND RANGE";
    case KN_DRMAP: return "DRUM MAP";
    case KN_FXCH: return "FX CHANNEL";
    case KN_CLKOUT: return "CLOCK OUT";
    case KN_STEPADV: return "STEP ADVANCE";
    case KN_DRPREV: return "DRUM PREVIEW";
    case KN_LAUNCH: return "SCENE LAUNCH";
    case KN_RREPL: return "REC MODE";
    case KN_HOMEVIEW: return "HOME VIEW";
    case KN_AUTOLOOP: return "AUTO LOOP";
    case KN_FACTORY: return "FACTORY RESET";
    case KN_BATT: return "BATTERY";
    case KN_AUTOSAVE: return "AUTOSAVE";
    case KN_ABOUT: return "VERSION";
    default:
        if (id >= KN_MIDI && id < KN_MIDI + NCH) {
            copy_text(t, "CHANNEL  ", sizeof t);
            t[8] = (char)('1' + id - KN_MIDI);
            return t;
        }
        return kn_label(id);
    }
}

/* SETTINGS: the FM-1's own, full screen below the top bar (y 26..239, four bands: 56, 56, 56, 46): a title bar
 * (SETTINGS, the group, its pages' dots or a message), then the page's four rows, KNOB 1-4 one each */
static void draw_settings(uint32_t band, uint32_t now)
{
    static const int32_t B0[4] = {0, 56, 112, 168}, BH[4] = {56, 56, 56, 46};
    uint16_t red = th.ch[0];
    int8_t l[GMAX];
    int32_t n = group_list(G_MENU, l), i, np = 0, pi = 0, x;
    const char *g = set_group(ui.page);
    uint32_t r;
    cv_begin(240, (uint32_t)BH[band], TC(BG));
    cv_oy = -B0[band];
    cv_y0 = 0;
    cv_y1 = BH[band];
    x = cv_text(10, at_base(&FONT_T, 13), &FONT_T, "SETTINGS", TC(SUB));
    cv_text(10, at_base(&FONT_B, 29), &FONT_B, g, TC(FG));
    for (i = 0; i < n; i++)              /* the group's pages: how many, which */
        if (set_group((uint32_t)l[i]) == g) {
            if (l[i] == ui.page)
                pi = np;
            np++;
        }
    if (now < ui.msg_until) {
        int32_t w = text_w(&FONT_T, ui.msg[0]) + 6 + text_w(&FONT_T, ui.msg[1]);
        x = cv_text(230 - w, at_base(&FONT_T, 29), &FONT_T, ui.msg[0], red);
        cv_text(x + 6, at_base(&FONT_T, 29), &FONT_T, ui.msg[1], red);
    } else if (np > 1)
        for (i = 0; i < np; i++)
            cv_rect(226 - 8 * (np - 1) + 8 * i, 20, 5, 5, i == pi ? red : TC(DIM));
    (void)x;
    cv_rect(0, 35, 240, 1, TC(LINE));
    for (r = 0; r < 4u; r++) {
        uint32_t id = PAGE_KN[ui.page][r], v;
        int32_t y = 38 + 44 * (int32_t)r;
        char t[16], k[2] = {(char)('1' + r), 0};
        const felucca_font_t *f = &FONT_M;
        if (r)
            cv_rect(10, y - 2, 220, 1, TC(PANEL));
        if (id == KN_NONE)
            continue;
        v = kn_value(id, t);
        knob(24, y + 20, v, red);
        cv_text(24 - text_w(&FONT_T, k) / 2, at_mid(&FONT_T, y + 20), &FONT_T, k, TC(SUB));
        if (text_w(f, t) > 100)
            f = &FONT_B;
        cv_text(230 - text_w(f, t), at_mid(f, y + 20), f, t, TC(FG));
        text_fit(48, at_mid(&FONT_B, y + 20), &FONT_B, set_label(id), TC(SUB), 230 - text_w(f, t) - 56);
    }
    cv_oy = 0;
    cv_blit(0, (uint32_t)(26 + B0[band]));
}

static const char *bar_title(void)      /* SEQ held: "STEPS BAR 2/4" */
{
    static char t[20];
    uint32_t np = (seq_steps(seq_pat(set.channel)) + 15u) / 16u, l = 0;
    copy_text(t, "STEPS BAR ", sizeof t);
    while (t[l])
        l++;
    int_text(ui.step_page + 1, t + l);
    text_cat(t, "/");
    while (t[l])
        l++;
    int_text((int32_t)np, t + l);
    return t;
}

/* the footer, y 194..239: a rule, the title (or a message), the page's dots, the four knobs */
static void draw_foot(uint32_t now)
{
    uint32_t ch = set.channel, k, q, np, pi = 0;
    int setp = ui.group_btn >= NB && !layer_shown();   /* nullwave's settings, the menu */
    uint16_t c = setp ? th.ch[0] : th.ch[ch];
    int32_t x, ty = at_base(&FONT_T, 205) - 194;
    const uint16_t *kn = knobs_now();
    char t[24];
    const char *title;
    cv_begin(240, 46, TC(BG));
    cv_rect(0, 1, 240, 1, TC(LINE));
    if (layer_shown())
        title = ui.layer_lock && ui.layer == B_SEL ? "KEY KEPT  TAP SEL TO PLAY"
              : ui.layer_lock && ui.layer == B_GLO ? "MIX KEPT  TAP GLO TO PLAY"
              : ui.layer == B_GLO ? "MIX" : ui.layer == B_SEL ? "KEY" : ui.layer == B_EDIT ? (synth_sound(ch)->engine == 12u ? "ERASE  LANE MUTES" : "ERASE  OP PIT GLO MONO POLY")
              : ui.layer == B_SEQ ? (ui.held_step >= 0 ? "STEP" : bar_title()) : ui.layer == B_FX ? "PUNCH"
              : ui.layer == B_ARP ? "ROLL" : "SCENES";
    else if (seq_rec() == REC_ARMED && ui.page == PG_HOME)
        title = seq_cfg.rec_replace ? "REC REPLACE" : "REC OVERDUB";
    else if (ui.page == PG_EDIT1 || ui.page == PG_EDIT2 || (ui.page == PG_HOME && !ui.loop))
        title = synth_page_title(ch, ui.page == PG_EDIT2);
    else if (ui.page == PG_HOME && silent_why(ch))   /* LOOP: why the channel is silent, where the title is */
        title = silent_why(ch);
    else
        title = PAGE_TITLE[ui.page];
    if (now < ui.msg_until) {
        x = cv_text(4, ty, &FONT_T, ui.msg[0], c);
        cv_text(x + 6, ty, &FONT_T, ui.msg[1], c);
    } else {
        int32_t ct, cb;
        t[0] = (char)('1' + ch);
        t[1] = ' ';
        t[2] = 0;
        text_cat(t, chan_name(ch));
        x = cv_text(4, ty, &FONT_T, setp ? (ui.group_btn == G_MENU ? "MENU" : "SETTINGS") : t, c);
        font_caps(&FONT_T, &ct, &cb);
        cv_rect(x + 3, ty + (ct + cb) / 2 - 1, 2, 2, c);
        x = cv_text(x + 9, ty, &FONT_T, title, c);
        np = layer_shown() || ui.page == PG_HOME || ui.page == PG_NAME ? 0u : (uint32_t)group_pages();
        pi = (uint32_t)page_index();
        if (np > 1u) {                       /* the page's number: "GLOBAL 1/8" */
            int_text((int32_t)pi + 1, t);
            text_cat(t, "/");
            int_text((int32_t)np, t + 2 + (pi + 1u >= 10u));
            cv_text(x + 5, ty, &FONT_T, t, TC(SUB));
        }
    }
    np = layer_shown() || ui.page == PG_HOME || ui.page == PG_NAME ? 0u : (uint32_t)group_pages();
    pi = (uint32_t)page_index();
    for (q = 0; q < np && np > 1u; q++)
        cv_rect(234 - 6 * ((int32_t)np - 1) + 6 * (int32_t)q, 7, 4, 4, q == pi ? c : TC(DIM));
    if (ui.layer == B_SAVE && layer_shown()) {
        cv_text(4, at_base(&FONT_T, 222 - 194), &FONT_T, "KEYS: SCENES (MORE: A CHAIN)", TC(SUB));
        cv_text(4, at_base(&FONT_T, 236 - 194), &FONT_T, "F#: COPY  G#: LOOP/SONG  A#: SONG REC", TC(DIM));
    } else {
        for (k = 0; k < SYN_KNOBS && kn; k++) {
            int32_t cx = 14 + 60 * (int32_t)k;
            const felucca_font_t *f = &FONT_B;
            uint32_t v;
            if (kn[k] == KN_NONE)
                continue;
            v = kn_value(kn[k], t);
            knob(cx, 225 - 194, v, c);
            text_fit(cx + 13, at_base(&FONT_T, 219 - 194), &FONT_T, kn_label(kn[k]), TC(SUB), 34);
            if (text_w(f, t) > 34)
                f = &FONT_XS;
            text_fit(cx + 13, at_base(f, 233 - 194), f, t, TC(FG), 34);
        }
    }
    cv_blit(0, 194);
}

/* NOTES: the pattern as a piano roll, the note picked lit; DRUMS: the kit's eight lanes on 16 steps, the cursor */
static void draw_roll(uint32_t ch)
{
    const pat_t *p = seq_pat(ch);
    uint8_t ix[PAT_EV];
    uint32_t n = seq_ev_list(ch, ix, PAT_EV), i, len = (uint32_t)p->bars * TBAR, b;
    int32_t lo = 127, hi = 0, y0 = 16, y1 = 156, w = GX1 - GX0, rh, pos = seq_pos(ch);
    char t[12];
    for (i = 0; i < n; i++) {
        const ev_t *e = seq_ev(ch, ix[i]);
        lo = e->note < lo ? e->note : lo;
        hi = e->note > hi ? e->note : hi;
    }
    if (!n) {
        lo = 60;
        hi = 71;
    }
    lo -= 1;
    hi += 1;
    if (hi - lo < 11) {
        lo -= (11 - (hi - lo)) / 2;
        hi = lo + 11;
    }
    rh = (y1 - y0) / (hi - lo + 1);
    if (rh < 2)
        rh = 2;
    for (i = 0; i <= (uint32_t)(hi - lo); i++) {   /* the black keys' rows shaded */
        int32_t note = hi - (int32_t)i;
        if ((0x54Au >> (note % 12) & 1u))
            cv_rect(GX0, y0 + (int32_t)i * rh, w, rh, TC(PANEL));
        if (note % 12 == 0) {
            note_text(note, t);
            g_text(1, y0 + (int32_t)i * rh + rh / 2 + 3, t, TC(DIM));
        }
    }
    for (b = 0; b <= (uint32_t)p->bars * 4u; b++)  /* the beats, the bars brighter */
        cv_rect(GX0 + (int32_t)(b * TPQ * (uint32_t)w / len), y0, 1, (hi - lo + 1) * rh, b % 4u ? TC(PANEL) : TC(LINE));
    for (i = 0; i < n; i++) {
        const ev_t *e = seq_ev(ch, ix[i]);
        int32_t x = GX0 + (int32_t)((uint32_t)e->pos * (uint32_t)w / len);
        int32_t ww = (int32_t)((uint32_t)e->len * 6u * (uint32_t)w / len), y = y0 + (hi - e->note) * rh;
        ww = ww < 3 ? 3 : x + ww > GX1 ? GX1 - x : ww;
        rrect(x, y, ww, rh > 3 ? rh - 1 : rh, 1, i == ui.nsel ? TC(FG) : g_col);
    }
    if (pos >= 0)
        cv_rect(GX0 + (int32_t)((uint32_t)pos * (uint32_t)w / len), y0, 1, (hi - lo + 1) * rh, TC(REC));
    int_text((int32_t)n, t);
    text_cat(t, n == 1u ? " NOTE" : " NOTES");
    g_text(GX0, 10, t, TC(SUB));
}
static void draw_drums(uint32_t ch)
{
    const pat_t *p = seq_pat(ch);
    uint32_t l, k, pg = ui.dstep / 16u, steps = seq_steps(p), st = seq_step_ticks(p);
    int32_t pos = seq_pos(ch), now = pos >= 0 ? pos / (int32_t)st : -1;
    char t[16];
    for (l = 0; l < 8u; l++) {
        int32_t y = 14 + 18 * (int32_t)l, note = (int32_t)synth_drum_note(l);
        g_text(4, y + 12, synth_kit_sound(l), l == ui.dlane ? TC(FG) : TC(SUB));
        for (k = 0; k < 16u; k++) {
            uint32_t s = pg * 16u + k;
            int32_t x = 48 + 12 * (int32_t)k + 2 * (int32_t)(k / 4u), hit;
            uint16_t c;
            if (s >= steps)
                break;
            hit = seq_hit_find(ch, s, (uint32_t)note);
            c = hit >= 0 ? (seq_ev(ch, (uint32_t)hit)->vel >= 64u ? g_col : th.ch_line[ch]) : (int32_t)s == now ? TC(LINE) : TC(PANEL);
            if (l == ui.dlane && s == ui.dstep)
                rrect(x - 1, y - 1, 12, 17, 2, TC(FG));
            rrect(x, y, 10, 15, 2, c);
        }
    }
    int_text((int32_t)pg * 16 + 1, t);
    text_cat(t, "-");
    {
        uint32_t e = pg * 16u + 16u > steps ? steps : pg * 16u + 16u, l2 = 0;
        while (t[l2])
            l2++;
        int_text((int32_t)e, t + l2);
    }
    text_cat(t, " OF ");
    {
        uint32_t l2 = 0;
        while (t[l2])
            l2++;
        int_text((int32_t)steps, t + l2);
    }
    g_text(GX0, 165, t, TC(DIM));
}
static void draw_seqview(uint32_t band)
{
    g_col = th.ch[set.channel];
    cv_begin(240, 84, TC(BG));
    cv_oy = -84 * (int32_t)band;
    cv_y0 = 0;
    cv_y1 = 84;
    if (ui.page == PG_NOTES)
        draw_roll(set.channel);
    else
        draw_drums(set.channel);
    cv_oy = 0;
    cv_blit(0, 26 + 84 * band);
}
static uint32_t seqview_sig(void)
{
    const pat_t *p = seq_pat(set.channel);
    uint32_t h = sig_add(2166136261u, (uint32_t)ui.page << 24 | (uint32_t)ui.nsel << 16 | (uint32_t)ui.dlane << 8 | ui.dstep), i;
    int32_t pos = seq_pos(set.channel);
    h = sig_add(h, set.channel << 8 | p->bars << 4 | p->div);
    for (i = 0; i < p->n; i++)
        h = sig_add(h, (uint32_t)p->ev[i].pos << 16 | (uint32_t)p->ev[i].note << 8 | (p->ev[i].len ^ (uint32_t)p->ev[i].vel << 24));
    h = sig_add(h, pos >= 0 ? (uint32_t)pos / TPS : 0xFFFFu);
    return h;
}

/* NAME: the channel, its eight places (the cursor's in its colour, blinking), what the buttons do */
static void draw_name(uint32_t band, uint32_t blink)
{
    uint32_t i, ch = ui.name_ch % NCH;
    uint16_t c = th.ch[ch];
    char t[24];
    cv_begin(240, 84, TC(BG));
    cv_oy = -84 * (int32_t)band;
    cv_y0 = 0;
    cv_y1 = 84;
    channel_text(ch, t);
    text_cat(t, "  ");
    text_cat(t, synth_engine_name(synth_sound(ch)->engine));
    cv_text(12, 18, &FONT_T, t, TC(SUB));
    for (i = 0; i < SONG_NAME; i++) {
        int32_t x = 12 + 27 * (int32_t)i, cur = i == ui.name_cur;
        char g[2] = {ui.name[i], 0};
        rrect(x, 48, 24, 40, 4, cur ? (blink ? c : th.ch_fill[ch]) : TC(PANEL));
        if (g[0] != ' ')
            cv_text(x + (24 - text_w(&FONT_B, g)) / 2, at_mid(&FONT_B, 68), &FONT_B, g,
                    cur && blink ? TC(BG) : TC(FG));
    }
    cv_text(12, 112, &FONT_T, "SAVE  KEEP IT", TC(SUB));
    cv_text(12, 134, &FONT_T, "HOME  AS IT WAS", TC(SUB));
    cv_oy = 0;
    cv_blit(0, 26 + 84 * band);
}

#ifdef PLAT_HOST                         /* (the host: what drawing costs, for the tests: expect draw_us, draw_px) */
#include <time.h>
static uint64_t draw_ns, draw_n, draw_px0;
#endif
/* each part redrawn only when what it shows has changed */
static void draw_(uint32_t now)
{
    uint32_t i, h, gk = graph_now(), mode = layer_shown() ? 1u : ui.group_btn == G_MENU ? 2u : gk ? 3u : 0u, blink = now / 250u & 1u;
    int bat = battery_state();
    const uint16_t *kn;
    char t[12];
    if (mode == 3u)
        mode = 3u + gk;                  /* (another graph: the band again) */
    else if (mode == 0u && (ui.page == PG_NOTES || ui.page == PG_DGRID))
        mode = 3u + GR_N + (ui.page == PG_DGRID);   /* NOTES, DRUMS: their views */
    else if (mode == 0u && ui.page == PG_NAME)
        mode = 9u + GR_N;                /* NAME: the name being edited */
    else if (mode == 0u && !ui.loop)
        mode = 10u + GR_N;               /* the channels as PLAY (0: as LOOP) */
    if (ui.theme_drawn != prefs.display || mode != ui.mode_drawn) {   /* another theme or view: everything again */
        if (ui.theme_drawn != prefs.display) {
            theme_set(prefs.display);
            lcd_fill(0, 0, 240, 240, TC(BG));
        }
        ui.theme_drawn = prefs.display;
        ui.mode_drawn = (uint8_t)mode;
        ui.sig_top = ui.sig_foot = 0;
        for (i = 0; i < NCH; i++)
            ui.sig_row[i] = 0;
    }
    h = sig_add(2166136261u, (uint32_t)(bat + 1));
    h = sig_add(h, seq_slot() << 24 | seq_scene() << 16 | (uint32_t)(seq_queued() + 1) << 8 | seq_rec());
    h = sig_add(h, (uint32_t)seq_playing() << 1 | blink);
    h = sig_add(h, seq_bpm10() << 8 | (uint32_t)seq_count());
    h = sig_add(h, sel_bar(set.channel) << 8 | seq_pat(set.channel)->bars << 4 | song.mode);
    if (h != ui.sig_top) {
        ui.sig_top = h;
        draw_top(bat, now);
    }
    synth_scope_ch(gk == GR_SCOPE || mode == 10u + GR_N ? set.channel : NCH);
    if (mode == 0u) {                    /* LOOP: four channels with their steps */
        for (i = 0; i < 2u; i++) {
            h = chans_sig(i);
            if (h != ui.sig_row[i]) {
                ui.sig_row[i] = h;
                draw_chans(i);
            }
        }
    } else if (mode == 10u + GR_N) {     /* PLAY: the selected channel, alive */
        uint32_t ch = set.channel, k, lit;
        const sound_t *s = synth_sound(ch);
        char t1[24], t2[24];
        h = sig_str(sig_add(2166136261u, ch << 16 | (uint32_t)s->engine << 8 | s->preset), chan_given(ch));
        for (k = 0; k < NCH; k++) {
            uint32_t age = ui.pulse_t[k] ? now - ui.pulse_t[k] : PULSE_MS;
            h = sig_add(h, k << 16 | (uint32_t)synth_audible(k) << 8 | (age < PULSE_MS ? age / 30u : 99u));
        }
        if (h != ui.sig_row[0]) {
            ui.sig_row[0] = h;
            draw_play_head(now);
        }
        h = sig_add(2166136261u, ch << 24 | (uint32_t)s->engine << 16 | (anim_busy(ch) ? now / 40u : now / 80u));
        for (k = 0; k < 8u; k++)
            h = sig_add(h, (uint32_t)synth_param(ch, SP_E0 + k));
        h = sig_add(h, play_keys_state(&lit, t1, t2) ^ lit);
        h = sig_str(sig_str(h, t1), t2);
        h = sig_str(h, silent_why(ch) ? silent_why(ch) : "");
        if (h != ui.sig_row[1]) {
            ui.sig_row[1] = h;
            anim_step(ch, now);          /* (the frame's sound and dots once, then each band) */
            for (k = 0; k < 2u; k++)
                draw_play_anim(k, now);
        }
    } else if (mode == 9u + GR_N) {
        h = sig_str(sig_add(2166136261u, (uint32_t)ui.name_ch << 8 | ui.name_cur), ui.name);
        h = sig_add(h, blink);
        if (h != ui.sig_row[0]) {
            ui.sig_row[0] = h;
            for (i = 0; i < 2u; i++)
                draw_name(i, blink);
        }
    } else if (mode >= 3u + GR_N) {
        h = seqview_sig();
        if (h != ui.sig_row[0]) {
            ui.sig_row[0] = h;
            for (i = 0; i < 2u; i++)
                draw_seqview(i);
        }
    } else if (mode >= 3u) {
        h = graph_sig(gk, now);
        if (h != ui.sig_row[0]) {
            ui.sig_row[0] = h;
            for (i = 0; i < 2u; i++)
                draw_graph(i, gk);
        }
    } else if (mode == 2u) {             /* SETTINGS: the whole screen below the top bar, the footer too */
        uint32_t r;
        char t[16];
        h = sig_add(2166136261u, (uint32_t)ui.page);
        for (r = 0; r < 4u; r++) {
            uint32_t v = kn_value(PAGE_KN[ui.page][r], t);
            h = sig_add(sig_str(h, t), v);
        }
        if (now < ui.msg_until)
            h = sig_str(sig_str(h, ui.msg[0]), ui.msg[1]);
        if (h != ui.sig_row[0]) {
            ui.sig_row[0] = h;
            for (i = 0; i < 4u; i++)
                draw_settings(i, now);
        }
        ui.sig_foot = 0;
        return;
    } else {
        for (i = 0; i < 3u; i++) {
            h = sig_add(2166136261u, mode << 8 | i);
            {
                uint32_t w;
                h = sig_add(h, (uint32_t)ui.layer << 8 | ui.step_page);
                for (w = 0; w < 16u; w++) {
                    int st;
                    tile_info(w, t, &st);
                    h = sig_add(sig_str(h, t), (uint32_t)st);
                }
                h = sig_add(h, set.channel);
            }
            if (h != ui.sig_row[i]) {
                ui.sig_row[i] = h;
                draw_tiles(i);
            }
        }
    }
    h = sig_add(2166136261u, (uint32_t)ui.page << 16 | (uint32_t)(ui.layer + 1) << 8 | ui.group_btn);
    h = sig_add(h, set.channel << 8 | (uint32_t)(ui.held_step + 1));
    if (now < ui.msg_until)
        h = sig_str(sig_str(h, ui.msg[0]), ui.msg[1]);
    kn = knobs_now();
    for (i = 0; i < SYN_KNOBS && kn; i++) {
        uint32_t v = kn_value(kn[i], t);
        h = sig_add(sig_str(sig_str(h, t), kn_label(kn[i])), v);
    }
    h = sig_add(h, synth_sound(set.channel)->engine);
    if (h != ui.sig_foot) {
        ui.sig_foot = h;
        draw_foot(now);
    }
}

static void draw(uint32_t now)
{
#ifdef PLAT_HOST
    struct timespec a, b;
    clock_gettime(CLOCK_MONOTONIC, &a);
    draw_(now);
    clock_gettime(CLOCK_MONOTONIC, &b);
    draw_ns += (uint64_t)(b.tv_sec - a.tv_sec) * 1000000000u + (uint64_t)(b.tv_nsec - a.tv_nsec);
    draw_n++;
#else
    draw_(now);
#endif
}

void app_ui_init(void)
{
    uint32_t ch, k;
    touch_on(1);                             /* velocity and pressure: the keys' readings on */
    keys_config();
    ui.theme_drawn = 0xFF;
    ui.layer = -1;
    ui.held_step = -1;
    ui.logo_on = 1;
    ui.logo_t0 = plat_ms();
    ui.input_t = ui.logo_t0;
    ui.save_key = -1;
    if (boot_reset) {
        app_say("SETTINGS RESET", "SONGS KEPT");
        ui.msg_until = plat_ms() + 5000u;
    }
    ui.edit_key = -1;
    ui.page = PG_HOME;
    ui.group_btn = B_HOME;
    ui.loop = prefs.home_view;
    ui.lock_id = SP_CUT;
    slot_pick = (uint8_t)seq_slot();
    for (ch = 0; ch < NCH; ch++)
        ui.entry[ch] = synth_is_kit(ch) ? 36 : 60;
    for (k = 0; k < NKEYS; k++)
        for (ch = 0; ch < 5u; ch++)
            ui.key_note[k][ch] = -1;
}

static int factory_step(void);
static void factory_start(void);
void app_frame(void)
{
    uint32_t i, now = plat_ms();
    if (factory_step())                  /* (a factory reset under way: nothing else, the ring filling) */
        return;
    if (ui.fr_hold) {                    /* FACTORY RESET: PLAY held; let go early, nothing happens */
        if (!(ui.btn >> B_PLAY & 1u)) {
            ui.fr_hold = ui.fr_arm = 0;
            app_say("FACTORY RESET", "NOT DONE");
        } else if (now - ui.fr_hold >= 3000u) {
            ui.fr_hold = ui.fr_arm = 0;
            factory_start();
            return;
        } else {
            char c[8] = "HOLD  S";
            c[5] = (char)('0' + (3000u - (now - ui.fr_hold) + 999u) / 1000u);
            app_say("FACTORY RESET", c);
        }
    }
    input();
    cc_poll();
    if (pv.note >= 0 && (int32_t)(now - pv.until) >= 0)
        preview_end();
    if (ui.layer == B_SAVE && ui.save_key >= 0 && now - ui.save_t >= 1000u) {   /* SAVE + a scene key held 1 s */
        char t[16] = "SCENE ";
        int_text(ui.save_key + 1, t + 6);
        if ((uint32_t)ui.save_key != seq_scene())
            seq_scene_copy(seq_scene(), (uint32_t)ui.save_key);   /* (what plays now, into that scene) */
        else
            seq_touch();
        app_say(t, "STORED");
        ui.save_key = -1;
        ui.copy_mode = 0;
    }
    synth_lowcut(prefs.lowcut && plat_headphones() != 1);   /* (the speaker only: headphones in, the lows stay) */
    plat_usb_audio_full(prefs.usb_full);
    sysex_poll();
    speak();
    if (FM1B_BLE_CENTRAL) {                  /* a device connected, given up or lost */
        int ev;
        btdev_page(ui.page == PG_C_BT && ui.group_btn == G_MENU && !set.ble_off);   /* (it scans while the BT settings page is up) */
        while ((ev = btdev_event()) != BTDEV_NONE)
            if (ev == BTDEV_CONNECTED || ev == BTDEV_NO_ANSWER || ev == BTDEV_LOST)
                app_say("DEVICE", btdev_word(ev));
    }
    {
        static uint32_t shed_seen;       /* the overload guard faded a note: said, so it is never silent for no reason */
        if (synth_budget_sheds() != shed_seen) {
            static uint32_t shed_at;
            shed_seen = synth_budget_sheds();
            if (now - shed_at > 1000u) {
                shed_at = now;
                app_say("VOICE LIMIT", "CPU");
            }
        }
    }
    {
        static uint32_t seen;
        if (overloads != seen) {             /* the render ran late: said, once a second at most */
            static uint32_t said_at;
            seen = overloads;
            if (now - said_at > 1000u) {
                said_at = now;
                app_say("CPU", "OVERLOAD");
            }
        }
    }
    for (i = 0; i < NCH; i++) {              /* the meters: up at once, down ~0.4 s; a jump up: a note struck */
        uint8_t p = meter_px(synth_peak(i));
        if (p > ui.lvl[i] + 12u)
            ui.pulse_t[i] = now ? now : 1u;
        ui.lvl[i] = p > ui.lvl[i] ? p : ui.lvl[i] > 4u ? (uint8_t)(ui.lvl[i] - 4u) : 0u;
    }
    if (seq_take_kept())
        app_say("KEPT", "LAST TAKE");    /* (a REPLACE pass stopped short) */
    {                                        /* playing or recording: LOOP (the channels), while it lasts */
        int busy = seq_playing() || seq_rec() != REC_OFF;
        if (!busy)
            ui.loop_user = 0;
        if (busy && !ui.loop && !prefs.loop_manual && !ui.loop_user) {
            ui.loop = 1;
            ui.loop_auto = 1;
        } else if (!busy && ui.loop_auto) {
            ui.loop = 0;
            ui.loop_auto = 0;
        }
    }
    {                                        /* the four channels shown: the selected one among them, sliding */
        int32_t top = ui.pv_top, goal;
        if ((int32_t)set.channel < top)
            top = set.channel;
        else if ((int32_t)set.channel > top + 3)
            top = (int32_t)set.channel - 3;
        ui.pv_top = (uint8_t)clamp(top, 0, NCH - 4);
        goal = ui.pv_top * 8 * 42;           /* (in 1/8 of a row of 42: each view scales it to its own rows) */
        if (ui.pv_y != goal) {
            int32_t d = (goal - ui.pv_y) / 4;
            ui.pv_y = (int16_t)(ui.pv_y + (d ? d : goal > ui.pv_y ? 1 : -1));
        }
    }
    /* what the FM-1 keeps, written when it is quiet: the settings after 2 s, the song (AUTOSAVE) after 3 s stopped */
    if (set_dirty && now - set_dirty_at > 2000u && now - ui.input_t > 1000u && !seq_playing()) {
        set_dirty = 0;
        save_prefs();
    }
    if (prefs.autosave && seq_dirty() && !seq_playing() && seq_rec() == REC_OFF && now - ui.input_t > 3000u
        && now > 5000u) {
        if (seq_save(seq_slot()) != 0)
            prefs.autosave = 0;              /* (a song too big for its slot: no more tries; SAVE says why) */
    }
    if (ui.logo_on) {                    /* the boot animation, ~1.3 s; anything touched ends it */
        uint32_t t = now - ui.logo_t0;
        if (t >= LOGO_MS + 200u || (int32_t)(ui.input_t - ui.logo_t0) > 0) {
            ui.logo_on = 0;
            ui.theme_drawn = 0xFF;           /* (the screen drawn afresh) */
            if (fresh_build) {               /* (a new build's first start: said on the logo and after it, once) */
                fresh_build = 0;
                app_say("INSTALLED", APP_VERSION);
                ui.msg_until = now + 4000u;
                prefs.seen_build = (uint32_t)FM1B_BUILD;
                app_mark();
            }
        } else {
            if (now - ui.logo_drawn >= 30u) {
                ui.logo_drawn = now;
                logo_frame(t > LOGO_MS ? LOGO_MS : t, prefs.display);
            }
            return;
        }
    }
    lights(now);
    draw(now);
}
