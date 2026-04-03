#include "app_config.h"

static const app_config_t s_app_config = {
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
        .node_id = "sensor",
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
};

const app_config_t *app_config_get(void)
{
    return &s_app_config;
}
