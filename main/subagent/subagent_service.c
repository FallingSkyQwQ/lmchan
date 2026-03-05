#include "subagent/subagent_service.h"

#include "bus/message_bus.h"
#include "llm/llm_proxy.h"

#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <time.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "cJSON.h"

typedef struct {
    bool used;
    bool cancel_requested;
    char job_id[24];
    subagent_job_spec_t spec;
    subagent_job_status_t status;
    int64_t created_ts;
    int64_t updated_ts;
    int64_t finished_ts;
    char last_error[160];
    char result_summary[LMCHAN_SUBAGENT_RESULT_MAX_BYTES];
} subagent_job_t;

static const char *TAG = "subagent";
static subagent_job_t s_jobs[LMCHAN_SUBAGENT_MAX_JOBS];
static QueueHandle_t s_job_queue = NULL;
static SemaphoreHandle_t s_lock = NULL;
static TaskHandle_t s_worker = NULL;
static uint32_t s_id_seq = 0;

static const char *default_agent_type(const char *agent_type)
{
    return (agent_type && agent_type[0]) ? agent_type : "explorer";
}

static const char *system_prompt_for_type(const subagent_job_spec_t *spec)
{
    const char *agent_type = default_agent_type(spec ? spec->agent_type : NULL);
    if (strcmp(agent_type, "custom") == 0) {
        if (spec && spec->custom_prompt[0]) {
            return spec->custom_prompt;
        }
        return "You are a focused background subagent. Follow the provided goal exactly and return concise results.";
    }
    if (strcmp(agent_type, "planner") == 0) {
        return "You are a planner subagent. Produce decision-complete implementation steps, assumptions, risks, and verification criteria. Keep it compact and actionable.";
    }
    if (strcmp(agent_type, "implementer") == 0) {
        return "You are an implementer subagent. Focus on concrete execution output, important decisions made, and concise validation results.";
    }
    if (strcmp(agent_type, "reviewer") == 0) {
        return "You are a reviewer subagent. Prioritize bugs, behavioral regressions, risks, and missing tests. Order findings by severity.";
    }
    return "You are an explorer subagent. Gather relevant facts, highlight uncertainty, and return concise evidence-backed findings.";
}

static const char *status_str(subagent_job_status_t s)
{
    switch (s) {
    case SUBAGENT_JOB_QUEUED: return "queued";
    case SUBAGENT_JOB_RUNNING: return "running";
    case SUBAGENT_JOB_SUCCEEDED: return "succeeded";
    case SUBAGENT_JOB_FAILED: return "failed";
    case SUBAGENT_JOB_CANCELLED: return "cancelled";
    default: return "unknown";
    }
}

static int64_t now_s(void)
{
    return (int64_t)time(NULL);
}

static void gen_job_id(char *buf, size_t size)
{
    uint32_t t = (uint32_t)(esp_timer_get_time() / 1000);
    s_id_seq++;
    snprintf(buf, size, "sa_%08x_%03u", t, (unsigned)(s_id_seq % 1000));
}

static void save_jobs_locked(void)
{
    cJSON *root = cJSON_CreateObject();
    cJSON *arr = cJSON_CreateArray();
    cJSON_AddItemToObject(root, "jobs", arr);

    for (int i = 0; i < LMCHAN_SUBAGENT_MAX_JOBS; i++) {
        subagent_job_t *j = &s_jobs[i];
        if (!j->used) continue;
        cJSON *it = cJSON_CreateObject();
        cJSON_AddStringToObject(it, "job_id", j->job_id);
        cJSON_AddStringToObject(it, "status", status_str(j->status));
        cJSON_AddNumberToObject(it, "created_ts", (double)j->created_ts);
        cJSON_AddNumberToObject(it, "updated_ts", (double)j->updated_ts);
        cJSON_AddNumberToObject(it, "finished_ts", (double)j->finished_ts);
        cJSON_AddStringToObject(it, "last_error", j->last_error);
        cJSON_AddStringToObject(it, "result_summary", j->result_summary);
        cJSON_AddStringToObject(it, "source_channel", j->spec.source_channel);
        cJSON_AddStringToObject(it, "source_chat_id", j->spec.source_chat_id);
        cJSON_AddStringToObject(it, "created_by", j->spec.created_by);
        cJSON_AddStringToObject(it, "agent_type", default_agent_type(j->spec.agent_type));
        cJSON_AddStringToObject(it, "goal", j->spec.goal);
        cJSON_AddStringToObject(it, "context_json", j->spec.context_json);
        cJSON_AddStringToObject(it, "custom_prompt", j->spec.custom_prompt);
        cJSON_AddItemToArray(arr, it);
    }

    char *json = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (!json) return;

    FILE *f = fopen(LMCHAN_SUBAGENT_FILE, "w");
    if (f) {
        fputs(json, f);
        fclose(f);
    }
    free(json);
}

