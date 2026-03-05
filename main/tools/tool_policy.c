#include "tools/tool_policy.h"
#include "lmchan_config.h"

#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include "esp_log.h"
#include "cJSON.h"

static const char *TAG = "tool_policy";

#define POLICY_MAX_RULES 64
#define POLICY_MAX_TOOLS 20

typedef struct {
    bool in_use;
    bool allow;
    char channel[16];
    char chat_id[96];
    char type[48];
    char tools[POLICY_MAX_TOOLS][32];
    int tool_count;
    bool any_tool;
} policy_rule_t;

static policy_rule_t s_rules[POLICY_MAX_RULES];
static int s_rule_count = 0;
static bool s_default_allow = true; /* compatibility fallback */
static bool s_initialized = false;
static time_t s_policy_mtime = -1;
static bool s_policy_exists = false;

static void add_fallback_allow_all_rule(void)
{
    memset(s_rules, 0, sizeof(s_rules));
    s_rule_count = 1;
    s_rules[0].in_use = true;
    s_rules[0].allow = true;
    strncpy(s_rules[0].channel, "*", sizeof(s_rules[0].channel) - 1);
    strncpy(s_rules[0].chat_id, "*", sizeof(s_rules[0].chat_id) - 1);
    strncpy(s_rules[0].type, "*", sizeof(s_rules[0].type) - 1);
    s_rules[0].any_tool = true;
    s_default_allow = true;
}

static bool str_eq_or_wildcard(const char *rule_value, const char *actual_value)
{
    if (!rule_value || !rule_value[0] || strcmp(rule_value, "*") == 0) {
        return true;
    }
    if (!actual_value) {
        actual_value = "";
    }
    return strcmp(rule_value, actual_value) == 0;
}

static bool rule_matches_tool(const policy_rule_t *rule, const char *tool_name)
{
    if (!rule || !tool_name || !tool_name[0]) {
        return false;
    }
    if (rule->any_tool) {
        return true;
    }
    for (int i = 0; i < rule->tool_count; i++) {
        if (strcmp(rule->tools[i], tool_name) == 0) {
            return true;
        }
    }
    return false;
}

static bool rule_matches_ctx(const policy_rule_t *rule, const lmchan_tool_exec_ctx_t *ctx)
{
    const char *channel = (ctx && ctx->channel && ctx->channel[0]) ? ctx->channel : "";
    const char *chat_id = (ctx && ctx->chat_id && ctx->chat_id[0]) ? ctx->chat_id : "";
    const char *type = (ctx && ctx->type && ctx->type[0]) ? ctx->type : "unknown";
    return str_eq_or_wildcard(rule->channel, channel) &&
           str_eq_or_wildcard(rule->chat_id, chat_id) &&
           str_eq_or_wildcard(rule->type, type);
}

static bool parse_rules(cJSON *rules)
{
    memset(s_rules, 0, sizeof(s_rules));
    s_rule_count = 0;
    if (!rules || !cJSON_IsArray(rules)) {
        return true;
    }

    cJSON *item = NULL;
    cJSON_ArrayForEach(item, rules) {
        if (s_rule_count >= POLICY_MAX_RULES) {
            break;
        }
        if (!cJSON_IsObject(item)) {
            continue;
        }

        const char *effect = cJSON_GetStringValue(cJSON_GetObjectItem(item, "effect"));
        const char *channel = cJSON_GetStringValue(cJSON_GetObjectItem(item, "channel"));
        const char *chat_id = cJSON_GetStringValue(cJSON_GetObjectItem(item, "chat_id"));
        const char *type = cJSON_GetStringValue(cJSON_GetObjectItem(item, "type"));
        cJSON *tools = cJSON_GetObjectItem(item, "tools");
        if (!effect || !tools || !cJSON_IsArray(tools)) {
            continue;
        }

        policy_rule_t *rule = &s_rules[s_rule_count];
        rule->in_use = true;
        rule->allow = (strcmp(effect, "allow") == 0);
        strncpy(rule->channel, channel ? channel : "*", sizeof(rule->channel) - 1);
        strncpy(rule->chat_id, chat_id ? chat_id : "*", sizeof(rule->chat_id) - 1);
        strncpy(rule->type, type ? type : "*", sizeof(rule->type) - 1);

        cJSON *tool = NULL;
        cJSON_ArrayForEach(tool, tools) {
            const char *tool_name = cJSON_GetStringValue(tool);
            if (!tool_name || !tool_name[0]) {
                continue;
            }
            if (strcmp(tool_name, "*") == 0) {
                rule->any_tool = true;
                rule->tool_count = 0;
                break;
            }
            if (rule->tool_count < POLICY_MAX_TOOLS) {
                strncpy(rule->tools[rule->tool_count], tool_name,
                        sizeof(rule->tools[rule->tool_count]) - 1);
                rule->tool_count++;
            }
        }

        if (rule->any_tool || rule->tool_count > 0) {
            s_rule_count++;
        }
    }

    return true;
}

