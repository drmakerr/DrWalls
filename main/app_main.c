/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdio.h>
#include <inttypes.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "nvs_flash.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_wifi.h"
#include "esp_now.h"
#include "esp_netif.h"
#include "esp_event.h"

#include "esp_wifi_sensing.h"

#include "protocol_examples_common.h"
#include "led_control.h"
#include "web_serial_monitor.h"
#include "wifi_web_server.h"
#include "drwalls_wifi.h"
#include "mdns.h"

static const char *TAG = "wifi_sensing_demo";
static esp_wifi_sensing_fsm_handle_t s_hms = NULL;
static uint8_t s_mac_ap[6] = {0};

/*
 * The demo monitors three channels:
 * 1. The connected AP BSSID.
 * 2. A fixed CSI sender peer.
 * 3. A second fixed peer used as an extra reference channel.
 */
static const uint8_t CONFIG_CSI_SEND_MAC[] = {0x1a, 0x00, 0x00, 0x00, 0x00, 0x00};
static const uint8_t CONFIG_CSI_SEND_MAC_2[] = {0x1a, 0x00, 0x00, 0x00, 0x00, 0x01};

static void wifi_event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data)
{
    (void)arg;
    (void)event_data;

    if (event_base == WIFI_EVENT)
    {
        if (event_id == WIFI_EVENT_STA_CONNECTED)
        {
            led_set_wifi_connected(true);
        }
        else if (event_id == WIFI_EVENT_STA_DISCONNECTED)
        {
            led_set_wifi_connected(false);
        }
    }
    else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP)
    {
        led_set_wifi_connected(true);
    }
}

static void espnow_recv_cb(const esp_now_recv_info_t *recv_info, const uint8_t *data, int data_len)
{
    if (!recv_info || !data || data_len <= 0)
    {
        return;
    }

    if (memcmp(recv_info->src_addr, CONFIG_CSI_SEND_MAC_2, 6) != 0)
    {
        /* Keep the console focused on the dedicated CSI sender. */
        return;
    }

    if (data_len >= (int)sizeof(uint32_t))
    {
        uint32_t cnt = 0;
        memcpy(&cnt, data, sizeof(cnt));
        ESP_LOGD(TAG, "ESPNOW RX <- " MACSTR " len=%d cnt=%" PRIu32, MAC2STR(recv_info->src_addr), data_len, cnt);
    }
    else
    {
        ESP_LOGD(TAG, "ESPNOW RX <- " MACSTR " len=%d", MAC2STR(recv_info->src_addr), data_len);
    }
}

static void espnow_rx_init(void)
{
    esp_err_t err = esp_now_init();
    if (err == ESP_ERR_ESPNOW_EXIST)
    {
        err = ESP_OK;
    }
    ESP_ERROR_CHECK(err);

    /*
     * The current sender keeps ESPNOW encryption disabled.
     * Setting a PMK here is optional and harmless for the receiver path.
     */
    ESP_ERROR_CHECK(esp_now_set_pmk((uint8_t *)"pmk1234567890123"));
    ESP_ERROR_CHECK(esp_now_register_recv_cb(espnow_recv_cb));

    /*
     * Adding a broadcast peer is not strictly required for RX, but it makes the
     * setup more tolerant across targets and console workflows.
     */
    if (!esp_now_is_peer_exist((const uint8_t[6]){
            0xff, 0xff, 0xff, 0xff, 0xff, 0xff}))
    {
        esp_now_peer_info_t peer = {0};
        memcpy(peer.peer_addr, (const uint8_t[6]){0xff, 0xff, 0xff, 0xff, 0xff, 0xff}, 6);
        peer.channel = 0; /* Follow the current STA channel from the connected AP. */
        peer.encrypt = false;
        peer.ifidx = WIFI_IF_STA;
        ESP_ERROR_CHECK(esp_now_add_peer(&peer));
    }
}

