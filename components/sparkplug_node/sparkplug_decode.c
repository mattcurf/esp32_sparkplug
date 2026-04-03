#include "sparkplug_node.h"

#include <stddef.h>
#include <string.h>

#include "pb_decode.h"
#include "sparkplug_node_internal.h"

typedef struct {
    char buffer[64];
    size_t length;
} sparkplug_node_string_decode_t;

typedef struct {
    sparkplug_node_ncmd_t *command;
} sparkplug_node_ncmd_decode_t;

static bool sparkplug_node_decode_string(pb_istream_t *stream, const pb_field_t *field, void **arg)
{
    sparkplug_node_string_decode_t *state = (sparkplug_node_string_decode_t *)(*arg);
    size_t remaining = stream->bytes_left;
    size_t capacity = sizeof(state->buffer) - 1U;
    (void)field;

    while (remaining > 0U) {
        uint8_t chunk[16];
        size_t to_read = remaining < sizeof(chunk) ? remaining : sizeof(chunk);

        if (!pb_read(stream, chunk, to_read)) {
            return false;
        }

        if (state->length < capacity) {
            size_t available = capacity - state->length;
            size_t to_copy = to_read < available ? to_read : available;
            memcpy(&state->buffer[state->length], chunk, to_copy);
            state->length += to_copy;
        }

        remaining -= to_read;
    }

    state->buffer[state->length] = '\0';
    return true;
}

static bool sparkplug_node_decode_metric(pb_istream_t *stream, const pb_field_t *field, void **arg)
{
    sparkplug_node_ncmd_decode_t *state = (sparkplug_node_ncmd_decode_t *)(*arg);
    sparkplug_node_string_decode_t name = { 0 };
    org_eclipse_tahu_protobuf_Payload_Metric metric = org_eclipse_tahu_protobuf_Payload_Metric_init_zero;
    bool matches_rebirth = false;
    (void)field;

    metric.name.funcs.decode = sparkplug_node_decode_string;
    metric.name.arg = &name;

    if (!pb_decode(stream, org_eclipse_tahu_protobuf_Payload_Metric_fields, &metric)) {
        return false;
    }

    matches_rebirth = (strcmp(name.buffer, SPARKPLUG_NODE_METRIC_NAME_NODE_CONTROL_REBIRTH) == 0)
        || (metric.has_alias && metric.alias == SPARKPLUG_NODE_METRIC_ALIAS_NODE_CONTROL_REBIRTH);

    if (matches_rebirth
        && metric.which_value == org_eclipse_tahu_protobuf_Payload_Metric_boolean_value_tag
        && metric.value.boolean_value) {
        state->command->rebirth_requested = true;
    }

    return true;
}

esp_err_t sparkplug_node_decode_ncmd(
    const uint8_t *buffer,
    size_t buffer_size,
    sparkplug_node_ncmd_t *command)
{
    org_eclipse_tahu_protobuf_Payload payload = org_eclipse_tahu_protobuf_Payload_init_zero;
    sparkplug_node_ncmd_decode_t state = {
        .command = command,
    };
    pb_istream_t stream;

    if (buffer == NULL || buffer_size == 0U || command == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    command->rebirth_requested = false;
    payload.metrics.funcs.decode = sparkplug_node_decode_metric;
    payload.metrics.arg = &state;
    stream = pb_istream_from_buffer(buffer, buffer_size);

    return pb_decode(&stream, org_eclipse_tahu_protobuf_Payload_fields, &payload) ? ESP_OK : ESP_FAIL;
}
