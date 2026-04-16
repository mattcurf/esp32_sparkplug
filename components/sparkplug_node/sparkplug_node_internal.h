/*
 * SPDX-FileCopyrightText: 2026 Matt Curfman
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "pb.h"
#include "sparkplug_b.pb.h"

typedef enum {
    SPARKPLUG_NODE_VALUE_UINT64 = 0,
    SPARKPLUG_NODE_VALUE_INT64,
    SPARKPLUG_NODE_VALUE_BOOL,
    SPARKPLUG_NODE_VALUE_FLOAT,
} sparkplug_node_metric_value_type_t;

typedef struct {
    const char *name;
    bool has_alias;
    uint64_t alias;
    bool has_timestamp;
    uint64_t timestamp_ms;
    bool has_datatype;
    uint32_t datatype;
    sparkplug_node_metric_value_type_t value_type;
    union {
        uint64_t uint64_value;
        int64_t int64_value;
        bool bool_value;
        float float_value;
    } value;
} sparkplug_node_metric_descriptor_t;

typedef struct {
    const sparkplug_node_metric_descriptor_t *metrics;
    size_t metric_count;
} sparkplug_node_metric_list_t;

bool sparkplug_node_encode_payload(
    const org_eclipse_tahu_protobuf_Payload *payload,
    uint8_t *buffer,
    size_t buffer_size,
    size_t *encoded_size);

void sparkplug_node_prepare_metric_callback(
    org_eclipse_tahu_protobuf_Payload *payload,
    const sparkplug_node_metric_list_t *metric_list);
