#include "app_console.h"

#include <inttypes.h>
#include <stdio.h>
#include <string.h>

#include "sdkconfig.h"

#include "esp_app_desc.h"
#include "esp_chip_info.h"
#include "esp_console.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"

static const char *TAG = "app_console";
static const char *APP_CONSOLE_PROMPT = "sparkplug> ";

typedef int (*app_console_command_handler_t)(int argc, char **argv);

typedef struct {
    const char *name;
    const char *help;
    app_console_command_handler_t handler;
} app_console_command_def_t;

static esp_console_repl_t *s_repl;
static bool s_console_initialized;
static bool s_console_started;
static bool s_commands_registered;
static app_console_providers_t s_providers;
static portMUX_TYPE s_provider_lock = portMUX_INITIALIZER_UNLOCKED;

static const char *app_bool_to_str(bool value)
{
    return value ? "true" : "false";
}

static const char *app_reset_reason_to_str(esp_reset_reason_t reason)
{
    switch (reason) {
    case ESP_RST_UNKNOWN:
        return "unknown";
    case ESP_RST_POWERON:
        return "poweron";
    case ESP_RST_EXT:
        return "external";
    case ESP_RST_SW:
        return "software";
    case ESP_RST_PANIC:
        return "panic";
    case ESP_RST_INT_WDT:
        return "int_wdt";
    case ESP_RST_TASK_WDT:
        return "task_wdt";
    case ESP_RST_WDT:
        return "wdt";
    case ESP_RST_DEEPSLEEP:
        return "deepsleep";
    case ESP_RST_BROWNOUT:
        return "brownout";
    case ESP_RST_SDIO:
        return "sdio";
    default:
        return "other";
    }
}

static void app_console_snapshot_providers(app_console_providers_t *providers)
{
    portENTER_CRITICAL(&s_provider_lock);
    *providers = s_providers;
    portEXIT_CRITICAL(&s_provider_lock);
}

static void app_console_print_provider_unwired(const char *name)
{
    printf("%s: runtime provider not attached\n", name);
}

static int app_console_report_provider_error(const char *name, esp_err_t err)
{
    printf("%s: provider failed (%s)\n", name, esp_err_to_name(err));
    return 1;
}

static int app_console_report_action_result(const char *name, app_console_action_fn action,
                                            void *ctx)
{
    if (action == NULL) {
        app_console_print_provider_unwired(name);
        return 0;
    }

    esp_err_t err = action(ctx);
    if (err != ESP_OK) {
        printf("%s: request failed (%s)\n", name, esp_err_to_name(err));
        return 1;
    }

    printf("%s: request accepted\n", name);
    return 0;
}

