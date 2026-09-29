/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#pragma once

#include <stdbool.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialize the status LED backend used by the demo.
 *
 * The LED policy is intentionally simple for bring-up and public examples:
 * blinking while Wi-Fi is disconnected, solid on motion, then fade out after
 * the AP channel becomes inactive again.
 */
esp_err_t led_init(void);

/**
 * @brief Update the Wi-Fi connectivity state shown on the LED.
 */
void led_set_wifi_connected(bool connected);

/**
 * @brief Notify the LED state machine that AP-channel motion became active.
 */
void led_notify_ap_active(void);

/**
 * @brief Notify the LED state machine that AP-channel motion became inactive.
 */
void led_notify_ap_inactive(void);

#ifdef __cplusplus
}
#endif
