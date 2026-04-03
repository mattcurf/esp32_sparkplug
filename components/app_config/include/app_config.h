#pragma once

#include <stdint.h>

#include "driver/gpio.h"
#include "esp_adc/adc_oneshot.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    const char *ssid;
    const char *password;
} app_config_wifi_t;

typedef struct {
    const char *broker_uri;
    const char *username;
    const char *password;
    const char *group_id;
    const char *node_id;
    const char *topic_nbirth;
    const char *topic_ndata;
    const char *topic_ndeath;
    const char *topic_ncmd;
    const char *metric_bdseq_name;
    const char *metric_rebirth_name;
    const char *metric_temperature_name;
    const char *metric_synthetic_name;
    uint16_t metric_rebirth_alias;
    uint16_t metric_temperature_alias;
    uint16_t metric_synthetic_alias;
} app_config_sparkplug_t;

typedef struct {
    gpio_num_t gpio_num;
    adc_unit_t unit;
    adc_channel_t channel;
    adc_atten_t attenuation;
    adc_bitwidth_t bitwidth;
    uint32_t sample_interval_ms;
    uint32_t multisample_count;
} app_config_sensor_t;

typedef struct {
    const char *engineering_unit;
    float publish_deadband_c;
    uint32_t max_publish_interval_ms;
} app_config_temperature_t;

typedef struct {
    const char *sntp_server;
    uint32_t sync_wait_timeout_ms;
    uint32_t sync_poll_interval_ms;
} app_config_time_t;

typedef struct {
    const char *target_chip;
    app_config_wifi_t wifi;
    app_config_sparkplug_t sparkplug;
    app_config_sensor_t sensor;
    app_config_temperature_t temperature;
    app_config_time_t time;
} app_config_t;

const app_config_t *app_config_get(void);

#ifdef __cplusplus
}
#endif
