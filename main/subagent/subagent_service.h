#pragma once

#include "esp_err.h"
#include "lmchan_config.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef enum {
    SUBAGENT_JOB_QUEUED = 0,
    SUBAGENT_JOB_RUNNING,
    SUBAGENT_JOB_SUCCEEDED,
    SUBAGENT_JOB_FAILED,
    SUBAGENT_JOB_CANCELLED,
} subagent_job_status_t;

typedef struct {
    char source_channel[16];
    char source_chat_id[96];
    char created_by[16];
    char agent_type[16];
    char goal[512];
    char context_json[1024];
    char custom_prompt[1024];
} subagent_job_spec_t;

typedef struct {
    char job_id[24];
    subagent_job_status_t status;
    int64_t created_ts;
    int64_t updated_ts;
    int64_t finished_ts;
    char last_error[160];
    char result_summary[LMCHAN_SUBAGENT_RESULT_MAX_BYTES];
    char source_channel[16];
    char source_chat_id[96];
    char agent_type[16];
} subagent_job_info_t;

esp_err_t subagent_service_init(void);
esp_err_t subagent_service_start(void);

esp_err_t subagent_submit(const subagent_job_spec_t *spec, char *out_job_id, size_t out_size);
esp_err_t subagent_cancel(const char *job_id);
esp_err_t subagent_get(const char *job_id, subagent_job_info_t *out);
esp_err_t subagent_list(char *out_json, size_t out_size);
