#include "feishu/feishu_client.h"
#include "lmchan_config.h"
#include "bus/message_bus.h"
#include "wifi/wifi_manager.h"

#include <string.h>
#include <stdlib.h>
#include <inttypes.h>
#include <ctype.h>
#include <time.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_http_client.h"
#include "esp_crt_bundle.h"
#include "esp_websocket_client.h"
#include "esp_timer.h"
#include "nvs.h"
#include "cJSON.h"

static const char *TAG = "feishu";

static char s_app_id[128] = LMCHAN_SECRET_FS_APP_ID;
static char s_app_secret[192] = LMCHAN_SECRET_FS_APP_SECRET;
static char s_ws_url[192] = LMCHAN_SECRET_FS_WS_URL;
static bool s_group_mention_only = true;

static char s_tenant_token[512] = {0};
static int64_t s_tenant_token_expire_epoch = 0;

static TaskHandle_t s_ws_task = NULL;
static int64_t s_next_no_cred_warn_epoch = 0;

typedef struct {
    char *buf;
    size_t len;
    size_t cap;
} http_resp_t;

static esp_websocket_client_handle_t s_ws = NULL;
static char *s_ws_payload_buf = NULL;
static size_t s_ws_payload_cap = 0;
static int64_t s_next_ws_endpoint_warn_epoch = 0;
static char s_seen_message_ids[64][80] = {0};
static size_t s_seen_message_ids_idx = 0;
static int64_t s_last_msg_create_ms = 0;
static char s_last_msg_id[80] = {0};

static bool equals_ignore_case(const char *a, const char *b)
{
    if (!a || !b) return false;
    while (*a && *b) {
        if (tolower((unsigned char)*a) != tolower((unsigned char)*b)) {
            return false;
        }
        a++;
        b++;
    }
    return (*a == '\0' && *b == '\0');
}

static int64_t epoch_now(void)
{
    return (int64_t)time(NULL);
}

static int64_t parse_i64(const char *s)
{
    if (!s || !s[0]) return 0;
    char *end = NULL;
    long long v = strtoll(s, &end, 10);
    if (!end || *end != '\0') return 0;
    return (int64_t)v;
}

static int64_t parse_message_create_ms(const cJSON *message)
{
    if (!message || !cJSON_IsObject((cJSON *)message)) return 0;
    cJSON *create_time = cJSON_GetObjectItem((cJSON *)message, "create_time");
    int64_t raw = 0;
    if (cJSON_IsString(create_time)) {
        raw = parse_i64(create_time->valuestring);
    } else if (cJSON_IsNumber(create_time)) {
        raw = (int64_t)create_time->valuedouble;
    }
    if (raw <= 0) return 0;
    if (raw < 100000000000LL) {
        return raw * 1000LL;
    }
    return raw;
}

static void load_last_message_guard_from_nvs(void)
{
    s_last_msg_create_ms = 0;
    s_last_msg_id[0] = '\0';

    nvs_handle_t nvs;
    if (nvs_open(LMCHAN_NVS_FEISHU, NVS_READONLY, &nvs) != ESP_OK) {
        return;
    }

    int64_t ts = 0;
    if (nvs_get_i64(nvs, LMCHAN_NVS_KEY_FS_LAST_MSG_TS, &ts) == ESP_OK && ts > 0) {
        s_last_msg_create_ms = ts;
    }

    size_t len = sizeof(s_last_msg_id);
    if (nvs_get_str(nvs, LMCHAN_NVS_KEY_FS_LAST_MSG_ID, s_last_msg_id, &len) != ESP_OK) {
        s_last_msg_id[0] = '\0';
    }
    nvs_close(nvs);
}

static void save_last_message_guard_to_nvs(void)
{
    nvs_handle_t nvs;
    if (nvs_open(LMCHAN_NVS_FEISHU, NVS_READWRITE, &nvs) != ESP_OK) {
        return;
    }
    nvs_set_i64(nvs, LMCHAN_NVS_KEY_FS_LAST_MSG_TS, s_last_msg_create_ms);
    nvs_set_str(nvs, LMCHAN_NVS_KEY_FS_LAST_MSG_ID, s_last_msg_id[0] ? s_last_msg_id : "");
    nvs_commit(nvs);
    nvs_close(nvs);
}

static bool is_replayed_or_stale_message(const cJSON *message, const char *message_id)
{
#if LMCHAN_FS_STALE_GUARD_ENABLED
    int64_t create_ms = parse_message_create_ms(message);
    if (create_ms > 0 && s_last_msg_create_ms > 0) {
        if (create_ms < s_last_msg_create_ms) {
            ESP_LOGW(TAG, "Drop stale feishu message by create_time (%lld < %lld)",
                     (long long)create_ms, (long long)s_last_msg_create_ms);
            return true;
        }
        if (create_ms == s_last_msg_create_ms &&
            message_id && s_last_msg_id[0] && strcmp(message_id, s_last_msg_id) == 0) {
            ESP_LOGW(TAG, "Drop replayed feishu message_id=%s", message_id);
            return true;
        }
    }
#endif
    return false;
}

