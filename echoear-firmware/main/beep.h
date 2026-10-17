/*
 * Short notification tone on the ES8311 speaker. Placeholder for the Phase 4 audio
 * pipeline (prerecorded clips + bridge TTS).
 */
#pragma once

#include "esp_err.h"

/* Opens the speaker codec and starts the playback task. Call after bsp_display_start(). */
esp_err_t beep_init(void);

/* Queues one beep; non-blocking, safe from any task. No-op if beep_init() failed. */
void beep_play(void);
