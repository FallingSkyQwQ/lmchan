#include "heartbeat/heartbeat.h"
#include "lmchan_config.h"
#include "bus/message_bus.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <ctype.h>
#include "freertos/FreeRTOS.h"
#include "freertos/timers.h"
#include "esp_log.h"
#include "nvs.h"

static const char *TAG = "heartbeat";

#define HEARTBEAT_PROMPT \
    "Read " LMCHAN_HEARTBEAT_FILE " and follow any instructions or tasks listed there. " \
    "If nothing needs attention, reply with just: HEARTBEAT_OK"

static TimerHandle_t s_heartbeat_timer = NULL;
static uint32_t s_heartbeat_interval_ms = LMCHAN_HEARTBEAT_INTERVAL_MS;

/* ── Content check ────────────────────────────────────────────── */

/**
 * Check if HEARTBEAT.md has actionable content.
 * Returns true if any line is NOT:
 *   - empty / whitespace-only
 *   - a markdown header (starts with #)
 *   - a completed checkbox (- [x] or * [x])
 */
static bool heartbeat_has_tasks(void)
{
    FILE *f = fopen(LMCHAN_HEARTBEAT_FILE, "r");
    if (!f) {
        return false;
    }

    char line[256];
    bool found_task = false;

    while (fgets(line, sizeof(line), f)) {
        /* Skip leading whitespace */
        const char *p = line;
        while (*p && isspace((unsigned char)*p)) {
            p++;
        }

        /* Skip empty lines */
        if (*p == '\0') {
            continue;
        }

        /* Skip markdown headers */
        if (*p == '#') {
            continue;
        }

        /* Skip completed checkboxes: "- [x]" or "* [x]" */
        if ((*p == '-' || *p == '*') && *(p + 1) == ' ' && *(p + 2) == '[') {
            char mark = *(p + 3);
            if ((mark == 'x' || mark == 'X') && *(p + 4) == ']') {
                continue;
            }
        }

        /* Found an actionable line */
        found_task = true;
        break;
    }

    fclose(f);
    return found_task;
}

/* ── Send heartbeat to agent ──────────────────────────────────── */

static bool heartbeat_send(void)
{
    if (!heartbeat_has_tasks()) {
        ESP_LOGD(TAG, "No actionable tasks in HEARTBEAT.md");
        return false;
    }

    lmchan_msg_t msg;
    memset(&msg, 0, sizeof(msg));
    strncpy(msg.channel, LMCHAN_CHAN_SYSTEM, sizeof(msg.channel) - 1);
    strncpy(msg.chat_id, "heartbeat", sizeof(msg.chat_id) - 1);
    msg.content = strdup(HEARTBEAT_PROMPT);

    if (!msg.content) {
        ESP_LOGE(TAG, "Failed to allocate heartbeat prompt");
        return false;
    }

    esp_err_t err = message_bus_push_inbound(&msg);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Failed to push heartbeat message: %s", esp_err_to_name(err));
        free(msg.content);
        return false;
    }

    ESP_LOGI(TAG, "Triggered agent check");
    return true;
}

/* ── Timer callback ───────────────────────────────────────────── */

static void heartbeat_timer_callback(TimerHandle_t xTimer)
{
    (void)xTimer;
    heartbeat_send();
}

/* ── Public API ───────────────────────────────────────────────── */

esp_err_t heartbeat_init(void)
{
    nvs_handle_t nvs;
    if (nvs_open(LMCHAN_NVS_HEARTBEAT, NVS_READONLY, &nvs) == ESP_OK) {
        uint32_t minutes = 0;
        if (nvs_get_u32(nvs, LMCHAN_NVS_KEY_HEARTBEAT_INTERVAL_MIN, &minutes) == ESP_OK && minutes > 0) {
            s_heartbeat_interval_ms = minutes * 60 * 1000;
        }
        nvs_close(nvs);
    }
    ESP_LOGI(TAG, "Heartbeat service initialized (file: %s, interval: %ds)",
             LMCHAN_HEARTBEAT_FILE, s_heartbeat_interval_ms / 1000);
    return ESP_OK;
}

esp_err_t heartbeat_start(void)
{
    if (s_heartbeat_timer) {
        ESP_LOGW(TAG, "Heartbeat timer already running");
        return ESP_OK;
    }

    s_heartbeat_timer = xTimerCreate(
        "heartbeat",
        pdMS_TO_TICKS(s_heartbeat_interval_ms),
        pdTRUE,    /* auto-reload */
        NULL,
        heartbeat_timer_callback
    );

    if (!s_heartbeat_timer) {
        ESP_LOGE(TAG, "Failed to create heartbeat timer");
        return ESP_FAIL;
    }

    if (xTimerStart(s_heartbeat_timer, pdMS_TO_TICKS(1000)) != pdPASS) {
        ESP_LOGE(TAG, "Failed to start heartbeat timer");
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "Heartbeat started (every %d min)", s_heartbeat_interval_ms / 60000);
    return ESP_OK;
}

void heartbeat_stop(void)
{
    if (s_heartbeat_timer) {
        xTimerStop(s_heartbeat_timer, pdMS_TO_TICKS(1000));
        xTimerDelete(s_heartbeat_timer, pdMS_TO_TICKS(1000));
        s_heartbeat_timer = NULL;
        ESP_LOGI(TAG, "Heartbeat stopped");
    }
}

bool heartbeat_trigger(void)
{
    return heartbeat_send();
}

esp_err_t heartbeat_set_interval_minutes(uint32_t minutes)
{
    if (minutes == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    nvs_handle_t nvs;
    ESP_ERROR_CHECK(nvs_open(LMCHAN_NVS_HEARTBEAT, NVS_READWRITE, &nvs));
    ESP_ERROR_CHECK(nvs_set_u32(nvs, LMCHAN_NVS_KEY_HEARTBEAT_INTERVAL_MIN, minutes));
    ESP_ERROR_CHECK(nvs_commit(nvs));
    nvs_close(nvs);

    s_heartbeat_interval_ms = minutes * 60 * 1000;
    if (s_heartbeat_timer) {
        xTimerChangePeriod(s_heartbeat_timer, pdMS_TO_TICKS(s_heartbeat_interval_ms), pdMS_TO_TICKS(1000));
    }
    ESP_LOGI(TAG, "Heartbeat interval updated to %u min", (unsigned)minutes);
    return ESP_OK;
}

uint32_t heartbeat_get_interval_minutes(void)
{
    return s_heartbeat_interval_ms / 60000;
}