static void update_last_message_guard(const cJSON *message, const char *message_id)
{
#if LMCHAN_FS_STALE_GUARD_ENABLED
    int64_t create_ms = parse_message_create_ms(message);
    bool changed = false;
    if (create_ms > s_last_msg_create_ms) {
        s_last_msg_create_ms = create_ms;
        changed = true;
    }
    if (create_ms > 0 && create_ms == s_last_msg_create_ms && message_id && message_id[0]) {
        if (strcmp(s_last_msg_id, message_id) != 0) {
            strncpy(s_last_msg_id, message_id, sizeof(s_last_msg_id) - 1);
            s_last_msg_id[sizeof(s_last_msg_id) - 1] = '\0';
            changed = true;
        }
    }
    if (changed) {
        save_last_message_guard_to_nvs();
    }
#else
    (void)message;
    (void)message_id;
#endif
}

static esp_err_t http_event_handler(esp_http_client_event_t *evt)
{
    http_resp_t *resp = (http_resp_t *)evt->user_data;
    if (!resp) return ESP_OK;
    if (evt->event_id == HTTP_EVENT_ON_DATA) {
        if (resp->len + evt->data_len + 1 > resp->cap) {
            size_t next = resp->cap * 2;
            size_t min_need = resp->len + evt->data_len + 1;
            if (next < min_need) next = min_need;
            char *tmp = realloc(resp->buf, next);
            if (!tmp) return ESP_ERR_NO_MEM;
            resp->buf = tmp;
            resp->cap = next;
        }
        memcpy(resp->buf + resp->len, evt->data, evt->data_len);
        resp->len += evt->data_len;
        resp->buf[resp->len] = '\0';
    }
    return ESP_OK;
}

static esp_err_t http_post_json(const char *url, const char *auth_bearer,
                                const char *json_body, char **out_resp)
{
    if (!url || !json_body || !out_resp) return ESP_ERR_INVALID_ARG;
    *out_resp = NULL;

    http_resp_t resp = {
        .buf = calloc(1, 2048),
        .len = 0,
        .cap = 2048,
    };
    if (!resp.buf) return ESP_ERR_NO_MEM;

    esp_http_client_config_t cfg = {
        .url = url,
        .method = HTTP_METHOD_POST,
        .event_handler = http_event_handler,
        .user_data = &resp,
        .timeout_ms = 15000,
        .buffer_size = 2048,
        .buffer_size_tx = 2048,
        .crt_bundle_attach = esp_crt_bundle_attach,
    };

    esp_http_client_handle_t client = esp_http_client_init(&cfg);
    if (!client) {
        free(resp.buf);
        return ESP_FAIL;
    }

    esp_http_client_set_header(client, "Content-Type", "application/json; charset=utf-8");
    if (auth_bearer && auth_bearer[0]) {
        char auth_hdr[640];
        snprintf(auth_hdr, sizeof(auth_hdr), "Bearer %s", auth_bearer);
        esp_http_client_set_header(client, "Authorization", auth_hdr);
    }
    esp_http_client_set_post_field(client, json_body, (int)strlen(json_body));

    esp_err_t err = esp_http_client_perform(client);
    esp_http_client_cleanup(client);
    if (err != ESP_OK) {
        free(resp.buf);
        return err;
    }

    *out_resp = resp.buf;
    return ESP_OK;
}

static esp_err_t ensure_tenant_token(void)
{
    int64_t now = epoch_now();
    if (s_tenant_token[0] && (s_tenant_token_expire_epoch - now > 60)) {
        return ESP_OK;
    }

    if (!s_app_id[0] || !s_app_secret[0]) {
        return ESP_ERR_INVALID_STATE;
    }

    cJSON *body = cJSON_CreateObject();
    cJSON_AddStringToObject(body, "app_id", s_app_id);
    cJSON_AddStringToObject(body, "app_secret", s_app_secret);
    char *req = cJSON_PrintUnformatted(body);
    cJSON_Delete(body);
    if (!req) return ESP_ERR_NO_MEM;

    char *resp = NULL;
    esp_err_t err = http_post_json(
        "https://open.feishu.cn/open-apis/auth/v3/tenant_access_token/internal",
        NULL, req, &resp);
    free(req);
    if (err != ESP_OK || !resp) {
        free(resp);
        return err != ESP_OK ? err : ESP_FAIL;
    }

    cJSON *root = cJSON_Parse(resp);
    free(resp);
    if (!root) return ESP_FAIL;

    cJSON *code = cJSON_GetObjectItem(root, "code");
    cJSON *token = cJSON_GetObjectItem(root, "tenant_access_token");
    cJSON *expire = cJSON_GetObjectItem(root, "expire");
    if (!cJSON_IsNumber(code) || code->valueint != 0 || !cJSON_IsString(token)) {
        cJSON_Delete(root);
        return ESP_FAIL;
    }

    strncpy(s_tenant_token, token->valuestring, sizeof(s_tenant_token) - 1);
    s_tenant_token_expire_epoch = now + (cJSON_IsNumber(expire) ? expire->valueint : 7200);
    cJSON_Delete(root);

    ESP_LOGI(TAG, "Tenant token refreshed");
    return ESP_OK;
}

