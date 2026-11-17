/* SPDX-License-Identifier: GPL-3.0-only
 * BB1 2.0 kit builder: user kits stored as REFERENCES to installed machines,
 * never as sample bytes. Pure main-loop module: no audio-owner access, no heap,
 * no flash, no floats. The audio owner only ever sees a resolved route table and
 * an immutable composite zone array built here (bb_audio_user_kit_install_owned).
 *
 * One user kit = 17 voice entries in phrase source order (BB_FDV_BD..BB_FDV_RD).
 * Each entry names a machine (index into the kit's own table of up to eight
 * machine keys) and a source voice of that machine, plus level, tune, decay.
 * SY/ZAP stay the shared AUX bank and the shaker keeps its maracas route, exactly
 * as on every installed kit.
 *
 * Wire record "BBUK" v1, 120 bytes, little-endian:
 *   0  'BBUK'            8  CRC32 (IEEE) of bytes 0..7 and 12..119
 *   4  version = 1       12 name, 8 bytes ASCII 32..126, NUL padded (empty = slot default)
 *   5  machine count 1..8 20 machine keys 8 x u32 (unused = 0, must be zero)
 *   6  flags = 0         52 voices 17 x 4: [machine(3) | voice(5)<<3], level, tune, decay
 *   7  reserved = 0
 * Unknown version, flags or reserved bits refuse; nothing is silently converted.
 */
#ifndef BB_USER_KIT_H
#define BB_USER_KIT_H
#include <stdint.h>
#include <stddef.h>
#include "bb_sample_kit_r1.h"

#define BB_UK_VOICES 17u          /* BD SD LT MT HT LC MC HC RS CL CP MA CB CY OH CH RD */
#define BB_UK_MACHINES 8u         /* distinct source machines one kit may draw from */
#define BB_UK_NAME 8u
#define BB_UK_RECORD_BYTES 120u
#define BB_UK_VERSION 1u
#define BB_UK_SLOTS 4u            /* USER 1..4, own object "BBUB" */
#define BB_UK_BANK_BYTES (16u+BB_UK_SLOTS*BB_UK_RECORD_BYTES)   /* 496 */
#define BB_UK_ZONES 16u           /* distinct sampled sounds per kit (bb_sample_kit_trigger bound) */
#define BB_UK_TUNE_NATIVE 64u     /* sampled: 4 steps per semitone, -16..+15.75 st */
#define BB_UK_FULL 127u           /* sampled level/decay: unity / whole sample */

/* Machine engines. A key is CRC32 of "<prefix>:<name>": N = native synthesised
 * (N:808), S = sampled BBSK kit by its 12-byte directory name, L = SLOOP synth
 * (reserved for 2.x). Pack slot numbers are never stored: a repack that keeps a
 * kit's name keeps every user kit that references it. */
enum { BB_UK_ENGINE_NONE, BB_UK_ENGINE_808, BB_UK_ENGINE_SAMPLE, BB_UK_ENGINE_AUX };

typedef struct { uint8_t machine, voice, level, tune, decay; } bb_uk_voice;
typedef struct {
    char name[BB_UK_NAME];
    uint32_t key[BB_UK_MACHINES];
    uint8_t machines;
    bb_uk_voice v[BB_UK_VOICES];
} bb_user_kit;

/* One installed machine, as the main loop already knows it from the validated
 * pack directory (sampled) or the firmware (808). Zones stay immutable. */
typedef struct {
    uint32_t key;
    uint8_t engine;               /* BB_UK_ENGINE_808 / _SAMPLE */
    uint8_t id;                   /* catalogue id, or BB_SAMPLE_808_ID (254) */
    uint32_t voices;              /* native source-voice mask (bit v = BB_FDV v) */
    const bb_sample_zone *zones;
    uint8_t nz;
} bb_uk_machine;