static const char *channel_name(const uint8_t mac[6])
{
    if (memcmp(mac, s_mac_ap, 6) == 0)
    {
        return "AP";
    }
    if (memcmp(mac, CONFIG_CSI_SEND_MAC, 6) == 0)
    {
        return "MAC_1";
    }
    if (memcmp(mac, CONFIG_CSI_SEND_MAC_2, 6) == 0)
    {
        return "MAC_2";
    }
    return "?";
}

static void on_motion_event(esp_wifi_sensing_fsm_handle_t handle,
                            const uint8_t peer_mac[6],
                            esp_wifi_sensing_fsm_event_t event,
                            uint32_t data,
                            void *usr_data)
{
    (void)handle;
    (void)usr_data;

    const char *name = channel_name(peer_mac);
    bool is_ap_channel = (memcmp(peer_mac, s_mac_ap, 6) == 0);

    if (event == ESP_WIFI_SENSING_FSM_EVENT_ACTIVE)
    {

        if (is_ap_channel)
        {
            led_notify_ap_active();
            wifi_web_server_set_motion(true);
        }

        ESP_LOGI(TAG, "[%s] ACTIVE peer=" MACSTR " data=%" PRIu32,
                 name, MAC2STR(peer_mac), data);
    }
    else if (event == ESP_WIFI_SENSING_FSM_EVENT_INACTIVE)
    {

        if (is_ap_channel)
        {
            led_notify_ap_inactive();
            wifi_web_server_set_motion(false);
        }

        ESP_LOGI(TAG, "[%s] INACTIVE peer=" MACSTR,
                 name, MAC2STR(peer_mac));
    }
}

static void demo_init(void)
{
    /*
     * Follow the public component workflow:
     * 1. Resolve the peers used by this demo.
     * 2. Create one FSM handle.
     * 3. Add one channel per peer.
     * 4. Register ACTIVE / INACTIVE callbacks.
     * 5. Start the FSM and optionally enable router-ping-assisted sampling.
     */
    wifi_ap_record_t ap_info = {0};
    ESP_ERROR_CHECK(esp_wifi_sta_get_ap_info(&ap_info));
    memcpy(s_mac_ap, ap_info.bssid, 6);
    ESP_LOGI(TAG, "peer mac (AP BSSID): " MACSTR, MAC2STR(ap_info.bssid));
    ESP_LOGI(TAG, "peer mac 1 (CONFIG_CSI_SEND_MAC): " MACSTR, MAC2STR(CONFIG_CSI_SEND_MAC));
    ESP_LOGI(TAG, "peer mac 2 (CONFIG_CSI_SEND_MAC_2): " MACSTR, MAC2STR(CONFIG_CSI_SEND_MAC_2));

    esp_wifi_sensing_fsm_config_t cfg = DEFAULT_ESP_WIFI_SENSING_FSM_CONFIG();
    cfg.max_channel_num = 3;

    /* More responsive motion detection for visualization/demo use. */
    cfg.default_channel_config.sensitivity = 0.75f;

    ESP_ERROR_CHECK(esp_wifi_sensing_fsm_create(&cfg, &s_hms));

    /* The demo monitors the connected AP plus two fixed reference peers. */
    ESP_ERROR_CHECK(esp_wifi_sensing_fsm_add_channel(s_hms, ap_info.bssid));
    ESP_ERROR_CHECK(esp_wifi_sensing_fsm_add_channel(s_hms, CONFIG_CSI_SEND_MAC));
    ESP_ERROR_CHECK(esp_wifi_sensing_fsm_add_channel(s_hms, CONFIG_CSI_SEND_MAC_2));

    /* Register the same callback for both state transitions to keep logging symmetric. */
    ESP_ERROR_CHECK(esp_wifi_sensing_fsm_register_event_cb(s_hms, ESP_WIFI_SENSING_FSM_EVENT_ACTIVE, on_motion_event, NULL));
    ESP_ERROR_CHECK(esp_wifi_sensing_fsm_register_event_cb(s_hms, ESP_WIFI_SENSING_FSM_EVENT_INACTIVE, on_motion_event, NULL));
    ESP_ERROR_CHECK(esp_wifi_sensing_fsm_control(s_hms, ESP_WIFI_SENSING_FSM_CTRL_START, NULL));

    ESP_LOGI(TAG,
             "default config: motion_detection_sensitivity=%.3f active_jitter_min=%.3f hold=%" PRIu32 "ms confirm=%d ping=%" PRIu32 "Hz",
             cfg.default_channel_config.sensitivity,
             cfg.default_channel_config.active_jitter_min,
             cfg.default_channel_config.active_filter_ms,
             CONFIG_ESP_WIFI_SENSING_CONFIRM_COUNT,
             cfg.ping_frequency_hz);

    esp_err_t ping_err = esp_wifi_sensing_fsm_ping_router_start(s_hms);
    if (ping_err == ESP_OK)
    {
        ESP_LOGI(TAG, "router ping started");
    }
    else if (ping_err == ESP_ERR_INVALID_STATE)
    {
        ESP_LOGW(TAG, "router ping start failed: Wi-Fi STA not connected or no gateway");
    }
    else
    {
        ESP_LOGE(TAG, "router ping start failed: %s", esp_err_to_name(ping_err));
    }
}