static esp_err_t fetch_ws_endpoint_url(char *out_url, size_t out_size)
{
    if (!out_url || out_size == 0) return ESP_ERR_INVALID_ARG;
    out_url[0] = '\0';

    cJSON *body = cJSON_CreateObject();
    if (!body) return ESP_ERR_NO_MEM;
    cJSON_AddStringToObject(body, "AppID", s_app_id);
    cJSON_AddStringToObject(body, "AppSecret", s_app_secret);
    cJSON_AddStringToObject(body, "app_id", s_app_id);
    cJSON_AddStringToObject(body, "app_secret", s_app_secret);
    char *req = cJSON_PrintUnformatted(body);
    cJSON_Delete(body);
    if (!req) return ESP_ERR_NO_MEM;

    char *resp = NULL;
    esp_err_t err = http_post_json(s_ws_url, NULL, req, &resp);
    free(req);
    if (err != ESP_OK || !resp) {
        free(resp);
        return err != ESP_OK ? err : ESP_FAIL;
    }

    cJSON *root = cJSON_Parse(resp);
    free(resp);
    if (!root) return ESP_FAIL;

    cJSON *code = cJSON_GetObjectItem(root, "code");
    cJSON *msg = cJSON_GetObjectItem(root, "msg");
    cJSON *data = cJSON_GetObjectItem(root, "data");
    const cJSON *url_item = NULL;
    if (data && cJSON_IsObject(data)) {
        url_item = cJSON_GetObjectItem(data, "URL");
        if (!cJSON_IsString((cJSON *)url_item)) {
            url_item = cJSON_GetObjectItem(data, "url");
        }
    }

    if (!cJSON_IsNumber(code) || code->valueint != 0 || !cJSON_IsString((cJSON *)url_item) ||
        !url_item->valuestring || !url_item->valuestring[0]) {
        int64_t now = epoch_now();
        if (now >= s_next_ws_endpoint_warn_epoch) {
            ESP_LOGE(TAG, "Fetch ws endpoint failed: code=%d msg=%s",
                     cJSON_IsNumber(code) ? code->valueint : -1,
                     cJSON_IsString(msg) ? msg->valuestring : "(none)");
            s_next_ws_endpoint_warn_epoch = now + 30;
        }
        cJSON_Delete(root);
        return ESP_FAIL;
    }

    strncpy(out_url, url_item->valuestring, out_size - 1);
    out_url[out_size - 1] = '\0';
    cJSON_Delete(root);
    return ESP_OK;
}

static bool should_forward_group_message(cJSON *message, const char *text)
{
    cJSON *chat_type = cJSON_GetObjectItem(message, "chat_type");
    bool is_group = cJSON_IsString(chat_type) &&
        (strcmp(chat_type->valuestring, "group") == 0 ||
         strcmp(chat_type->valuestring, "supergroup") == 0);

    if (!is_group || !s_group_mention_only) {
        return true;
    }

    cJSON *mentions = cJSON_GetObjectItem(message, "mentions");
    if (mentions && cJSON_IsArray(mentions) && cJSON_GetArraySize(mentions) > 0) {
        return true;
    }

    if (text && strchr(text, '@')) {
        return true;
    }

    return false;
}

static void push_inbound_text(const char *chat_id, const char *text)
{
    if (!chat_id || !chat_id[0] || !text || !text[0]) return;

    lmchan_msg_t msg = {0};
    strncpy(msg.channel, LMCHAN_CHAN_FEISHU, sizeof(msg.channel) - 1);
    strncpy(msg.chat_id, chat_id, sizeof(msg.chat_id) - 1);
    msg.content = strdup(text);
    if (!msg.content) return;
    if (message_bus_push_inbound(&msg) != ESP_OK) {
        ESP_LOGW(TAG, "Inbound queue full, drop feishu message");
        free(msg.content);
    }
}

static bool seen_message_id(const char *message_id)
{
    if (!message_id || !message_id[0]) return false;
    for (size_t i = 0; i < 64; i++) {
        if (s_seen_message_ids[i][0] && strcmp(s_seen_message_ids[i], message_id) == 0) {
            return true;
        }
    }
    return false;
}

static void remember_message_id(const char *message_id)
{
    if (!message_id || !message_id[0]) return;
    strncpy(s_seen_message_ids[s_seen_message_ids_idx], message_id,
            sizeof(s_seen_message_ids[s_seen_message_ids_idx]) - 1);
    s_seen_message_ids[s_seen_message_ids_idx][sizeof(s_seen_message_ids[0]) - 1] = '\0';
    s_seen_message_ids_idx = (s_seen_message_ids_idx + 1) % 64;
}

