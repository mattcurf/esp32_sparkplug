#include "sparkplug_session.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "app_config.h"
#include "esp_check.h"
#include "esp_event.h"
#include "esp_log.h"
#include "mqtt_client.h"
#include "sparkplug_node.h"
#include "time_sync.h"
#include "freertos/FreeRTOS.h"
#include "freertos/portmacro.h"
#include "freertos/queue.h"
#include "freertos/task.h"

#define SPARKPLUG_SESSION_QUEUE_LEN 16
#define SPARKPLUG_SESSION_TASK_STACK_SIZE 8192
#define SPARKPLUG_SESSION_TASK_PRIORITY 6
#define SPARKPLUG_SESSION_TOPIC_MAX_LEN 64
#define SPARKPLUG_SESSION_PAYLOAD_MAX_LEN 256
#define SPARKPLUG_SESSION_SYNTHETIC_MIN_VALUE 0.0f
#define SPARKPLUG_SESSION_SYNTHETIC_MAX_VALUE 100.0f
#define SPARKPLUG_SESSION_SYNTHETIC_PERIOD_MS 20000LL
#define SPARKPLUG_SESSION_SYNTHETIC_TWO_PI 6.28318530718f

typedef enum {
    SPARKPLUG_SESSION_CMD_START = 0,
    SPARKPLUG_SESSION_CMD_STOP,
    SPARKPLUG_SESSION_CMD_SENSOR_SAMPLE,
    SPARKPLUG_SESSION_CMD_FORCE_PUBLISH,
    SPARKPLUG_SESSION_CMD_FORCE_REBIRTH,
    SPARKPLUG_SESSION_CMD_MQTT_CONNECTED,
    SPARKPLUG_SESSION_CMD_MQTT_DISCONNECTED,
    SPARKPLUG_SESSION_CMD_MQTT_SUBSCRIBED,
    SPARKPLUG_SESSION_CMD_MQTT_REBIRTH_REQUEST,
} sparkplug_session_cmd_type_t;

typedef struct {
    sparkplug_session_cmd_type_t type;
    sensor_tmp36_reading_t reading;
    int64_t sample_time_ms;
} sparkplug_session_cmd_t;

typedef struct {
    QueueHandle_t queue;
    TaskHandle_t task;
    esp_mqtt_client_handle_t client;
    bool initialized;
    bool reconnect_enabled;
    bool mqtt_configured;
    bool mqtt_started;
    bool mqtt_connected;
    bool ncmd_subscribed;
    bool session_active;
    bool birth_complete;
    bool rebirth_pending;
    bool has_temperature;
    bool has_last_published_temperature;
    uint32_t mqtt_reconnect_count;
    uint64_t bdseq;
    uint8_t seq;
    float last_temperature_c;
    float last_published_temperature_c;
    int64_t last_sample_ms;
    int64_t last_publish_ms;
    char last_message[SPARKPLUG_SESSION_LAST_MESSAGE_MAX_LEN + 1];
    char topic_nbirth[SPARKPLUG_SESSION_TOPIC_MAX_LEN];
    char topic_ndata[SPARKPLUG_SESSION_TOPIC_MAX_LEN];
    char topic_ndeath[SPARKPLUG_SESSION_TOPIC_MAX_LEN];
    char topic_ncmd[SPARKPLUG_SESSION_TOPIC_MAX_LEN];
    uint8_t will_payload[SPARKPLUG_SESSION_PAYLOAD_MAX_LEN];
    size_t will_payload_len;
    int ncmd_subscribe_msg_id;
    sparkplug_session_status_t status_snapshot;
} sparkplug_session_state_t;

static const char *TAG = "sparkplug_session";
static sparkplug_session_state_t s_state;
static portMUX_TYPE s_status_lock = portMUX_INITIALIZER_UNLOCKED;

static bool sparkplug_session_queue_command(const sparkplug_session_cmd_t *cmd, TickType_t timeout_ticks);
static void sparkplug_session_refresh_status(void);
static void sparkplug_session_mqtt_event_handler(void *handler_args,
                                                 esp_event_base_t base,
                                                 int32_t event_id,
                                                 void *event_data);

