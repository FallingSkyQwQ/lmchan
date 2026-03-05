#pragma once

#include "esp_err.h"
#include <stddef.h>

/**
 * Initialize the Telegram bot.
 */
esp_err_t telegram_bot_init(void);

/**
 * Start the Telegram polling task (long polling on Core 0).
 */
esp_err_t telegram_bot_start(void);

/**
 * Send a text message to a Telegram chat.
 * Automatically splits messages longer than 4096 chars.
 * @param chat_id  Telegram chat ID (numeric string)
 * @param text     Message text (supports Markdown)
 */
esp_err_t telegram_send_message(const char *chat_id, const char *text);

/**
 * Save the Telegram bot token to NVS.
 */
esp_err_t telegram_set_token(const char *token);

/**
 * Set Telegram user allowlist (comma-separated user IDs).
 * Empty string means allow all users.
 */
esp_err_t telegram_set_allowlist(const char *allowlist_csv);

/**
 * Get current Telegram allowlist value.
 */
esp_err_t telegram_get_allowlist(char *buf, size_t size);

