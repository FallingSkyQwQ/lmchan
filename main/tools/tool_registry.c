#include "tool_registry.h"
#include "lmchan_config.h"
#include "tools/tool_policy.h"
#include "tools/tool_web_search.h"
#include "tools/tool_get_time.h"
#include "tools/tool_files.h"
#include "tools/tool_cron.h"
#include "tools/tool_subagent.h"

#include <string.h>
#include <stdlib.h>
#include "esp_log.h"
#include "cJSON.h"

static const char *TAG = "tools";

#define MAX_TOOLS 20

static lmchan_tool_t s_tools[MAX_TOOLS];
static int s_tool_count = 0;
static char *s_tools_json = NULL;  /* cached JSON array string */
static char *s_tools_json_filtered = NULL;  /* context-filtered JSON array string */

static void register_tool(const lmchan_tool_t *tool)
{
    if (s_tool_count >= MAX_TOOLS) {
        ESP_LOGE(TAG, "Tool registry full");
        return;
    }
    s_tools[s_tool_count++] = *tool;
    ESP_LOGI(TAG, "Registered tool: %s", tool->name);
}

static void build_tools_json(void)
{
    cJSON *arr = cJSON_CreateArray();

    for (int i = 0; i < s_tool_count; i++) {
        cJSON *tool = cJSON_CreateObject();
        cJSON_AddStringToObject(tool, "name", s_tools[i].name);
        cJSON_AddStringToObject(tool, "description", s_tools[i].description);

        cJSON *schema = cJSON_Parse(s_tools[i].input_schema_json);
        if (schema) {
            cJSON_AddItemToObject(tool, "input_schema", schema);
        }

        cJSON_AddItemToArray(arr, tool);
    }

    free(s_tools_json);
    s_tools_json = cJSON_PrintUnformatted(arr);
    cJSON_Delete(arr);

    ESP_LOGI(TAG, "Tools JSON built (%d tools)", s_tool_count);
}

static bool tool_allowed_for_context(const char *tool_name, const lmchan_tool_exec_ctx_t *ctx,
                                     const char **reason_out)
{
    return tool_policy_is_allowed(ctx, tool_name, reason_out);
}

static void build_tools_json_for_context(const lmchan_tool_exec_ctx_t *ctx)
{
    cJSON *arr = cJSON_CreateArray();

    for (int i = 0; i < s_tool_count; i++) {
        if (!tool_allowed_for_context(s_tools[i].name, ctx, NULL)) {
            continue;
        }
        cJSON *tool = cJSON_CreateObject();
        cJSON_AddStringToObject(tool, "name", s_tools[i].name);
        cJSON_AddStringToObject(tool, "description", s_tools[i].description);

        cJSON *schema = cJSON_Parse(s_tools[i].input_schema_json);
        if (schema) {
            cJSON_AddItemToObject(tool, "input_schema", schema);
        }

        cJSON_AddItemToArray(arr, tool);
    }

    free(s_tools_json_filtered);
    s_tools_json_filtered = cJSON_PrintUnformatted(arr);
    cJSON_Delete(arr);
}

