#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

enum {
    APP_CONSOLE_WIFI_SSID_MAX_LEN = 32,
    APP_CONSOLE_IP_ADDR_MAX_LEN = 48,
    APP_CONSOLE_URI_MAX_LEN = 96,
    APP_CONSOLE_MESSAGE_NAME_MAX_LEN = 24,
};

typedef struct {
    bool valid;
    uint32_t sample_count;
    int32_t raw_average;
    int32_t calibrated_mv;
    float temperature_c;
    int64_t last_sample_ms;
} app_console_sensor_status_t;

typedef struct {
    bool initialized;
    bool connected;
    bool has_ip;
    char ssid[APP_CONSOLE_WIFI_SSID_MAX_LEN + 1];
    char ip_address[APP_CONSOLE_IP_ADDR_MAX_LEN + 1];
    int32_t rssi_dbm;
    uint32_t reconnect_count;
} app_console_wifi_status_t;

typedef struct {
    bool initialized;
    bool synchronized;
    int64_t unix_time_ms;
    int64_t last_sync_ms;
} app_console_time_status_t;

typedef struct {
    bool configured;
    bool started;
    bool connected;
    bool ncmd_subscribed;
    char broker_uri[APP_CONSOLE_URI_MAX_LEN + 1];
    uint32_t reconnect_count;
} app_console_mqtt_status_t;

typedef struct {
    bool session_active;
    bool birth_complete;
    bool rebirth_pending;
    bool disconnect_sim_enabled;
    bool disconnect_sim_active;
    int64_t disconnect_sim_interval_ms;
    int64_t disconnect_sim_duration_ms;
    uint8_t bdseq;
    uint8_t seq;
    int64_t last_publish_ms;
    char last_message[APP_CONSOLE_MESSAGE_NAME_MAX_LEN + 1];
} app_console_sparkplug_status_t;

typedef esp_err_t (*app_console_get_sensor_status_fn)(app_console_sensor_status_t *status,
                                                      void *ctx);
typedef esp_err_t (*app_console_get_wifi_status_fn)(app_console_wifi_status_t *status,
                                                    void *ctx);
typedef esp_err_t (*app_console_get_time_status_fn)(app_console_time_status_t *status,
                                                    void *ctx);
typedef esp_err_t (*app_console_get_mqtt_status_fn)(app_console_mqtt_status_t *status,
                                                    void *ctx);
typedef esp_err_t (*app_console_get_sparkplug_status_fn)(app_console_sparkplug_status_t *status,
                                                         void *ctx);
typedef esp_err_t (*app_console_action_fn)(void *ctx);
typedef esp_err_t (*app_console_set_enabled_fn)(bool enabled, void *ctx);

typedef struct {
    app_console_get_sensor_status_fn get_sensor_status;
    void *sensor_status_ctx;
    app_console_get_wifi_status_fn get_wifi_status;
    void *wifi_status_ctx;
    app_console_get_time_status_fn get_time_status;
    void *time_status_ctx;
    app_console_get_mqtt_status_fn get_mqtt_status;
    void *mqtt_status_ctx;
    app_console_get_sparkplug_status_fn get_sparkplug_status;
    void *sparkplug_status_ctx;
    app_console_action_fn request_publish;
    void *publish_ctx;
    app_console_action_fn request_rebirth;
    void *rebirth_ctx;
    app_console_set_enabled_fn set_disconnect_sim_enabled;
    void *disconnect_sim_ctx;
} app_console_providers_t;

/*
 * Initialize the UART-backed ESP-IDF REPL and register the built-in command set.
 *
 * This call is idempotent and does not start the REPL task yet, which keeps console
 * startup optional until the coordinator decides the system is ready.
 */
esp_err_t app_console_init(void);

/* Start the REPL task after init. Safe to call more than once. */
esp_err_t app_console_start(void);

/* Return whether the REPL has been started already. */
bool app_console_is_started(void);

/*
 * Replace the current status/action providers. Pass NULL to clear all providers.
 *
 * Commands remain safe to invoke before providers are attached and will report
 * that runtime wiring is still pending.
 */
esp_err_t app_console_set_providers(const app_console_providers_t *providers);

#ifdef __cplusplus
}
#endif