static subagent_job_status_t parse_status(const char *s)
{
    if (!s) return SUBAGENT_JOB_FAILED;
    if (strcmp(s, "queued") == 0) return SUBAGENT_JOB_QUEUED;
    if (strcmp(s, "running") == 0) return SUBAGENT_JOB_RUNNING;
    if (strcmp(s, "succeeded") == 0) return SUBAGENT_JOB_SUCCEEDED;
    if (strcmp(s, "failed") == 0) return SUBAGENT_JOB_FAILED;
    if (strcmp(s, "cancelled") == 0) return SUBAGENT_JOB_CANCELLED;
    return SUBAGENT_JOB_FAILED;
}

static void load_jobs(void)
{
    FILE *f = fopen(LMCHAN_SUBAGENT_FILE, "r");
    if (!f) return;
    char *buf = calloc(1, 24 * 1024);
    if (!buf) {
        fclose(f);
        return;
    }
    size_t n = fread(buf, 1, 24 * 1024 - 1, f);
    fclose(f);
    buf[n] = '\0';
    cJSON *root = cJSON_Parse(buf);
    free(buf);
    if (!root) return;
    cJSON *arr = cJSON_GetObjectItem(root, "jobs");
    if (!arr || !cJSON_IsArray(arr)) {
        cJSON_Delete(root);
        return;
    }

    memset(s_jobs, 0, sizeof(s_jobs));
    int idx = 0;
    cJSON *it = NULL;
    cJSON_ArrayForEach(it, arr) {
        if (idx >= LMCHAN_SUBAGENT_MAX_JOBS) break;
        if (!cJSON_IsObject(it)) continue;
        subagent_job_t *j = &s_jobs[idx++];
        j->used = true;
        const char *job_id = cJSON_GetStringValue(cJSON_GetObjectItem(it, "job_id"));
        const char *status = cJSON_GetStringValue(cJSON_GetObjectItem(it, "status"));
        const char *last_error = cJSON_GetStringValue(cJSON_GetObjectItem(it, "last_error"));
        const char *result_summary = cJSON_GetStringValue(cJSON_GetObjectItem(it, "result_summary"));
        const char *source_channel = cJSON_GetStringValue(cJSON_GetObjectItem(it, "source_channel"));
        const char *source_chat_id = cJSON_GetStringValue(cJSON_GetObjectItem(it, "source_chat_id"));
        const char *created_by = cJSON_GetStringValue(cJSON_GetObjectItem(it, "created_by"));
        const char *agent_type = cJSON_GetStringValue(cJSON_GetObjectItem(it, "agent_type"));
        const char *goal = cJSON_GetStringValue(cJSON_GetObjectItem(it, "goal"));
        const char *context_json = cJSON_GetStringValue(cJSON_GetObjectItem(it, "context_json"));
        const char *custom_prompt = cJSON_GetStringValue(cJSON_GetObjectItem(it, "custom_prompt"));
        if (job_id) strncpy(j->job_id, job_id, sizeof(j->job_id) - 1);
        j->status = parse_status(status);
        j->created_ts = (int64_t)cJSON_GetNumberValue(cJSON_GetObjectItem(it, "created_ts"));
        j->updated_ts = (int64_t)cJSON_GetNumberValue(cJSON_GetObjectItem(it, "updated_ts"));
        j->finished_ts = (int64_t)cJSON_GetNumberValue(cJSON_GetObjectItem(it, "finished_ts"));
        if (last_error) strncpy(j->last_error, last_error, sizeof(j->last_error) - 1);
        if (result_summary) strncpy(j->result_summary, result_summary, sizeof(j->result_summary) - 1);
        if (source_channel) strncpy(j->spec.source_channel, source_channel, sizeof(j->spec.source_channel) - 1);
        if (source_chat_id) strncpy(j->spec.source_chat_id, source_chat_id, sizeof(j->spec.source_chat_id) - 1);
        if (created_by) strncpy(j->spec.created_by, created_by, sizeof(j->spec.created_by) - 1);
        if (agent_type) strncpy(j->spec.agent_type, agent_type, sizeof(j->spec.agent_type) - 1);
        else strncpy(j->spec.agent_type, "explorer", sizeof(j->spec.agent_type) - 1);
        if (goal) strncpy(j->spec.goal, goal, sizeof(j->spec.goal) - 1);
        if (context_json) strncpy(j->spec.context_json, context_json, sizeof(j->spec.context_json) - 1);
        if (custom_prompt) strncpy(j->spec.custom_prompt, custom_prompt, sizeof(j->spec.custom_prompt) - 1);
        if (j->status == SUBAGENT_JOB_RUNNING) {
            /* Recover interrupted runs as failed after reboot. */
            j->status = SUBAGENT_JOB_FAILED;
            strncpy(j->last_error, "interrupted by reboot", sizeof(j->last_error) - 1);
        }
    }
    cJSON_Delete(root);
}