static float sparkplug_session_synthetic_sinewave_at_ms(int64_t timestamp_ms)
{
    const float midpoint = (SPARKPLUG_SESSION_SYNTHETIC_MIN_VALUE + SPARKPLUG_SESSION_SYNTHETIC_MAX_VALUE) * 0.5f;
    const float amplitude = (SPARKPLUG_SESSION_SYNTHETIC_MAX_VALUE - SPARKPLUG_SESSION_SYNTHETIC_MIN_VALUE) * 0.5f;
    int64_t phase_ms = timestamp_ms;

    if (phase_ms < 0) {
        phase_ms = 0;
    }
    phase_ms %= SPARKPLUG_SESSION_SYNTHETIC_PERIOD_MS;

    return midpoint
           + (amplitude * sinf((SPARKPLUG_SESSION_SYNTHETIC_TWO_PI * (float)phase_ms)
                               / (float)SPARKPLUG_SESSION_SYNTHETIC_PERIOD_MS));
}

static bool sparkplug_session_queue_command(const sparkplug_session_cmd_t *cmd, TickType_t timeout_ticks)
{
    if (s_state.queue == NULL || cmd == NULL) {
        return false;
    }

    return xQueueSend(s_state.queue, cmd, timeout_ticks) == pdTRUE;
}

static void sparkplug_session_set_last_message(const char *message)
{
    snprintf(s_state.last_message, sizeof(s_state.last_message), "%s", message != NULL ? message : "");
}

static void sparkplug_session_refresh_status(void)
{
    portENTER_CRITICAL(&s_status_lock);
    s_state.status_snapshot.mqtt_configured = s_state.mqtt_configured;
    s_state.status_snapshot.mqtt_started = s_state.mqtt_started;
    s_state.status_snapshot.mqtt_connected = s_state.mqtt_connected;
    s_state.status_snapshot.ncmd_subscribed = s_state.ncmd_subscribed;
    s_state.status_snapshot.session_active = s_state.session_active;
    s_state.status_snapshot.birth_complete = s_state.birth_complete;
    s_state.status_snapshot.rebirth_pending = s_state.rebirth_pending;
    s_state.status_snapshot.has_temperature = s_state.has_temperature;
    s_state.status_snapshot.mqtt_reconnect_count = s_state.mqtt_reconnect_count;
    s_state.status_snapshot.bdseq = s_state.bdseq;
    s_state.status_snapshot.seq = s_state.seq;
    s_state.status_snapshot.last_temperature_c = s_state.last_temperature_c;
    s_state.status_snapshot.last_sample_ms = s_state.last_sample_ms;
    s_state.status_snapshot.last_publish_ms = s_state.last_publish_ms;
    memcpy(s_state.status_snapshot.last_message,
           s_state.last_message,
           sizeof(s_state.status_snapshot.last_message));
    portEXIT_CRITICAL(&s_status_lock);
}

static void sparkplug_session_store_sample(const sensor_tmp36_reading_t *reading, int64_t sample_time_ms)
{
    if (reading == NULL || !reading->valid) {
        return;
    }

    s_state.has_temperature = true;
    s_state.last_temperature_c = reading->temperature_c;
    s_state.last_sample_ms = sample_time_ms;
    sparkplug_session_refresh_status();
}

static bool sparkplug_session_should_publish(bool force)
{
    const app_config_temperature_t *temperature_config = &app_config_get()->temperature;
    int64_t now_ms = time_sync_now_ms();

    if (!s_state.birth_complete || !s_state.mqtt_connected || !s_state.ncmd_subscribed || !s_state.has_temperature) {
        return false;
    }
    if (force) {
        return true;
    }
    if (s_state.last_sample_ms > s_state.last_publish_ms) {
        return true;
    }
    if (!s_state.has_last_published_temperature) {
        return true;
    }
    if (fabsf(s_state.last_temperature_c - s_state.last_published_temperature_c)
        >= temperature_config->publish_deadband_c) {
        return true;
    }
    if ((now_ms - s_state.last_publish_ms) >= (int64_t)temperature_config->max_publish_interval_ms) {
        return true;
    }

    return false;
}

