#pragma once

#include <stdbool.h>

// LED RGB di bordo (WS2812 su GPIO21 della S3-Zero).
void led_init(void);
void led_set_master(bool on);