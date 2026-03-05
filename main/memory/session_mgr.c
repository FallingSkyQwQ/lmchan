#include "session_mgr.h"
#include "lmchan_config.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>
#include <inttypes.h>
#include <dirent.h>
#include <time.h>
#include <sys/stat.h>
#include <errno.h>
#include "esp_log.h"
#include "cJSON.h"

static const char *TAG = "session";
static const int COMPACT_TRIGGER_MSGS = LMCHAN_SESSION_MAX_MSGS * 2;

static uint32_t fnv1a32(const char *s)
{
    uint32_t h = 2166136261u;
    if (!s) return h;
    while (*s) {
        h ^= (uint8_t)(*s++);
        h *= 16777619u;
    }
    return h;
}

static void session_path(const char *chat_id, char *buf, size_t size)
{
    uint32_t id = fnv1a32(chat_id);
    snprintf(buf, size, "%s/s_%08" PRIx32 ".jsonl", LMCHAN_SPIFFS_BASE, id);
}

static void session_summary_path(const char *chat_id, char *buf, size_t size)
{
    uint32_t id = fnv1a32(chat_id);
    snprintf(buf, size, "%s/s_%08" PRIx32 "_sum.md", LMCHAN_SPIFFS_BASE, id);
}

static void session_meta_path(const char *chat_id, char *buf, size_t size)
{
    uint32_t id = fnv1a32(chat_id);
    snprintf(buf, size, "%s/s_%08" PRIx32 ".meta.json", LMCHAN_SPIFFS_BASE, id);
}

static esp_err_t session_update_meta(const char *chat_id, const char *source)
{
    char path[192];
    session_meta_path(chat_id, path, sizeof(path));

    int64_t now = (int64_t)time(NULL);
    int64_t created_at = now;
    int message_count = 0;
    char source_buf[64] = {0};

    FILE *f = fopen(path, "r");
    if (f) {
        fseek(f, 0, SEEK_END);
        long fsize = ftell(f);
        fseek(f, 0, SEEK_SET);
        if (fsize > 0 && fsize < 2048) {
            char *buf = calloc(1, (size_t)fsize + 1);
            if (buf) {
                size_t n = fread(buf, 1, (size_t)fsize, f);
                buf[n] = '\0';
                cJSON *meta = cJSON_Parse(buf);
                if (meta) {
                    cJSON *created = cJSON_GetObjectItem(meta, "created_at");
                    cJSON *count = cJSON_GetObjectItem(meta, "message_count");
                    cJSON *src = cJSON_GetObjectItem(meta, "source");
                    if (created && cJSON_IsNumber(created) && created->valuedouble > 0) {
                        created_at = (int64_t)created->valuedouble;
                    }
                    if (count && cJSON_IsNumber(count) && count->valueint >= 0) {
                        message_count = count->valueint;
                    }
                    if (src && cJSON_IsString(src) && src->valuestring[0]) {
                        strncpy(source_buf, src->valuestring, sizeof(source_buf) - 1);
                    }
                    cJSON_Delete(meta);
                }
                free(buf);
            }
        }
        fclose(f);
    }

    if (!source_buf[0]) {
        strncpy(source_buf, (source && source[0]) ? source : "unknown", sizeof(source_buf) - 1);
    }
    message_count++;

    cJSON *meta = cJSON_CreateObject();
    cJSON_AddStringToObject(meta, "chat_id", chat_id);
    cJSON_AddNumberToObject(meta, "created_at", (double)created_at);
    cJSON_AddNumberToObject(meta, "updated_at", (double)now);
    cJSON_AddStringToObject(meta, "source", source_buf);
    cJSON_AddNumberToObject(meta, "message_count", message_count);
    char *json = cJSON_PrintUnformatted(meta);
    cJSON_Delete(meta);
    if (!json) {
        return ESP_ERR_NO_MEM;
    }

    f = fopen(path, "w");
    if (!f) {
        free(json);
        ESP_LOGE(TAG, "Cannot open meta file %s", path);
        return ESP_FAIL;
    }
    size_t len = strlen(json);
    size_t written = fwrite(json, 1, len, f);
    fclose(f);
    free(json);
    if (written != len) {
        ESP_LOGE(TAG, "Cannot write full meta file %s", path);
        return ESP_FAIL;
    }
    return ESP_OK;
}

