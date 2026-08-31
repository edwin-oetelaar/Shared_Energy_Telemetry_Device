#include "inc/status_led.h"

#include "led_strip.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "led_ring";

static bool status_leds_initialized = false;

static esp_err_t status_led_set(gpio_num_t gpio, bool on)
{
    if (!status_leds_initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    return gpio_set_level(gpio, on ? 1 : 0);
}

esp_err_t status_leds_init(void)
{
    gpio_config_t config = {
        .pin_bit_mask = (1ULL << STATUS_LED_WIFI_GPIO) |
                        (1ULL << STATUS_LED_POWER_GPIO) |
                        (1ULL << STATUS_LED_DATA_GPIO),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };

    esp_err_t err = gpio_config(&config);
    if (err != ESP_OK) {
        return err;
    }

    status_leds_initialized = true;

    err = status_led_set_wifi(false);
    if (err != ESP_OK) {
        return err;
    }

    err = status_led_set_data(false);
    if (err != ESP_OK) {
        return err;
    }

    return status_led_set_power(true);
}

esp_err_t status_led_set_wifi(bool on)
{
    return status_led_set(STATUS_LED_WIFI_GPIO, on);
}

esp_err_t status_led_set_power(bool on)
{
    return status_led_set(STATUS_LED_POWER_GPIO, on);
}

esp_err_t status_led_set_data(bool on)
{
    return status_led_set(STATUS_LED_DATA_GPIO, on);
}

static uint8_t apply_brightness(uint8_t value, uint8_t brightness)
{
    return ((uint16_t)value * brightness) / 255;
}

esp_err_t led_ring_init(led_ring_t *ring, int gpio_num)
{
    if (ring == NULL) return ESP_ERR_INVALID_ARG;

    ring->strip = NULL;
    ring->brightness = 255;

    led_strip_config_t strip_config = {
        .strip_gpio_num = gpio_num,
        .max_leds = LED_RING_NUM_LEDS,
        .led_model = LED_MODEL_WS2812,
        .color_component_format = LED_STRIP_COLOR_COMPONENT_FMT_GRB,
        .flags = {
            .invert_out = false,
        },
    };

    led_strip_rmt_config_t rmt_config = {
        .clk_src = RMT_CLK_SRC_DEFAULT,
        .resolution_hz = 10 * 1000 * 1000,
        .mem_block_symbols = 64,
        .flags = {
            .with_dma = false,
        },
    };

    esp_err_t err = led_strip_new_rmt_device(&strip_config, &rmt_config, &ring->strip);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create LED strip: %s", esp_err_to_name(err));
        return err;
    }

    return led_ring_clear(ring);
}

esp_err_t led_ring_set_brightness(led_ring_t *ring, float brightness_percent)
{
    if (ring == NULL || ring->strip == NULL) return ESP_ERR_INVALID_STATE;

    if (brightness_percent < 0.0f) {
        brightness_percent = 0.0f;
    } else if (brightness_percent > 100.0f) {
        brightness_percent = 100.0f;
    }

    ring->brightness = (uint8_t)((brightness_percent / 100.0f) * 255.0f);

    return led_ring_show(ring);
}

esp_err_t led_ring_set_pixel(led_ring_t *ring, uint8_t index, led_rgb_t color)
{
    if (ring == NULL || ring->strip == NULL) return ESP_ERR_INVALID_STATE;
    if (index >= LED_RING_NUM_LEDS) return ESP_ERR_INVALID_ARG;

    ring->pixels[index] = color;
    return ESP_OK;
}

esp_err_t led_ring_set_all(led_ring_t *ring, led_rgb_t color)
{
    if (ring == NULL || ring->strip == NULL) return ESP_ERR_INVALID_STATE;

    for (uint8_t i = 0; i < LED_RING_NUM_LEDS; i++) {
        ring->pixels[i] = color;
    }

    return led_ring_show(ring);
}

