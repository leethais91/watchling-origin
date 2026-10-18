#pragma once

#include <stdint.h>

#define BMI270_CONFIG_SIZE 8192

/* Bosch's BMI270 config blob (BSD-3-Clause, see bmi270_config.c). */
extern const uint8_t bmi270_config_file[BMI270_CONFIG_SIZE];