static int find_job_by_id(const char *job_id)
{
    if (!job_id) return -1;
    for (int i = 0; i < LMCHAN_SUBAGENT_MAX_JOBS; i++) {
        if (s_jobs[i].used && strcmp(s_jobs[i].job_id, job_id) == 0) {
            return i;
        }
    }
    return -1;
}

static int alloc_job_slot(void)
{
    for (int i = 0; i < LMCHAN_SUBAGENT_MAX_JOBS; i++) {
        if (!s_jobs[i].used) return i;
    }
    return -1;
}

static void push_result_message_locked(subagent_job_t *job)
{
    if (!job || !job->spec.source_channel[0] || !job->spec.source_chat_id[0]) return;
    lmchan_msg_t out = {0};
    strncpy(out.channel, job->spec.source_channel, sizeof(out.channel) - 1);
    strncpy(out.chat_id, job->spec.source_chat_id, sizeof(out.chat_id) - 1);
    strncpy(out.message_type, "text", sizeof(out.message_type) - 1);
    snprintf(out.event_type, sizeof(out.event_type), "subagent.result");

    char buf[LMCHAN_SUBAGENT_RESULT_MAX_BYTES + 96];
    snprintf(buf, sizeof(buf), "[subagent:%s] %s: %s",
             job->job_id,
             (job->status == SUBAGENT_JOB_SUCCEEDED) ? "done" :
             (job->status == SUBAGENT_JOB_CANCELLED) ? "cancelled" : "failed",
             job->status == SUBAGENT_JOB_SUCCEEDED ? job->result_summary : job->last_error);
    out.content = strdup(buf);
    if (!out.content) return;
    if (message_bus_push_outbound(&out) != ESP_OK) {
        free(out.content);
    }
}

static void worker_task(void *arg)
{
    (void)arg;
    while (1) {
        int idx = -1;
        if (xQueueReceive(s_job_queue, &idx, portMAX_DELAY) != pdTRUE) continue;
        if (idx < 0 || idx >= LMCHAN_SUBAGENT_MAX_JOBS) continue;

        subagent_job_spec_t spec = {0};
        char job_id[24] = {0};
        bool cancel_requested = false;

        xSemaphoreTake(s_lock, portMAX_DELAY);
        subagent_job_t *job = &s_jobs[idx];
        if (!job->used || job->status != SUBAGENT_JOB_QUEUED) {
            xSemaphoreGive(s_lock);
            continue;
        }
        job->status = SUBAGENT_JOB_RUNNING;
        job->updated_ts = now_s();
        strncpy(job_id, job->job_id, sizeof(job_id) - 1);
        spec = job->spec;
        cancel_requested = job->cancel_requested;
        save_jobs_locked();
        xSemaphoreGive(s_lock);

        if (cancel_requested) {
            xSemaphoreTake(s_lock, portMAX_DELAY);
            job = &s_jobs[idx];
            job->status = SUBAGENT_JOB_CANCELLED;
            job->updated_ts = now_s();
            job->finished_ts = job->updated_ts;
            save_jobs_locked();
            push_result_message_locked(job);
            xSemaphoreGive(s_lock);
            continue;
        }

        cJSON *messages = cJSON_CreateArray();
        cJSON *u = cJSON_CreateObject();
        cJSON_AddStringToObject(u, "role", "user");
        char prompt[2200];
        const char *agent_type = default_agent_type(spec.agent_type);
        snprintf(prompt, sizeof(prompt),
                 "Subagent type: %s\n\nTask goal:\n%s\n\nContext(JSON):\n%s\n\n"
                 "Return concise execution result and important facts only.",
                 agent_type,
                 spec.goal,
                 spec.context_json[0] ? spec.context_json : "{}");
        cJSON_AddStringToObject(u, "content", prompt);
        cJSON_AddItemToArray(messages, u);

        llm_response_t resp;
        esp_err_t err = llm_chat_text(
            system_prompt_for_type(&spec),
            messages, &resp);
        cJSON_Delete(messages);

        xSemaphoreTake(s_lock, portMAX_DELAY);
        job = &s_jobs[idx];
        if (!job->used) {
            xSemaphoreGive(s_lock);
            if (err == ESP_OK) llm_response_free(&resp);
            continue;
        }
        if (job->cancel_requested) {
            job->status = SUBAGENT_JOB_CANCELLED;
            snprintf(job->last_error, sizeof(job->last_error), "cancelled by request");
        } else if (err == ESP_OK && resp.text && resp.text[0]) {
            job->status = SUBAGENT_JOB_SUCCEEDED;
            strncpy(job->result_summary, resp.text, sizeof(job->result_summary) - 1);
        } else if (err == ESP_OK) {
            job->status = SUBAGENT_JOB_FAILED;
            snprintf(job->last_error, sizeof(job->last_error), "empty model response");
        } else {
            job->status = SUBAGENT_JOB_FAILED;
            snprintf(job->last_error, sizeof(job->last_error), "llm error: %s", esp_err_to_name(err));
        }
        if (err == ESP_OK) llm_response_free(&resp);
        job->updated_ts = now_s();
        job->finished_ts = job->updated_ts;
        save_jobs_locked();
        push_result_message_locked(job);
        xSemaphoreGive(s_lock);
    }
}