static int cmd_status(int argc, char **argv)
{
    if (argc != 1) {
        printf("usage: status\n");
        return 1;
    }

    const esp_app_desc_t *app = esp_app_get_description();
    esp_chip_info_t chip_info = {0};
    esp_chip_info(&chip_info);

    printf("project=%s version=%s idf=%s target=%s\n",
           app->project_name,
           app->version,
           app->idf_ver,
           CONFIG_IDF_TARGET);
    printf("uptime_ms=%" PRIi64 " reset_reason=%s cores=%d revision=%d\n",
           esp_timer_get_time() / 1000,
           app_reset_reason_to_str(esp_reset_reason()),
           chip_info.cores,
           chip_info.revision);
    printf("console: initialized=%s started=%s\n",
           app_bool_to_str(s_console_initialized),
           app_bool_to_str(s_console_started));

    app_console_providers_t providers = {0};
    app_console_snapshot_providers(&providers);

    int exit_code = 0;

    if (providers.get_sensor_status != NULL) {
        app_console_sensor_status_t sensor = {0};
        esp_err_t err = providers.get_sensor_status(&sensor, providers.sensor_status_ctx);
        if (err != ESP_OK) {
            exit_code = app_console_report_provider_error("sensor", err);
        } else {
            printf("sensor: valid=%s temperature_c=%.2f calibrated_mv=%" PRId32 " samples=%" PRIu32 "\n",
                   app_bool_to_str(sensor.valid),
                   sensor.temperature_c,
                   sensor.calibrated_mv,
                   sensor.sample_count);
        }
    } else {
        app_console_print_provider_unwired("sensor");
    }

    if (providers.get_wifi_status != NULL) {
        app_console_wifi_status_t wifi = {0};
        esp_err_t err = providers.get_wifi_status(&wifi, providers.wifi_status_ctx);
        if (err != ESP_OK) {
            exit_code = app_console_report_provider_error("wifi", err);
        } else {
            printf("wifi: connected=%s has_ip=%s ssid=%s ip=%s reconnects=%" PRIu32 "\n",
                   app_bool_to_str(wifi.connected),
                   app_bool_to_str(wifi.has_ip),
                   wifi.ssid[0] != '\0' ? wifi.ssid : "<unset>",
                   wifi.ip_address[0] != '\0' ? wifi.ip_address : "<unset>",
                   wifi.reconnect_count);
        }
    } else {
        app_console_print_provider_unwired("wifi");
    }

    if (providers.get_time_status != NULL) {
        app_console_time_status_t time_status = {0};
        esp_err_t err = providers.get_time_status(&time_status, providers.time_status_ctx);
        if (err != ESP_OK) {
            exit_code = app_console_report_provider_error("time", err);
        } else {
            printf("time: synchronized=%s unix_time_ms=%" PRIi64 " last_sync_ms=%" PRIi64 "\n",
                   app_bool_to_str(time_status.synchronized),
                   time_status.unix_time_ms,
                   time_status.last_sync_ms);
        }
    } else {
        app_console_print_provider_unwired("time");
    }

    if (providers.get_mqtt_status != NULL) {
        app_console_mqtt_status_t mqtt = {0};
        esp_err_t err = providers.get_mqtt_status(&mqtt, providers.mqtt_status_ctx);
        if (err != ESP_OK) {
            exit_code = app_console_report_provider_error("mqtt", err);
        } else {
            printf("mqtt: connected=%s subscribed_ncmd=%s broker=%s reconnects=%" PRIu32 "\n",
                   app_bool_to_str(mqtt.connected),
                   app_bool_to_str(mqtt.ncmd_subscribed),
                   mqtt.broker_uri[0] != '\0' ? mqtt.broker_uri : "<unset>",
                   mqtt.reconnect_count);
        }
    } else {
        app_console_print_provider_unwired("mqtt");
    }

    if (providers.get_sparkplug_status != NULL) {
        app_console_sparkplug_status_t sparkplug = {0};
        esp_err_t err = providers.get_sparkplug_status(&sparkplug, providers.sparkplug_status_ctx);
        if (err != ESP_OK) {
            exit_code = app_console_report_provider_error("sparkplug", err);
        } else {
            printf("sparkplug: session_active=%s birth_complete=%s bdSeq=%u seq=%u last=%s\n",
                   app_bool_to_str(sparkplug.session_active),
                   app_bool_to_str(sparkplug.birth_complete),
                   sparkplug.bdseq,
                   sparkplug.seq,
                   sparkplug.last_message[0] != '\0' ? sparkplug.last_message : "<none>");
        }
    } else {
        app_console_print_provider_unwired("sparkplug");
    }

    return exit_code;
}

static int cmd_sensor(int argc, char **argv)
{
    if (argc != 1) {
        printf("usage: sensor\n");
        return 1;
    }

    app_console_providers_t providers = {0};
    app_console_snapshot_providers(&providers);
    if (providers.get_sensor_status == NULL) {
        app_console_print_provider_unwired("sensor");
        return 0;
    }

    app_console_sensor_status_t status = {0};
    esp_err_t err = providers.get_sensor_status(&status, providers.sensor_status_ctx);
    if (err != ESP_OK) {
        return app_console_report_provider_error("sensor", err);
    }

    printf("sensor.valid=%s\n", app_bool_to_str(status.valid));
    printf("sensor.sample_count=%" PRIu32 "\n", status.sample_count);
    printf("sensor.raw_average=%" PRId32 "\n", status.raw_average);
    printf("sensor.calibrated_mv=%" PRId32 "\n", status.calibrated_mv);
    printf("sensor.temperature_c=%.2f\n", status.temperature_c);
    printf("sensor.last_sample_ms=%" PRIi64 "\n", status.last_sample_ms);
    return 0;
}