static void handle_feishu_event_payload(cJSON *payload)
{
    if (!payload) return;

    cJSON *header = cJSON_GetObjectItem(payload, "header");
    const char *event_type = cJSON_GetStringValue(
        header ? cJSON_GetObjectItem(header, "event_type") : NULL);
    if (!event_type) {
        event_type = cJSON_GetStringValue(cJSON_GetObjectItem(payload, "type"));
    }

    cJSON *event = cJSON_GetObjectItem(payload, "event");
    if (!event || !cJSON_IsObject(event)) {
        return;
    }

    if (!event_type || strcmp(event_type, "im.message.receive_v1") != 0) {
        return;
    }

    cJSON *message = cJSON_GetObjectItem(event, "message");
    if (!message || !cJSON_IsObject(message)) {
        return;
    }

    const char *message_id = cJSON_GetStringValue(cJSON_GetObjectItem(message, "message_id"));
    if (seen_message_id(message_id)) {
        ESP_LOGW(TAG, "Drop duplicated feishu message_id=%s", message_id ? message_id : "(null)");
        return;
    }
    if (is_replayed_or_stale_message(message, message_id)) {
        return;
    }

    const char *chat_id = cJSON_GetStringValue(cJSON_GetObjectItem(message, "chat_id"));
    const char *msg_type = cJSON_GetStringValue(cJSON_GetObjectItem(message, "message_type"));
    const char *content_raw = cJSON_GetStringValue(cJSON_GetObjectItem(message, "content"));
    if (!chat_id || !msg_type || !content_raw || strcmp(msg_type, "text") != 0) {
        return;
    }

    cJSON *content_json = cJSON_Parse(content_raw);
    if (!content_json) return;
    const char *text = cJSON_GetStringValue(cJSON_GetObjectItem(content_json, "text"));
    if (!text || !text[0]) {
        cJSON_Delete(content_json);
        return;
    }

    if (!should_forward_group_message(message, text)) {
        cJSON_Delete(content_json);
        return;
    }

    remember_message_id(message_id);
    update_last_message_guard(message, message_id);
    ESP_LOGI(TAG, "Feishu message: chat=%s %.48s", chat_id, text);
    push_inbound_text(chat_id, text);
    cJSON_Delete(content_json);
}

static bool pb_read_varint(const uint8_t *buf, size_t len, size_t *pos, uint64_t *out)
{
    uint64_t v = 0;
    int shift = 0;
    while (*pos < len && shift < 64) {
        uint8_t b = buf[(*pos)++];
        v |= ((uint64_t)(b & 0x7F)) << shift;
        if ((b & 0x80) == 0) {
            *out = v;
            return true;
        }
        shift += 7;
    }
    return false;
}

typedef struct {
    char key[48];
    char value[128];
} ws_header_kv_t;

typedef struct {
    ws_header_kv_t headers[24];
    size_t header_count;
    uint64_t service;
    uint64_t method;
    uint64_t seq_id;
    uint64_t log_id;
    const uint8_t *payload;
    size_t payload_len;
} ws_pb_frame_t;

static bool pb_parse_header_entry(const uint8_t *buf, size_t len, ws_header_kv_t *out)
{
    if (!buf || !out) return false;
    memset(out, 0, sizeof(*out));

    size_t pos = 0;
    while (pos < len) {
        uint64_t tag = 0;
        if (!pb_read_varint(buf, len, &pos, &tag)) return false;
        uint32_t field = (uint32_t)(tag >> 3);
        uint32_t wire = (uint32_t)(tag & 0x7);
        if (wire != 2) return false;

        uint64_t l = 0;
        if (!pb_read_varint(buf, len, &pos, &l)) return false;
        if (pos + l > len) return false;
        if (field == 1) {
            size_t n = (size_t)l < sizeof(out->key) - 1 ? (size_t)l : sizeof(out->key) - 1;
            memcpy(out->key, buf + pos, n);
            out->key[n] = '\0';
        } else if (field == 2) {
            size_t n = (size_t)l < sizeof(out->value) - 1 ? (size_t)l : sizeof(out->value) - 1;
            memcpy(out->value, buf + pos, n);
            out->value[n] = '\0';
        }
        pos += (size_t)l;
    }
    return out->key[0] != '\0';
}

static bool pb_parse_frame(const uint8_t *buf, size_t len, ws_pb_frame_t *out)
{
    if (!buf || len == 0 || !out) return false;
    memset(out, 0, sizeof(*out));

    size_t pos = 0;
    while (pos < len) {
        uint64_t tag = 0;
        if (!pb_read_varint(buf, len, &pos, &tag)) return false;
        uint32_t field = (uint32_t)(tag >> 3);
        uint32_t wire = (uint32_t)(tag & 0x7);

        if (wire == 0) {
            uint64_t v = 0;
            if (!pb_read_varint(buf, len, &pos, &v)) return false;
            if (field == 2) out->service = v;
            else if (field == 3) out->method = v;
            else if (field == 4) out->seq_id = v;
            else if (field == 5) out->log_id = v;
        } else if (wire == 2) {
            uint64_t l = 0;
            if (!pb_read_varint(buf, len, &pos, &l)) return false;
            if (pos + l > len) return false;
            if (field == 1 && out->header_count < (sizeof(out->headers) / sizeof(out->headers[0]))) {
                pb_parse_header_entry(buf + pos, (size_t)l, &out->headers[out->header_count]);
                if (out->headers[out->header_count].key[0]) {
                    out->header_count++;
                }
            } else if (field == 8) {
                out->payload = buf + pos;
                out->payload_len = (size_t)l;
            }
            pos += (size_t)l;
        } else if (wire == 1) {
            if (pos + 8 > len) return false;
            pos += 8;
        } else if (wire == 5) {
            if (pos + 4 > len) return false;
            pos += 4;
        } else {
            return false;
        }
    }
    return true;
}

