/*
 * BMI270 accelerometer on the core board, polled at 10 Hz and turned into a few posture
 * events. Only the accelerometer runs; the gyro and the chip's feature engine stay off.
 */
#pragma once

#include <stdbool.h>
#include "esp_err.h"

typedef enum {
    IMU_EVENT_FACE_DOWN, /* lying on its screen */
    IMU_EVENT_FACE_UP,   /* no longer lying on its screen */
    IMU_EVENT_MOVED,     /* picked up, bumped or shaken */
    IMU_EVENT_TILT_LEFT, /* leaning to the robot's left, as the user faces it */
    IMU_EVENT_TILT_RIGHT,
    IMU_EVENT_LEVEL,    /* back upright after a tilt */
    IMU_EVENT_SHAKEN,   /* shaken hard for a moment */
    IMU_EVENT_ON_BACK,  /* lying on its back, screen to the ceiling */
    IMU_EVENT_OFF_BACK, /* no longer on its back */
} imu_event_t;

/* Runs on the esp_timer task. */
typedef void (*imu_event_cb_t)(imu_event_t event);

/*
 * The smoothed gravity vector in the robot's frame, readable from any task: `lateral_mg`
 * positive toward the robot's right as the user faces it, `down_mg` toward the bottom of the
 * screen (about +1000 standing upright, -1000 upside down). False until the first sample.
 */
bool imu_gravity(int *lateral_mg, int *down_mg);

/*
 * Finds the BMI270 on the BSP I2C bus, uploads its config and starts polling.
 * ESP_ERR_NOT_FOUND when no BMI270 answers. Call after bsp_display_start().
 */
esp_err_t imu_start(imu_event_cb_t cb);