static esp_err_t sparkplug_session_publish_bytes(const char *topic,
                                                 const uint8_t *payload,
                                                 size_t payload_len)
{
    int msg_id;

    if (s_state.client == NULL || topic == NULL || payload == NULL || payload_len == 0U) {
        return ESP_ERR_INVALID_STATE;
    }

    msg_id = esp_mqtt_client_publish(s_state.client, topic, (const char *)payload, (int)payload_len, 0, 0);
    return msg_id >= 0 ? ESP_OK : ESP_FAIL;
}

static esp_err_t sparkplug_session_publish_birth(void)
{
    const int64_t publish_time_ms = time_sync_now_ms();
    sparkplug_node_birth_payload_t payload = {
        .timestamp_ms = (uint64_t)publish_time_ms,
        .bdseq = s_state.bdseq,
        .temperature_c = s_state.last_temperature_c,
        .synthetic_sinewave = sparkplug_session_synthetic_sinewave_at_ms(publish_time_ms),
    };
    uint8_t buffer[SPARKPLUG_SESSION_PAYLOAD_MAX_LEN] = {0};
    size_t encoded_size = 0;
    esp_err_t err;

    if (!s_state.has_temperature) {
        return ESP_ERR_INVALID_STATE;
    }

    err = sparkplug_node_encode_nbirth(&payload, buffer, sizeof(buffer), &encoded_size);
    if (err != ESP_OK) {
        return err;
    }

    err = sparkplug_session_publish_bytes(s_state.topic_nbirth, buffer, encoded_size);
    if (err != ESP_OK) {
        return err;
    }

    s_state.birth_complete = true;
    s_state.rebirth_pending = false;
    s_state.session_active = true;
    s_state.seq = 0U;
    s_state.last_publish_ms = (int64_t)payload.timestamp_ms;
    s_state.last_published_temperature_c = s_state.last_temperature_c;
    s_state.has_last_published_temperature = true;
    sparkplug_session_set_last_message("NBIRTH");
    sparkplug_session_refresh_status();
    ESP_LOGI(TAG,
             "published NBIRTH bdSeq=%llu temperature=%.2fC synthetic=%.2f",
             (unsigned long long)s_state.bdseq,
             (double)s_state.last_temperature_c,
             (double)payload.synthetic_sinewave);
    return ESP_OK;
}

static esp_err_t sparkplug_session_publish_data(void)
{
    const int64_t publish_time_ms = s_state.last_sample_ms > 0 ? s_state.last_sample_ms : time_sync_now_ms();
    sparkplug_node_data_payload_t payload = {
        .timestamp_ms = (uint64_t)publish_time_ms,
        .seq = (uint64_t)((uint8_t)(s_state.seq + 1U)),
        .temperature_c = s_state.last_temperature_c,
        .synthetic_sinewave = sparkplug_session_synthetic_sinewave_at_ms(publish_time_ms),
    };
    uint8_t buffer[SPARKPLUG_SESSION_PAYLOAD_MAX_LEN] = {0};
    size_t encoded_size = 0;
    esp_err_t err = sparkplug_node_encode_ndata(&payload, buffer, sizeof(buffer), &encoded_size);
    if (err != ESP_OK) {
        return err;
    }

    err = sparkplug_session_publish_bytes(s_state.topic_ndata, buffer, encoded_size);
    if (err != ESP_OK) {
        return err;
    }

    s_state.seq = (uint8_t)payload.seq;
    s_state.last_publish_ms = (int64_t)payload.timestamp_ms;
    s_state.last_published_temperature_c = s_state.last_temperature_c;
    s_state.has_last_published_temperature = true;
    sparkplug_session_set_last_message("NDATA");
    sparkplug_session_refresh_status();
    ESP_LOGI(TAG,
             "published NDATA seq=%u temperature=%.2fC synthetic=%.2f",
             s_state.seq,
             (double)s_state.last_temperature_c,
             (double)payload.synthetic_sinewave);
    return ESP_OK;
}

