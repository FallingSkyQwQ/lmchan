#pragma once

#include "esp_err.h"
#include <stdbool.h>
#include <stddef.h>

esp_err_t feishu_client_init(void);
esp_err_t feishu_client_start(void);

esp_err_t feishu_send_message(const char *chat_id, const char *text);

esp_err_t feishu_set_app_id(const char *app_id);
esp_err_t feishu_set_app_secret(const char *app_secret);
esp_err_t feishu_set_ws_url(const char *ws_url);
esp_err_t feishu_set_group_mode(const char *mode);

esp_err_t feishu_get_group_mode(char *buf, size_t size);
bool feishu_group_mention_only(void);
