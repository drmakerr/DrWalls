/*
 * Wi-Fi Sensing Web UI
 *
 * Based on Espressif ESP-CSI wifi_sensing_demo.
 * Adds a local HTTP interface for viewing sensing results
 * from phones and computers on the same network.
 */

#include <stdio.h>
#include <string.h>
#include "esp_timer.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_http_server.h"

#include "wifi_web_server.h"

#include "drwalls_wifi.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "wifi_web";

static esp_wifi_sensing_fsm_handle_t s_fsm = NULL;
static uint8_t s_ap_mac[6] = {0};
static volatile bool s_motion_active = false;
static volatile int64_t s_last_motion_us = 0;
#define MOTION_DISPLAY_HOLD_US (3LL * 1000000LL)

static esp_err_t root_handler(httpd_req_t *req)
{
    const char *html =
        "<!DOCTYPE html>"
        "<html>"
        "<head>"
        "<meta name='viewport' content='width=device-width,initial-scale=1'>"
        "<title>Dr. Maker CSI Radar</title>"

        "<style>"

        "body{"
        "font-family:Arial;"
        "background:#111;"
        "color:white;"
        "text-align:center;"
        "padding:40px;"
        "margin:0;"
        "box-sizing:border-box;"
        "min-height:100vh;"
        "transition:background-color 0.25s ease;"
        "}"

        "body.motion-active{"
        "background:#00c853;"
        "}"

        "h1{"
        "font-size:32px;"
        "color:orange;"
        "line-height:1.15;"
        "margin:20px 0 0 0;"
        "}"

        "#status{"
        "font-size:46px;"
        "font-weight:bold;"
        "margin-top:70px;"
        "color:#777;"
        "}"

        "body.motion-active #status{"
        "color:white;"
        "}"

        "#resetWifi{"
        "position:fixed;"
        "bottom:55px;"
        "left:50%;"
        "transform:translateX(-50%);"
        "background:transparent;"
        "color:#777;"
        "border:1px solid #555;"
        "border-radius:8px;"
        "padding:9px 16px;"
        "font-size:11px;"
        "cursor:pointer;"
        "}"

        "body.motion-active #resetWifi{"
        "color:white;"
        "border-color:white;"
        "}"
        ".maker-love{"
        "font-size:12px;"
        "color:#888;"
        "position:fixed;"
        "bottom:20px;"
        "left:0;"
        "width:100%;"
        "margin:0;"
        "}"

        "body.motion-active .maker-love{"
        "color:white;"
        "}"

        "</style>"
        "</head>"

        "<body>"

        "<h1>"
        "Dr. Maker<br>"
        "CSI Radar"
        "</h1>"

        "<div id='status'>NO MOTION</div>"

        "<button id='resetWifi' onclick='resetWifi()'>RESET WI-FI</button>"

        "<p class='maker-love'>Made with Love &lt;3 for all makers</p>"

        "<script>"

        "async function update(){"
        "try{"
        "const r=await fetch('/api/status');"
        "const d=await r.json();"
        "const s=document.getElementById('status');"

        "if(d.motion){"
        "s.textContent='MOTION DETECTED';"
        "document.body.classList.add('motion-active');"
        "}else{"
        "s.textContent='NO MOTION';"
        "document.body.classList.remove('motion-active');"
        "}"

        "}catch(e){}"
        "}"
        "async function resetWifi(){"
        "if(!confirm('Reset Wi-Fi settings? DrWalls will restart in setup mode.'))return;"
        "try{"
        "document.getElementById('resetWifi').textContent='RESETTING...';"
        "await fetch('/api/reset-wifi',{method:'POST'});"
        "document.body.classList.remove('motion-active');"
        "document.getElementById('status').textContent='WI-FI RESET';"
        "alert('Wi-Fi settings erased. Connect to DrWalls-Setup to configure a new network.');"
        "}catch(e){}"
        "}"
        "setInterval(update,250);"
        "update();"

        "</script>"

        "</body>"
        "</html>";

    httpd_resp_set_type(req, "text/html");
    return httpd_resp_send(req, html, HTTPD_RESP_USE_STRLEN);
}

static esp_err_t status_handler(httpd_req_t *req)
{
    char json[64];

    int64_t now = esp_timer_get_time();

    bool display_active =
        s_motion_active ||
        ((now - s_last_motion_us) < MOTION_DISPLAY_HOLD_US);

    snprintf(json, sizeof(json),
             "{\"motion\":%s}",
             display_active ? "true" : "false");

    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");

    return httpd_resp_send(req, json, HTTPD_RESP_USE_STRLEN);
}

void wifi_web_server_set_motion(bool active)
{
    s_motion_active = active;

    if (active)
    {
        s_last_motion_us = esp_timer_get_time();
    }
}

static esp_err_t reset_wifi_handler(httpd_req_t *req)
{
    ESP_LOGW(TAG, "Wi-Fi reset requested");

    esp_err_t err = drwalls_wifi_reset();

    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to erase Wi-Fi credentials: %s",
                 esp_err_to_name(err));

        httpd_resp_send_err(
            req,
            HTTPD_500_INTERNAL_SERVER_ERROR,
            "Failed to reset Wi-Fi");

        return err;
    }

    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, "{\"success\":true}");

    /*
     * Give the HTTP response time to reach the browser
     * before restarting the ESP32.
     */
    vTaskDelay(pdMS_TO_TICKS(1000));

    esp_restart();

    return ESP_OK;
}

esp_err_t wifi_web_server_start(
    esp_wifi_sensing_fsm_handle_t fsm,
    const uint8_t ap_mac[6])
{
    s_fsm = fsm;
    memcpy(s_ap_mac, ap_mac, 6);

    httpd_config_t config = HTTPD_DEFAULT_CONFIG();

    httpd_handle_t server = NULL;

    ESP_LOGI(TAG, "Starting HTTP server");

    esp_err_t err = httpd_start(&server, &config);

    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "HTTP server failed: %s", esp_err_to_name(err));
        return err;
    }

    static const httpd_uri_t root = {
        .uri = "/",
        .method = HTTP_GET,
        .handler = root_handler,
        .user_ctx = NULL};

    err = httpd_register_uri_handler(server, &root);

    static const httpd_uri_t status = {
        .uri = "/api/status",
        .method = HTTP_GET,
        .handler = status_handler,
        .user_ctx = NULL};

    err = httpd_register_uri_handler(server, &status);
    static const httpd_uri_t reset_wifi = {
        .uri = "/api/reset-wifi",
        .method = HTTP_POST,
        .handler = reset_wifi_handler,
        .user_ctx = NULL};

    err = httpd_register_uri_handler(server, &reset_wifi);

    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to register /api/reset-wifi");
        return err;
    }

    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to register /api/status");
        return err;
    }
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to register /");
        return err;
    }

    ESP_LOGI(TAG, "HTTP server started");

    return ESP_OK;
}