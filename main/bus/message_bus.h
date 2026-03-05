#pragma once

#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

/* Channel identifiers */
#define LMCHAN_CHAN_TELEGRAM   "telegram"
#define LMCHAN_CHAN_WEBSOCKET  "websocket"
#define LMCHAN_CHAN_FEISHU     "feishu"
#define LMCHAN_CHAN_CLI        "cli"
#define LMCHAN_CHAN_SYSTEM     "system"

/* Message types on the bus */
typedef struct {
    char channel[16];       /* "telegram", "websocket", "cli" */
    char chat_id[96];       /* Channel-specific chat target ID */
    char source_message_id[96];
    char message_type[24];
    char event_type[48];
    char *content;          /* Heap-allocated message text (consumer must free) */
    char *raw_json;         /* Optional heap-allocated payload snippet (consumer must free) */
} lmchan_msg_t;

/**
 * Initialize the message bus (inbound + outbound FreeRTOS queues).
 */
esp_err_t message_bus_init(void);

/**
 * Push a message to the inbound queue (towards Agent Loop).
 * The bus takes ownership of msg->content.
 */
esp_err_t message_bus_push_inbound(const lmchan_msg_t *msg);

/**
 * Pop a message from the inbound queue (blocking).
 * Caller must free msg->content when done.
 */
esp_err_t message_bus_pop_inbound(lmchan_msg_t *msg, uint32_t timeout_ms);

/**
 * Push a message to the outbound queue (towards channels).
 * The bus takes ownership of msg->content.
 */
esp_err_t message_bus_push_outbound(const lmchan_msg_t *msg);

/**
 * Pop a message from the outbound queue (blocking).
 * Caller must free msg->content when done.
 */
esp_err_t message_bus_pop_outbound(lmchan_msg_t *msg, uint32_t timeout_ms);