static int cmd_wifi(int argc, char **argv)
{
    if (argc != 1) {
        printf("usage: wifi\n");
        return 1;
    }

    app_console_providers_t providers = {0};
    app_console_snapshot_providers(&providers);
    if (providers.get_wifi_status == NULL) {
        app_console_print_provider_unwired("wifi");
        return 0;
    }

    app_console_wifi_status_t status = {0};
    esp_err_t err = providers.get_wifi_status(&status, providers.wifi_status_ctx);
    if (err != ESP_OK) {
        return app_console_report_provider_error("wifi", err);
    }

    printf("wifi.initialized=%s\n", app_bool_to_str(status.initialized));
    printf("wifi.connected=%s\n", app_bool_to_str(status.connected));
    printf("wifi.has_ip=%s\n", app_bool_to_str(status.has_ip));
    printf("wifi.ssid=%s\n", status.ssid[0] != '\0' ? status.ssid : "<unset>");
    printf("wifi.ip=%s\n", status.ip_address[0] != '\0' ? status.ip_address : "<unset>");
    printf("wifi.rssi_dbm=%" PRId32 "\n", status.rssi_dbm);
    printf("wifi.reconnect_count=%" PRIu32 "\n", status.reconnect_count);
    return 0;
}

static int cmd_time(int argc, char **argv)
{
    if (argc != 1) {
        printf("usage: time\n");
        return 1;
    }

    app_console_providers_t providers = {0};
    app_console_snapshot_providers(&providers);
    if (providers.get_time_status == NULL) {
        app_console_print_provider_unwired("time");
        return 0;
    }

    app_console_time_status_t status = {0};
    esp_err_t err = providers.get_time_status(&status, providers.time_status_ctx);
    if (err != ESP_OK) {
        return app_console_report_provider_error("time", err);
    }

    printf("time.initialized=%s\n", app_bool_to_str(status.initialized));
    printf("time.synchronized=%s\n", app_bool_to_str(status.synchronized));
    printf("time.unix_time_ms=%" PRIi64 "\n", status.unix_time_ms);
    printf("time.last_sync_ms=%" PRIi64 "\n", status.last_sync_ms);
    return 0;
}

static int cmd_mqtt(int argc, char **argv)
{
    if (argc != 1) {
        printf("usage: mqtt\n");
        return 1;
    }

    app_console_providers_t providers = {0};
    app_console_snapshot_providers(&providers);
    if (providers.get_mqtt_status == NULL) {
        app_console_print_provider_unwired("mqtt");
        return 0;
    }

    app_console_mqtt_status_t status = {0};
    esp_err_t err = providers.get_mqtt_status(&status, providers.mqtt_status_ctx);
    if (err != ESP_OK) {
        return app_console_report_provider_error("mqtt", err);
    }

    printf("mqtt.configured=%s\n", app_bool_to_str(status.configured));
    printf("mqtt.started=%s\n", app_bool_to_str(status.started));
    printf("mqtt.connected=%s\n", app_bool_to_str(status.connected));
    printf("mqtt.ncmd_subscribed=%s\n", app_bool_to_str(status.ncmd_subscribed));
    printf("mqtt.broker_uri=%s\n", status.broker_uri[0] != '\0' ? status.broker_uri : "<unset>");
    printf("mqtt.reconnect_count=%" PRIu32 "\n", status.reconnect_count);
    return 0;
}

static int cmd_sparkplug(int argc, char **argv)
{
    if (argc != 1) {
        printf("usage: sparkplug\n");
        return 1;
    }

    app_console_providers_t providers = {0};
    app_console_snapshot_providers(&providers);
    if (providers.get_sparkplug_status == NULL) {
        app_console_print_provider_unwired("sparkplug");
        return 0;
    }

    app_console_sparkplug_status_t status = {0};
    esp_err_t err = providers.get_sparkplug_status(&status, providers.sparkplug_status_ctx);
    if (err != ESP_OK) {
        return app_console_report_provider_error("sparkplug", err);
    }

    printf("sparkplug.session_active=%s\n", app_bool_to_str(status.session_active));
    printf("sparkplug.birth_complete=%s\n", app_bool_to_str(status.birth_complete));
    printf("sparkplug.rebirth_pending=%s\n", app_bool_to_str(status.rebirth_pending));
    printf("sparkplug.bdSeq=%u\n", status.bdseq);
    printf("sparkplug.seq=%u\n", status.seq);
    printf("sparkplug.last_publish_ms=%" PRIi64 "\n", status.last_publish_ms);
    printf("sparkplug.last_message=%s\n",
           status.last_message[0] != '\0' ? status.last_message : "<none>");
    return 0;
}

