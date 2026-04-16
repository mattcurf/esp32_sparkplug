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

#define TIME_SYNC_MAX_SERVER_NAME_LEN 64
#define TIME_SYNC_DEFAULT_SERVER_NAME "pool.ntp.org"
#define TIME_SYNC_DEFAULT_VALID_EPOCH_SEC 1704067200LL

typedef struct {
    const char *server_name;
    int64_t minimum_valid_epoch_sec;
} time_sync_config_t;

#define TIME_SYNC_DEFAULT_CONFIG() { \
    .server_name = TIME_SYNC_DEFAULT_SERVER_NAME, \
    .minimum_valid_epoch_sec = TIME_SYNC_DEFAULT_VALID_EPOCH_SEC, \
}

typedef struct {
    bool started;
    bool valid;
    bool sync_in_progress;
    uint32_t sync_count;
    int64_t now_ms;
    int64_t last_sync_ms;
    char server_name[TIME_SYNC_MAX_SERVER_NAME_LEN];
} time_sync_status_t;

esp_err_t time_sync_start(const time_sync_config_t *config);
bool time_sync_is_valid(void);
esp_err_t time_sync_wait_ready(uint32_t timeout_ms);
int64_t time_sync_now_ms(void);
esp_err_t time_sync_get_status(time_sync_status_t *out_status);
esp_err_t time_sync_stop(void);

#ifdef __cplusplus
}
#endif
