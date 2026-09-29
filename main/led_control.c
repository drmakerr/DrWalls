/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "led_control.h"

#include <stdint.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/ledc.h"
#include "esp_check.h"
#include "esp_log.h"
#include "sdkconfig.h"

#if CONFIG_ESP_WIFI_SENSING_DEMO_LED_TYPE_WS2812
#include "led_strip.h"
#endif

static const char *TAG = "demo_led";

#define LED_UPDATE_PERIOD_MS      20
#define LED_BLINK_PERIOD_MS       500
#define LED_BLINK_DUTY_PERCENT    50
#define LED_FADE_OUT_TIME_MS      1000

typedef enum {
    LED_MOTION_IDLE = 0,
    LED_MOTION_ACTIVE,
    LED_MOTION_FADING,
} led_motion_state_t;

typedef struct {
    bool initialized;
    bool wifi_connected;
    led_motion_state_t motion_state;
    TickType_t fade_start_tick;
    TaskHandle_t task_handle;
#if CONFIG_ESP_WIFI_SENSING_DEMO_LED_TYPE_WS2812
    led_strip_handle_t strip_handle;
    uint8_t last_r;
    uint8_t last_g;
    uint8_t last_b;
#else
    uint32_t last_duty;
#endif
} led_ctx_t;

static led_ctx_t s_led_ctx = {0};
static portMUX_TYPE s_led_lock = portMUX_INITIALIZER_UNLOCKED;

#if CONFIG_ESP_WIFI_SENSING_DEMO_LED_TYPE_GPIO
static const ledc_mode_t s_ledc_mode = LEDC_LOW_SPEED_MODE;
static const ledc_timer_t s_ledc_timer = LEDC_TIMER_0;
static const ledc_channel_t s_ledc_channel = LEDC_CHANNEL_0;
static const uint32_t s_ledc_max_duty = (1U << LEDC_TIMER_8_BIT) - 1U;
#endif

static void led_task(void *arg);
static void led_apply_state(TickType_t now);

static void led_trigger_update(void)
{
    if (s_led_ctx.task_handle != NULL) {
        xTaskNotifyGive(s_led_ctx.task_handle);
    }
}

#if CONFIG_ESP_WIFI_SENSING_DEMO_LED_TYPE_GPIO
static void led_apply_gpio_brightness(float brightness)
{
    if (brightness < 0.0f) {
        brightness = 0.0f;
    } else if (brightness > 1.0f) {
        brightness = 1.0f;
    }

    uint32_t duty = (uint32_t)(brightness * s_ledc_max_duty + 0.5f);
    if (duty == s_led_ctx.last_duty) {
        return;
    }

    ESP_ERROR_CHECK(ledc_set_duty(s_ledc_mode, s_ledc_channel, duty));
    ESP_ERROR_CHECK(ledc_update_duty(s_ledc_mode, s_ledc_channel));
    s_led_ctx.last_duty = duty;
}
#else
static void led_apply_ws2812_color(uint8_t red, uint8_t green, uint8_t blue)
{
    if (red == s_led_ctx.last_r && green == s_led_ctx.last_g && blue == s_led_ctx.last_b) {
        return;
    }

    ESP_ERROR_CHECK(led_strip_set_pixel(s_led_ctx.strip_handle, 0, red, green, blue));
    ESP_ERROR_CHECK(led_strip_refresh(s_led_ctx.strip_handle));
    s_led_ctx.last_r = red;
    s_led_ctx.last_g = green;
    s_led_ctx.last_b = blue;
}
#endif

static void led_apply_state(TickType_t now)
{
    bool wifi_connected;
    led_motion_state_t motion_state;
    TickType_t fade_start_tick;

    portENTER_CRITICAL(&s_led_lock);
    wifi_connected = s_led_ctx.wifi_connected;
    motion_state = s_led_ctx.motion_state;
    fade_start_tick = s_led_ctx.fade_start_tick;
    portEXIT_CRITICAL(&s_led_lock);

    if (!wifi_connected) {
        /* Wi-Fi not ready yet: blink to signal the demo is still connecting. */
        uint32_t phase_ms = (uint32_t)(now * portTICK_PERIOD_MS) % LED_BLINK_PERIOD_MS;
        bool on_phase = phase_ms < (LED_BLINK_PERIOD_MS / 2);
#if CONFIG_ESP_WIFI_SENSING_DEMO_LED_TYPE_GPIO
        led_apply_gpio_brightness(on_phase ? (LED_BLINK_DUTY_PERCENT / 100.0f) : 0.0f);
#else
        uint8_t red = (uint8_t)((255U * LED_BLINK_DUTY_PERCENT) / 100U);
        led_apply_ws2812_color(on_phase ? red : 0, 0, 0);
#endif
        return;
    }

    if (motion_state == LED_MOTION_ACTIVE) {
        /* Motion on the AP channel is mapped to a fully lit "active" state. */
#if CONFIG_ESP_WIFI_SENSING_DEMO_LED_TYPE_GPIO
        led_apply_gpio_brightness(1.0f);
#else
        led_apply_ws2812_color(0, 255, 0);
#endif
        return;
    }

    if (motion_state == LED_MOTION_FADING) {
        /* Fade out after motion stops so short inactive gaps remain readable. */
        uint32_t elapsed_ms = (uint32_t)((now - fade_start_tick) * portTICK_PERIOD_MS);
        if (elapsed_ms >= LED_FADE_OUT_TIME_MS) {
            portENTER_CRITICAL(&s_led_lock);
            if (s_led_ctx.motion_state == LED_MOTION_FADING) {
                s_led_ctx.motion_state = LED_MOTION_IDLE;
            }
            portEXIT_CRITICAL(&s_led_lock);
#if CONFIG_ESP_WIFI_SENSING_DEMO_LED_TYPE_GPIO
            led_apply_gpio_brightness(0.0f);
#else
            led_apply_ws2812_color(0, 0, 0);
#endif
            return;
        }

        float brightness = 1.0f - ((float)elapsed_ms / (float)LED_FADE_OUT_TIME_MS);
#if CONFIG_ESP_WIFI_SENSING_DEMO_LED_TYPE_GPIO
        led_apply_gpio_brightness(brightness);
#else
        uint8_t green = (uint8_t)(255.0f * brightness + 0.5f);
        led_apply_ws2812_color(0, green, 0);
#endif
        return;
    }

#if CONFIG_ESP_WIFI_SENSING_DEMO_LED_TYPE_GPIO
    led_apply_gpio_brightness(0.0f);
#else
    led_apply_ws2812_color(0, 0, 0);
#endif
}

