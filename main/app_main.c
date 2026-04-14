#include <stdbool.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "app_config.h"
#include "app_console.h"
#include "driver/gpio.h"
#include "esp_check.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_netif_ip_addr.h"
#include "nvs_flash.h"
#include "sensor_tmp36.h"
#include "sparkplug_session.h"
#include "time_sync.h"
#include "wifi_manager.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/portmacro.h"

#define STATUS_LED_TASK_STACK_SIZE 2048
#define STATUS_LED_TASK_PRIORITY 4
#define STATUS_LED_TASK_INTERVAL_MS 50U
#define STATUS_LED_PULSE_PERIOD_MS 500U
#define STATUS_LED_PULSE_ON_MS 100U
#define SENSOR_TASK_STACK_SIZE 4096
#define SENSOR_TASK_PRIORITY 5

static const char *TAG = "app_main";

typedef struct {
    bool valid;
    sensor_tmp36_reading_t reading;
    int64_t sample_time_ms;
} app_sensor_snapshot_t;

typedef enum {
    APP_STATUS_LED_MODE_OFF = 0,
    APP_STATUS_LED_MODE_PULSE,
    APP_STATUS_LED_MODE_ON,
} app_status_led_mode_t;

static app_sensor_snapshot_t s_sensor_snapshot;
static portMUX_TYPE s_sensor_snapshot_lock = portMUX_INITIALIZER_UNLOCKED;
static bool s_status_led_mqtt_connected;
static portMUX_TYPE s_status_led_state_lock = portMUX_INITIALIZER_UNLOCKED;

static void app_set_status_led(bool on)
{
    const app_config_status_led_t *status_led = &app_config_get()->status_led;
    int level = on ? 1 : 0;

    if (!status_led->active_high) {
        level = !level;
    }

    ESP_ERROR_CHECK(gpio_set_level(status_led->gpio_num, level));
}

static bool app_status_led_mqtt_connected(void)
{
    bool mqtt_connected;

    portENTER_CRITICAL(&s_status_led_state_lock);
    mqtt_connected = s_status_led_mqtt_connected;
    portEXIT_CRITICAL(&s_status_led_state_lock);
    return mqtt_connected;
}

static void app_handle_sparkplug_status_update(const sparkplug_session_status_t *status, void *ctx)
{
    (void)ctx;
    if (status == NULL) {
        return;
    }

    portENTER_CRITICAL(&s_status_led_state_lock);
    s_status_led_mqtt_connected = status->mqtt_connected;
    portEXIT_CRITICAL(&s_status_led_state_lock);
}

static void app_initialize_status_led(void)
{
    const app_config_status_led_t *status_led = &app_config_get()->status_led;
    const gpio_config_t io_config = {
        .pin_bit_mask = 1ULL << (uint32_t)status_led->gpio_num,
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };

    ESP_ERROR_CHECK(gpio_config(&io_config));
    app_set_status_led(false);
    ESP_LOGI(TAG,
             "status LED configured on GPIO %d active_%s",
             status_led->gpio_num,
             status_led->active_high ? "high" : "low");
}

static void app_status_led_task(void *arg)
{
    const TickType_t interval_ticks = pdMS_TO_TICKS(STATUS_LED_TASK_INTERVAL_MS);
    TickType_t last_wake_time = xTaskGetTickCount();
    app_status_led_mode_t mode = APP_STATUS_LED_MODE_OFF;
    app_status_led_mode_t previous_mode = APP_STATUS_LED_MODE_OFF;
    uint32_t pulse_elapsed_ms = 0;
    (void)arg;

    while (true) {
        if (app_status_led_mqtt_connected()) {
            mode = APP_STATUS_LED_MODE_ON;
        } else if (wifi_manager_has_ip()) {
            mode = APP_STATUS_LED_MODE_PULSE;
        } else {
            mode = APP_STATUS_LED_MODE_OFF;
        }

        if (mode != previous_mode) {
            pulse_elapsed_ms = 0;
            previous_mode = mode;
        }

        switch (mode) {
        case APP_STATUS_LED_MODE_ON:
            app_set_status_led(true);
            break;
        case APP_STATUS_LED_MODE_PULSE:
            app_set_status_led(pulse_elapsed_ms < STATUS_LED_PULSE_ON_MS);
            pulse_elapsed_ms += STATUS_LED_TASK_INTERVAL_MS;
            if (pulse_elapsed_ms >= STATUS_LED_PULSE_PERIOD_MS) {
                pulse_elapsed_ms = 0;
            }
            break;
        case APP_STATUS_LED_MODE_OFF:
        default:
            app_set_status_led(false);
            break;
        }

        vTaskDelayUntil(&last_wake_time, interval_ticks);
    }
}

