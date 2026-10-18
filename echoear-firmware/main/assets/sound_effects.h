/*
 * UI sound effects in flash (16 kHz mono s16le), generated with ElevenLabs by
 * tools/sound_effects.py. Source WAVs live in assets/sounds/.
 */
#pragma once

#include <stddef.h>
#include <stdint.h>

typedef struct {
    const int16_t *pcm;
    size_t samples;
} sound_effect_t;

extern const sound_effect_t SFX_BOOT;
extern const sound_effect_t SFX_WORKING;
extern const sound_effect_t SFX_DONE;
extern const sound_effect_t SFX_ATTENTION;
extern const sound_effect_t SFX_ERROR;
