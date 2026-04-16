#include "sparkplug_node.h"

#include <string.h>

#include "pb_encode.h"
#include "sparkplug_node_internal.h"

static bool sparkplug_node_encode_string(pb_ostream_t *stream, const pb_field_t *field, void * const *arg)
{
    const char *value = (const char *)(*arg);

    if (value == NULL) {
        return true;
    }

    if (!pb_encode_tag_for_field(stream, field)) {
        return false;
    }

    return pb_encode_string(stream, (const pb_byte_t *)value, strlen(value));
}

static bool sparkplug_node_encode_metric(
    pb_ostream_t *stream,
    const sparkplug_node_metric_descriptor_t *descriptor)
{
    org_eclipse_tahu_protobuf_Payload_Metric metric = org_eclipse_tahu_protobuf_Payload_Metric_init_zero;

    if (descriptor->name != NULL) {
        metric.name.funcs.encode = sparkplug_node_encode_string;
        metric.name.arg = (void *)descriptor->name;
    }
    if (descriptor->has_alias) {
        metric.has_alias = true;
        metric.alias = descriptor->alias;
    }
    if (descriptor->has_timestamp) {
        metric.has_timestamp = true;
        metric.timestamp = descriptor->timestamp_ms;
    }
    if (descriptor->has_datatype) {
        metric.has_datatype = true;
        metric.datatype = descriptor->datatype;
    }

    switch (descriptor->value_type) {
    case SPARKPLUG_NODE_VALUE_UINT64:
        metric.which_value = org_eclipse_tahu_protobuf_Payload_Metric_long_value_tag;
        metric.value.long_value = descriptor->value.uint64_value;
        break;
    case SPARKPLUG_NODE_VALUE_INT64:
        metric.which_value = org_eclipse_tahu_protobuf_Payload_Metric_long_value_tag;
        metric.value.long_value = descriptor->value.int64_value;
        break;
    case SPARKPLUG_NODE_VALUE_BOOL:
        metric.which_value = org_eclipse_tahu_protobuf_Payload_Metric_boolean_value_tag;
        metric.value.boolean_value = descriptor->value.bool_value;
        break;
    case SPARKPLUG_NODE_VALUE_FLOAT:
        metric.which_value = org_eclipse_tahu_protobuf_Payload_Metric_float_value_tag;
        metric.value.float_value = descriptor->value.float_value;
        break;
    default:
        return false;
    }

    return pb_encode_submessage(stream, org_eclipse_tahu_protobuf_Payload_Metric_fields, &metric);
}

static bool sparkplug_node_encode_metrics(pb_ostream_t *stream, const pb_field_t *field, void * const *arg)
{
    const sparkplug_node_metric_list_t *metric_list = (const sparkplug_node_metric_list_t *)(*arg);

    if (metric_list == NULL) {
        return true;
    }

    for (size_t i = 0; i < metric_list->metric_count; ++i) {
        if (!pb_encode_tag_for_field(stream, field)) {
            return false;
        }
        if (!sparkplug_node_encode_metric(stream, &metric_list->metrics[i])) {
            return false;
        }
    }

    return true;
}

bool sparkplug_node_encode_payload(
    const org_eclipse_tahu_protobuf_Payload *payload,
    uint8_t *buffer,
    size_t buffer_size,
    size_t *encoded_size)
{
    pb_ostream_t stream;

    if (payload == NULL || buffer == NULL || buffer_size == 0U || encoded_size == NULL) {
        return false;
    }

    stream = pb_ostream_from_buffer(buffer, buffer_size);
    if (!pb_encode(&stream, org_eclipse_tahu_protobuf_Payload_fields, payload)) {
        return false;
    }

    *encoded_size = stream.bytes_written;
    return true;
}

void sparkplug_node_prepare_metric_callback(
    org_eclipse_tahu_protobuf_Payload *payload,
    const sparkplug_node_metric_list_t *metric_list)
{
    payload->metrics.funcs.encode = sparkplug_node_encode_metrics;
    payload->metrics.arg = (void *)metric_list;
}