esp_err_t led_ring_clear(led_ring_t *ring)
{
    if (ring == NULL || ring->strip == NULL) return ESP_ERR_INVALID_STATE;

    for (uint8_t i = 0; i < LED_RING_NUM_LEDS; i++) {
        ring->pixels[i] = (led_rgb_t){0};
    }

    return led_ring_show(ring);
}

esp_err_t led_ring_show(led_ring_t *ring)
{
    if (ring == NULL || ring->strip == NULL) return ESP_ERR_INVALID_STATE;

    //  A pixel that will not take a colour is reported to the caller, not
    //  aborted on: this runs from the telemetry task, and a display fault must
    //  not take the whole device down with it.
    for (uint8_t i = 0; i < LED_RING_NUM_LEDS; i++) {
        esp_err_t err = led_strip_set_pixel(
            ring->strip,
            i,
            apply_brightness(ring->pixels[i].r, ring->brightness),
            apply_brightness(ring->pixels[i].g, ring->brightness),
            apply_brightness(ring->pixels[i].b, ring->brightness)
        );

        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Failed to set pixel %u: %s", (unsigned) i, esp_err_to_name(err));
            return err;
        }
    }

    return led_strip_refresh(ring->strip);
}

esp_err_t led_ring_set_fill(
    led_ring_t *ring,
    float percentage,
    led_rgb_t color)
{
    if (ring == NULL || ring->strip == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    if (percentage < 0.0f) {
        percentage = 0.0f;
    } else if (percentage > 100.0f) {
        percentage = 100.0f;
    }

    float leds = (percentage / 100.0f) * LED_RING_NUM_LEDS;

    uint8_t full_leds = (uint8_t)leds;
    float partial = leds - full_leds;

    led_ring_clear(ring);

    // Fully lit LEDs
    for (uint8_t i = 0; i < full_leds; i++) {
        ring->pixels[i] = color;
    }

    // Partially lit LED
    if (full_leds < LED_RING_NUM_LEDS && partial > 0.0f) {
        ring->pixels[full_leds].r = color.r * partial;
        ring->pixels[full_leds].g = color.g * partial;
        ring->pixels[full_leds].b = color.b * partial;
    }

    return led_ring_show(ring);
}

typedef struct {
    led_ring_t *ring;
    led_rgb_t color;
    uint32_t delay_ms;
    int loop_count;
} led_ring_loop_args_t;

static void led_ring_loop_task(void *pvParameters)
{
    led_ring_loop_args_t *args = (led_ring_loop_args_t *)pvParameters;

    if (args == NULL || args->ring == NULL || args->ring->strip == NULL || args->loop_count <= 0) {
        free(args);
        vTaskDelete(NULL);
    }

    for (int count = 0; count < args->loop_count; count++) {
        for (uint8_t i = 0; i < LED_RING_NUM_LEDS; i++) {
            led_ring_set_pixel(args->ring, i, args->color);
            led_ring_show(args->ring);

            vTaskDelay(pdMS_TO_TICKS(args->delay_ms));
        }
        led_ring_clear(args->ring);

    }

    led_ring_clear(args->ring);
    led_ring_show(args->ring);

    free(args);
    vTaskDelete(NULL);
}

esp_err_t led_ring_start_loop_async(
    led_ring_t *ring,
    led_rgb_t color,
    uint32_t delay_ms,
    int loop_count
)
{
    if (ring == NULL || ring->strip == NULL || loop_count <= 0) {
        return ESP_ERR_INVALID_ARG;
    }

    led_ring_loop_args_t *args = malloc(sizeof(led_ring_loop_args_t));
    if (args == NULL) {
        return ESP_ERR_NO_MEM;
    }

    args->ring = ring;
    args->color = color;
    args->delay_ms = delay_ms;
    args->loop_count = loop_count;

    BaseType_t ok = xTaskCreate(
        led_ring_loop_task,
        "led_ring_loop",
        2048,
        args,
        5,
        NULL
    );

    if (ok != pdPASS) {
        free(args);
        return ESP_FAIL;
    }

    return ESP_OK;
}
