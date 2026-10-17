/*
 * Speaker output on the ES8311: short synthesized chimes plus speech clips streamed by
 * the bridge (TTS runs on the host). One playback task plays everything in order, so a
 * chime and the phrase that follows it never overlap.
 */
#pragma once

#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"

typedef enum {
    AUDIO_CHIME_CONFIRM,   /* permission request: two rising notes */
    AUDIO_CHIME_DONE,      /* task finished: major arpeggio */
    AUDIO_CHIME_ATTENTION, /* waiting for input: repeated note */
    AUDIO_CHIME_ERROR,     /* error: falling notes */
    AUDIO_CHIME_COUNT,
} audio_chime_t;

/* Opens the speaker codec, renders the chimes and starts playback. Call after bsp_display_start(). */
esp_err_t audio_init(void);

/* Queues a chime; non-blocking, safe from any task. No-op if audio_init() failed. */
void audio_play_chime(audio_chime_t chime);

/*
 * Speech clip assembly (16 kHz mono s16le). Call these from one task only (the host link
 * RX task): begin allocates, data copies chunks in at their offset, end queues playback.
 * A new begin discards an unfinished clip; chunks for another id are ignored.
 */
void audio_speech_begin(uint16_t id, size_t bytes);
void audio_speech_data(uint16_t id, uint32_t offset, const uint8_t *pcm, size_t len);
void audio_speech_end(uint16_t id);
