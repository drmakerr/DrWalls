/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#pragma once

#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"
#include "esp_wifi_sensing.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Maximum number of peers supported by the demo's browser monitor.
 */
#define WEB_SERIAL_MONITOR_MAX_PEERS 8

/**
 * @brief Peer entry exported to the browser monitor.
 */
typedef struct {
    const char *name; /**< Human-readable peer name shown by the UI. */
    uint8_t mac[6];   /**< Peer MAC address bound to the sensing channel. */
} web_serial_monitor_peer_t;

/**
 * @brief Configuration passed to `web_serial_monitor_init()`.
 */
typedef struct {
    esp_wifi_sensing_fsm_handle_t fsm;           /**< Sensing FSM instance to inspect and control. */
    const web_serial_monitor_peer_t *peers;      /**< Peer table visible to the UI. */
    size_t peer_num;                             /**< Number of valid entries in `peers`. */
    uint32_t stream_period_ms;                   /**< Streaming period in milliseconds. Set `0` to use the default. */
} web_serial_monitor_config_t;

/**
 * @brief Start the lightweight serial protocol used by the browser monitor.
 *
 * The module emits line-delimited JSON messages with the `HMS:` prefix and
 * accepts line-delimited commands prefixed with `HMSCMD `. This keeps the demo
 * easy to inspect from a terminal while still being simple for a Web Serial UI
 * to parse.
 *
 * @param config Browser-monitor configuration.
 * @return
 *      - ESP_OK: Success
 *      - ESP_ERR_INVALID_ARG: `config` is invalid
 *      - ESP_ERR_INVALID_STATE: The monitor is already initialized
 *      - ESP_FAIL: Failed to create the background task or configure inputs
 */
esp_err_t web_serial_monitor_init(const web_serial_monitor_config_t *config);

#ifdef __cplusplus
}
#endif
