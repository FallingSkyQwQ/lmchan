#pragma once

/* lmchan Global Configuration */

/* Build-time secrets (highest priority, override NVS) */
#if __has_include("lmchan_secrets.h")
#include "lmchan_secrets.h"
#endif

#ifndef LMCHAN_SECRET_WIFI_SSID
#define LMCHAN_SECRET_WIFI_SSID       ""
#endif
#ifndef LMCHAN_SECRET_WIFI_PASS
#define LMCHAN_SECRET_WIFI_PASS       ""
#endif
#ifndef LMCHAN_SECRET_TG_TOKEN
#define LMCHAN_SECRET_TG_TOKEN        ""
#endif
#ifndef LMCHAN_SECRET_TG_ALLOWLIST
#define LMCHAN_SECRET_TG_ALLOWLIST    ""
#endif
#ifndef LMCHAN_SECRET_FS_APP_ID
#define LMCHAN_SECRET_FS_APP_ID       ""
#endif
#ifndef LMCHAN_SECRET_FS_APP_SECRET
#define LMCHAN_SECRET_FS_APP_SECRET   ""
#endif
#ifndef LMCHAN_SECRET_FS_WS_URL
#define LMCHAN_SECRET_FS_WS_URL       "https://open.feishu.cn/callback/ws/endpoint"
#endif
#ifndef LMCHAN_SECRET_FS_GROUP_MODE
#define LMCHAN_SECRET_FS_GROUP_MODE   "mention"
#endif
#ifndef LMCHAN_SECRET_API_KEY
#define LMCHAN_SECRET_API_KEY         ""
#endif
#ifndef LMCHAN_SECRET_MODEL
#define LMCHAN_SECRET_MODEL           ""
#endif
#ifndef LMCHAN_SECRET_MODEL_PROVIDER
#define LMCHAN_SECRET_MODEL_PROVIDER  "anthropic"
#endif
#ifndef LMCHAN_SECRET_API_BASE_URL
#define LMCHAN_SECRET_API_BASE_URL    ""
#endif
#ifndef LMCHAN_SECRET_API_PATH
#define LMCHAN_SECRET_API_PATH        "/v1/chat/completions"
#endif
#ifndef LMCHAN_SECRET_PROXY_HOST
#define LMCHAN_SECRET_PROXY_HOST      ""
#endif
#ifndef LMCHAN_SECRET_PROXY_PORT
#define LMCHAN_SECRET_PROXY_PORT      ""
#endif
#ifndef LMCHAN_SECRET_PROXY_TYPE
#define LMCHAN_SECRET_PROXY_TYPE      ""
#endif
#ifndef LMCHAN_SECRET_SEARCH_KEY
#define LMCHAN_SECRET_SEARCH_KEY      ""
#endif

/* WiFi */
#define LMCHAN_WIFI_MAX_RETRY          10
#define LMCHAN_WIFI_RETRY_BASE_MS      1000
#define LMCHAN_WIFI_RETRY_MAX_MS       30000

/* Telegram Bot */
#define LMCHAN_TG_POLL_TIMEOUT_S       30
#define LMCHAN_TG_MAX_MSG_LEN          4096
#define LMCHAN_TG_POLL_STACK           (12 * 1024)
#define LMCHAN_TG_POLL_PRIO            5
#define LMCHAN_TG_POLL_CORE            0
#define LMCHAN_TG_CARD_SHOW_MS         3000
#define LMCHAN_TG_CARD_BODY_SCALE      3

/* Feishu Long Connection */
#define LMCHAN_FS_WS_STACK             (12 * 1024)
#define LMCHAN_FS_WS_PRIO              5
#define LMCHAN_FS_WS_CORE              0
#define LMCHAN_FS_MAX_MSG_LEN          3000
#define LMCHAN_FS_STALE_GUARD_ENABLED  1

/* Agent Loop */
#define LMCHAN_AGENT_STACK             (24 * 1024)
#define LMCHAN_AGENT_PRIO              6
#define LMCHAN_AGENT_CORE              1
#define LMCHAN_AGENT_MAX_HISTORY       20
#define LMCHAN_AGENT_MAX_TOOL_ITER     10
#define LMCHAN_MAX_TOOL_CALLS          4
#define LMCHAN_AGENT_SEND_WORKING_STATUS 1

/* Timezone (POSIX TZ format) */
#define LMCHAN_TIMEZONE                "PST8PDT,M3.2.0,M11.1.0"

/* LLM */
#define LMCHAN_LLM_DEFAULT_MODEL       "claude-opus-4-5"
#define LMCHAN_LLM_PROVIDER_DEFAULT    "anthropic"
#define LMCHAN_LLM_MAX_TOKENS          4096
#define LMCHAN_LLM_API_URL             "https://api.anthropic.com/v1/messages"
#define LMCHAN_OPENAI_API_URL          "https://api.openai.com/v1/chat/completions"
#define LMCHAN_LLM_API_VERSION         "2023-06-01"
#define LMCHAN_LLM_STREAM_BUF_SIZE     (32 * 1024)
#define LMCHAN_LLM_LOG_VERBOSE_PAYLOAD 0
#define LMCHAN_LLM_LOG_PREVIEW_BYTES   160

/* Message Bus */
#define LMCHAN_BUS_QUEUE_LEN           16
#define LMCHAN_OUTBOUND_STACK          (12 * 1024)
#define LMCHAN_OUTBOUND_PRIO           5
#define LMCHAN_OUTBOUND_CORE           0