static esp_err_t sparkplug_session_publish_death(void)
{
    sparkplug_node_death_payload_t payload = {
        .timestamp_ms = (uint64_t)time_sync_now_ms(),
        .bdseq = s_state.bdseq,
    };
    uint8_t buffer[SPARKPLUG_SESSION_PAYLOAD_MAX_LEN] = {0};
    size_t encoded_size = 0;
    esp_err_t err = sparkplug_node_encode_ndeath(&payload, buffer, sizeof(buffer), &encoded_size);
    if (err != ESP_OK) {
        return err;
    }

    err = sparkplug_session_publish_bytes(s_state.topic_ndeath, buffer, encoded_size);
    if (err != ESP_OK) {
        return err;
    }

    s_state.last_publish_ms = (int64_t)payload.timestamp_ms;
    sparkplug_session_set_last_message("NDEATH");
    sparkplug_session_refresh_status();
    ESP_LOGI(TAG, "published explicit NDEATH bdSeq=%llu", (unsigned long long)s_state.bdseq);
    return ESP_OK;
}

static esp_err_t sparkplug_session_prepare_topics(void)
{
    sparkplug_node_topic_config_t topic_config = {
        .group_id = app_config_get()->sparkplug.group_id,
        .node_id = app_config_get()->sparkplug.node_id,
    };
    esp_err_t err;

    err = sparkplug_node_build_topic(&topic_config,
                                     SPARKPLUG_NODE_TOPIC_NBIRTH,
                                     s_state.topic_nbirth,
                                     sizeof(s_state.topic_nbirth));
    if (err != ESP_OK) {
        return err;
    }
    err = sparkplug_node_build_topic(&topic_config,
                                     SPARKPLUG_NODE_TOPIC_NDATA,
                                     s_state.topic_ndata,
                                     sizeof(s_state.topic_ndata));
    if (err != ESP_OK) {
        return err;
    }
    err = sparkplug_node_build_topic(&topic_config,
                                     SPARKPLUG_NODE_TOPIC_NDEATH,
                                     s_state.topic_ndeath,
                                     sizeof(s_state.topic_ndeath));
    if (err != ESP_OK) {
        return err;
    }
    return sparkplug_node_build_topic(&topic_config,
                                      SPARKPLUG_NODE_TOPIC_NCMD,
                                      s_state.topic_ncmd,
                                      sizeof(s_state.topic_ncmd));
}

static esp_err_t sparkplug_session_prepare_will_payload(void)
{
    sparkplug_node_death_payload_t will_payload = {
        .timestamp_ms = (uint64_t)time_sync_now_ms(),
        .bdseq = s_state.bdseq,
    };
    return sparkplug_node_encode_ndeath(&will_payload,
                                        s_state.will_payload,
                                        sizeof(s_state.will_payload),
                                        &s_state.will_payload_len);
}

static void sparkplug_session_destroy_client(void)
{
    if (s_state.client == NULL) {
        return;
    }

    (void)esp_mqtt_client_stop(s_state.client);
    esp_mqtt_client_destroy(s_state.client);
    s_state.client = NULL;
    s_state.mqtt_configured = false;
    s_state.mqtt_started = false;
    s_state.mqtt_connected = false;
    s_state.ncmd_subscribed = false;
    s_state.birth_complete = false;
    s_state.session_active = false;
    sparkplug_session_refresh_status();
}