esp_err_t subagent_service_init(void)
{
    s_lock = xSemaphoreCreateMutex();
    s_job_queue = xQueueCreate(LMCHAN_SUBAGENT_QUEUE_LEN, sizeof(int));
    if (!s_lock || !s_job_queue) {
        return ESP_ERR_NO_MEM;
    }
    memset(s_jobs, 0, sizeof(s_jobs));
    load_jobs();
    ESP_LOGI(TAG, "Subagent service initialized");
    return ESP_OK;
}

esp_err_t subagent_service_start(void)
{
    if (s_worker) return ESP_OK;
    BaseType_t ok = xTaskCreatePinnedToCore(
        worker_task, "subagent_worker",
        LMCHAN_SUBAGENT_WORKER_STACK, NULL,
        LMCHAN_SUBAGENT_WORKER_PRIO, &s_worker, LMCHAN_SUBAGENT_WORKER_CORE);
    return ok == pdPASS ? ESP_OK : ESP_FAIL;
}

esp_err_t subagent_submit(const subagent_job_spec_t *spec, char *out_job_id, size_t out_size)
{
    if (!spec || !spec->goal[0]) return ESP_ERR_INVALID_ARG;
    if (out_job_id && out_size > 0) out_job_id[0] = '\0';

    xSemaphoreTake(s_lock, portMAX_DELAY);
    int idx = alloc_job_slot();
    if (idx < 0) {
        xSemaphoreGive(s_lock);
        return ESP_ERR_NO_MEM;
    }
    subagent_job_t *j = &s_jobs[idx];
    memset(j, 0, sizeof(*j));
    j->used = true;
    j->status = SUBAGENT_JOB_QUEUED;
    j->created_ts = now_s();
    j->updated_ts = j->created_ts;
    j->spec = *spec;
    if (!j->spec.source_channel[0]) strncpy(j->spec.source_channel, LMCHAN_CHAN_SYSTEM, sizeof(j->spec.source_channel) - 1);
    if (!j->spec.source_chat_id[0]) strncpy(j->spec.source_chat_id, "subagent", sizeof(j->spec.source_chat_id) - 1);
    if (!j->spec.created_by[0]) strncpy(j->spec.created_by, "agent", sizeof(j->spec.created_by) - 1);
    if (!j->spec.agent_type[0]) strncpy(j->spec.agent_type, "explorer", sizeof(j->spec.agent_type) - 1);
    gen_job_id(j->job_id, sizeof(j->job_id));
    save_jobs_locked();
    xSemaphoreGive(s_lock);

    if (xQueueSend(s_job_queue, &idx, pdMS_TO_TICKS(1000)) != pdTRUE) {
        xSemaphoreTake(s_lock, portMAX_DELAY);
        memset(j, 0, sizeof(*j));
        save_jobs_locked();
        xSemaphoreGive(s_lock);
        return ESP_ERR_TIMEOUT;
    }

    if (out_job_id && out_size > 0) {
        strncpy(out_job_id, j->job_id, out_size - 1);
        out_job_id[out_size - 1] = '\0';
    }
    return ESP_OK;
}

