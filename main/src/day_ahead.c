#include "freertos/FreeRTOS.h"
#include "inc/day_ahead.h"
#include "inc/wifi_provisioning.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "inc/day_ahead_json.h"
#include "esp_crt_bundle.h"
#include "esp_heap_caps.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_netif_sntp.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#define RESPONSE_LIMIT (256 * 1024)
#define REFRESH_SECONDS (30 * 60)

static const char *TAG = "[day_ahead]";
static StaticSemaphore_t s_lock_storage;
static SemaphoreHandle_t s_lock;
static day_ahead_snapshot_t s_cache;

typedef struct { char *body; size_t used; bool overflow; } response_t;

static esp_err_t s_receive(esp_http_client_event_t *event)
{
    response_t *response = event->user_data;
    if (event->event_id != HTTP_EVENT_ON_DATA) return ESP_OK;
    if (event->data_len < 0 || (size_t)event->data_len > RESPONSE_LIMIT - response->used - 1) {
        response->overflow = true;
        return ESP_ERR_INVALID_SIZE;
    }
    memcpy(response->body + response->used, event->data, (size_t)event->data_len);
    response->used += (size_t)event->data_len;
    response->body[response->used] = '\0';
    return ESP_OK;
}

static esp_err_t s_fetch(day_ahead_snapshot_t *next)
{
    time_t now = time(NULL);
    struct tm date;
    day_ahead_localtime(now, &date);
    char day[16], url[200];
    strftime(day, sizeof(day), "%d-%m-%Y", &date);
    snprintf(url, sizeof(url), "https://public.api.energyzero.nl/v1/prices?date=%s"
             "&interval=INTERVAL_QUARTER&energy_type=ENERGY_TYPE_ELECTRICITY", day);
    if (!day_ahead_day_init(&next->days[0], now, 0) ||
        !day_ahead_day_init(&next->days[1], now, 1)) return ESP_FAIL;

    response_t response = { .body = heap_caps_malloc(RESPONSE_LIMIT, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT) };
    if (!response.body) return ESP_ERR_NO_MEM;
    response.body[0] = '\0';
    esp_http_client_config_t config = {
        .url = url, .crt_bundle_attach = esp_crt_bundle_attach,
        .timeout_ms = 15000, .event_handler = s_receive, .user_data = &response,
    };
    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (!client) { free(response.body); return ESP_ERR_NO_MEM; }
    esp_err_t err = esp_http_client_perform(client);
    int status = esp_http_client_get_status_code(client);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "GET %s failed: %s (HTTP %d, %u bytes, overflow %s)",
                 day, esp_err_to_name(err), status, (unsigned)response.used,
                 response.overflow ? "yes" : "no");
    } else if (response.overflow || status != 200 ||
               !esp_http_client_is_complete_data_received(client)) {
        ESP_LOGW(TAG, "Incomplete price response for %s (HTTP %d, %u bytes, overflow %s)",
                 day, status, (unsigned)response.used, response.overflow ? "yes" : "no");
        err = ESP_ERR_INVALID_RESPONSE;
    } else if (!day_ahead_json_parse(response.body, next->days)) {
        ESP_LOGW(TAG, "Invalid EnergyZero price data for %s (%u bytes)", day, (unsigned)response.used);
        err = ESP_ERR_INVALID_RESPONSE;
    } else if (next->days[0].available == 0) {
        ESP_LOGW(TAG, "No quarters for requested date %s (%u bytes)", day, (unsigned)response.used);
        err = ESP_ERR_INVALID_RESPONSE;
    }
    esp_http_client_cleanup(client);
    free(response.body);
    if (err == ESP_OK) next->fetched_at = time(NULL);
    return err;
}

static void s_state(day_ahead_state_t state)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
    s_cache.state = state;
    xSemaphoreGive(s_lock);
}

void day_ahead_snapshot(day_ahead_snapshot_t *out)
{
    if (!out) return;
    if (!s_lock) { memset(out, 0, sizeof(*out)); return; }
    xSemaphoreTake(s_lock, portMAX_DELAY);
    *out = s_cache;
    xSemaphoreGive(s_lock);
}

static void s_task(void *argument)
{
    (void)argument;
    bool clock_started = false;
    bool clock_synced = false;
    int64_t next_fetch = 0;
    time_t cached_day = 0;
    unsigned failures = 0;
    while (true) {
        if (!wifi_prov_is_connected()) {
            s_state(DAY_AHEAD_WAIT_WIFI);
            next_fetch = 0;
        } else {
            if (!clock_started) {
                esp_sntp_config_t config = ESP_NETIF_SNTP_DEFAULT_CONFIG("pool.ntp.org");
                esp_err_t err = esp_netif_sntp_init(&config);
                if (err == ESP_OK) clock_started = true;
                else ESP_LOGW(TAG, "Could not start time synchronization: %s", esp_err_to_name(err));
            }
            // A retained RTC can look plausible before SNTP has corrected it.
            // Wait for actual synchronization before choosing a date or doing TLS.
            if (clock_started && !clock_synced && esp_netif_sntp_sync_wait(0) == ESP_OK) {
                clock_synced = true;
                ESP_LOGI(TAG, "Clock synchronized; ready to request prices");
            }
            time_t now = time(NULL);
            if (!clock_synced || now < 1735689600) {
                s_state(DAY_AHEAD_WAIT_TIME);
            } else {
                day_ahead_day_t today;
                bool date_changed = day_ahead_day_init(&today, now, 0) && today.start != cached_day;
                if (esp_timer_get_time() >= next_fetch || date_changed) {
                    s_state(DAY_AHEAD_LOADING);
                    day_ahead_snapshot_t next = { .state = DAY_AHEAD_READY };
                    esp_err_t err = s_fetch(&next);
                    unsigned delay;
                    if (err == ESP_OK) {
                        xSemaphoreTake(s_lock, portMAX_DELAY);
                        s_cache = next;
                        xSemaphoreGive(s_lock);
                        cached_day = next.days[0].start;
                        failures = 0;
                        delay = REFRESH_SECONDS;
                    } else {
                        ESP_LOGW(TAG, "Prices unavailable: %s", esp_err_to_name(err));
                        s_state(DAY_AHEAD_ERROR);
                        if (failures < 5) failures++;
                        delay = 15U << failures;
                    }
                    next_fetch = esp_timer_get_time() +
                        (int64_t)(delay + esp_random() % (delay / 5 + 1)) * 1000000;
                    // A failed midnight refresh must still respect backoff.
                    if (date_changed) cached_day = today.start;
                }
            }
        }
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

esp_err_t day_ahead_init(void)
{
    if (s_lock) return ESP_OK;
    s_lock = xSemaphoreCreateMutexStatic(&s_lock_storage);
    if (xTaskCreate(s_task, "day_ahead", 12288, NULL, 4, NULL) != pdPASS) {
        s_lock = NULL;
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}