static esp_err_t sparkplug_session_start_client(void)
{
    esp_mqtt_client_config_t mqtt_config = {0};

    s_state.bdseq++;
    s_state.seq = 0U;
    s_state.birth_complete = false;
    s_state.ncmd_subscribed = false;
    s_state.session_active = false;
    s_state.ncmd_subscribe_msg_id = -1;
    sparkplug_session_set_last_message("");

    ESP_RETURN_ON_ERROR(sparkplug_session_prepare_topics(), TAG, "failed to build Sparkplug topics");
    ESP_RETURN_ON_ERROR(sparkplug_session_prepare_will_payload(), TAG, "failed to encode NDEATH will payload");

    mqtt_config.broker.address.uri = app_config_get()->sparkplug.broker_uri;
    mqtt_config.credentials.username = app_config_get()->sparkplug.username;
    mqtt_config.credentials.authentication.password = app_config_get()->sparkplug.password;
    mqtt_config.session.protocol_ver = MQTT_PROTOCOL_V_3_1_1;
    mqtt_config.session.disable_clean_session = false;
    mqtt_config.session.keepalive = 60;
    mqtt_config.session.last_will.topic = s_state.topic_ndeath;
    mqtt_config.session.last_will.msg = (const char *)s_state.will_payload;
    mqtt_config.session.last_will.msg_len = (int)s_state.will_payload_len;
    mqtt_config.session.last_will.qos = 0;
    mqtt_config.session.last_will.retain = false;
    mqtt_config.network.disable_auto_reconnect = true;

    s_state.client = esp_mqtt_client_init(&mqtt_config);
    if (s_state.client == NULL) {
        return ESP_FAIL;
    }

    ESP_RETURN_ON_ERROR(esp_mqtt_client_register_event(s_state.client,
                                                       ESP_EVENT_ANY_ID,
                                                       sparkplug_session_mqtt_event_handler,
                                                       NULL),
                        TAG,
                        "failed to register mqtt events");
    ESP_RETURN_ON_ERROR(esp_mqtt_client_start(s_state.client), TAG, "failed to start mqtt client");

    s_state.mqtt_configured = true;
    s_state.mqtt_started = true;
    sparkplug_session_refresh_status();
    ESP_LOGI(TAG, "started MQTT client for bdSeq=%llu", (unsigned long long)s_state.bdseq);
    return ESP_OK;
}

static void sparkplug_session_maybe_publish_after_sample(bool force_publish)
{
    esp_err_t err;

    if (!s_state.birth_complete && s_state.mqtt_connected && s_state.ncmd_subscribed && s_state.has_temperature) {
        err = sparkplug_session_publish_birth();
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "failed to publish NBIRTH: %s", esp_err_to_name(err));
        }
        return;
    }

    if (s_state.rebirth_pending && s_state.mqtt_connected && s_state.ncmd_subscribed && s_state.has_temperature) {
        err = sparkplug_session_publish_birth();
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "failed to publish rebirth NBIRTH: %s", esp_err_to_name(err));
        }
        return;
    }

    if (sparkplug_session_should_publish(force_publish)) {
        err = sparkplug_session_publish_data();
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "failed to publish NDATA: %s", esp_err_to_name(err));
        }
    }
}

