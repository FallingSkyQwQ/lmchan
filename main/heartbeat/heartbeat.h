#pragma once

#include "esp_err.h"
#include <stdbool.h>
#include <stdint.h>

/**
 * Initialize the heartbeat service (logs ready state).
 */
esp_err_t heartbeat_init(void);

/**
 * Start the heartbeat timer. Checks HEARTBEAT.md periodically
 * and sends a prompt to the agent if actionable tasks are found.
 */
esp_err_t heartbeat_start(void);

/**
 * Stop and delete the heartbeat timer.
 */
void heartbeat_stop(void);

/**
 * Manually trigger a heartbeat check (for CLI testing).
 * Returns true if the agent was prompted, false if no tasks found.
 */
bool heartbeat_trigger(void);

/**
 * Set heartbeat interval in minutes and persist to NVS.
 */
esp_err_t heartbeat_set_interval_minutes(uint32_t minutes);

/**
 * Get current heartbeat interval in minutes.
 */
uint32_t heartbeat_get_interval_minutes(void);