static void app_store_sensor_snapshot(const sensor_tmp36_reading_t *reading, int64_t sample_time_ms)
{
    portENTER_CRITICAL(&s_sensor_snapshot_lock);
    s_sensor_snapshot.valid = reading != NULL && reading->valid;
    if (reading != NULL) {
        s_sensor_snapshot.reading = *reading;
    } else {
        memset(&s_sensor_snapshot.reading, 0, sizeof(s_sensor_snapshot.reading));
    }
    s_sensor_snapshot.sample_time_ms = sample_time_ms;
    portEXIT_CRITICAL(&s_sensor_snapshot_lock);
}

static esp_err_t app_get_sensor_status(app_console_sensor_status_t *status, void *ctx)
{
    app_sensor_snapshot_t snapshot = {0};
    (void)ctx;

    if (status == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    memset(status, 0, sizeof(*status));
    portENTER_CRITICAL(&s_sensor_snapshot_lock);
    snapshot = s_sensor_snapshot;
    portEXIT_CRITICAL(&s_sensor_snapshot_lock);

    if (!snapshot.valid) {
        return ESP_OK;
    }

    status->valid = true;
    status->sample_count = snapshot.reading.sample_count;
    status->raw_average = snapshot.reading.raw_average;
    status->calibrated_mv = snapshot.reading.calibrated_mv;
    status->temperature_c = snapshot.reading.temperature_c;
    status->last_sample_ms = snapshot.sample_time_ms;
    return ESP_OK;
}

static esp_err_t app_get_wifi_status(app_console_wifi_status_t *status, void *ctx)
{
    wifi_manager_status_t wifi_status = {0};
    (void)ctx;

    if (status == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    ESP_RETURN_ON_ERROR(wifi_manager_get_status(&wifi_status), TAG, "failed to query wifi status");

    memset(status, 0, sizeof(*status));
    status->initialized = wifi_status.initialized;
    status->connected = wifi_status.connected;
    status->has_ip = wifi_status.has_ip;
    status->rssi_dbm = wifi_status.rssi_dbm;
    status->reconnect_count = wifi_status.reconnect_count;
    snprintf(status->ssid, sizeof(status->ssid), "%s", wifi_status.ssid);
    if (wifi_status.has_ip) {
        snprintf(status->ip_address,
                 sizeof(status->ip_address),
                 IPSTR,
                 IP2STR(&wifi_status.ipv4_addr));
    }
    return ESP_OK;
}

static esp_err_t app_get_time_status(app_console_time_status_t *status, void *ctx)
{
    time_sync_status_t time_status = {0};
    (void)ctx;

    if (status == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    ESP_RETURN_ON_ERROR(time_sync_get_status(&time_status), TAG, "failed to query time status");

    memset(status, 0, sizeof(*status));
    status->initialized = time_status.started;
    status->synchronized = time_status.valid;
    status->unix_time_ms = time_status.now_ms;
    status->last_sync_ms = time_status.last_sync_ms;
    return ESP_OK;
}

static esp_err_t app_get_mqtt_status(app_console_mqtt_status_t *status, void *ctx)
{
    sparkplug_session_status_t session_status = {0};
    (void)ctx;

    if (status == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    ESP_RETURN_ON_ERROR(sparkplug_session_get_status(&session_status), TAG, "failed to query sparkplug session status");

    memset(status, 0, sizeof(*status));
    status->configured = session_status.mqtt_configured;
    status->started = session_status.mqtt_started;
    status->connected = session_status.mqtt_connected;
    status->ncmd_subscribed = session_status.ncmd_subscribed;
    status->reconnect_count = session_status.mqtt_reconnect_count;
    snprintf(status->broker_uri, sizeof(status->broker_uri), "%s", app_config_get()->sparkplug.broker_uri);
    return ESP_OK;
}

static esp_err_t app_get_sparkplug_status(app_console_sparkplug_status_t *status, void *ctx)
{
    sparkplug_session_status_t session_status = {0};
    (void)ctx;

    if (status == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    ESP_RETURN_ON_ERROR(sparkplug_session_get_status(&session_status), TAG, "failed to query sparkplug session status");

    memset(status, 0, sizeof(*status));
    status->session_active = session_status.session_active;
    status->birth_complete = session_status.birth_complete;
    status->rebirth_pending = session_status.rebirth_pending;
    status->disconnect_sim_enabled = session_status.disconnect_sim_enabled;
    status->disconnect_sim_active = session_status.disconnect_sim_active;
    status->bdseq = (uint8_t)(session_status.bdseq & 0xFFU);
    status->seq = session_status.seq;
    status->last_publish_ms = session_status.last_publish_ms;
    snprintf(status->last_message, sizeof(status->last_message), "%s", session_status.last_message);
    return ESP_OK;
}

static esp_err_t app_request_publish(void *ctx)
{
    (void)ctx;
    return sparkplug_session_request_publish();
}

static esp_err_t app_request_rebirth(void *ctx)
{
    (void)ctx;
    return sparkplug_session_request_rebirth();
}

static esp_err_t app_set_disconnect_sim_enabled(bool enabled, void *ctx)
{
    (void)ctx;
    return sparkplug_session_set_disconnect_sim_enabled(enabled);
}

static void app_sensor_task(void *arg)
{
    const TickType_t interval_ticks = pdMS_TO_TICKS(app_config_get()->sensor.sample_interval_ms);
    TickType_t last_wake_time = xTaskGetTickCount();
    (void)arg;

    while (true) {
        sensor_tmp36_reading_t reading = {0};
        esp_err_t err = sensor_tmp36_read(&reading);
        if (err == ESP_OK) {
            int64_t sample_time_ms = time_sync_now_ms();
            app_store_sensor_snapshot(&reading, sample_time_ms);
            err = sparkplug_session_submit_temperature(&reading, sample_time_ms);
            if (err != ESP_OK) {
                ESP_LOGW(TAG, "failed to queue sensor sample: %s", esp_err_to_name(err));
            }
        } else {
            ESP_LOGE(TAG, "sensor read failed: %s", esp_err_to_name(err));
        }

        vTaskDelayUntil(&last_wake_time, interval_ticks);
    }
}

static void app_initialize_console(void)
{
    const app_console_providers_t providers = {
        .get_sensor_status = app_get_sensor_status,
        .get_wifi_status = app_get_wifi_status,
        .get_time_status = app_get_time_status,
        .get_mqtt_status = app_get_mqtt_status,
        .get_sparkplug_status = app_get_sparkplug_status,
        .request_publish = app_request_publish,
        .request_rebirth = app_request_rebirth,
        .set_disconnect_sim_enabled = app_set_disconnect_sim_enabled,
    };

    ESP_ERROR_CHECK(app_console_init());
    ESP_ERROR_CHECK(app_console_set_providers(&providers));
    ESP_ERROR_CHECK(app_console_start());
}

static void app_initialize_nvs(void)
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);
}

void app_main(void)
{
    const app_config_t *config = app_config_get();
    const time_sync_config_t time_config = {
        .server_name = config->time.sntp_server,
        .minimum_valid_epoch_sec = TIME_SYNC_DEFAULT_VALID_EPOCH_SEC,
    };
    sensor_tmp36_reading_t initial_reading = {0};
    int64_t initial_sample_time_ms = 0;

    app_initialize_nvs();
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    app_initialize_status_led();
    if (xTaskCreate(app_status_led_task,
                    "status_led",
                    STATUS_LED_TASK_STACK_SIZE,
                    NULL,
                    STATUS_LED_TASK_PRIORITY,
                    NULL) != pdPASS) {
        ESP_LOGE(TAG, "failed to start status LED task");
        abort();
    }

    ESP_ERROR_CHECK(wifi_manager_init());
    ESP_ERROR_CHECK(wifi_manager_start());
    ESP_ERROR_CHECK(wifi_manager_wait_connected(UINT32_MAX));

    ESP_ERROR_CHECK(time_sync_start(&time_config));
    ESP_ERROR_CHECK(time_sync_wait_ready(config->time.sync_wait_timeout_ms));

    ESP_ERROR_CHECK(sensor_tmp36_init());
    ESP_ERROR_CHECK(sensor_tmp36_read(&initial_reading));
    initial_sample_time_ms = time_sync_now_ms();
    app_store_sensor_snapshot(&initial_reading, initial_sample_time_ms);

    ESP_ERROR_CHECK(sparkplug_session_init());
    ESP_ERROR_CHECK(sparkplug_session_set_status_callback(app_handle_sparkplug_status_update, NULL));
    ESP_ERROR_CHECK(sparkplug_session_submit_temperature(&initial_reading, initial_sample_time_ms));
    ESP_ERROR_CHECK(sparkplug_session_start());

    app_initialize_console();

    if (xTaskCreate(app_sensor_task,
                    "sensor_task",
                    SENSOR_TASK_STACK_SIZE,
                    NULL,
                    SENSOR_TASK_PRIORITY,
                    NULL) != pdPASS) {
        ESP_LOGE(TAG, "failed to start sensor task");
        abort();
    }

    ESP_LOGI(TAG, "Sparkplug temperature node started");
}
