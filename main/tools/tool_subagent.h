#pragma once

#include "esp_err.h"
#include <stddef.h>

esp_err_t tool_subagent_create_execute(const char *input_json, char *output, size_t output_size);
esp_err_t tool_subagent_status_execute(const char *input_json, char *output, size_t output_size);
esp_err_t tool_subagent_list_execute(const char *input_json, char *output, size_t output_size);
esp_err_t tool_subagent_cancel_execute(const char *input_json, char *output, size_t output_size);