esp_err_t tool_registry_init(void)
{
    s_tool_count = 0;
    tool_policy_init();

    /* Register web_search */
    tool_web_search_init();

    lmchan_tool_t ws = {
        .name = "web_search",
        .description = "Search the web for current information. Use this when you need up-to-date facts, news, weather, or anything beyond your training data.",
        .input_schema_json =
            "{\"type\":\"object\","
            "\"properties\":{\"query\":{\"type\":\"string\",\"description\":\"The search query\"}},"
            "\"required\":[\"query\"]}",
        .execute = tool_web_search_execute,
    };
    register_tool(&ws);

    /* Register get_current_time */
    lmchan_tool_t gt = {
        .name = "get_current_time",
        .description = "Get the current date and time. Also sets the system clock. Call this when you need to know what time or date it is.",
        .input_schema_json =
            "{\"type\":\"object\","
            "\"properties\":{},"
            "\"required\":[]}",
        .execute = tool_get_time_execute,
    };
    register_tool(&gt);

    /* Register read_file */
    lmchan_tool_t rf = {
        .name = "read_file",
        .description = "Read a file from SPIFFS storage. Path must start with " LMCHAN_SPIFFS_BASE "/.",
        .input_schema_json =
            "{\"type\":\"object\","
            "\"properties\":{\"path\":{\"type\":\"string\",\"description\":\"Absolute path starting with " LMCHAN_SPIFFS_BASE "/\"}},"
            "\"required\":[\"path\"]}",
        .execute = tool_read_file_execute,
    };
    register_tool(&rf);

    /* Register write_file */
    lmchan_tool_t wf = {
        .name = "write_file",
        .description = "Write or overwrite a file on SPIFFS storage. Path must start with " LMCHAN_SPIFFS_BASE "/.",
        .input_schema_json =
            "{\"type\":\"object\","
            "\"properties\":{\"path\":{\"type\":\"string\",\"description\":\"Absolute path starting with " LMCHAN_SPIFFS_BASE "/\"},"
            "\"content\":{\"type\":\"string\",\"description\":\"File content to write\"}},"
            "\"required\":[\"path\",\"content\"]}",
        .execute = tool_write_file_execute,
    };
    register_tool(&wf);

    /* Register edit_file */
    lmchan_tool_t ef = {
        .name = "edit_file",
        .description = "Find and replace text in a file on SPIFFS. Replaces first occurrence of old_string with new_string.",
        .input_schema_json =
            "{\"type\":\"object\","
            "\"properties\":{\"path\":{\"type\":\"string\",\"description\":\"Absolute path starting with " LMCHAN_SPIFFS_BASE "/\"},"
            "\"old_string\":{\"type\":\"string\",\"description\":\"Text to find\"},"
            "\"new_string\":{\"type\":\"string\",\"description\":\"Replacement text\"}},"
            "\"required\":[\"path\",\"old_string\",\"new_string\"]}",
        .execute = tool_edit_file_execute,
    };
    register_tool(&ef);

    /* Register list_dir */
    lmchan_tool_t ld = {
        .name = "list_dir",
        .description = "List files on SPIFFS storage, optionally filtered by path prefix.",
        .input_schema_json =
            "{\"type\":\"object\","
            "\"properties\":{\"prefix\":{\"type\":\"string\",\"description\":\"Optional path prefix filter, e.g. " LMCHAN_SPIFFS_BASE "/memory/\"}},"
            "\"required\":[]}",
        .execute = tool_list_dir_execute,
    };
    register_tool(&ld);

    /* Register cron_add */
    lmchan_tool_t ca = {
        .name = "cron_add",
        .description = "Schedule a recurring or one-shot task. The message will trigger an agent turn when the job fires.",
        .input_schema_json =
            "{\"type\":\"object\","
            "\"properties\":{"
            "\"name\":{\"type\":\"string\",\"description\":\"Short name for the job\"},"
            "\"schedule_type\":{\"type\":\"string\",\"description\":\"'every' for recurring interval or 'at' for one-shot at a unix timestamp\"},"
            "\"interval_s\":{\"type\":\"integer\",\"description\":\"Interval in seconds (required for 'every')\"},"
            "\"at_epoch\":{\"type\":\"integer\",\"description\":\"Unix timestamp to fire at (required for 'at')\"},"
            "\"message\":{\"type\":\"string\",\"description\":\"Message to inject when the job fires, triggering an agent turn\"},"
            "\"channel\":{\"type\":\"string\",\"description\":\"Optional reply channel (e.g. 'feishu'). If omitted, current turn channel is used when available\"},"
            "\"chat_id\":{\"type\":\"string\",\"description\":\"Optional reply chat_id. Required when channel='feishu'. If omitted during a Feishu turn, current chat_id is used\"}"
            "},"
            "\"required\":[\"name\",\"schedule_type\",\"message\"]}",
        .execute = tool_cron_add_execute,
    };
    register_tool(&ca);

    /* Register cron_list */
    lmchan_tool_t cl = {
        .name = "cron_list",
        .description = "List all scheduled cron jobs with their status, schedule, and IDs.",
        .input_schema_json =
            "{\"type\":\"object\","
            "\"properties\":{},"
            "\"required\":[]}",
        .execute = tool_cron_list_execute,
    };
    register_tool(&cl);

    /* Register cron_remove */
    lmchan_tool_t cr = {
        .name = "cron_remove",
        .description = "Remove a scheduled cron job by its ID.",
        .input_schema_json =
            "{\"type\":\"object\","
            "\"properties\":{\"job_id\":{\"type\":\"string\",\"description\":\"The 8-character job ID to remove\"}},"
            "\"required\":[\"job_id\"]}",
        .execute = tool_cron_remove_execute,
    };
    register_tool(&cr);

    /* Register subagent_create */
    lmchan_tool_t sa_create = {
        .name = "subagent_create",
        .description = "Create a background subagent job to execute a longer task asynchronously.",
        .input_schema_json =
            "{\"type\":\"object\","
            "\"properties\":{"
            "\"goal\":{\"type\":\"string\",\"description\":\"Task objective for the subagent\"},"
            "\"context\":{\"type\":\"string\",\"description\":\"Optional JSON/string context for the task\"},"
            "\"agent_type\":{\"type\":\"string\",\"description\":\"Subagent preset role: explorer | planner | implementer | reviewer | custom\"},"
            "\"custom_prompt\":{\"type\":\"string\",\"description\":\"Required when agent_type='custom'; full custom system instructions\"},"
            "\"reply_channel\":{\"type\":\"string\",\"description\":\"Optional result channel (default current turn channel)\"},"
            "\"reply_chat_id\":{\"type\":\"string\",\"description\":\"Optional result chat id (default current turn chat_id)\"}"
            "},"
            "\"required\":[\"goal\"]}",
        .execute = tool_subagent_create_execute,
    };
    register_tool(&sa_create);

    /* Register subagent_status */
    lmchan_tool_t sa_status = {
        .name = "subagent_status",
        .description = "Get status and result summary of a subagent job by job_id.",
        .input_schema_json =
            "{\"type\":\"object\","
            "\"properties\":{\"job_id\":{\"type\":\"string\",\"description\":\"Subagent job id\"}},"
            "\"required\":[\"job_id\"]}",
        .execute = tool_subagent_status_execute,
    };
    register_tool(&sa_status);

    /* Register subagent_list */
    lmchan_tool_t sa_list = {
        .name = "subagent_list",
        .description = "List all subagent jobs and their current status.",
        .input_schema_json =
            "{\"type\":\"object\",\"properties\":{},\"required\":[]}",
        .execute = tool_subagent_list_execute,
    };
    register_tool(&sa_list);

    /* Register subagent_cancel */
    lmchan_tool_t sa_cancel = {
        .name = "subagent_cancel",
        .description = "Cancel a queued/running subagent job.",
        .input_schema_json =
            "{\"type\":\"object\","
            "\"properties\":{\"job_id\":{\"type\":\"string\",\"description\":\"Subagent job id\"}},"
            "\"required\":[\"job_id\"]}",
        .execute = tool_subagent_cancel_execute,
    };
    register_tool(&sa_cancel);

    build_tools_json();

    ESP_LOGI(TAG, "Tool registry initialized");
    return ESP_OK;
}

