#include "sensor_tmp36.h"

#include <inttypes.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "app_config.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_log.h"
#include "freertos/portmacro.h"

#define TMP36_OFFSET_MV 500.0f
#define TMP36_MV_PER_DEG_C 10.0f
#define TMP36_DEFAULT_VREF_MV 1100

static const char *TAG = "sensor_tmp36";

typedef enum {
    SENSOR_TMP36_CALI_SCHEME_NONE = 0,
    SENSOR_TMP36_CALI_SCHEME_CURVE_FITTING,
    SENSOR_TMP36_CALI_SCHEME_LINE_FITTING,
} sensor_tmp36_cali_scheme_t;

typedef struct {
    bool initialized;
    bool has_last_reading;
    adc_oneshot_unit_handle_t adc_handle;
    adc_cali_handle_t cali_handle;
    sensor_tmp36_cali_scheme_t cali_scheme;
    sensor_tmp36_reading_t last_reading;
} sensor_tmp36_state_t;

static sensor_tmp36_state_t s_sensor;
static portMUX_TYPE s_sensor_lock = portMUX_INITIALIZER_UNLOCKED;

static const app_config_sensor_t *sensor_tmp36_config(void)
{
    return &app_config_get()->sensor;
}

static esp_err_t sensor_tmp36_delete_calibration(adc_cali_handle_t handle,
                                                 sensor_tmp36_cali_scheme_t scheme)
{
    if (handle == NULL || scheme == SENSOR_TMP36_CALI_SCHEME_NONE) {
        return ESP_OK;
    }

    switch (scheme) {
#if defined(ADC_CALI_SCHEME_CURVE_FITTING_SUPPORTED) && ADC_CALI_SCHEME_CURVE_FITTING_SUPPORTED
    case SENSOR_TMP36_CALI_SCHEME_CURVE_FITTING:
        return adc_cali_delete_scheme_curve_fitting(handle);
#endif
#if defined(ADC_CALI_SCHEME_LINE_FITTING_SUPPORTED) && ADC_CALI_SCHEME_LINE_FITTING_SUPPORTED
    case SENSOR_TMP36_CALI_SCHEME_LINE_FITTING:
        return adc_cali_delete_scheme_line_fitting(handle);
#endif
    case SENSOR_TMP36_CALI_SCHEME_NONE:
    default:
        return ESP_ERR_NOT_SUPPORTED;
    }
}