static const char *pb_frame_header_value(const ws_pb_frame_t *frame, const char *key)
{
    if (!frame || !key) return NULL;
    for (size_t i = 0; i < frame->header_count; i++) {
        if (strcmp(frame->headers[i].key, key) == 0) {
            return frame->headers[i].value;
        }
    }
    return NULL;
}

static bool pb_write_varint(uint8_t *buf, size_t cap, size_t *pos, uint64_t v)
{
    while (1) {
        if (*pos >= cap) return false;
        uint8_t b = (uint8_t)(v & 0x7F);
        v >>= 7;
        if (v) b |= 0x80;
        buf[(*pos)++] = b;
        if (!v) return true;
    }
}

static bool pb_write_bytes_field(uint8_t *buf, size_t cap, size_t *pos,
                                 uint32_t field, const uint8_t *data, size_t data_len)
{
    if (!pb_write_varint(buf, cap, pos, ((uint64_t)field << 3) | 2)) return false;
    if (!pb_write_varint(buf, cap, pos, data_len)) return false;
    if (*pos + data_len > cap) return false;
    memcpy(buf + *pos, data, data_len);
    *pos += data_len;
    return true;
}

static bool pb_write_varint_field(uint8_t *buf, size_t cap, size_t *pos, uint32_t field, uint64_t v)
{
    if (!pb_write_varint(buf, cap, pos, ((uint64_t)field << 3) | 0)) return false;
    return pb_write_varint(buf, cap, pos, v);
}

static bool feishu_send_ack_frame(const ws_pb_frame_t *in_frame, int code, int64_t biz_rt_ms)
{
    if (!s_ws || !in_frame) return false;

    char payload[64];
    int pn = snprintf(payload, sizeof(payload), "{\"code\":%d}", code);
    if (pn <= 0 || pn >= (int)sizeof(payload)) return false;

    uint8_t out[4096];
    size_t pos = 0;

    for (size_t i = 0; i < in_frame->header_count; i++) {
        uint8_t hb[256];
        size_t hp = 0;
        const char *k = in_frame->headers[i].key;
        const char *v = in_frame->headers[i].value;
        if (!k[0]) continue;
        if (!pb_write_bytes_field(hb, sizeof(hb), &hp, 1, (const uint8_t *)k, strlen(k))) return false;
        if (!pb_write_bytes_field(hb, sizeof(hb), &hp, 2, (const uint8_t *)v, strlen(v))) return false;
        if (!pb_write_bytes_field(out, sizeof(out), &pos, 1, hb, hp)) return false;
    }

    char rt[24];
    int rn = snprintf(rt, sizeof(rt), "%lld", (long long)biz_rt_ms);
    if (rn > 0 && rn < (int)sizeof(rt)) {
        uint8_t hb[128];
        size_t hp = 0;
        if (!pb_write_bytes_field(hb, sizeof(hb), &hp, 1,
                                  (const uint8_t *)"biz_rt", strlen("biz_rt"))) return false;
        if (!pb_write_bytes_field(hb, sizeof(hb), &hp, 2,
                                  (const uint8_t *)rt, strlen(rt))) return false;
        if (!pb_write_bytes_field(out, sizeof(out), &pos, 1, hb, hp)) return false;
    }

    if (!pb_write_varint_field(out, sizeof(out), &pos, 2, in_frame->service)) return false;
    if (!pb_write_varint_field(out, sizeof(out), &pos, 3, in_frame->method)) return false;
    if (!pb_write_varint_field(out, sizeof(out), &pos, 4, in_frame->seq_id)) return false;
    if (!pb_write_varint_field(out, sizeof(out), &pos, 5, in_frame->log_id)) return false;
    if (!pb_write_bytes_field(out, sizeof(out), &pos, 8, (const uint8_t *)payload, (size_t)pn)) return false;

    int sent = esp_websocket_client_send_bin(s_ws, (const char *)out, (int)pos, 1000);
    return sent >= 0;
}

static bool ws_handle_event_json(cJSON *root)
{
    if (!root) return false;

    handle_feishu_event_payload(root);
    return true;
}

static cJSON *parse_json_from_bytes(const uint8_t *buf, size_t len)
{
    if (!buf || len == 0) return NULL;
    char *tmp = calloc(1, len + 1);
    if (!tmp) return NULL;
    memcpy(tmp, buf, len);
    cJSON *root = cJSON_Parse(tmp);
    free(tmp);
    return root;
}