static int cmd_publish(int argc, char **argv)
{
    if (argc != 1) {
        printf("usage: publish\n");
        return 1;
    }

    app_console_providers_t providers = {0};
    app_console_snapshot_providers(&providers);
    return app_console_report_action_result("publish", providers.request_publish,
                                            providers.publish_ctx);
}

static int cmd_rebirth(int argc, char **argv)
{
    if (argc != 1) {
        printf("usage: rebirth\n");
        return 1;
    }

    app_console_providers_t providers = {0};
    app_console_snapshot_providers(&providers);
    return app_console_report_action_result("rebirth", providers.request_rebirth,
                                            providers.rebirth_ctx);
}

static int cmd_restart(int argc, char **argv)
{
    if (argc != 1) {
        printf("usage: restart\n");
        return 1;
    }

    printf("restart: rebooting device\n");
    fflush(stdout);
    esp_restart();
    return 0;
}

static esp_err_t app_console_register_command(const app_console_command_def_t *command)
{
    const esp_console_cmd_t cmd = {
        .command = command->name,
        .help = command->help,
        .hint = NULL,
        .func = command->handler,
        .argtable = NULL,
    };

    return esp_console_cmd_register(&cmd);
}

static esp_err_t app_console_register_commands_once(void)
{
    if (s_commands_registered) {
        return ESP_OK;
    }

    esp_err_t err = esp_console_register_help_command();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        return err;
    }

    static const app_console_command_def_t commands[] = {
        {"status", "Show device and subsystem summary", cmd_status},
        {"sensor", "Show last TMP36 reading snapshot", cmd_sensor},
        {"wifi", "Show Wi-Fi connection snapshot", cmd_wifi},
        {"time", "Show SNTP/time synchronization snapshot", cmd_time},
        {"mqtt", "Show MQTT transport snapshot", cmd_mqtt},
        {"sparkplug", "Show Sparkplug session snapshot", cmd_sparkplug},
        {"publish", "Request an immediate NDATA publish", cmd_publish},
        {"rebirth", "Request a fresh NBIRTH sequence", cmd_rebirth},
        {"restart", "Restart the device", cmd_restart},
    };

    for (size_t i = 0; i < sizeof(commands) / sizeof(commands[0]); ++i) {
        err = app_console_register_command(&commands[i]);
        if (err != ESP_OK) {
            return err;
        }
    }

    s_commands_registered = true;
    return ESP_OK;
}

esp_err_t app_console_init(void)
{
    if (s_console_initialized) {
        return ESP_OK;
    }

    esp_console_repl_config_t repl_config = ESP_CONSOLE_REPL_CONFIG_DEFAULT();
    esp_console_dev_uart_config_t uart_config = ESP_CONSOLE_DEV_UART_CONFIG_DEFAULT();
    repl_config.prompt = APP_CONSOLE_PROMPT;

    esp_err_t err = esp_console_new_repl_uart(&uart_config, &repl_config, &s_repl);
    if (err != ESP_OK) {
        return err;
    }

    err = app_console_register_commands_once();
    if (err != ESP_OK) {
        return err;
    }

    s_console_initialized = true;
    ESP_LOGI(TAG, "Console initialized; call app_console_start() to start the REPL");
    return ESP_OK;
}

esp_err_t app_console_start(void)
{
    if (s_console_started) {
        return ESP_OK;
    }

    esp_err_t err = app_console_init();
    if (err != ESP_OK) {
        return err;
    }

    err = esp_console_start_repl(s_repl);
    if (err != ESP_OK) {
        return err;
    }

    s_console_started = true;
    ESP_LOGI(TAG, "Console REPL started on UART");
    return ESP_OK;
}

bool app_console_is_started(void)
{
    return s_console_started;
}

esp_err_t app_console_set_providers(const app_console_providers_t *providers)
{
    portENTER_CRITICAL(&s_provider_lock);
    if (providers == NULL) {
        memset(&s_providers, 0, sizeof(s_providers));
    } else {
        s_providers = *providers;
    }
    portEXIT_CRITICAL(&s_provider_lock);

    return ESP_OK;
}