const char *tool_registry_get_tools_json(void)
{
    return s_tools_json;
}

const char *tool_registry_get_tools_json_for_context(const lmchan_tool_exec_ctx_t *ctx)
{
    if (!ctx) {
        return s_tools_json;
    }
    tool_policy_load_if_changed();
    build_tools_json_for_context(ctx);
    return s_tools_json_filtered ? s_tools_json_filtered : "[]";
}

esp_err_t tool_registry_execute(const char *name, const char *input_json,
                                char *output, size_t output_size)
{
    return tool_registry_execute_with_context(name, input_json, output, output_size, NULL);
}

esp_err_t tool_registry_execute_with_context(const char *name, const char *input_json,
                                             char *output, size_t output_size,
                                             const lmchan_tool_exec_ctx_t *ctx)
{
    tool_policy_load_if_changed();
    const char *reason = NULL;
    if (!tool_allowed_for_context(name, ctx, &reason)) {
        snprintf(output, output_size,
                 "Error: permission denied for tool '%s' (reason=%s channel=%s chat_id=%s type=%s)",
                 name,
                 reason ? reason : "unknown",
                 (ctx && ctx->channel) ? ctx->channel : "(none)",
                 (ctx && ctx->chat_id) ? ctx->chat_id : "(none)",
                 (ctx && ctx->type) ? ctx->type : "unknown");
        ESP_LOGW(TAG, "Tool blocked: %s (reason=%s)", name, reason ? reason : "unknown");
        return ESP_ERR_INVALID_STATE;
    }

    for (int i = 0; i < s_tool_count; i++) {
        if (strcmp(s_tools[i].name, name) == 0) {
            ESP_LOGI(TAG, "Executing tool: %s", name);
            return s_tools[i].execute(input_json, output, output_size);
        }
    }

    ESP_LOGW(TAG, "Unknown tool: %s", name);
    snprintf(output, output_size, "Error: unknown tool '%s'", name);
    return ESP_ERR_NOT_FOUND;
}