static cJSON *parse_event_json_best_effort(const uint8_t *frame_buf, size_t frame_len,
                                           const uint8_t *payload_buf, size_t payload_len)
{
    cJSON *root = NULL;
    if (payload_buf && payload_len > 0) {
        root = parse_json_from_bytes(payload_buf, payload_len);
        if (root) return root;
    }

    root = parse_json_from_bytes(frame_buf, frame_len);
    if (root) return root;

    if (!frame_buf || frame_len == 0) return NULL;
    for (size_t i = 0; i < frame_len; i++) {
        if (frame_buf[i] != '{') continue;
        root = parse_json_from_bytes(frame_buf + i, frame_len - i);
        if (root) return root;
    }
    return NULL;
}

static void ws_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id,
                             void *event_data)
{
    (void)handler_args;
    (void)base;
    esp_websocket_event_data_t *data = (esp_websocket_event_data_t *)event_data;
    if (!data) return;

    if (event_id == WEBSOCKET_EVENT_CONNECTED) {
        ESP_LOGI(TAG, "Feishu websocket connected");
        return;
    }
    if (event_id == WEBSOCKET_EVENT_DISCONNECTED) {
        ESP_LOGW(TAG, "Feishu websocket disconnected");
        return;
    }
    if (event_id != WEBSOCKET_EVENT_DATA) {
        return;
    }

    size_t total = data->payload_len > 0 ? (size_t)data->payload_len : (size_t)data->data_len;
    size_t offset = (size_t)data->payload_offset;
    size_t len = (size_t)data->data_len;

    if (offset == 0) {
        free(s_ws_payload_buf);
        s_ws_payload_buf = NULL;
        s_ws_payload_cap = 0;
        if (total > 0) {
            s_ws_payload_buf = calloc(1, total + 1);
            if (s_ws_payload_buf) {
                s_ws_payload_cap = total + 1;
            }
        }
    }

    if (!s_ws_payload_buf) {
        return;
    }
    if (offset + len + 1 > s_ws_payload_cap) {
        return;
    }

    memcpy(s_ws_payload_buf + offset, data->data_ptr, len);
    if (offset + len >= total) {
        bool handled = false;
        bool acked = false;
        ws_pb_frame_t frame = {0};
        int64_t start_ms = esp_timer_get_time() / 1000;
        const char *message_id = NULL;
        const char *type = NULL;
        bool is_data_frame = false;

        if (pb_parse_frame((const uint8_t *)s_ws_payload_buf, total, &frame)) {
            type = pb_frame_header_value(&frame, "type");
            message_id = pb_frame_header_value(&frame, "message_id");
            is_data_frame = (frame.method == 1);

            cJSON *root = parse_event_json_best_effort((const uint8_t *)s_ws_payload_buf, total,
                                                       frame.payload, frame.payload_len);
            if (root) {
                handled = ws_handle_event_json(root);
                cJSON_Delete(root);
            } else {
                ESP_LOGW(TAG, "Feishu payload JSON parse failed (type=%s method=%llu)",
                         type ? type : "(null)", (unsigned long long)frame.method);
            }

            /* ACK every data frame to avoid replay storms after reconnect/reboot. */
            if (is_data_frame) {
                int64_t rt_ms = (esp_timer_get_time() / 1000) - start_ms;
                acked = feishu_send_ack_frame(&frame, 200, rt_ms);
                ESP_LOGI(TAG, "Feishu ack %s message_id=%s type=%s",
                         acked ? "sent" : "failed",
                         message_id ? message_id : "(null)",
                         type ? type : "(null)");
            }
        } else {
            ESP_LOGW(TAG, "Feishu frame parse failed (len=%u)", (unsigned)total);
        }

        if (!handled && !acked) {
            char *tmp = calloc(1, total + 1);
            if (tmp) {
                memcpy(tmp, s_ws_payload_buf, total);
                cJSON *root = cJSON_Parse(tmp);
                free(tmp);
                if (root) {
                    ws_handle_event_json(root);
                    cJSON_Delete(root);
                }
            }
        }

        free(s_ws_payload_buf);
        s_ws_payload_buf = NULL;
        s_ws_payload_cap = 0;
    }
}

static void feishu_ws_task(void *arg)
{
    (void)arg;

    while (1) {
        if (!wifi_manager_is_connected()) {
            vTaskDelay(pdMS_TO_TICKS(1000));
            continue;
        }

        if (!s_app_id[0] || !s_app_secret[0]) {
            int64_t now = epoch_now();
            if (now >= s_next_no_cred_warn_epoch) {
                ESP_LOGW(TAG, "No Feishu app credentials configured");
                s_next_no_cred_warn_epoch = now + 60;
            }
            vTaskDelay(pdMS_TO_TICKS(5000));
            continue;
        }
        s_next_no_cred_warn_epoch = 0;

        char uri[512] = {0};
        if (fetch_ws_endpoint_url(uri, sizeof(uri)) != ESP_OK || !uri[0]) {
            vTaskDelay(pdMS_TO_TICKS(5000));
            continue;
        }

        esp_websocket_client_config_t cfg = {
            .uri = uri,
            .task_stack = LMCHAN_FS_WS_STACK,
            .network_timeout_ms = 10000,
            .disable_auto_reconnect = true,
            .crt_bundle_attach = esp_crt_bundle_attach,
        };

        s_ws = esp_websocket_client_init(&cfg);
        if (!s_ws) {
            ESP_LOGE(TAG, "Failed to init feishu websocket client");
            vTaskDelay(pdMS_TO_TICKS(3000));
            continue;
        }
        esp_websocket_register_events(s_ws, WEBSOCKET_EVENT_ANY, ws_event_handler, NULL);

        if (esp_websocket_client_start(s_ws) != ESP_OK) {
            ESP_LOGE(TAG, "Failed to start feishu websocket");
            esp_websocket_client_destroy(s_ws);
            s_ws = NULL;
            vTaskDelay(pdMS_TO_TICKS(3000));
            continue;
        }

        while (esp_websocket_client_is_connected(s_ws) && wifi_manager_is_connected()) {
            vTaskDelay(pdMS_TO_TICKS(2000));
        }

        esp_websocket_client_stop(s_ws);
        esp_websocket_client_destroy(s_ws);
        s_ws = NULL;

        vTaskDelay(pdMS_TO_TICKS(2000));
    }
}

