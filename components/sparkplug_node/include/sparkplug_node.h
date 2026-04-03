#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#if __has_include("esp_err.h")
#include "esp_err.h"
#else
typedef int esp_err_t;

#ifndef ESP_OK
#define ESP_OK 0
#endif
#ifndef ESP_FAIL
#define ESP_FAIL -1
#endif
#ifndef ESP_ERR_INVALID_ARG
#define ESP_ERR_INVALID_ARG 0x102
#endif
#ifndef ESP_ERR_INVALID_SIZE
#define ESP_ERR_INVALID_SIZE 0x104
#endif
#endif

#ifdef __cplusplus
extern "C" {
#endif

#define SPARKPLUG_NODE_TOPIC_NAMESPACE "spBv1.0"
#define SPARKPLUG_NODE_METRIC_NAME_BDSEQ "bdSeq"
#define SPARKPLUG_NODE_METRIC_NAME_NODE_CONTROL_REBIRTH "Node Control/Rebirth"
#define SPARKPLUG_NODE_METRIC_NAME_TEMPERATURE_C "temperature_c"
#define SPARKPLUG_NODE_METRIC_NAME_SYNTHETIC_SINEWAVE "synthetic_sinewave"
#define SPARKPLUG_NODE_METRIC_ALIAS_NODE_CONTROL_REBIRTH 1U
#define SPARKPLUG_NODE_METRIC_ALIAS_TEMPERATURE_C 2U
#define SPARKPLUG_NODE_METRIC_ALIAS_SYNTHETIC_SINEWAVE 3U

typedef enum {
    SPARKPLUG_NODE_TOPIC_NBIRTH = 0,
    SPARKPLUG_NODE_TOPIC_NDATA,
    SPARKPLUG_NODE_TOPIC_NDEATH,
    SPARKPLUG_NODE_TOPIC_NCMD,
} sparkplug_node_topic_type_t;

typedef struct {
    const char *group_id;
    const char *node_id;
} sparkplug_node_topic_config_t;

typedef struct {
    uint64_t timestamp_ms;
    uint64_t bdseq;
    float temperature_c;
    float synthetic_sinewave;
} sparkplug_node_birth_payload_t;

typedef struct {
    uint64_t timestamp_ms;
    uint64_t seq;
    float temperature_c;
    float synthetic_sinewave;
} sparkplug_node_data_payload_t;

typedef struct {
    uint64_t timestamp_ms;
    uint64_t bdseq;
} sparkplug_node_death_payload_t;

typedef struct {
    bool rebirth_requested;
} sparkplug_node_ncmd_t;

esp_err_t sparkplug_node_build_topic(
    const sparkplug_node_topic_config_t *config,
    sparkplug_node_topic_type_t topic_type,
    char *buffer,
    size_t buffer_size);

esp_err_t sparkplug_node_encode_nbirth(
    const sparkplug_node_birth_payload_t *payload,
    uint8_t *buffer,
    size_t buffer_size,
    size_t *encoded_size);

esp_err_t sparkplug_node_encode_ndata(
    const sparkplug_node_data_payload_t *payload,
    uint8_t *buffer,
    size_t buffer_size,
    size_t *encoded_size);

esp_err_t sparkplug_node_encode_ndeath(
    const sparkplug_node_death_payload_t *payload,
    uint8_t *buffer,
    size_t buffer_size,
    size_t *encoded_size);

esp_err_t sparkplug_node_decode_ncmd(
    const uint8_t *buffer,
    size_t buffer_size,
    sparkplug_node_ncmd_t *command);

#ifdef __cplusplus
}
#endif
