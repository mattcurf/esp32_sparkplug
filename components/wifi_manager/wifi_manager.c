#include "wifi_manager.h"

#include <inttypes.h>
#include <stdio.h>
#include <string.h>

#include "app_config.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/portmacro.h"

#define WIFI_MANAGER_CONNECTED_BIT BIT0

static const char *TAG = "wifi_manager";

typedef struct {
    bool initialized;
    bool started;
    bool connected;
    bool has_ip;
    uint32_t reconnect_count;
    esp_ip4_addr_t ipv4_addr;
    esp_netif_t *sta_netif;
    EventGroupHandle_t event_group;
} wifi_manager_state_t;

static wifi_manager_state_t s_state;
static portMUX_TYPE s_state_lock = portMUX_INITIALIZER_UNLOCKED;

static void wifi_manager_event_handler(void *arg,
                                       esp_event_base_t event_base,
                                       int32_t event_id,
                                       void *event_data);

static void wifi_manager_cleanup_init_failure(void)
{
    if (s_state.sta_netif != NULL) {
        esp_netif_destroy_default_wifi(s_state.sta_netif);
    }

    esp_event_handler_unregister(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_manager_event_handler);
    esp_event_handler_unregister(IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_manager_event_handler);
    esp_wifi_deinit();

    if (s_state.event_group != NULL) {
        vEventGroupDelete(s_state.event_group);
    }

    memset(&s_state, 0, sizeof(s_state));
}

static const app_config_wifi_t *wifi_manager_config(void)
{
    return &app_config_get()->wifi;
}

static void wifi_manager_event_handler(void *arg,
                                       esp_event_base_t event_base,
                                       int32_t event_id,
                                       void *event_data)
{
    (void)arg;

    if (event_base == WIFI_EVENT) {
        switch (event_id) {
        case WIFI_EVENT_STA_START:
            if (!s_state.started) {
                return;
            }
            ESP_LOGI(TAG, "station started, connecting to %s", wifi_manager_config()->ssid);
            ESP_ERROR_CHECK_WITHOUT_ABORT(esp_wifi_connect());
            return;
        case WIFI_EVENT_STA_CONNECTED:
            portENTER_CRITICAL(&s_state_lock);
            s_state.connected = true;
            portEXIT_CRITICAL(&s_state_lock);
            ESP_LOGI(TAG, "connected to access point");
            return;
        case WIFI_EVENT_STA_DISCONNECTED: {
            const wifi_event_sta_disconnected_t *disconnected = event_data;
            uint32_t reconnect_count;

            portENTER_CRITICAL(&s_state_lock);
            s_state.connected = false;
            s_state.has_ip = false;
            s_state.ipv4_addr.addr = 0;
            s_state.reconnect_count++;
            reconnect_count = s_state.reconnect_count;
            portEXIT_CRITICAL(&s_state_lock);

            if (s_state.event_group != NULL) {
                xEventGroupClearBits(s_state.event_group, WIFI_MANAGER_CONNECTED_BIT);
            }

            ESP_LOGW(TAG,
                     "disconnected reason=%" PRIu8 ", reconnect_count=%" PRIu32,
                     disconnected != NULL ? disconnected->reason : 0,
                     reconnect_count);
            ESP_ERROR_CHECK_WITHOUT_ABORT(esp_wifi_connect());
            return;
        }
        default:
            return;
        }
    }

    if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        const ip_event_got_ip_t *got_ip = event_data;
        if (got_ip == NULL) {
            return;
        }

        portENTER_CRITICAL(&s_state_lock);
        s_state.connected = true;
        s_state.has_ip = true;
        s_state.ipv4_addr = got_ip->ip_info.ip;
        portEXIT_CRITICAL(&s_state_lock);

        if (s_state.event_group != NULL) {
            xEventGroupSetBits(s_state.event_group, WIFI_MANAGER_CONNECTED_BIT);
        }

        ESP_LOGI(TAG, "got IPv4 address: " IPSTR, IP2STR(&got_ip->ip_info.ip));
    }
}

