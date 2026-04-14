#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"
#include "esp_netif_ip_addr.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    bool initialized;
    bool started;
    bool connected;
    bool has_ip;
    uint32_t reconnect_count;
    int32_t rssi_dbm;
    char ssid[33];
    esp_ip4_addr_t ipv4_addr;
} wifi_manager_status_t;

esp_err_t wifi_manager_init(void);
esp_err_t wifi_manager_start(void);
bool wifi_manager_is_connected(void);
bool wifi_manager_has_ip(void);
esp_err_t wifi_manager_wait_connected(uint32_t timeout_ms);
esp_err_t wifi_manager_get_status(wifi_manager_status_t *status);
esp_err_t wifi_manager_simulate_disconnect(uint32_t duration_ms);
esp_err_t wifi_manager_resume_simulated_disconnect(void);

#ifdef __cplusplus
}
#endif