static esp_err_t load_policy_from_file(void)
{
    FILE *f = fopen(LMCHAN_TOOL_POLICY_FILE, "r");
    if (!f) {
        add_fallback_allow_all_rule();
        ESP_LOGW(TAG, "Policy file not found, fallback to allow-all compatibility mode");
        return ESP_OK;
    }

    fseek(f, 0, SEEK_END);
    long fsize = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (fsize <= 0 || fsize > 32768) {
        fclose(f);
        add_fallback_allow_all_rule();
        ESP_LOGW(TAG, "Policy file invalid size, fallback to allow-all");
        return ESP_OK;
    }

    char *buf = calloc(1, (size_t)fsize + 1);
    if (!buf) {
        fclose(f);
        return ESP_ERR_NO_MEM;
    }
    size_t n = fread(buf, 1, (size_t)fsize, f);
    fclose(f);
    buf[n] = '\0';

    cJSON *root = cJSON_Parse(buf);
    free(buf);
    if (!root || !cJSON_IsObject(root)) {
        cJSON_Delete(root);
        add_fallback_allow_all_rule();
        ESP_LOGW(TAG, "Policy JSON parse failed, fallback to allow-all");
        return ESP_OK;
    }

    const char *mode = cJSON_GetStringValue(cJSON_GetObjectItem(root, "mode"));
    const char *default_effect = cJSON_GetStringValue(cJSON_GetObjectItem(root, "default_effect"));

    if (mode && strcmp(mode, "explicit_allow") == 0) {
        s_default_allow = false;
    } else if (mode && strcmp(mode, "compat_allow") == 0) {
        s_default_allow = true;
    } else if (default_effect && strcmp(default_effect, "allow") == 0) {
        s_default_allow = true;
    } else {
        s_default_allow = false;
    }

    parse_rules(cJSON_GetObjectItem(root, "rules"));
    cJSON_Delete(root);

    ESP_LOGI(TAG, "Policy loaded: rules=%d default=%s",
             s_rule_count, s_default_allow ? "allow" : "deny");
    return ESP_OK;
}

esp_err_t tool_policy_init(void)
{
    struct stat st;
    s_policy_exists = (stat(LMCHAN_TOOL_POLICY_FILE, &st) == 0);
    s_policy_mtime = s_policy_exists ? st.st_mtime : -1;
    esp_err_t err = load_policy_from_file();
    if (err == ESP_OK) {
        s_initialized = true;
    }
    return err;
}

esp_err_t tool_policy_load_if_changed(void)
{
    struct stat st;
    bool exists = (stat(LMCHAN_TOOL_POLICY_FILE, &st) == 0);
    time_t mtime = exists ? st.st_mtime : -1;
    if (s_initialized && exists == s_policy_exists && mtime == s_policy_mtime) {
        return ESP_OK;
    }

    s_policy_exists = exists;
    s_policy_mtime = mtime;
    esp_err_t err = load_policy_from_file();
    if (err == ESP_OK) {
        s_initialized = true;
    }
    return err;
}

bool tool_policy_is_allowed(const lmchan_tool_exec_ctx_t *ctx, const char *tool_name,
                            const char **reason_out)
{
    if (reason_out) {
        *reason_out = "default_allow";
    }
    if (!s_initialized) {
        tool_policy_init();
    }

    bool has_allow = false;
    for (int i = 0; i < s_rule_count; i++) {
        const policy_rule_t *rule = &s_rules[i];
        if (!rule->in_use) {
            continue;
        }
        if (!rule_matches_ctx(rule, ctx) || !rule_matches_tool(rule, tool_name)) {
            continue;
        }
        if (!rule->allow) {
            if (reason_out) {
                *reason_out = "rule_deny";
            }
            return false;
        }
        has_allow = true;
    }

    if (has_allow) {
        if (reason_out) {
            *reason_out = "rule_allow";
        }
        return true;
    }

    if (reason_out) {
        *reason_out = s_default_allow ? "default_allow" : "default_deny";
    }
    return s_default_allow;
}