esp_err_t sparkplug_node_encode_nbirth(
    const sparkplug_node_birth_payload_t *payload,
    uint8_t *buffer,
    size_t buffer_size,
    size_t *encoded_size)
{
    org_eclipse_tahu_protobuf_Payload message = org_eclipse_tahu_protobuf_Payload_init_zero;
    const sparkplug_node_metric_descriptor_t metrics[] = {
        {
            .name = SPARKPLUG_NODE_METRIC_NAME_BDSEQ,
            .has_datatype = true,
            .datatype = org_eclipse_tahu_protobuf_DataType_Int64,
            .value_type = SPARKPLUG_NODE_VALUE_INT64,
            .value.int64_value = payload != NULL ? (int64_t)payload->bdseq : 0,
        },
        {
            .name = SPARKPLUG_NODE_METRIC_NAME_NODE_CONTROL_REBIRTH,
            .alias = SPARKPLUG_NODE_METRIC_ALIAS_NODE_CONTROL_REBIRTH,
            .has_datatype = true,
            .datatype = org_eclipse_tahu_protobuf_DataType_Boolean,
            .value_type = SPARKPLUG_NODE_VALUE_BOOL,
            .value.bool_value = false,
        },
        {
            .name = SPARKPLUG_NODE_METRIC_NAME_TEMPERATURE_C,
            .has_alias = true,
            .alias = SPARKPLUG_NODE_METRIC_ALIAS_TEMPERATURE_C,
            .has_timestamp = true,
            .timestamp_ms = payload != NULL ? payload->timestamp_ms : 0U,
            .has_datatype = true,
            .datatype = org_eclipse_tahu_protobuf_DataType_Float,
            .value_type = SPARKPLUG_NODE_VALUE_FLOAT,
            .value.float_value = payload != NULL ? payload->temperature_c : 0.0f,
        },
        {
            .name = SPARKPLUG_NODE_METRIC_NAME_SYNTHETIC_SINEWAVE,
            .has_alias = true,
            .alias = SPARKPLUG_NODE_METRIC_ALIAS_SYNTHETIC_SINEWAVE,
            .has_timestamp = true,
            .timestamp_ms = payload != NULL ? payload->timestamp_ms : 0U,
            .has_datatype = true,
            .datatype = org_eclipse_tahu_protobuf_DataType_Float,
            .value_type = SPARKPLUG_NODE_VALUE_FLOAT,
            .value.float_value = payload != NULL ? payload->synthetic_sinewave : 0.0f,
        },
    };
    const sparkplug_node_metric_list_t metric_list = {
        .metrics = metrics,
        .metric_count = sizeof(metrics) / sizeof(metrics[0]),
    };

    if (payload == NULL || buffer == NULL || encoded_size == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    message.has_timestamp = true;
    message.timestamp = payload->timestamp_ms;
    message.has_seq = true;
    message.seq = 0U;
    sparkplug_node_prepare_metric_callback(&message, &metric_list);

    return sparkplug_node_encode_payload(&message, buffer, buffer_size, encoded_size) ? ESP_OK : ESP_FAIL;
}

esp_err_t sparkplug_node_encode_ndata(
    const sparkplug_node_data_payload_t *payload,
    uint8_t *buffer,
    size_t buffer_size,
    size_t *encoded_size)
{
    org_eclipse_tahu_protobuf_Payload message = org_eclipse_tahu_protobuf_Payload_init_zero;
    const sparkplug_node_metric_descriptor_t metrics[] = {
        {
            .has_alias = true,
            .alias = SPARKPLUG_NODE_METRIC_ALIAS_TEMPERATURE_C,
            .has_timestamp = true,
            .timestamp_ms = payload != NULL ? payload->timestamp_ms : 0U,
            .value_type = SPARKPLUG_NODE_VALUE_FLOAT,
            .value.float_value = payload != NULL ? payload->temperature_c : 0.0f,
        },
        {
            .has_alias = true,
            .alias = SPARKPLUG_NODE_METRIC_ALIAS_SYNTHETIC_SINEWAVE,
            .has_timestamp = true,
            .timestamp_ms = payload != NULL ? payload->timestamp_ms : 0U,
            .value_type = SPARKPLUG_NODE_VALUE_FLOAT,
            .value.float_value = payload != NULL ? payload->synthetic_sinewave : 0.0f,
        },
    };
    const sparkplug_node_metric_list_t metric_list = {
        .metrics = metrics,
        .metric_count = sizeof(metrics) / sizeof(metrics[0]),
    };

    if (payload == NULL || buffer == NULL || encoded_size == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    message.has_timestamp = true;
    message.timestamp = payload->timestamp_ms;
    message.has_seq = true;
    message.seq = payload->seq;
    sparkplug_node_prepare_metric_callback(&message, &metric_list);

    return sparkplug_node_encode_payload(&message, buffer, buffer_size, encoded_size) ? ESP_OK : ESP_FAIL;
}

esp_err_t sparkplug_node_encode_ndeath(
    const sparkplug_node_death_payload_t *payload,
    uint8_t *buffer,
    size_t buffer_size,
    size_t *encoded_size)
{
    org_eclipse_tahu_protobuf_Payload message = org_eclipse_tahu_protobuf_Payload_init_zero;
    const sparkplug_node_metric_descriptor_t metrics[] = {
        {
            .name = SPARKPLUG_NODE_METRIC_NAME_BDSEQ,
            .has_datatype = true,
            .datatype = org_eclipse_tahu_protobuf_DataType_Int64,
            .value_type = SPARKPLUG_NODE_VALUE_INT64,
            .value.int64_value = payload != NULL ? (int64_t)payload->bdseq : 0,
        },
    };
    const sparkplug_node_metric_list_t metric_list = {
        .metrics = metrics,
        .metric_count = sizeof(metrics) / sizeof(metrics[0]),
    };

    if (payload == NULL || buffer == NULL || encoded_size == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    message.has_timestamp = true;
    message.timestamp = payload->timestamp_ms;
    sparkplug_node_prepare_metric_callback(&message, &metric_list);

    return sparkplug_node_encode_payload(&message, buffer, buffer_size, encoded_size) ? ESP_OK : ESP_FAIL;
}
