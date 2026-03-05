#pragma once

#include "esp_err.h"
#include <stdbool.h>
#include <stddef.h>

esp_err_t feishu_client_init(void);
esp_err_t feishu_client_start(void);

esp_err_t feishu_send_message(const char *chat_id, const char *text);
esp_err_t feishu_send_message_ex(const char *chat_id, const char *msg_type, const char *content_json);

esp_err_t feishu_upload_image(const char *spiffs_path, char *image_key, size_t key_size);
esp_err_t feishu_upload_file(const char *spiffs_path, const char *file_type, char *file_key, size_t key_size);

esp_err_t feishu_reaction_add(const char *message_id, const char *emoji_type);
esp_err_t feishu_reaction_remove(const char *message_id, const char *emoji_type);
esp_err_t feishu_delete_message(const char *message_id);

void feishu_begin_thinking(const char *source_message_id, const char *chat_id);
void feishu_finish_thinking(const char *source_message_id);

esp_err_t feishu_set_app_id(const char *app_id);
esp_err_t feishu_set_app_secret(const char *app_secret);
esp_err_t feishu_set_ws_url(const char *ws_url);
esp_err_t feishu_set_group_mode(const char *mode);

esp_err_t feishu_get_group_mode(char *buf, size_t size);
bool feishu_group_mention_only(void);
