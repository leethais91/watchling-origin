/*
 * BMI270 accelerometer on the core board, polled at 10 Hz and turned into a few posture
 * events. Only the accelerometer runs; the gyro and the chip's feature engine stay off.
 */
#pragma once

#include "esp_err.h"

typedef enum {
    IMU_EVENT_FACE_DOWN, /* lying on its screen */
    IMU_EVENT_FACE_UP,   /* no longer lying on its screen */
    IMU_EVENT_MOVED,     /* picked up, bumped or shaken */
    IMU_EVENT_TILT_LEFT, /* leaning to the robot's left, as the user faces it */
    IMU_EVENT_TILT_RIGHT,
    IMU_EVENT_LEVEL, /* back upright after a tilt */
} imu_event_t;

/* Runs on the esp_timer task. */
typedef void (*imu_event_cb_t)(imu_event_t event);

/*
 * Finds the BMI270 on the BSP I2C bus, uploads its config and starts polling.
 * ESP_ERR_NOT_FOUND when no BMI270 answers. Call after bsp_display_start().
 */
esp_err_t imu_start(imu_event_cb_t cb);