/* Memory / SPIFFS */
#define LMCHAN_SPIFFS_BASE             "/spiffs"
#define LMCHAN_SPIFFS_CONFIG_DIR       LMCHAN_SPIFFS_BASE "/config"
#define LMCHAN_SPIFFS_MEMORY_DIR       LMCHAN_SPIFFS_BASE "/memory"
#define LMCHAN_SPIFFS_SESSION_DIR      LMCHAN_SPIFFS_BASE "/sessions"
#define LMCHAN_MEMORY_FILE             LMCHAN_SPIFFS_MEMORY_DIR "/MEMORY.md"
#define LMCHAN_SOUL_FILE               LMCHAN_SPIFFS_CONFIG_DIR "/SOUL.md"
#define LMCHAN_USER_FILE               LMCHAN_SPIFFS_CONFIG_DIR "/USER.md"
#define LMCHAN_AGENTS_FILE             LMCHAN_SPIFFS_CONFIG_DIR "/AGENTS.md"
#define LMCHAN_TOOLS_FILE              LMCHAN_SPIFFS_CONFIG_DIR "/TOOLS.md"
#define LMCHAN_TOOL_POLICY_FILE        LMCHAN_SPIFFS_CONFIG_DIR "/TOOL_POLICY.json"
#define LMCHAN_CONTEXT_BUF_SIZE        (16 * 1024)
#define LMCHAN_SESSION_MAX_MSGS        20

/* Cron / Heartbeat */
#define LMCHAN_CRON_FILE               LMCHAN_SPIFFS_BASE "/cron.json"
#define LMCHAN_CRON_MAX_JOBS           16
#define LMCHAN_CRON_CHECK_INTERVAL_MS  (60 * 1000)
#define LMCHAN_HEARTBEAT_FILE          LMCHAN_SPIFFS_BASE "/HEARTBEAT.md"
#define LMCHAN_HEARTBEAT_INTERVAL_MS   (30 * 60 * 1000)

/* Subagent / Background Jobs */
#define LMCHAN_SUBAGENT_FILE           LMCHAN_SPIFFS_BASE "/subagent_jobs.json"
#define LMCHAN_SUBAGENT_MAX_JOBS       24
#define LMCHAN_SUBAGENT_QUEUE_LEN      8
#define LMCHAN_SUBAGENT_WORKER_STACK   (14 * 1024)
#define LMCHAN_SUBAGENT_WORKER_PRIO    5
#define LMCHAN_SUBAGENT_WORKER_CORE    0
#define LMCHAN_SUBAGENT_RESULT_MAX_BYTES 1024

/* Skills */
#define LMCHAN_SKILLS_PREFIX           LMCHAN_SPIFFS_BASE "/skills/"

/* WebSocket Gateway (disabled by default) */
#define LMCHAN_WS_PORT                 18789
#define LMCHAN_WS_MAX_CLIENTS          4

/* Serial CLI */
#define LMCHAN_CLI_STACK               (4 * 1024)
#define LMCHAN_CLI_PRIO                3
#define LMCHAN_CLI_CORE                0

/* Activity LED (WS2812/NeoPixel) */
#define LMCHAN_LED_ENABLED             1
#define LMCHAN_LED_GPIO                48
#define LMCHAN_LED_COUNT               8
#define LMCHAN_LED_MAX_BRIGHTNESS      8

/* NVS Namespaces */
#define LMCHAN_NVS_WIFI                "wifi_config"
#define LMCHAN_NVS_TG                  "tg_config"
#define LMCHAN_NVS_FEISHU              "fs_config"
#define LMCHAN_NVS_LLM                 "llm_config"
#define LMCHAN_NVS_PROXY               "proxy_config"
#define LMCHAN_NVS_SEARCH              "search_config"
#define LMCHAN_NVS_HEARTBEAT           "heartbeat_config"

/* NVS Keys */
#define LMCHAN_NVS_KEY_SSID            "ssid"
#define LMCHAN_NVS_KEY_PASS            "password"
#define LMCHAN_NVS_KEY_TG_TOKEN        "bot_token"
#define LMCHAN_NVS_KEY_FS_APP_ID       "app_id"
#define LMCHAN_NVS_KEY_FS_APP_SECRET   "app_secret"
#define LMCHAN_NVS_KEY_FS_WS_URL       "ws_url"
#define LMCHAN_NVS_KEY_FS_GROUP_MODE   "group_mode"
#define LMCHAN_NVS_KEY_FS_LAST_MSG_TS  "last_msg_ts"
#define LMCHAN_NVS_KEY_FS_LAST_MSG_ID  "last_msg_id"
#define LMCHAN_NVS_KEY_API_KEY         "api_key"
#define LMCHAN_NVS_KEY_MODEL           "model"
#define LMCHAN_NVS_KEY_PROVIDER        "provider"
#define LMCHAN_NVS_KEY_API_BASE_URL    "api_base_url"
#define LMCHAN_NVS_KEY_API_PATH        "api_path"
#define LMCHAN_NVS_KEY_PROXY_HOST      "host"
#define LMCHAN_NVS_KEY_PROXY_PORT      "port"
#define LMCHAN_NVS_KEY_TG_ALLOWLIST    "allowlist"
#define LMCHAN_NVS_KEY_HEARTBEAT_INTERVAL_MIN "interval_min"
