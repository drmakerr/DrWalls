#pragma once

#include <stdbool.h>
#include "esp_err.h"
#include "esp_wifi_sensing.h"

esp_err_t wifi_web_server_start(
    esp_wifi_sensing_fsm_handle_t fsm,
    const uint8_t ap_mac[6]
);

/* Update the motion state displayed by the web UI. */
void wifi_web_server_set_motion(bool active);