#include "drwalls_wifi.h"

#include <stdio.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"

#include "esp_log.h"
#include "esp_wifi.h"
#include "esp_netif.h"
#include "esp_event.h"
#include "esp_http_server.h"
#include "esp_system.h"

#include "nvs.h"
#include "nvs_flash.h"
#include "dns_server.h"

static const char *TAG = "drwalls_wifi";

#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT      BIT1

#define DRWALLS_NAMESPACE "drwalls_wifi"
#define DRWALLS_AP_SSID    "DrWalls-Setup"

static EventGroupHandle_t s_wifi_event_group;
static int s_retry_num = 0;
static const int MAX_RETRIES = 10;

/* -------------------------------------------------------------
 * NVS
 * ------------------------------------------------------------- */

static esp_err_t load_credentials(char *ssid,
                                  size_t ssid_size,
                                  char *password,
                                  size_t password_size)
{
    nvs_handle_t nvs;

    esp_err_t err = nvs_open(DRWALLS_NAMESPACE, NVS_READONLY, &nvs);

    if (err != ESP_OK) {
        return err;
    }

    size_t required_ssid = ssid_size;
    size_t required_password = password_size;

    err = nvs_get_str(nvs, "ssid", ssid, &required_ssid);

    if (err == ESP_OK) {
        err = nvs_get_str(nvs, "password", password, &required_password);
    }

    nvs_close(nvs);

    return err;
}

static esp_err_t save_credentials(const char *ssid,
                                  const char *password)
{
    nvs_handle_t nvs;

    esp_err_t err = nvs_open(DRWALLS_NAMESPACE, NVS_READWRITE, &nvs);

    if (err != ESP_OK) {
        return err;
    }

    err = nvs_set_str(nvs, "ssid", ssid);

    if (err == ESP_OK) {
        err = nvs_set_str(nvs, "password", password);
    }

    if (err == ESP_OK) {
        err = nvs_commit(nvs);
    }

    nvs_close(nvs);

    return err;
}

/* -------------------------------------------------------------
 * URL decoding
 * ------------------------------------------------------------- */

static int hex_to_int(char c)
{
    if (c >= '0' && c <= '9') {
        return c - '0';
    }

    if (c >= 'A' && c <= 'F') {
        return c - 'A' + 10;
    }

    if (c >= 'a' && c <= 'f') {
        return c - 'a' + 10;
    }

    return 0;
}

static void url_decode(char *dst, const char *src, size_t dst_size)
{
    size_t i = 0;

    while (*src && i < dst_size - 1) {

        if (*src == '%' && src[1] && src[2]) {

            dst[i++] =
                (char)((hex_to_int(src[1]) << 4) |
                       hex_to_int(src[2]));

            src += 3;

        } else if (*src == '+') {

            dst[i++] = ' ';
            src++;

        } else {

            dst[i++] = *src++;
        }
    }

    dst[i] = '\0';
}

/* -------------------------------------------------------------
 * Wi-Fi event handler
 * ------------------------------------------------------------- */

static void wifi_event_handler(void *arg,
                               esp_event_base_t event_base,
                               int32_t event_id,
                               void *event_data)
{
    if (event_base == WIFI_EVENT &&
        event_id == WIFI_EVENT_STA_START) {

        esp_wifi_connect();

    } else if (event_base == WIFI_EVENT &&
               event_id == WIFI_EVENT_STA_DISCONNECTED) {

        if (s_retry_num < MAX_RETRIES) {

            esp_wifi_connect();
            s_retry_num++;

            ESP_LOGI(TAG,
                     "Retrying Wi-Fi connection (%d/%d)",
                     s_retry_num,
                     MAX_RETRIES);

        } else {

            xEventGroupSetBits(
                s_wifi_event_group,
                WIFI_FAIL_BIT
            );
        }

    } else if (event_base == IP_EVENT &&
               event_id == IP_EVENT_STA_GOT_IP) {

        ip_event_got_ip_t *event =
            (ip_event_got_ip_t *)event_data;

        ESP_LOGI(TAG,
                 "Connected. IP: " IPSTR,
                 IP2STR(&event->ip_info.ip));

        s_retry_num = 0;

        xEventGroupSetBits(
            s_wifi_event_group,
            WIFI_CONNECTED_BIT
        );
    }
}

