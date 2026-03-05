#pragma once

#include "esp_err.h"
#include <stddef.h>

typedef struct {
    const char *channel;
    const char *chat_id;
    const char *type;
} lmchan_tool_exec_ctx_t;

typedef struct {
    const char *name;
    const char *description;
    const char *input_schema_json;  /* JSON Schema string for input */
    esp_err_t (*execute)(const char *input_json, char *output, size_t output_size);
} lmchan_tool_t;

/**
 * Initialize tool registry and register all built-in tools.
 */
esp_err_t tool_registry_init(void);

/**
 * Get the pre-built tools JSON array string for the API request.
 * Returns NULL if no tools are registered.
 */
const char *tool_registry_get_tools_json(void);

/**
 * Get tools JSON filtered by execution context and current policy.
 * Falls back to all tools when ctx is NULL.
 */
const char *tool_registry_get_tools_json_for_context(const lmchan_tool_exec_ctx_t *ctx);

/**
 * Execute a tool by name.
 *
 * @param name         Tool name (e.g. "web_search")
 * @param input_json   JSON string of tool input
 * @param output       Output buffer for tool result text
 * @param output_size  Size of output buffer
 * @return ESP_OK on success, ESP_ERR_NOT_FOUND if tool unknown
 */
esp_err_t tool_registry_execute(const char *name, const char *input_json,
                                char *output, size_t output_size);

/**
 * Execute a tool by name with context-aware policy enforcement.
 */
esp_err_t tool_registry_execute_with_context(const char *name, const char *input_json,
                                             char *output, size_t output_size,
                                             const lmchan_tool_exec_ctx_t *ctx);