static void sparkplug_session_handle_command(const sparkplug_session_cmd_t *cmd)
{
    esp_err_t err;

    switch (cmd->type) {
    case SPARKPLUG_SESSION_CMD_START:
        s_state.reconnect_enabled = true;
        if (s_state.client == NULL) {
            err = sparkplug_session_start_client();
            if (err != ESP_OK) {
                ESP_LOGE(TAG, "failed to start MQTT session: %s", esp_err_to_name(err));
            }
        }
        break;
    case SPARKPLUG_SESSION_CMD_STOP:
        s_state.reconnect_enabled = false;
        if (s_state.mqtt_connected) {
            err = sparkplug_session_publish_death();
            if (err != ESP_OK) {
                ESP_LOGE(TAG, "failed to publish NDEATH during stop: %s", esp_err_to_name(err));
            }
        }
        sparkplug_session_destroy_client();
        break;
    case SPARKPLUG_SESSION_CMD_SENSOR_SAMPLE:
        sparkplug_session_store_sample(&cmd->reading, cmd->sample_time_ms);
        sparkplug_session_maybe_publish_after_sample(false);
        break;
    case SPARKPLUG_SESSION_CMD_FORCE_PUBLISH:
        sparkplug_session_maybe_publish_after_sample(true);
        break;
    case SPARKPLUG_SESSION_CMD_FORCE_REBIRTH:
    case SPARKPLUG_SESSION_CMD_MQTT_REBIRTH_REQUEST:
        s_state.rebirth_pending = true;
        sparkplug_session_refresh_status();
        sparkplug_session_maybe_publish_after_sample(false);
        break;
    case SPARKPLUG_SESSION_CMD_MQTT_CONNECTED:
        s_state.mqtt_connected = true;
        s_state.session_active = true;
        s_state.ncmd_subscribe_msg_id = esp_mqtt_client_subscribe(s_state.client, s_state.topic_ncmd, 0);
        sparkplug_session_refresh_status();
        if (s_state.ncmd_subscribe_msg_id < 0) {
            ESP_LOGE(TAG, "failed to subscribe to NCMD topic");
        } else {
            ESP_LOGI(TAG, "subscribed to %s msg_id=%d", s_state.topic_ncmd, s_state.ncmd_subscribe_msg_id);
        }
        break;
    case SPARKPLUG_SESSION_CMD_MQTT_SUBSCRIBED:
        s_state.ncmd_subscribed = true;
        sparkplug_session_refresh_status();
        sparkplug_session_maybe_publish_after_sample(false);
        break;
    case SPARKPLUG_SESSION_CMD_MQTT_DISCONNECTED:
        s_state.mqtt_connected = false;
        s_state.ncmd_subscribed = false;
        s_state.birth_complete = false;
        s_state.session_active = false;
        s_state.mqtt_reconnect_count++;
        sparkplug_session_destroy_client();
        if (s_state.reconnect_enabled) {
            vTaskDelay(pdMS_TO_TICKS(1000));
            err = sparkplug_session_start_client();
            if (err != ESP_OK) {
                ESP_LOGE(TAG, "failed to restart MQTT session: %s", esp_err_to_name(err));
            }
        }
        break;
    default:
        break;
    }

    sparkplug_session_refresh_status();
}

static void sparkplug_session_task(void *arg)
{
    sparkplug_session_cmd_t cmd = {0};
    (void)arg;

    while (true) {
        if (xQueueReceive(s_state.queue, &cmd, portMAX_DELAY) == pdTRUE) {
            sparkplug_session_handle_command(&cmd);
        }
    }
}

static void sparkplug_session_mqtt_event_handler(void *handler_args,
                                                 esp_event_base_t base,
                                                 int32_t event_id,
                                                 void *event_data)
{
    const esp_mqtt_event_handle_t event = event_data;
    sparkplug_session_cmd_t cmd = {0};
    (void)handler_args;
    (void)base;

    switch ((esp_mqtt_event_id_t)event_id) {
    case MQTT_EVENT_CONNECTED:
        cmd.type = SPARKPLUG_SESSION_CMD_MQTT_CONNECTED;
        break;
    case MQTT_EVENT_DISCONNECTED:
        cmd.type = SPARKPLUG_SESSION_CMD_MQTT_DISCONNECTED;
        break;
    case MQTT_EVENT_SUBSCRIBED:
        cmd.type = SPARKPLUG_SESSION_CMD_MQTT_SUBSCRIBED;
        break;
    case MQTT_EVENT_DATA:
        if (event != NULL
            && event->topic_len == (int)strlen(s_state.topic_ncmd)
            && strncmp(event->topic, s_state.topic_ncmd, (size_t)event->topic_len) == 0
            && event->current_data_offset == 0
            && event->data_len == event->total_data_len) {
            sparkplug_node_ncmd_t decoded = {0};
            if (sparkplug_node_decode_ncmd((const uint8_t *)event->data,
                                           (size_t)event->data_len,
                                           &decoded) == ESP_OK
                && decoded.rebirth_requested) {
                cmd.type = SPARKPLUG_SESSION_CMD_MQTT_REBIRTH_REQUEST;
            } else {
                return;
            }
        } else {
            return;
        }
        break;
    default:
        return;
    }

    if (!sparkplug_session_queue_command(&cmd, 0)) {
        ESP_LOGW(TAG, "dropping MQTT event %ld because queue is full", (long)event_id);
    }
}