static void led_task(void *arg)
{
    (void)arg;

    while (true) {
        led_apply_state(xTaskGetTickCount());
        (void)ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(LED_UPDATE_PERIOD_MS));
    }
}

esp_err_t led_init(void)
{
    if (s_led_ctx.initialized) {
        return ESP_OK;
    }

#if CONFIG_ESP_WIFI_SENSING_DEMO_LED_TYPE_GPIO
    ledc_timer_config_t timer_config = {
        .speed_mode = s_ledc_mode,
        .duty_resolution = LEDC_TIMER_8_BIT,
        .timer_num = s_ledc_timer,
        .freq_hz = 5000,
        .clk_cfg = LEDC_AUTO_CLK,
    };
    ESP_RETURN_ON_ERROR(ledc_timer_config(&timer_config), TAG, "configure LEDC timer failed");

    ledc_channel_config_t channel_config = {
        .gpio_num = CONFIG_ESP_WIFI_SENSING_DEMO_LED_GPIO,
        .speed_mode = s_ledc_mode,
        .channel = s_ledc_channel,
        .intr_type = LEDC_INTR_DISABLE,
        .timer_sel = s_ledc_timer,
        .duty = 0,
        .hpoint = 0,
        .flags.output_invert = 0,
    };
    ESP_RETURN_ON_ERROR(ledc_channel_config(&channel_config), TAG, "configure LEDC channel failed");
#else
    led_strip_config_t strip_config = {
        .strip_gpio_num = CONFIG_ESP_WIFI_SENSING_DEMO_LED_GPIO,
        .max_leds = 1,
    };
#if CONFIG_ESP_WIFI_SENSING_DEMO_WS2812_BACKEND_RMT
    led_strip_rmt_config_t rmt_config = {
        .resolution_hz = 10 * 1000 * 1000,
        .flags.with_dma = false,
    };
    ESP_RETURN_ON_ERROR(led_strip_new_rmt_device(&strip_config, &rmt_config, &s_led_ctx.strip_handle),
                        TAG, "create RMT strip failed");
#else
    led_strip_spi_config_t spi_config = {
        .spi_bus = SPI2_HOST,
        .flags.with_dma = true,
    };
    ESP_RETURN_ON_ERROR(led_strip_new_spi_device(&strip_config, &spi_config, &s_led_ctx.strip_handle),
                        TAG, "create SPI strip failed");
#endif
    ESP_RETURN_ON_ERROR(led_strip_clear(s_led_ctx.strip_handle), TAG, "clear strip failed");
#endif

    BaseType_t task_created = xTaskCreate(led_task, "demo_led", 3072, NULL, 4, &s_led_ctx.task_handle);
    ESP_RETURN_ON_FALSE(task_created == pdPASS, ESP_ERR_NO_MEM, TAG, "create LED task failed");

    s_led_ctx.initialized = true;
    s_led_ctx.wifi_connected = false;
    s_led_ctx.motion_state = LED_MOTION_IDLE;
    ESP_LOGI(TAG, "status LED initialized on GPIO %d", CONFIG_ESP_WIFI_SENSING_DEMO_LED_GPIO);
    led_trigger_update();
    return ESP_OK;
}

void led_set_wifi_connected(bool connected)
{
    if (!s_led_ctx.initialized) {
        return;
    }

    portENTER_CRITICAL(&s_led_lock);
    s_led_ctx.wifi_connected = connected;
    if (!connected) {
        s_led_ctx.motion_state = LED_MOTION_IDLE;
    }
    portEXIT_CRITICAL(&s_led_lock);
    led_trigger_update();
}

void led_notify_ap_active(void)
{
    if (!s_led_ctx.initialized) {
        return;
    }

    portENTER_CRITICAL(&s_led_lock);
    if (s_led_ctx.wifi_connected) {
        s_led_ctx.motion_state = LED_MOTION_ACTIVE;
    }
    portEXIT_CRITICAL(&s_led_lock);
    led_trigger_update();
}

void led_notify_ap_inactive(void)
{
    if (!s_led_ctx.initialized) {
        return;
    }

    portENTER_CRITICAL(&s_led_lock);
    if (s_led_ctx.wifi_connected) {
        s_led_ctx.motion_state = LED_MOTION_FADING;
        s_led_ctx.fade_start_tick = xTaskGetTickCount();
    }
    portEXIT_CRITICAL(&s_led_lock);
    led_trigger_update();
}