static void build_summary_line(const cJSON *obj, char *line, size_t line_size)
{
    const cJSON *role = cJSON_GetObjectItem((cJSON *)obj, "role");
    const cJSON *content = cJSON_GetObjectItem((cJSON *)obj, "content");
    const char *role_s = (role && cJSON_IsString(role)) ? role->valuestring : "unknown";
    const char *text_s = (content && cJSON_IsString(content)) ? content->valuestring : "";
    char preview[97] = {0};
    strncpy(preview, text_s, sizeof(preview) - 1);
    for (size_t i = 0; preview[i]; i++) {
        if (preview[i] == '\n' || preview[i] == '\r') {
            preview[i] = ' ';
        }
    }
    snprintf(line, line_size, "- %s: %s", role_s, preview);
}

static void session_compact_if_needed(const char *chat_id, const char *path)
{
    FILE *f = fopen(path, "r");
    if (!f) return;

    char *lines[256] = {0};
    int total = 0;
    char line[2048];
    while (fgets(line, sizeof(line), f) && total < 256) {
        size_t len = strlen(line);
        while (len > 0 && (line[len - 1] == '\n' || line[len - 1] == '\r')) {
            line[--len] = '\0';
        }
        if (len == 0) continue;
        lines[total++] = strdup(line);
    }
    fclose(f);

    if (total <= COMPACT_TRIGGER_MSGS) {
        for (int i = 0; i < total; i++) free(lines[i]);
        return;
    }

    int keep = LMCHAN_SESSION_MAX_MSGS;
    int archive_count = total - keep;
    char summary_path[192];
    session_summary_path(chat_id, summary_path, sizeof(summary_path));

    FILE *sf = fopen(summary_path, "a");
    if (sf) {
        fprintf(sf, "\n## Compacted at %lld\n", (long long)time(NULL));
        for (int i = 0; i < archive_count; i++) {
            cJSON *obj = cJSON_Parse(lines[i]);
            if (!obj) continue;
            char summary_line[160];
            build_summary_line(obj, summary_line, sizeof(summary_line));
            fprintf(sf, "%s\n", summary_line);
            cJSON_Delete(obj);
        }
        fclose(sf);
    }

    f = fopen(path, "w");
    if (f) {
        for (int i = archive_count; i < total; i++) {
            fprintf(f, "%s\n", lines[i]);
        }
        fclose(f);
        ESP_LOGI(TAG, "Compacted session %s: archived %d messages", chat_id, archive_count);
    }

    for (int i = 0; i < total; i++) free(lines[i]);
}

esp_err_t session_mgr_init(void)
{
    struct stat st;
    if (stat(LMCHAN_SPIFFS_SESSION_DIR, &st) != 0) {
        if (mkdir(LMCHAN_SPIFFS_SESSION_DIR, 0777) != 0 && errno != EEXIST) {
            ESP_LOGW(TAG, "mkdir failed for %s (errno=%d)", LMCHAN_SPIFFS_SESSION_DIR, errno);
        }
    }
    ESP_LOGI(TAG, "Session manager initialized at %s", LMCHAN_SPIFFS_SESSION_DIR);
    return ESP_OK;
}

esp_err_t session_append_with_source(const char *chat_id, const char *role,
                                     const char *content, const char *source)
{
    char path[192];
    session_path(chat_id, path, sizeof(path));

    FILE *f = fopen(path, "a");
    if (!f) {
        ESP_LOGE(TAG, "Cannot open session file %s", path);
        return ESP_FAIL;
    }

    cJSON *obj = cJSON_CreateObject();
    cJSON_AddStringToObject(obj, "role", role);
    cJSON_AddStringToObject(obj, "content", content);
    cJSON_AddNumberToObject(obj, "ts", (double)time(NULL));

    char *line = cJSON_PrintUnformatted(obj);
    cJSON_Delete(obj);

    if (line) {
        fprintf(f, "%s\n", line);
        free(line);
    }

    fclose(f);
    esp_err_t meta_err = session_update_meta(chat_id, source);
    if (meta_err != ESP_OK) {
        ESP_LOGW(TAG, "Session meta update failed for %s: %s", chat_id, esp_err_to_name(meta_err));
    }
    session_compact_if_needed(chat_id, path);
    return meta_err == ESP_OK ? ESP_OK : meta_err;
}

