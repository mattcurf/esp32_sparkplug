#include "sparkplug_node.h"

#include <stdio.h>

static const char *sparkplug_node_topic_type_suffix(sparkplug_node_topic_type_t topic_type)
{
    switch (topic_type) {
    case SPARKPLUG_NODE_TOPIC_NBIRTH:
        return "NBIRTH";
    case SPARKPLUG_NODE_TOPIC_NDATA:
        return "NDATA";
    case SPARKPLUG_NODE_TOPIC_NDEATH:
        return "NDEATH";
    case SPARKPLUG_NODE_TOPIC_NCMD:
        return "NCMD";
    default:
        return NULL;
    }
}

esp_err_t sparkplug_node_build_topic(
    const sparkplug_node_topic_config_t *config,
    sparkplug_node_topic_type_t topic_type,
    char *buffer,
    size_t buffer_size)
{
    const char *suffix = NULL;
    int written = 0;

    if (config == NULL || config->group_id == NULL || config->node_id == NULL || buffer == NULL || buffer_size == 0U) {
        return ESP_ERR_INVALID_ARG;
    }

    suffix = sparkplug_node_topic_type_suffix(topic_type);
    if (suffix == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    written = snprintf(
        buffer,
        buffer_size,
        "%s/%s/%s/%s",
        SPARKPLUG_NODE_TOPIC_NAMESPACE,
        config->group_id,
        suffix,
        config->node_id);
    if (written < 0) {
        return ESP_FAIL;
    }
    if ((size_t)written >= buffer_size) {
        return ESP_ERR_INVALID_SIZE;
    }

    return ESP_OK;
}
