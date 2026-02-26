#pragma once

#include <stdbool.h>
#include "esp_err.h"

esp_err_t activity_led_init(void);
void activity_led_set_busy(bool busy);