esp_err_t feishu_client_init(void)
{
    s_group_mention_only = true;
    if (equals_ignore_case(LMCHAN_SECRET_FS_GROUP_MODE, "all")) {
        s_group_mention_only = false;
    }

    nvs_handle_t nvs;
    if (nvs_open(LMCHAN_NVS_FEISHU, NVS_READONLY, &nvs) == ESP_OK) {
        char tmp[256] = {0};
        size_t len = sizeof(tmp);
        if (nvs_get_str(nvs, LMCHAN_NVS_KEY_FS_APP_ID, tmp, &len) == ESP_OK && tmp[0]) {
            strncpy(s_app_id, tmp, sizeof(s_app_id) - 1);
        }
        memset(tmp, 0, sizeof(tmp));
        len = sizeof(tmp);
        if (nvs_get_str(nvs, LMCHAN_NVS_KEY_FS_APP_SECRET, tmp, &len) == ESP_OK && tmp[0]) {
            strncpy(s_app_secret, tmp, sizeof(s_app_secret) - 1);
        }
        memset(tmp, 0, sizeof(tmp));
        len = sizeof(tmp);
        if (nvs_get_str(nvs, LMCHAN_NVS_KEY_FS_WS_URL, tmp, &len) == ESP_OK && tmp[0]) {
            strncpy(s_ws_url, tmp, sizeof(s_ws_url) - 1);
        }
        memset(tmp, 0, sizeof(tmp));
        len = sizeof(tmp);
        if (nvs_get_str(nvs, LMCHAN_NVS_KEY_FS_GROUP_MODE, tmp, &len) == ESP_OK && tmp[0]) {
            s_group_mention_only = !equals_ignore_case(tmp, "all");
        }
        nvs_close(nvs);
    }
    load_last_message_guard_from_nvs();

    if (strstr(s_ws_url, "ws/v2") != NULL ||
        strncmp(s_ws_url, "wss://", 6) == 0 ||
        strncmp(s_ws_url, "ws://", 5) == 0) {
        ESP_LOGW(TAG, "Legacy ws url detected, fallback to callback endpoint");
        strncpy(s_ws_url, "https://open.feishu.cn/callback/ws/endpoint", sizeof(s_ws_url) - 1);
        s_ws_url[sizeof(s_ws_url) - 1] = '\0';
    }

    ESP_LOGI(TAG, "Feishu configured: app_id=%s ws=%s group_mode=%s",
             s_app_id[0] ? "set" : "empty",
             s_ws_url,
             s_group_mention_only ? "mention" : "all");
#if LMCHAN_FS_STALE_GUARD_ENABLED
    ESP_LOGI(TAG, "Feishu stale guard loaded: last_ts_ms=%lld last_id=%s",
             (long long)s_last_msg_create_ms,
             s_last_msg_id[0] ? "set" : "empty");
#endif
    return ESP_OK;
}

esp_err_t feishu_client_start(void)
{
    if (s_ws_task) return ESP_OK;
    BaseType_t ok = xTaskCreatePinnedToCore(
        feishu_ws_task, "fs_ws",
        LMCHAN_FS_WS_STACK, NULL, LMCHAN_FS_WS_PRIO, &s_ws_task, LMCHAN_FS_WS_CORE);
    return (ok == pdPASS) ? ESP_OK : ESP_FAIL;
}