void app_main(void)
{
    /* Standard ESP-IDF bring-up followed by Wi-Fi connection and sensing start. */
    ESP_ERROR_CHECK(nvs_flash_init());
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    ESP_ERROR_CHECK(led_init());
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, WIFI_EVENT_STA_CONNECTED, wifi_event_handler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, WIFI_EVENT_STA_DISCONNECTED, wifi_event_handler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, wifi_event_handler, NULL));
    led_set_wifi_connected(false);
    ESP_ERROR_CHECK(drwalls_wifi_start());

/* Advertise DrWalls on the local network as drwalls.local */
ESP_ERROR_CHECK(mdns_init());
ESP_ERROR_CHECK(mdns_hostname_set("drwalls"));
ESP_ERROR_CHECK(mdns_instance_name_set("DrWalls CSI Radar"));
ESP_ERROR_CHECK(mdns_service_add(
    "DrWalls CSI Radar",
    "_http",
    "_tcp",
    80,
    NULL,
    0
));

ESP_LOGI(TAG, "DrWalls available at http://drwalls.local");

/* Disable Wi-Fi power save to keep CSI sampling cadence stable. */
ESP_ERROR_CHECK(esp_wifi_set_ps(WIFI_PS_NONE));
    espnow_rx_init();
    demo_init();
    /* Start the local Wi-Fi web interface after sensing is ready. */
    ESP_ERROR_CHECK(wifi_web_server_start(s_hms, s_mac_ap));

#if CONFIG_ESP_WIFI_SENSING_WEB_SERIAL_ENABLE
    /* Web UI also maps esp-radar train (TRAIN_START/STOP/REMOVE) and streams wander / train thresholds. */
    /* Export the same three demo channels to the browser monitor UI. */
    web_serial_monitor_peer_t serial_peers[] = {
        {.name = "AP"},
        {.name = "MAC_1"},
        {.name = "MAC_2"},
    };
    memcpy(serial_peers[0].mac, s_mac_ap, sizeof(s_mac_ap));
    memcpy(serial_peers[1].mac, CONFIG_CSI_SEND_MAC, sizeof(serial_peers[1].mac));
    memcpy(serial_peers[2].mac, CONFIG_CSI_SEND_MAC_2, sizeof(serial_peers[2].mac));
    web_serial_monitor_config_t serial_cfg = {
        .fsm = s_hms,
        .peers = serial_peers,
        .peer_num = sizeof(serial_peers) / sizeof(serial_peers[0]),
        .stream_period_ms = CONFIG_ESP_WIFI_SENSING_WEB_SERIAL_STREAM_PERIOD_MS,
    };
    ESP_ERROR_CHECK(web_serial_monitor_init(&serial_cfg));
#endif

    while (1)
    {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
