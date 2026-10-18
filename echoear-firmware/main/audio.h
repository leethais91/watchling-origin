/*
 * Speaker output on the ES8311: short UI sound effects from flash plus speech clips streamed by
 * the bridge (TTS runs on the host). One playback task plays everything in order, so a
 * chime and the phrase that follows it never overlap.
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"

typedef enum {
    AUDIO_CHIME_BOOT,      /* device started */
    AUDIO_CHIME_WORKING,   /* an agent started a task */
    AUDIO_CHIME_DONE,      /* task finished; also the volume sample in settings */
    AUDIO_CHIME_ATTENTION, /* permission request or waiting for input: the user is needed */
    AUDIO_CHIME_ERROR,     /* error */
    AUDIO_CHIME_COUNT,
} audio_chime_t;

/* Opens the speaker codec and starts playback. Call after bsp_display_start(). */
esp_err_t audio_init(void);

/* Speaker volume 0..100 %. Safe before audio_init() (applied when the codec opens). */
void audio_set_volume(int percent);

/* Speech clips are dropped while disabled; chimes keep playing. Enabled at boot. */
void audio_set_speech_enabled(bool enabled);

/* Quiet mode: chimes and speech clips are both dropped while muted. Safe from any task. */
void audio_set_muted(bool muted);

/* Queues a chime; non-blocking, safe from any task. No-op if audio_init() failed or muted. */
void audio_play_chime(audio_chime_t chime);

/*
 * Runs on the playback task when a speech clip with a non-zero tag starts playing (after any
 * chime queued before it), with the clip's length. Lets the caller sync the face to the clip.
 */
typedef void (*audio_speech_start_cb_t)(uint32_t tag, uint32_t duration_ms);
void audio_set_speech_start_cb(audio_speech_start_cb_t cb);

/*
 * Speech clip assembly (16 kHz mono s16le). Call these from one task only (the host link
 * RX task): begin allocates, data copies chunks in at their offset, end queues playback.
 * A new begin discards an unfinished clip; chunks for another id are ignored. `tag` (0 = none)
 * is handed to the speech start callback.
 */
void audio_speech_begin(uint16_t id, size_t bytes, uint32_t tag);
void audio_speech_data(uint16_t id, uint32_t offset, const uint8_t *pcm, size_t len);
void audio_speech_end(uint16_t id);