/* -------------------------------------------------------------
 * Connect using stored credentials
 * ------------------------------------------------------------- */

static esp_err_t connect_to_wifi(const char *ssid,
                                 const char *password)
{
    s_wifi_event_group = xEventGroupCreate();

    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();

    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    ESP_ERROR_CHECK(
        esp_event_handler_register(
            WIFI_EVENT,
            ESP_EVENT_ANY_ID,
            wifi_event_handler,
            NULL
        )
    );

    ESP_ERROR_CHECK(
        esp_event_handler_register(
            IP_EVENT,
            IP_EVENT_STA_GOT_IP,
            wifi_event_handler,
            NULL
        )
    );

    wifi_config_t wifi_config = {0};

    strncpy(
        (char *)wifi_config.sta.ssid,
        ssid,
        sizeof(wifi_config.sta.ssid) - 1
    );

    strncpy(
        (char *)wifi_config.sta.password,
        password,
        sizeof(wifi_config.sta.password) - 1
    );

    wifi_config.sta.threshold.authmode =
        WIFI_AUTH_WPA2_PSK;

    ESP_ERROR_CHECK(
        esp_wifi_set_mode(WIFI_MODE_STA)
    );

    ESP_ERROR_CHECK(
        esp_wifi_set_config(
            WIFI_IF_STA,
            &wifi_config
        )
    );

    ESP_ERROR_CHECK(
        esp_wifi_start()
    );

    ESP_LOGI(TAG,
             "Connecting to saved Wi-Fi: %s",
             ssid);

    EventBits_t bits =
        xEventGroupWaitBits(
            s_wifi_event_group,
            WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
            pdFALSE,
            pdFALSE,
            portMAX_DELAY
        );

    if (bits & WIFI_CONNECTED_BIT) {
        return ESP_OK;
    }

    ESP_LOGE(TAG,
             "Unable to connect to saved Wi-Fi");

    return ESP_FAIL;
}

/* -------------------------------------------------------------
 * Setup webpage
 * ------------------------------------------------------------- */

static esp_err_t setup_page_handler(httpd_req_t *req)
{
    const char *html =
        "<!DOCTYPE html>"
        "<html>"
        "<head>"

        "<meta name='viewport' "
        "content='width=device-width,initial-scale=1'>"

        "<title>DrWalls Setup</title>"

        "<style>"

        "body{"
            "font-family:Arial;"
            "background:#111;"
            "color:white;"
            "text-align:center;"
            "padding:30px;"
        "}"

        "h1{"
            "color:orange;"
        "}"

        "input{"
            "width:90%;"
            "max-width:350px;"
            "padding:14px;"
            "margin:8px;"
            "font-size:16px;"
            "border-radius:8px;"
            "border:0;"
        "}"

        "button{"
            "padding:14px 35px;"
            "margin-top:15px;"
            "font-size:16px;"
            "background:orange;"
            "border:0;"
            "border-radius:8px;"
            "font-weight:bold;"
        "}"

        ".footer{"
            "font-size:12px;"
            "color:#888;"
            "margin-top:40px;"
        "}"

        "</style>"

        "</head>"

        "<body>"

        "<h1>DrWalls</h1>"

        "<h2>Wi-Fi Setup</h2>"

        "<p>Connect DrWalls to your Wi-Fi network.</p>"

        "<form method='POST' action='/save'>"

        "<input "
            "name='ssid' "
            "placeholder='Wi-Fi Name' "
            "required>"

        "<br>"

        "<input "
            "name='password' "
            "type='password' "
            "placeholder='Wi-Fi Password'>"

        "<br>"

        "<button type='submit'>"
            "CONNECT"
        "</button>"

        "</form>"

        "<p class='footer'>"
            "Made with Love &lt;3 for all makers"
        "</p>"

        "</body>"
        "</html>";

    httpd_resp_set_type(req, "text/html");

    return httpd_resp_send(
        req,
        html,
        HTTPD_RESP_USE_STRLEN
    );
}

