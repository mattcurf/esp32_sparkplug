/*
 * SPDX-FileCopyrightText: 2026 Matt Curfman
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    bool valid;
    uint32_t sample_count;
    int32_t raw_average;
    int32_t calibrated_mv;
    float temperature_c;
} sensor_tmp36_reading_t;

esp_err_t sensor_tmp36_init(void);
esp_err_t sensor_tmp36_deinit(void);
bool sensor_tmp36_is_initialized(void);
esp_err_t sensor_tmp36_read(sensor_tmp36_reading_t *reading_out);
esp_err_t sensor_tmp36_get_last_reading(sensor_tmp36_reading_t *reading_out);
esp_err_t sensor_tmp36_read_mv(int *millivolts_out);
esp_err_t sensor_tmp36_read_celsius(float *temperature_c_out);
float sensor_tmp36_mv_to_celsius(int millivolts);

#ifdef __cplusplus
}
#endif
