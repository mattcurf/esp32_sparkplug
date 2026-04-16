#include "app_config.h"

#include <stdio.h>
#include <string.h>

#include "esp_log.h"
#include "esp_random.h"
#include "nvs.h"
#include "nvs_flash.h"

#define APP_CONFIG_NVS_NAMESPACE "app_config"
#define APP_CONFIG_NVS_KEY_NODE_ID "node_id"
#define APP_CONFIG_NODE_ID_MAX_LEN 32

static const char *TAG = "app_config";

static char s_node_id_buf[APP_CONFIG_NODE_ID_MAX_LEN + 1];

static app_config_t s_app_config = {
    .target_chip = "esp32",
    .wifi = {
        .ssid = "YOUR_WIFI_SSID",
        .password = "YOUR_WIFI_PASSWORD",
    },
    .sparkplug = {
        .broker_uri = "mqtt://broker.example.com:1883",
        .username = NULL,
        .password = NULL,
        .group_id = "home",
        .node_id = NULL,
        .topic_nbirth = "spBv1.0/home/NBIRTH/sensor",
        .topic_ndata = "spBv1.0/home/NDATA/sensor",
        .topic_ndeath = "spBv1.0/home/NDEATH/sensor",
        .topic_ncmd = "spBv1.0/home/NCMD/sensor",
        .metric_bdseq_name = "bdSeq",
        .metric_rebirth_name = "Node Control/Rebirth",
        .metric_temperature_name = "temperature_c",
        .metric_synthetic_name = "synthetic_sinewave",
        .metric_rebirth_alias = 1,
        .metric_temperature_alias = 2,
        .metric_synthetic_alias = 3,
        .primary_host_id = "host-1",
    },
    .sensor = {
        .gpio_num = GPIO_NUM_32,
        .unit = ADC_UNIT_1,
        .channel = ADC_CHANNEL_4,
        .attenuation = ADC_ATTEN_DB_12,
        .bitwidth = ADC_BITWIDTH_DEFAULT,
        .sample_interval_ms = 1000U,
        .multisample_count = 32U,
    },
    .temperature = {
        .engineering_unit = "Celsius",
        .publish_deadband_c = 0.2f,
        .max_publish_interval_ms = 60000U,
    },
    .time = {
        .sntp_server = "pool.ntp.org",
        .sync_wait_timeout_ms = 30000U,
        .sync_poll_interval_ms = 500U,
    },
    .status_led = {
        .gpio_num = GPIO_NUM_2,
        .active_high = true,
    },
};

const app_config_t *app_config_get(void)
{
    return &s_app_config;
}

static void app_config_generate_node_id(char *buf, size_t buf_size)
{
    uint32_t r = esp_random();
    snprintf(buf, buf_size, "node-%08lx", (unsigned long)r);
}

esp_err_t app_config_init(void)
{
    if (s_app_config.sparkplug.node_id != NULL) {
        return ESP_OK;
    }

    nvs_handle_t nvs;
    esp_err_t err = nvs_open(APP_CONFIG_NVS_NAMESPACE, NVS_READWRITE, &nvs);
    if (err != ESP_OK) {
        return err;
    }

    size_t len = sizeof(s_node_id_buf);
    err = nvs_get_str(nvs, APP_CONFIG_NVS_KEY_NODE_ID, s_node_id_buf, &len);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        app_config_generate_node_id(s_node_id_buf, sizeof(s_node_id_buf));
        err = nvs_set_str(nvs, APP_CONFIG_NVS_KEY_NODE_ID, s_node_id_buf);
        if (err == ESP_OK) {
            err = nvs_commit(nvs);
        }
        if (err != ESP_OK) {
            nvs_close(nvs);
            return err;
        }
        ESP_LOGI(TAG, "generated node_id: %s", s_node_id_buf);
    } else if (err != ESP_OK) {
        nvs_close(nvs);
        return err;
    } else {
        ESP_LOGI(TAG, "loaded node_id from NVS: %s", s_node_id_buf);
    }

    nvs_close(nvs);
    s_app_config.sparkplug.node_id = s_node_id_buf;
    return ESP_OK;
}

esp_err_t app_config_reset_node_id(void)
{
    nvs_handle_t nvs;
    esp_err_t err = nvs_open(APP_CONFIG_NVS_NAMESPACE, NVS_READWRITE, &nvs);
    if (err != ESP_OK) {
        return err;
    }

    err = nvs_erase_key(nvs, APP_CONFIG_NVS_KEY_NODE_ID);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        err = ESP_OK;
    }
    if (err == ESP_OK) {
        err = nvs_commit(nvs);
    }

    nvs_close(nvs);
    return err;
}