static esp_err_t sensor_tmp36_create_calibration(adc_cali_handle_t *out_handle,
                                                 sensor_tmp36_cali_scheme_t *out_scheme)
{
    const app_config_sensor_t *config = sensor_tmp36_config();
    esp_err_t err = ESP_ERR_NOT_SUPPORTED;

    if (out_handle == NULL || out_scheme == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    *out_handle = NULL;
    *out_scheme = SENSOR_TMP36_CALI_SCHEME_NONE;

#if defined(ADC_CALI_SCHEME_CURVE_FITTING_SUPPORTED) && ADC_CALI_SCHEME_CURVE_FITTING_SUPPORTED
    adc_cali_curve_fitting_config_t curve_config = {
        .unit_id = config->unit,
        .chan = config->channel,
        .atten = config->attenuation,
        .bitwidth = config->bitwidth,
    };
    err = adc_cali_create_scheme_curve_fitting(&curve_config, out_handle);
    if (err == ESP_OK) {
        *out_scheme = SENSOR_TMP36_CALI_SCHEME_CURVE_FITTING;
        return ESP_OK;
    }
#endif

#if defined(ADC_CALI_SCHEME_LINE_FITTING_SUPPORTED) && ADC_CALI_SCHEME_LINE_FITTING_SUPPORTED
    adc_cali_line_fitting_config_t line_config = {
        .unit_id = config->unit,
        .atten = config->attenuation,
        .bitwidth = config->bitwidth,
        .default_vref = TMP36_DEFAULT_VREF_MV,
    };

    err = adc_cali_create_scheme_line_fitting(&line_config, out_handle);
    if (err == ESP_OK) {
        *out_scheme = SENSOR_TMP36_CALI_SCHEME_LINE_FITTING;
        return ESP_OK;
    }
#endif

    return err;
}

static esp_err_t sensor_tmp36_read_average_raw(int *raw_out)
{
    const app_config_sensor_t *config = sensor_tmp36_config();
    int64_t raw_sum = 0;

    if (raw_out == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    if (!s_sensor.initialized || config->multisample_count == 0U) {
        return ESP_ERR_INVALID_STATE;
    }

    for (uint32_t sample_index = 0; sample_index < config->multisample_count; ++sample_index) {
        int raw_sample = 0;
        esp_err_t err = adc_oneshot_read(s_sensor.adc_handle, config->channel, &raw_sample);
        if (err != ESP_OK) {
            return err;
        }
        raw_sum += raw_sample;
    }

    *raw_out = (int)(raw_sum / (int64_t)config->multisample_count);
    return ESP_OK;
}

esp_err_t sensor_tmp36_init(void)
{
    const app_config_sensor_t *config = sensor_tmp36_config();
    adc_oneshot_unit_handle_t adc_handle = NULL;
    adc_cali_handle_t cali_handle = NULL;
    sensor_tmp36_cali_scheme_t cali_scheme = SENSOR_TMP36_CALI_SCHEME_NONE;
    esp_err_t err;

    if (s_sensor.initialized) {
        return ESP_OK;
    }

    if (config->unit != ADC_UNIT_1 || config->multisample_count == 0U) {
        return ESP_ERR_INVALID_ARG;
    }

    adc_oneshot_unit_init_cfg_t unit_config = {
        .unit_id = config->unit,
    };
    err = adc_oneshot_new_unit(&unit_config, &adc_handle);
    if (err != ESP_OK) {
        return err;
    }

    adc_oneshot_chan_cfg_t channel_config = {
        .atten = config->attenuation,
        .bitwidth = config->bitwidth,
    };
    err = adc_oneshot_config_channel(adc_handle, config->channel, &channel_config);
    if (err != ESP_OK) {
        adc_oneshot_del_unit(adc_handle);
        return err;
    }

    err = sensor_tmp36_create_calibration(&cali_handle, &cali_scheme);
    if (err != ESP_OK) {
        adc_oneshot_del_unit(adc_handle);
        return err;
    }

    s_sensor.adc_handle = adc_handle;
    s_sensor.cali_handle = cali_handle;
    s_sensor.cali_scheme = cali_scheme;
    s_sensor.initialized = true;
    s_sensor.has_last_reading = false;
    memset(&s_sensor.last_reading, 0, sizeof(s_sensor.last_reading));

    ESP_LOGI(TAG,
             "initialized ADC unit=%d channel=%d gpio=%d samples=%" PRIu32,
             config->unit,
             config->channel,
             config->gpio_num,
             config->multisample_count);
    return ESP_OK;
}

esp_err_t sensor_tmp36_deinit(void)
{
    esp_err_t err = ESP_OK;

    if (!s_sensor.initialized) {
        return ESP_OK;
    }

    if (s_sensor.cali_handle != NULL) {
        err = sensor_tmp36_delete_calibration(s_sensor.cali_handle, s_sensor.cali_scheme);
        if (err != ESP_OK) {
            return err;
        }
    }

    if (s_sensor.adc_handle != NULL) {
        err = adc_oneshot_del_unit(s_sensor.adc_handle);
        if (err != ESP_OK) {
            return err;
        }
    }

    s_sensor = (sensor_tmp36_state_t){0};
    return ESP_OK;
}

bool sensor_tmp36_is_initialized(void)
{
    return s_sensor.initialized;
}

esp_err_t sensor_tmp36_read(sensor_tmp36_reading_t *reading_out)
{
    sensor_tmp36_reading_t reading = {0};
    const app_config_sensor_t *config = sensor_tmp36_config();
    int raw_average = 0;
    int calibrated_mv = 0;
    esp_err_t err;

    if (reading_out == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!s_sensor.initialized || s_sensor.cali_handle == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    err = sensor_tmp36_read_average_raw(&raw_average);
    if (err != ESP_OK) {
        return err;
    }

    err = adc_cali_raw_to_voltage(s_sensor.cali_handle, raw_average, &calibrated_mv);
    if (err != ESP_OK) {
        return err;
    }

    reading.valid = true;
    reading.sample_count = config->multisample_count;
    reading.raw_average = raw_average;
    reading.calibrated_mv = calibrated_mv;
    reading.temperature_c = sensor_tmp36_mv_to_celsius(reading.calibrated_mv);
    *reading_out = reading;

    portENTER_CRITICAL(&s_sensor_lock);
    s_sensor.last_reading = reading;
    s_sensor.has_last_reading = true;
    portEXIT_CRITICAL(&s_sensor_lock);

    return ESP_OK;
}

esp_err_t sensor_tmp36_get_last_reading(sensor_tmp36_reading_t *reading_out)
{
    if (reading_out == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    portENTER_CRITICAL(&s_sensor_lock);
    if (!s_sensor.has_last_reading) {
        portEXIT_CRITICAL(&s_sensor_lock);
        return ESP_ERR_NOT_FOUND;
    }
    *reading_out = s_sensor.last_reading;
    portEXIT_CRITICAL(&s_sensor_lock);
    return ESP_OK;
}

esp_err_t sensor_tmp36_read_mv(int *millivolts_out)
{
    sensor_tmp36_reading_t reading = {0};
    esp_err_t err;

    if (millivolts_out == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    err = sensor_tmp36_read(&reading);
    if (err != ESP_OK) {
        return err;
    }

    *millivolts_out = reading.calibrated_mv;
    return ESP_OK;
}

esp_err_t sensor_tmp36_read_celsius(float *temperature_c_out)
{
    sensor_tmp36_reading_t reading = {0};
    esp_err_t err;

    if (temperature_c_out == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    err = sensor_tmp36_read(&reading);
    if (err != ESP_OK) {
        return err;
    }

    *temperature_c_out = reading.temperature_c;
    return ESP_OK;
}

float sensor_tmp36_mv_to_celsius(int millivolts)
{
    return ((float)millivolts - TMP36_OFFSET_MV) / TMP36_MV_PER_DEG_C;
}
