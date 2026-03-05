#pragma once

#include <stdbool.h>
#include "esp_err.h"
#include "tools/tool_registry.h"

/**
 * Initialize policy state and load policy once.
 */
esp_err_t tool_policy_init(void);

/**
 * Reload policy when file timestamp changed.
 */
esp_err_t tool_policy_load_if_changed(void);

/**
 * Evaluate tool access for one context.
 *
 * @param ctx          Execution context (channel/chat_id/type), nullable
 * @param tool_name    Target tool name
 * @param reason_out   Optional static reason string
 */
bool tool_policy_is_allowed(const lmchan_tool_exec_ctx_t *ctx, const char *tool_name,
                            const char **reason_out);
