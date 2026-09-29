#pragma once

#include "esp_err.h"

/*
 * Starts DrWalls Wi-Fi.
 *
 * If credentials are already saved in NVS:
 *   -> Connect to Wi-Fi.
 *
 * If no credentials are saved:
 *   -> Start the DrWalls-Setup configuration portal.
 */
esp_err_t drwalls_wifi_start(void);

/*
 * Erases the Wi-Fi credentials saved by DrWalls.
 */
esp_err_t drwalls_wifi_reset(void);