esp_err_t sparkplug_session_init(void)
{
    if (s_state.initialized) {
        return ESP_OK;
    }

    s_state.queue = xQueueCreate(SPARKPLUG_SESSION_QUEUE_LEN, sizeof(sparkplug_session_cmd_t));
    if (s_state.queue == NULL) {
        return ESP_ERR_NO_MEM;
    }

    if (xTaskCreate(sparkplug_session_task,
                    "sparkplug_session",
                    SPARKPLUG_SESSION_TASK_STACK_SIZE,
                    NULL,
                    SPARKPLUG_SESSION_TASK_PRIORITY,
                    &s_state.task) != pdPASS) {
        vQueueDelete(s_state.queue);
        s_state.queue = NULL;
        return ESP_ERR_NO_MEM;
    }

    s_state.initialized = true;
    memset(&s_state.status_snapshot, 0, sizeof(s_state.status_snapshot));
    sparkplug_session_set_last_message("");
    sparkplug_session_refresh_status();
    return sparkplug_session_prepare_topics();
}

esp_err_t sparkplug_session_start(void)
{
    sparkplug_session_cmd_t cmd = {
        .type = SPARKPLUG_SESSION_CMD_START,
    };

    ESP_RETURN_ON_ERROR(sparkplug_session_init(), TAG, "failed to initialize session");
    return sparkplug_session_queue_command(&cmd, pdMS_TO_TICKS(100)) ? ESP_OK : ESP_ERR_TIMEOUT;
}

esp_err_t sparkplug_session_stop(void)
{
    sparkplug_session_cmd_t cmd = {
        .type = SPARKPLUG_SESSION_CMD_STOP,
    };

    if (!s_state.initialized) {
        return ESP_OK;
    }

    return sparkplug_session_queue_command(&cmd, pdMS_TO_TICKS(100)) ? ESP_OK : ESP_ERR_TIMEOUT;
}

esp_err_t sparkplug_session_submit_temperature(const sensor_tmp36_reading_t *reading, int64_t sample_time_ms)
{
    sparkplug_session_cmd_t cmd = {
        .type = SPARKPLUG_SESSION_CMD_SENSOR_SAMPLE,
        .sample_time_ms = sample_time_ms,
    };

    if (reading == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!s_state.initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    cmd.reading = *reading;
    return sparkplug_session_queue_command(&cmd, pdMS_TO_TICKS(100)) ? ESP_OK : ESP_ERR_TIMEOUT;
}

esp_err_t sparkplug_session_request_publish(void)
{
    sparkplug_session_cmd_t cmd = {
        .type = SPARKPLUG_SESSION_CMD_FORCE_PUBLISH,
    };

    if (!s_state.initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    return sparkplug_session_queue_command(&cmd, pdMS_TO_TICKS(100)) ? ESP_OK : ESP_ERR_TIMEOUT;
}

esp_err_t sparkplug_session_request_rebirth(void)
{
    sparkplug_session_cmd_t cmd = {
        .type = SPARKPLUG_SESSION_CMD_FORCE_REBIRTH,
    };

    if (!s_state.initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    return sparkplug_session_queue_command(&cmd, pdMS_TO_TICKS(100)) ? ESP_OK : ESP_ERR_TIMEOUT;
}

esp_err_t sparkplug_session_get_status(sparkplug_session_status_t *status)
{
    if (status == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    memset(status, 0, sizeof(*status));
    portENTER_CRITICAL(&s_status_lock);
    *status = s_state.status_snapshot;
    portEXIT_CRITICAL(&s_status_lock);
    return ESP_OK;
}