/* -------------------------------------------------------------
 * Save submitted credentials
 * ------------------------------------------------------------- */

static esp_err_t save_handler(httpd_req_t *req)
{
    char content[256] = {0};

    int length = req->content_len;

    if (length <= 0 ||
        length >= sizeof(content)) {

        httpd_resp_send_err(
            req,
            HTTPD_400_BAD_REQUEST,
            "Invalid request"
        );

        return ESP_FAIL;
    }

    int received =
        httpd_req_recv(
            req,
            content,
            length
        );

    if (received <= 0) {
        return ESP_FAIL;
    }

    content[received] = '\0';

    char ssid_encoded[96] = {0};
    char password_encoded[128] = {0};

    char ssid[33] = {0};
    char password[65] = {0};

    if (httpd_query_key_value(
            content,
            "ssid",
            ssid_encoded,
            sizeof(ssid_encoded)) != ESP_OK) {

        httpd_resp_send_err(
            req,
            HTTPD_400_BAD_REQUEST,
            "SSID missing"
        );

        return ESP_FAIL;
    }

    httpd_query_key_value(
        content,
        "password",
        password_encoded,
        sizeof(password_encoded)
    );

    url_decode(
        ssid,
        ssid_encoded,
        sizeof(ssid)
    );

    url_decode(
        password,
        password_encoded,
        sizeof(password)
    );

    if (strlen(ssid) == 0) {

        httpd_resp_send_err(
            req,
            HTTPD_400_BAD_REQUEST,
            "SSID cannot be empty"
        );

        return ESP_FAIL;
    }

    ESP_LOGI(TAG,
             "Saving Wi-Fi configuration for: %s",
             ssid);

    esp_err_t err =
        save_credentials(
            ssid,
            password
        );

    if (err != ESP_OK) {

        ESP_LOGE(TAG,
                 "Failed to save credentials: %s",
                 esp_err_to_name(err));

        httpd_resp_send_err(
            req,
            HTTPD_500_INTERNAL_SERVER_ERROR,
            "Unable to save Wi-Fi"
        );

        return err;
    }

    const char *response =
        "<!DOCTYPE html>"
        "<html>"
        "<head>"
        "<meta name='viewport' "
        "content='width=device-width,initial-scale=1'>"
        "</head>"

        "<body style='"
            "font-family:Arial;"
            "background:#111;"
            "color:white;"
            "text-align:center;"
            "padding:40px;'>"

        "<h1 style='color:orange;'>"
            "DrWalls"
        "</h1>"

        "<h2>Wi-Fi Saved!</h2>"

        "<p>"
            "DrWalls is restarting..."
        "</p>"

        "</body>"
        "</html>";

    httpd_resp_set_type(
        req,
        "text/html"
    );

    httpd_resp_send(
        req,
        response,
        HTTPD_RESP_USE_STRLEN
    );

    vTaskDelay(
        pdMS_TO_TICKS(1500)
    );

    esp_restart();

    return ESP_OK;
}

/* -------------------------------------------------------------
 * Start setup hotspot
 * ------------------------------------------------------------- */

