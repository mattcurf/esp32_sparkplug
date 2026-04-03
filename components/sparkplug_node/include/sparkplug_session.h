#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"
#include "sensor_tmp36.h"

#ifdef __cplusplus
extern "C" {
#endif

enum {
    SPARKPLUG_SESSION_LAST_MESSAGE_MAX_LEN = 24,
};

typedef struct {
    bool mqtt_configured;
    bool mqtt_started;
    bool mqtt_connected;
    bool ncmd_subscribed;
    bool session_active;
    bool birth_complete;
    bool rebirth_pending;
    bool has_temperature;
    uint32_t mqtt_reconnect_count;
    uint64_t bdseq;
    uint8_t seq;
    float last_temperature_c;
    int64_t last_sample_ms;
    int64_t last_publish_ms;
    char last_message[SPARKPLUG_SESSION_LAST_MESSAGE_MAX_LEN + 1];
} sparkplug_session_status_t;

esp_err_t sparkplug_session_init(void);
esp_err_t sparkplug_session_start(void);
esp_err_t sparkplug_session_stop(void);
esp_err_t sparkplug_session_submit_temperature(const sensor_tmp36_reading_t *reading,
                                               int64_t sample_time_ms);
esp_err_t sparkplug_session_request_publish(void);
esp_err_t sparkplug_session_request_rebirth(void);
esp_err_t sparkplug_session_get_status(sparkplug_session_status_t *status);

#ifdef __cplusplus
}
#endif