/* Resolution result: what the audio owner plays and what the screen shows. */
enum { BB_UK_OK, BB_UK_INVALID, BB_UK_FULL_ZONES };
typedef struct {
    uint8_t engine[BB_UK_VOICES];  /* BB_UK_ENGINE_808 / _SAMPLE */
    uint8_t arg[BB_UK_VOICES];     /* 808: source voice; sample: composite zone note */
    uint8_t level[BB_UK_VOICES];   /* sampled velocity scale, 127 = unity (808: 127, pot carries level) */
    uint32_t substituted;          /* dest voices playing a substitute ("~" on screen) */
    uint32_t missing;              /* dest voices whose machine is not installed ("?") */
    uint32_t shared808;            /* dest voices sharing an earlier voice's 808 sound settings */
    uint8_t machines_missing;      /* bit per record machine index not installed */
    uint8_t nz, uses808, uses_sample;
    bb_sample_zone zones[BB_UK_ZONES];
    uint16_t pot_mask;             /* 808 sounds (D8S order) whose LEVEL/TUNE/DECAY are set */
    uint8_t pot[16][3];
} bb_uk_resolved;

/* Provided by bb_audio.c (cold, main loop): first source frame after target
 * where the decoded sample changes sign; the whole length if none. */
uint32_t bb_sample_zero_after(const bb_sample_zone *,uint32_t target);
uint32_t bb_uk_key(char prefix, const char *name, unsigned max);
/* Wire codec. encode refuses an invalid kit; decode refuses any invalid byte
 * and leaves *out unchanged on failure. */
int bb_uk_valid(const bb_user_kit *);
size_t bb_uk_encode(const bb_user_kit *, uint8_t out[BB_UK_RECORD_BYTES]);
int bb_uk_decode(const uint8_t in[BB_UK_RECORD_BYTES], bb_user_kit *out);
/* A user kit that is exactly machine m with its own defaults (BUILD starts here). */
int bb_uk_from_machine(bb_user_kit *, const bb_uk_machine *m, const char *name);
/* Set one voice's source; level/tune/decay reset to that source's defaults.
 * Adds the machine key to the kit's table when needed. */
int bb_uk_set_source(bb_user_kit *, unsigned voice, const bb_uk_machine *m, unsigned source_voice);
/* Defaults for a source: 808 = the 808 Reference pots, sampled = 127/64/127. */
void bb_uk_defaults(const bb_uk_machine *m, unsigned source_voice, bb_uk_voice *out);
/* Resolve against the installed machines. A missing machine plays the native 808
 * through bb_voice_fallback; a voice its machine lacks takes the same
 * substitution route the installed kits use. aux is OR-ed exactly as playback does. */
int bb_uk_resolve(const bb_user_kit *, const bb_uk_machine *installed, unsigned count,
                  uint32_t aux, bb_uk_resolved *out);
/* 1 when the kit is exactly the native 808 (every voice its own 808 voice; pots
 * free): it installs as the 808 and drum B keeps its CPU1 worker. */
int bb_uk_native808(const bb_uk_resolved *);
/* Placement guard (README §2.4): drum A takes any user kit. Drum B takes a
 * native-808 kit (CPU1) or a sampled-only kit (CPU0, as sampled kits today);
 * a kit mixing 808 and sampled voices would move B's 808 work onto CPU0. */
enum { BB_UK_PLACE_ANY, BB_UK_PLACE_A_ONLY };
unsigned bb_uk_placement(const bb_uk_resolved *);
/* Bank object "BBUB": 16-byte header + four records. Per-record CRC; a damaged
 * slot reads as EMPTY without hiding the others. */
size_t bb_uk_bank_encode(const bb_user_kit *const slots[BB_UK_SLOTS], uint8_t out[BB_UK_BANK_BYTES]);
unsigned bb_uk_bank_decode(const uint8_t in[BB_UK_BANK_BYTES], bb_user_kit slots[BB_UK_SLOTS]); /* used mask */
#endif
