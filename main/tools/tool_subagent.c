#include "tools/tool_subagent.h"

#include "subagent/subagent_service.h"
#include "bus/message_bus.h"

#include <string.h>
#include <stdio.h>
#include <stdbool.h>
#include "cJSON.h"

static esp_err_t parse_common(const char *input_json, cJSON **out)
{
    *out = cJSON_Parse(input_json ? input_json : "{}");
    return *out ? ESP_OK : ESP_ERR_INVALID_ARG;
}

static bool is_valid_agent_type(const char *agent_type)
{
    if (!agent_type || !agent_type[0]) return true;
    return strcmp(agent_type, "explorer") == 0 ||
           strcmp(agent_type, "planner") == 0 ||
           strcmp(agent_type, "implementer") == 0 ||
           strcmp(agent_type, "reviewer") == 0 ||
           strcmp(agent_type, "custom") == 0;
}

esp_err_t tool_subagent_create_execute(const char *input_json, char *output, size_t output_size)
{
    cJSON *root = NULL;
    if (parse_common(input_json, &root) != ESP_OK) {
        snprintf(output, output_size, "Error: invalid JSON");
        return ESP_ERR_INVALID_ARG;
    }

    const char *goal = cJSON_GetStringValue(cJSON_GetObjectItem(root, "goal"));
    if (!goal || !goal[0]) {
        snprintf(output, output_size, "Error: missing goal");
        cJSON_Delete(root);
        return ESP_ERR_INVALID_ARG;
    }

    subagent_job_spec_t spec = {0};
    const char *reply_channel = cJSON_GetStringValue(cJSON_GetObjectItem(root, "reply_channel"));
    const char *reply_chat_id = cJSON_GetStringValue(cJSON_GetObjectItem(root, "reply_chat_id"));
    const char *created_by = cJSON_GetStringValue(cJSON_GetObjectItem(root, "created_by"));
    const char *context = cJSON_GetStringValue(cJSON_GetObjectItem(root, "context"));
    const char *agent_type = cJSON_GetStringValue(cJSON_GetObjectItem(root, "agent_type"));
    const char *custom_prompt = cJSON_GetStringValue(cJSON_GetObjectItem(root, "custom_prompt"));

    if (!is_valid_agent_type(agent_type)) {
        snprintf(output, output_size, "Error: invalid agent_type");
        cJSON_Delete(root);
        return ESP_ERR_INVALID_ARG;
    }
    if (agent_type && strcmp(agent_type, "custom") == 0 &&
        (!custom_prompt || !custom_prompt[0])) {
        snprintf(output, output_size, "Error: custom_prompt is required when agent_type=custom");
        cJSON_Delete(root);
        return ESP_ERR_INVALID_ARG;
    }

    strncpy(spec.goal, goal, sizeof(spec.goal) - 1);
    if (reply_channel) strncpy(spec.source_channel, reply_channel, sizeof(spec.source_channel) - 1);
    if (reply_chat_id) strncpy(spec.source_chat_id, reply_chat_id, sizeof(spec.source_chat_id) - 1);
    if (created_by) strncpy(spec.created_by, created_by, sizeof(spec.created_by) - 1);
    else strncpy(spec.created_by, "agent", sizeof(spec.created_by) - 1);
    if (agent_type && agent_type[0]) strncpy(spec.agent_type, agent_type, sizeof(spec.agent_type) - 1);
    else strncpy(spec.agent_type, "explorer", sizeof(spec.agent_type) - 1);
    if (context) strncpy(spec.context_json, context, sizeof(spec.context_json) - 1);
    if (custom_prompt) strncpy(spec.custom_prompt, custom_prompt, sizeof(spec.custom_prompt) - 1);

    char job_id[24] = {0};
    esp_err_t err = subagent_submit(&spec, job_id, sizeof(job_id));
    if (err != ESP_OK) {
        snprintf(output, output_size, "Error: subagent_create failed (%s)", esp_err_to_name(err));
        cJSON_Delete(root);
        return err;
    }
    snprintf(output, output_size, "OK: subagent job created (id=%s)", job_id);
    cJSON_Delete(root);
    return ESP_OK;
}

esp_err_t tool_subagent_status_execute(const char *input_json, char *output, size_t output_size)
{
    cJSON *root = NULL;
    if (parse_common(input_json, &root) != ESP_OK) {
        snprintf(output, output_size, "Error: invalid JSON");
        return ESP_ERR_INVALID_ARG;
    }
    const char *job_id = cJSON_GetStringValue(cJSON_GetObjectItem(root, "job_id"));
    if (!job_id || !job_id[0]) {
        snprintf(output, output_size, "Error: missing job_id");
        cJSON_Delete(root);
        return ESP_ERR_INVALID_ARG;
    }

    subagent_job_info_t info;
    esp_err_t err = subagent_get(job_id, &info);
    if (err != ESP_OK) {
        snprintf(output, output_size, "Error: job not found (%s)", esp_err_to_name(err));
        cJSON_Delete(root);
        return err;
    }

    snprintf(output, output_size,
             "job_id=%s status=%d type=%s created=%lld updated=%lld finished=%lld error=%s result=%s",
             info.job_id, (int)info.status,
             info.agent_type[0] ? info.agent_type : "explorer",
             (long long)info.created_ts, (long long)info.updated_ts, (long long)info.finished_ts,
             info.last_error[0] ? info.last_error : "(none)",
             info.result_summary[0] ? info.result_summary : "(none)");
    cJSON_Delete(root);
    return ESP_OK;
}

esp_err_t tool_subagent_list_execute(const char *input_json, char *output, size_t output_size)
{
    (void)input_json;
    return subagent_list(output, output_size);
}

esp_err_t tool_subagent_cancel_execute(const char *input_json, char *output, size_t output_size)
{
    cJSON *root = NULL;
    if (parse_common(input_json, &root) != ESP_OK) {
        snprintf(output, output_size, "Error: invalid JSON");
        return ESP_ERR_INVALID_ARG;
    }
    const char *job_id = cJSON_GetStringValue(cJSON_GetObjectItem(root, "job_id"));
    if (!job_id || !job_id[0]) {
        snprintf(output, output_size, "Error: missing job_id");
        cJSON_Delete(root);
        return ESP_ERR_INVALID_ARG;
    }
    esp_err_t err = subagent_cancel(job_id);
    if (err != ESP_OK) {
        snprintf(output, output_size, "Error: cancel failed (%s)", esp_err_to_name(err));
        cJSON_Delete(root);
        return err;
    }
    snprintf(output, output_size, "OK: cancelled %s", job_id);
    cJSON_Delete(root);
    return ESP_OK;
}