esp_err_t subagent_cancel(const char *job_id)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
    int idx = find_job_by_id(job_id);
    if (idx < 0) {
        xSemaphoreGive(s_lock);
        return ESP_ERR_NOT_FOUND;
    }
    subagent_job_t *j = &s_jobs[idx];
    if (j->status == SUBAGENT_JOB_SUCCEEDED || j->status == SUBAGENT_JOB_FAILED || j->status == SUBAGENT_JOB_CANCELLED) {
        xSemaphoreGive(s_lock);
        return ESP_ERR_INVALID_STATE;
    }
    j->cancel_requested = true;
    if (j->status == SUBAGENT_JOB_QUEUED) {
        j->status = SUBAGENT_JOB_CANCELLED;
        j->finished_ts = now_s();
    }
    j->updated_ts = now_s();
    snprintf(j->last_error, sizeof(j->last_error), "cancelled by request");
    save_jobs_locked();
    xSemaphoreGive(s_lock);
    return ESP_OK;
}

esp_err_t subagent_get(const char *job_id, subagent_job_info_t *out)
{
    if (!job_id || !out) return ESP_ERR_INVALID_ARG;
    memset(out, 0, sizeof(*out));
    xSemaphoreTake(s_lock, portMAX_DELAY);
    int idx = find_job_by_id(job_id);
    if (idx < 0) {
        xSemaphoreGive(s_lock);
        return ESP_ERR_NOT_FOUND;
    }
    subagent_job_t *j = &s_jobs[idx];
    strncpy(out->job_id, j->job_id, sizeof(out->job_id) - 1);
    out->status = j->status;
    out->created_ts = j->created_ts;
    out->updated_ts = j->updated_ts;
    out->finished_ts = j->finished_ts;
    strncpy(out->last_error, j->last_error, sizeof(out->last_error) - 1);
    strncpy(out->result_summary, j->result_summary, sizeof(out->result_summary) - 1);
    strncpy(out->source_channel, j->spec.source_channel, sizeof(out->source_channel) - 1);
    strncpy(out->source_chat_id, j->spec.source_chat_id, sizeof(out->source_chat_id) - 1);
    strncpy(out->agent_type, default_agent_type(j->spec.agent_type), sizeof(out->agent_type) - 1);
    xSemaphoreGive(s_lock);
    return ESP_OK;
}

esp_err_t subagent_list(char *out_json, size_t out_size)
{
    if (!out_json || out_size == 0) return ESP_ERR_INVALID_ARG;
    cJSON *arr = cJSON_CreateArray();

    xSemaphoreTake(s_lock, portMAX_DELAY);
    for (int i = 0; i < LMCHAN_SUBAGENT_MAX_JOBS; i++) {
        subagent_job_t *j = &s_jobs[i];
        if (!j->used) continue;
        cJSON *it = cJSON_CreateObject();
        cJSON_AddStringToObject(it, "job_id", j->job_id);
        cJSON_AddStringToObject(it, "status", status_str(j->status));
        cJSON_AddNumberToObject(it, "created_ts", (double)j->created_ts);
        cJSON_AddNumberToObject(it, "updated_ts", (double)j->updated_ts);
        cJSON_AddNumberToObject(it, "finished_ts", (double)j->finished_ts);
        cJSON_AddStringToObject(it, "source_channel", j->spec.source_channel);
        cJSON_AddStringToObject(it, "source_chat_id", j->spec.source_chat_id);
        cJSON_AddStringToObject(it, "agent_type", default_agent_type(j->spec.agent_type));
        cJSON_AddStringToObject(it, "result_summary", j->result_summary);
        cJSON_AddStringToObject(it, "last_error", j->last_error);
        cJSON_AddItemToArray(arr, it);
    }
    xSemaphoreGive(s_lock);

    char *json = cJSON_PrintUnformatted(arr);
    cJSON_Delete(arr);
    if (!json) return ESP_ERR_NO_MEM;
    strncpy(out_json, json, out_size - 1);
    out_json[out_size - 1] = '\0';
    free(json);
    return ESP_OK;
}