esp_err_t session_append(const char *chat_id, const char *role, const char *content)
{
    return session_append_with_source(chat_id, role, content, NULL);
}

esp_err_t session_get_history_json(const char *chat_id, char *buf, size_t size, int max_msgs)
{
    char path[192];
    session_path(chat_id, path, sizeof(path));

    FILE *f = fopen(path, "r");
    if (!f) {
        /* No history yet */
        snprintf(buf, size, "[]");
        return ESP_OK;
    }

    /* Read all lines into a ring buffer of cJSON objects */
    cJSON *messages[LMCHAN_SESSION_MAX_MSGS];
    int count = 0;
    int write_idx = 0;

    char line[2048];
    while (fgets(line, sizeof(line), f)) {
        /* Strip newline */
        size_t len = strlen(line);
        if (len > 0 && line[len - 1] == '\n') line[len - 1] = '\0';
        if (line[0] == '\0') continue;

        cJSON *obj = cJSON_Parse(line);
        if (!obj) continue;

        /* Ring buffer: overwrite oldest if full */
        if (count >= max_msgs) {
            cJSON_Delete(messages[write_idx]);
        }
        messages[write_idx] = obj;
        write_idx = (write_idx + 1) % max_msgs;
        if (count < max_msgs) count++;
    }
    fclose(f);

    /* Build JSON array with only role + content */
    cJSON *arr = cJSON_CreateArray();
    int start = (count < max_msgs) ? 0 : write_idx;
    for (int i = 0; i < count; i++) {
        int idx = (start + i) % max_msgs;
        cJSON *src = messages[idx];

        cJSON *entry = cJSON_CreateObject();
        cJSON *role = cJSON_GetObjectItem(src, "role");
        cJSON *content = cJSON_GetObjectItem(src, "content");
        if (role && content) {
            cJSON_AddStringToObject(entry, "role", role->valuestring);
            cJSON_AddStringToObject(entry, "content", content->valuestring);
        }
        cJSON_AddItemToArray(arr, entry);
    }

    /* Cleanup ring buffer */
    int cleanup_start = (count < max_msgs) ? 0 : write_idx;
    for (int i = 0; i < count; i++) {
        int idx = (cleanup_start + i) % max_msgs;
        cJSON_Delete(messages[idx]);
    }

    char *json_str = cJSON_PrintUnformatted(arr);
    cJSON_Delete(arr);

    if (json_str) {
        strncpy(buf, json_str, size - 1);
        buf[size - 1] = '\0';
        free(json_str);
    } else {
        snprintf(buf, size, "[]");
    }

    return ESP_OK;
}

esp_err_t session_clear(const char *chat_id)
{
    char path[192];
    session_path(chat_id, path, sizeof(path));
    char meta_path[192];
    session_meta_path(chat_id, meta_path, sizeof(meta_path));
    char summary_path[192];
    session_summary_path(chat_id, summary_path, sizeof(summary_path));

    if (remove(path) == 0) {
        remove(meta_path);
        remove(summary_path);
        ESP_LOGI(TAG, "Session %s cleared", chat_id);
        return ESP_OK;
    }
    return ESP_ERR_NOT_FOUND;
}

void session_list(void)
{
    DIR *dir = opendir(LMCHAN_SPIFFS_SESSION_DIR);
    if (!dir) {
        /* SPIFFS is flat, so list all files matching pattern */
        dir = opendir(LMCHAN_SPIFFS_BASE);
        if (!dir) {
            ESP_LOGW(TAG, "Cannot open SPIFFS directory");
            return;
        }
    }

    struct dirent *entry;
    int count = 0;
    while ((entry = readdir(dir)) != NULL) {
        if (strstr(entry->d_name, "s_") && strstr(entry->d_name, ".jsonl")) {
            ESP_LOGI(TAG, "  Session: %s", entry->d_name);
            count++;
        }
    }
    closedir(dir);

    if (count == 0) {
        ESP_LOGI(TAG, "  No sessions found");
    }
}