esp_err_t feishu_send_message(const char *chat_id, const char *text)
{
    if (!chat_id || !chat_id[0] || !text) return ESP_ERR_INVALID_ARG;

    esp_err_t err = ensure_tenant_token();
    if (err != ESP_OK) return err;

    size_t total = strlen(text);
    size_t off = 0;
    while (off < total) {
        size_t chunk = total - off;
        if (chunk > LMCHAN_FS_MAX_MSG_LEN) {
            chunk = LMCHAN_FS_MAX_MSG_LEN;
        }

        char *segment = calloc(1, chunk + 1);
        if (!segment) return ESP_ERR_NO_MEM;
        memcpy(segment, text + off, chunk);

        cJSON *content = cJSON_CreateObject();
        cJSON_AddStringToObject(content, "text", segment);
        char *content_str = cJSON_PrintUnformatted(content);
        cJSON_Delete(content);
        free(segment);
        if (!content_str) return ESP_ERR_NO_MEM;

        cJSON *body = cJSON_CreateObject();
        cJSON_AddStringToObject(body, "receive_id", chat_id);
        cJSON_AddStringToObject(body, "msg_type", "text");
        cJSON_AddStringToObject(body, "content", content_str);
        char *body_str = cJSON_PrintUnformatted(body);
        cJSON_Delete(body);
        free(content_str);
        if (!body_str) return ESP_ERR_NO_MEM;

        char *resp = NULL;
        err = http_post_json("https://open.feishu.cn/open-apis/im/v1/messages?receive_id_type=chat_id",
                             s_tenant_token, body_str, &resp);
        free(body_str);
        if (err != ESP_OK || !resp) {
            ESP_LOGE(TAG, "Feishu send HTTP failed: %s", esp_err_to_name(err));
            free(resp);
            return err != ESP_OK ? err : ESP_FAIL;
        }

        cJSON *root = cJSON_Parse(resp);
        free(resp);
        if (!root) {
            ESP_LOGE(TAG, "Feishu send parse response failed");
            return ESP_FAIL;
        }
        cJSON *code = cJSON_GetObjectItem(root, "code");
        cJSON *msg = cJSON_GetObjectItem(root, "msg");
        if (!cJSON_IsNumber(code) || code->valueint != 0) {
            ESP_LOGE(TAG, "Feishu send rejected: code=%d msg=%s",
                     cJSON_IsNumber(code) ? code->valueint : -1,
                     cJSON_IsString(msg) ? msg->valuestring : "(none)");
            cJSON_Delete(root);
            return ESP_FAIL;
        }
        cJSON_Delete(root);
        off += chunk;
    }

    return ESP_OK;
}

esp_err_t feishu_set_app_id(const char *app_id)
{
    if (!app_id) return ESP_ERR_INVALID_ARG;
    nvs_handle_t nvs;
    ESP_ERROR_CHECK(nvs_open(LMCHAN_NVS_FEISHU, NVS_READWRITE, &nvs));
    ESP_ERROR_CHECK(nvs_set_str(nvs, LMCHAN_NVS_KEY_FS_APP_ID, app_id));
    ESP_ERROR_CHECK(nvs_commit(nvs));
    nvs_close(nvs);
    strncpy(s_app_id, app_id, sizeof(s_app_id) - 1);
    s_tenant_token[0] = '\0';
    return ESP_OK;
}

esp_err_t feishu_set_app_secret(const char *app_secret)
{
    if (!app_secret) return ESP_ERR_INVALID_ARG;
    nvs_handle_t nvs;
    ESP_ERROR_CHECK(nvs_open(LMCHAN_NVS_FEISHU, NVS_READWRITE, &nvs));
    ESP_ERROR_CHECK(nvs_set_str(nvs, LMCHAN_NVS_KEY_FS_APP_SECRET, app_secret));
    ESP_ERROR_CHECK(nvs_commit(nvs));
    nvs_close(nvs);
    strncpy(s_app_secret, app_secret, sizeof(s_app_secret) - 1);
    s_tenant_token[0] = '\0';
    return ESP_OK;
}

esp_err_t feishu_set_ws_url(const char *ws_url)
{
    if (!ws_url || !ws_url[0]) return ESP_ERR_INVALID_ARG;
    nvs_handle_t nvs;
    ESP_ERROR_CHECK(nvs_open(LMCHAN_NVS_FEISHU, NVS_READWRITE, &nvs));
    ESP_ERROR_CHECK(nvs_set_str(nvs, LMCHAN_NVS_KEY_FS_WS_URL, ws_url));
    ESP_ERROR_CHECK(nvs_commit(nvs));
    nvs_close(nvs);
    strncpy(s_ws_url, ws_url, sizeof(s_ws_url) - 1);
    return ESP_OK;
}

esp_err_t feishu_set_group_mode(const char *mode)
{
    if (!mode) return ESP_ERR_INVALID_ARG;
    if (!equals_ignore_case(mode, "mention") && !equals_ignore_case(mode, "all")) {
        return ESP_ERR_INVALID_ARG;
    }

    const char *saved = equals_ignore_case(mode, "all") ? "all" : "mention";
    nvs_handle_t nvs;
    ESP_ERROR_CHECK(nvs_open(LMCHAN_NVS_FEISHU, NVS_READWRITE, &nvs));
    ESP_ERROR_CHECK(nvs_set_str(nvs, LMCHAN_NVS_KEY_FS_GROUP_MODE, saved));
    ESP_ERROR_CHECK(nvs_commit(nvs));
    nvs_close(nvs);

    s_group_mention_only = !equals_ignore_case(saved, "all");
    return ESP_OK;
}

esp_err_t feishu_get_group_mode(char *buf, size_t size)
{
    if (!buf || size == 0) return ESP_ERR_INVALID_ARG;
    snprintf(buf, size, "%s", s_group_mention_only ? "mention" : "all");
    return ESP_OK;
}

bool feishu_group_mention_only(void)
{
    return s_group_mention_only;
}
