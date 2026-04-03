#include "time_sync.h"

#include <stddef.h>
#include <string.h>
#include <sys/time.h>
#include <time.h>

#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/semphr.h"
#include "esp_log.h"
#include "esp_sntp.h"

#define TIME_SYNC_READY_BIT BIT0

static const char *TAG = "time_sync";

typedef struct {
    bool started;
    uint32_t sync_count;
    int64_t minimum_valid_epoch_sec;
    int64_t last_sync_ms;
    char server_name[TIME_SYNC_MAX_SERVER_NAME_LEN];
    EventGroupHandle_t event_group;
    SemaphoreHandle_t lock;
} time_sync_context_t;

static time_sync_context_t s_ctx;

static esp_err_t time_sync_lock(void)
{
    if (s_ctx.lock == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    if (xSemaphoreTake(s_ctx.lock, portMAX_DELAY) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }

    return ESP_OK;
}

static void time_sync_unlock(void)
{
    if (s_ctx.lock != NULL) {
        xSemaphoreGive(s_ctx.lock);
    }
}

static bool time_sync_is_valid_epoch_locked(time_t now_sec)
{
    return now_sec >= (time_t)s_ctx.minimum_valid_epoch_sec;
}

static void time_sync_notification_cb(struct timeval *tv)
{
    char time_buffer[32] = { 0 };
    struct tm utc_tm = { 0 };
    time_t now_sec = tv != NULL ? tv->tv_sec : 0;

    if (now_sec > 0) {
        gmtime_r(&now_sec, &utc_tm);
        strftime(time_buffer, sizeof(time_buffer), "%Y-%m-%dT%H:%M:%SZ", &utc_tm);
    }

    if (time_sync_lock() != ESP_OK) {
        ESP_LOGE(TAG, "Failed to lock time sync state in callback");
        return;
    }

    if (!s_ctx.started) {
        time_sync_unlock();
        return;
    }

    s_ctx.sync_count++;
    s_ctx.last_sync_ms = tv != NULL
                             ? ((int64_t)tv->tv_sec * 1000LL) + ((int64_t)tv->tv_usec / 1000LL)
                             : 0;
    if (time_sync_is_valid_epoch_locked(now_sec)) {
        xEventGroupSetBits(s_ctx.event_group, TIME_SYNC_READY_BIT);
    }
    time_sync_unlock();

    if (time_buffer[0] != '\0') {
        ESP_LOGI(TAG, "Synchronized SNTP time: %s", time_buffer);
    } else {
        ESP_LOGI(TAG, "Synchronized SNTP time");
    }
}

esp_err_t time_sync_start(const time_sync_config_t *config)
{
    if (config == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    const char *server_name = config->server_name != NULL ? config->server_name : TIME_SYNC_DEFAULT_SERVER_NAME;
    size_t server_name_len = strnlen(server_name, TIME_SYNC_MAX_SERVER_NAME_LEN);
    if (server_name_len == 0U || server_name_len >= TIME_SYNC_MAX_SERVER_NAME_LEN) {
        return ESP_ERR_INVALID_ARG;
    }
    if (config->minimum_valid_epoch_sec <= 0) {
        return ESP_ERR_INVALID_ARG;
    }
    if (s_ctx.started) {
        return ESP_ERR_INVALID_STATE;
    }

    if (s_ctx.lock == NULL) {
        s_ctx.lock = xSemaphoreCreateMutex();
    }
    if (s_ctx.event_group == NULL) {
        s_ctx.event_group = xEventGroupCreate();
    }
    if (s_ctx.lock == NULL || s_ctx.event_group == NULL) {
        return ESP_ERR_NO_MEM;
    }

    if (time_sync_lock() != ESP_OK) {
        return ESP_ERR_INVALID_STATE;
    }

    memset(s_ctx.server_name, 0, sizeof(s_ctx.server_name));
    strlcpy(s_ctx.server_name, server_name, sizeof(s_ctx.server_name));
    s_ctx.minimum_valid_epoch_sec = config->minimum_valid_epoch_sec;
    s_ctx.last_sync_ms = 0;
    s_ctx.sync_count = 0;
    s_ctx.started = true;
    xEventGroupClearBits(s_ctx.event_group, TIME_SYNC_READY_BIT);
    time_sync_unlock();

    sntp_set_time_sync_notification_cb(time_sync_notification_cb);
    esp_sntp_setoperatingmode(ESP_SNTP_OPMODE_POLL);
    esp_sntp_setservername(0, s_ctx.server_name);
    esp_sntp_init();

    if (time_sync_is_valid()) {
        xEventGroupSetBits(s_ctx.event_group, TIME_SYNC_READY_BIT);
        ESP_LOGI(TAG, "System time already valid before SNTP sync completion");
    }

    ESP_LOGI(TAG, "Started SNTP using server '%s'", s_ctx.server_name);
    return ESP_OK;
}

bool time_sync_is_valid(void)
{
    struct timeval now = { 0 };
    gettimeofday(&now, NULL);

    if (time_sync_lock() != ESP_OK) {
        return false;
    }

    bool valid = time_sync_is_valid_epoch_locked(now.tv_sec);
    time_sync_unlock();
    return valid;
}

esp_err_t time_sync_wait_ready(uint32_t timeout_ms)
{
    if (!s_ctx.started) {
        return ESP_ERR_INVALID_STATE;
    }

    if (time_sync_is_valid()) {
        return ESP_OK;
    }

    TickType_t timeout_ticks = timeout_ms == UINT32_MAX ? portMAX_DELAY : pdMS_TO_TICKS(timeout_ms);
    TickType_t start_ticks = xTaskGetTickCount();

    while (true) {
        TickType_t wait_ticks = pdMS_TO_TICKS(250);
        if (timeout_ticks != portMAX_DELAY) {
            TickType_t elapsed = xTaskGetTickCount() - start_ticks;
            if (elapsed >= timeout_ticks) {
                break;
            }

            TickType_t remaining = timeout_ticks - elapsed;
            if (remaining < wait_ticks) {
                wait_ticks = remaining;
            }
        }

        EventBits_t bits = xEventGroupWaitBits(s_ctx.event_group,
                                               TIME_SYNC_READY_BIT,
                                               pdFALSE,
                                               pdFALSE,
                                               wait_ticks);
        if (!s_ctx.started) {
            return ESP_ERR_INVALID_STATE;
        }
        if ((bits & TIME_SYNC_READY_BIT) != 0U || time_sync_is_valid()) {
            return ESP_OK;
        }

        if (timeout_ticks == portMAX_DELAY) {
            continue;
        }
    }

    return ESP_ERR_TIMEOUT;
}

int64_t time_sync_now_ms(void)
{
    struct timeval now = { 0 };
    gettimeofday(&now, NULL);
    return ((int64_t)now.tv_sec * 1000LL) + ((int64_t)now.tv_usec / 1000LL);
}

esp_err_t time_sync_get_status(time_sync_status_t *out_status)
{
    if (out_status == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    if (time_sync_lock() != ESP_OK) {
        return ESP_ERR_INVALID_STATE;
    }

    memset(out_status, 0, sizeof(*out_status));
    out_status->started = s_ctx.started;
    out_status->sync_count = s_ctx.sync_count;
    out_status->last_sync_ms = s_ctx.last_sync_ms;
    strlcpy(out_status->server_name, s_ctx.server_name, sizeof(out_status->server_name));
    time_sync_unlock();

    out_status->now_ms = time_sync_now_ms();
    out_status->valid = time_sync_is_valid();
    out_status->sync_in_progress = sntp_get_sync_status() == SNTP_SYNC_STATUS_IN_PROGRESS;
    return ESP_OK;
}

esp_err_t time_sync_stop(void)
{
    if (!s_ctx.started) {
        return ESP_OK;
    }

    if (time_sync_lock() != ESP_OK) {
        return ESP_ERR_INVALID_STATE;
    }

    s_ctx.started = false;
    xEventGroupClearBits(s_ctx.event_group, TIME_SYNC_READY_BIT);
    time_sync_unlock();

    esp_sntp_stop();
    ESP_LOGI(TAG, "Stopped SNTP service");
    return ESP_OK;
}