esp_err_t wifi_manager_init(void)
{
    if (s_state.initialized) {
        return ESP_OK;
    }

    s_state.event_group = xEventGroupCreate();
    if (s_state.event_group == NULL) {
        return ESP_ERR_NO_MEM;
    }

    esp_err_t err;

    s_state.sta_netif = esp_netif_create_default_wifi_sta();
    if (s_state.sta_netif == NULL) {
        wifi_manager_cleanup_init_failure();
        return ESP_FAIL;
    }

    wifi_init_config_t wifi_init_config = WIFI_INIT_CONFIG_DEFAULT();
    err = esp_wifi_init(&wifi_init_config);
    if (err != ESP_OK) {
        wifi_manager_cleanup_init_failure();
        return err;
    }

    err = esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_manager_event_handler, NULL);
    if (err != ESP_OK) {
        wifi_manager_cleanup_init_failure();
        return err;
    }

    err = esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_manager_event_handler, NULL);
    if (err != ESP_OK) {
        wifi_manager_cleanup_init_failure();
        return err;
    }

    err = esp_wifi_set_storage(WIFI_STORAGE_RAM);
    if (err != ESP_OK) {
        wifi_manager_cleanup_init_failure();
        return err;
    }

    err = esp_wifi_set_mode(WIFI_MODE_STA);
    if (err != ESP_OK) {
        wifi_manager_cleanup_init_failure();
        return err;
    }

    wifi_config_t wifi_config = {
        .sta = {
            .threshold.authmode = WIFI_AUTH_OPEN,
            .pmf_cfg = {
                .capable = true,
                .required = false,
            },
        },
    };
    snprintf((char *)wifi_config.sta.ssid, sizeof(wifi_config.sta.ssid), "%s", wifi_manager_config()->ssid);
    snprintf((char *)wifi_config.sta.password,
             sizeof(wifi_config.sta.password),
             "%s",
             wifi_manager_config()->password);

    err = esp_wifi_set_config(WIFI_IF_STA, &wifi_config);
    if (err != ESP_OK) {
        wifi_manager_cleanup_init_failure();
        return err;
    }

    portENTER_CRITICAL(&s_state_lock);
    s_state.initialized = true;
    portEXIT_CRITICAL(&s_state_lock);

    ESP_LOGI(TAG, "initialized Wi-Fi STA for SSID %s", wifi_manager_config()->ssid);
    return ESP_OK;
}

esp_err_t wifi_manager_start(void)
{
    if (s_state.started) {
        return ESP_OK;
    }

    esp_err_t err = wifi_manager_init();
    if (err != ESP_OK) {
        return err;
    }

    portENTER_CRITICAL(&s_state_lock);
    s_state.started = true;
    s_state.connected = false;
    s_state.has_ip = false;
    s_state.ipv4_addr.addr = 0;
    portEXIT_CRITICAL(&s_state_lock);

    err = esp_wifi_start();
    if (err != ESP_OK) {
        portENTER_CRITICAL(&s_state_lock);
        s_state.started = false;
        portEXIT_CRITICAL(&s_state_lock);
        return err;
    }

    return ESP_OK;
}

bool wifi_manager_is_connected(void)
{
    bool connected;

    portENTER_CRITICAL(&s_state_lock);
    connected = s_state.connected;
    portEXIT_CRITICAL(&s_state_lock);

    return connected;
}

bool wifi_manager_has_ip(void)
{
    bool has_ip;

    portENTER_CRITICAL(&s_state_lock);
    has_ip = s_state.has_ip;
    portEXIT_CRITICAL(&s_state_lock);

    return has_ip;
}

esp_err_t wifi_manager_wait_connected(uint32_t timeout_ms)
{
    if (s_state.event_group == NULL || !s_state.started) {
        return ESP_ERR_INVALID_STATE;
    }

    TickType_t wait_ticks = timeout_ms == UINT32_MAX ? portMAX_DELAY : pdMS_TO_TICKS(timeout_ms);
    EventBits_t bits = xEventGroupWaitBits(s_state.event_group,
                                           WIFI_MANAGER_CONNECTED_BIT,
                                           pdFALSE,
                                           pdFALSE,
                                           wait_ticks);
    return (bits & WIFI_MANAGER_CONNECTED_BIT) ? ESP_OK : ESP_ERR_TIMEOUT;
}

esp_err_t wifi_manager_get_status(wifi_manager_status_t *status)
{
    if (status == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    memset(status, 0, sizeof(*status));
    snprintf(status->ssid, sizeof(status->ssid), "%s", wifi_manager_config()->ssid);

    portENTER_CRITICAL(&s_state_lock);
    status->initialized = s_state.initialized;
    status->started = s_state.started;
    status->connected = s_state.connected;
    status->has_ip = s_state.has_ip;
    status->reconnect_count = s_state.reconnect_count;
    status->ipv4_addr = s_state.ipv4_addr;
    portEXIT_CRITICAL(&s_state_lock);

    if (status->connected) {
        wifi_ap_record_t ap_info = {0};
        if (esp_wifi_sta_get_ap_info(&ap_info) == ESP_OK) {
            status->rssi_dbm = ap_info.rssi;
        }
    }

    return ESP_OK;
}