static esp_err_t start_setup_portal(void)
{
    ESP_LOGI(TAG,
             "Starting DrWalls setup portal");

    esp_netif_create_default_wifi_ap();

    wifi_init_config_t cfg =
        WIFI_INIT_CONFIG_DEFAULT();

    ESP_ERROR_CHECK(
        esp_wifi_init(&cfg)
    );

    wifi_config_t wifi_config = {
        .ap = {
            .ssid = DRWALLS_AP_SSID,
            .ssid_len = 0,
            .channel = 1,
            .password = "",
            .max_connection = 4,
            .authmode = WIFI_AUTH_OPEN
        }
    };

    ESP_ERROR_CHECK(
        esp_wifi_set_mode(WIFI_MODE_AP)
    );

    ESP_ERROR_CHECK(
        esp_wifi_set_config(
            WIFI_IF_AP,
            &wifi_config
        )
    );

    ESP_ERROR_CHECK(
        esp_wifi_start()
    );



/* Start captive portal DNS.
 * All domain requests while connected to DrWalls-Setup
 * will resolve to the ESP32 setup page.
 */
dns_server_config_t dns_config = DNS_SERVER_CONFIG_SINGLE("*", "WIFI_AP_DEF");

dns_server_handle_t dns_server = start_dns_server(&dns_config);

if (dns_server == NULL) {
    ESP_LOGE(TAG, "Failed to start captive portal DNS");
    return ESP_FAIL;
}
ESP_LOGI(TAG,
         "Captive portal DNS started");



    ESP_LOGI(TAG,
             "================================");

    ESP_LOGI(TAG,
             "Wi-Fi setup network: %s",
             DRWALLS_AP_SSID);

    ESP_LOGI(TAG,
             "Open: http://192.168.4.1");

    ESP_LOGI(TAG,
             "================================");

    httpd_config_t server_config =
        HTTPD_DEFAULT_CONFIG();
	server_config.uri_match_fn = httpd_uri_match_wildcard;

    httpd_handle_t server = NULL;

    ESP_ERROR_CHECK(
        httpd_start(
            &server,
            &server_config
        )
    );

    static const httpd_uri_t root = {
        .uri = "/",
        .method = HTTP_GET,
        .handler = setup_page_handler,
        .user_ctx = NULL
    };

    static const httpd_uri_t save = {
        .uri = "/save",
        .method = HTTP_POST,
        .handler = save_handler,
        .user_ctx = NULL
    };
static const httpd_uri_t captive = {
    .uri = "/*",
    .method = HTTP_GET,
    .handler = setup_page_handler,
    .user_ctx = NULL
};

    ESP_ERROR_CHECK(
        httpd_register_uri_handler(
            server,
            &root
        )
    );
ESP_ERROR_CHECK(
    httpd_register_uri_handler(
        server,
        &captive
    )
);

    ESP_ERROR_CHECK(
        httpd_register_uri_handler(
            server,
            &save
        )
    );

    /*
     * Stay here while the setup portal is active.
     * Saving credentials causes esp_restart().
     */
    while (1) {
        vTaskDelay(
            pdMS_TO_TICKS(1000)
        );
    }

    return ESP_OK;
}

/* -------------------------------------------------------------
 * Public API
 * ------------------------------------------------------------- */

esp_err_t drwalls_wifi_start(void)
{
    char ssid[33] = {0};
    char password[65] = {0};

    ESP_LOGI(TAG,
             "Starting DrWalls Wi-Fi manager");

    esp_err_t err =
        load_credentials(
            ssid,
            sizeof(ssid),
            password,
            sizeof(password)
        );

    if (err == ESP_OK &&
        strlen(ssid) > 0) {

        ESP_LOGI(TAG,
                 "Saved Wi-Fi configuration found");

        err = connect_to_wifi(
            ssid,
            password
        );

        if (err == ESP_OK) {
            return ESP_OK;
        }

        ESP_LOGW(TAG,
                 "Saved Wi-Fi could not be reached");
    }
    else {

        ESP_LOGI(TAG,
                 "No saved Wi-Fi configuration");
    }

    return start_setup_portal();
}

esp_err_t drwalls_wifi_reset(void)
{
    nvs_handle_t nvs;

    esp_err_t err =
        nvs_open(
            DRWALLS_NAMESPACE,
            NVS_READWRITE,
            &nvs
        );

    if (err != ESP_OK) {
        return err;
    }

    err = nvs_erase_all(nvs);

    if (err == ESP_OK) {
        err = nvs_commit(nvs);
    }

    nvs_close(nvs);

    ESP_LOGI(TAG,
             "Saved Wi-Fi configuration erased");

    return err;
}