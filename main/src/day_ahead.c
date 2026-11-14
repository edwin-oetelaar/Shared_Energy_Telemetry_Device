/*  =========================================================================
    day_ahead - fetch Dutch day-ahead electricity prices
    =========================================================================
*/

#include "freertos/FreeRTOS.h"
#include "inc/day_ahead.h"

#include <assert.h>
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

typedef struct {
    char *body;
    size_t used;
    bool overflow;
} response_t;


//  --------------------------------------------------------------------------
//  Collect the response body. The size comes from outside, so it is checked
//  against what is left of the buffer before anything is copied.

static esp_err_t
    s_receive (esp_http_client_event_t *event)
{
    response_t *response = event->user_data;
    if (event->event_id != HTTP_EVENT_ON_DATA)
        return ESP_OK;
    if (event->data_len < 0 || (size_t) event->data_len > RESPONSE_LIMIT - response->used - 1) {
        response->overflow = true;
        return ESP_ERR_INVALID_SIZE;
    }
    memcpy (response->body + response->used, event->data, (size_t) event->data_len);
    response->used += (size_t) event->data_len;
    response->body [response->used] = '\0';
    return ESP_OK;
}


//  --------------------------------------------------------------------------
//  One request for today's date, which also brings yesterday and, once they
//  are published, tomorrow. The result goes into next; the cache is not
//  touched here, so a failed request never replaces good prices.

static esp_err_t
    s_fetch (day_ahead_snapshot_t *next)
{
    int64_t started_us = esp_timer_get_time ();
    time_t now = time (NULL);
    struct tm date;
    if (!day_ahead_localtime (now, &date))
        return ESP_FAIL;        //  Otherwise the date below is uninitialised
    char day [16], url [200];
    strftime (day, sizeof (day), "%d-%m-%Y", &date);
    snprintf (url, sizeof (url), "https://public.api.energyzero.nl/v1/prices?date=%s"
              "&interval=INTERVAL_QUARTER&energy_type=ENERGY_TYPE_ELECTRICITY", day);
    if (!day_ahead_day_init (&next->days [0], now, 0)
    ||  !day_ahead_day_init (&next->days [1], now, 1))
        return ESP_FAIL;

    response_t response = { .body = heap_caps_malloc (RESPONSE_LIMIT, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT) };
    if (!response.body)
        return ESP_ERR_NO_MEM;
    response.body [0] = '\0';
    esp_http_client_config_t config = {
        .url = url,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .timeout_ms = 15000,
        .event_handler = s_receive,
        .user_data = &response,
    };
    esp_http_client_handle_t client = esp_http_client_init (&config);
    if (!client) {
        free (response.body);
        return ESP_ERR_NO_MEM;
    }
    esp_err_t err = esp_http_client_perform (client);
    int status = esp_http_client_get_status_code (client);
    if (err != ESP_OK) {
        ESP_LOGW (TAG, "GET %s failed: %s (HTTP %d, %u bytes, overflow %s)",
                  day, esp_err_to_name (err), status, (unsigned) response.used,
                  response.overflow ? "yes" : "no");
    }
    else
    if (response.overflow || status != 200
    ||  !esp_http_client_is_complete_data_received (client)) {
        ESP_LOGW (TAG, "Incomplete price response for %s (HTTP %d, %u bytes, overflow %s)",
                  day, status, (unsigned) response.used, response.overflow ? "yes" : "no");
        err = ESP_ERR_INVALID_RESPONSE;
    }
    else
    if (!day_ahead_json_parse (response.body, next->days)) {
        ESP_LOGW (TAG, "Invalid EnergyZero price data for %s (%u bytes)", day, (unsigned) response.used);
        err = ESP_ERR_INVALID_RESPONSE;
    }
    else
    if (next->days [0].available == 0) {
        ESP_LOGW (TAG, "No quarters for requested date %s (%u bytes)", day, (unsigned) response.used);
        err = ESP_ERR_INVALID_RESPONSE;
    }
    else {
        //  Without this line a good fetch leaves no trace; only failures did.
        ESP_LOGI (TAG, "Prices for %s: today %u/%u, tomorrow %u/%u quarters (%u bytes, %d ms)",
                  day, (unsigned) next->days [0].available, (unsigned) next->days [0].count,
                  (unsigned) next->days [1].available, (unsigned) next->days [1].count,
                  (unsigned) response.used, (int) ((esp_timer_get_time () - started_us) / 1000));
    }
    esp_http_client_cleanup (client);
    free (response.body);
    if (err == ESP_OK)
        next->fetched_at = time (NULL);
    return err;
}


//  --------------------------------------------------------------------------
//  Change only the state of the cache, under its lock.

static void
    s_state (day_ahead_state_t state)
{
    xSemaphoreTake (s_lock, portMAX_DELAY);
    s_cache.state = state;
    xSemaphoreGive (s_lock);
}


//  --------------------------------------------------------------------------
//  Copy the cache for a reader. Before day_ahead_init () there is no cache,
//  and the reader gets an empty one.

void
    day_ahead_snapshot (day_ahead_snapshot_t *out)
{
    assert (out);               //  Caller's contract
    if (!s_lock) {
        memset (out, 0, sizeof (*out));
        return;
    }
    xSemaphoreTake (s_lock, portMAX_DELAY);
    *out = s_cache;
    xSemaphoreGive (s_lock);
}


//  --------------------------------------------------------------------------
//  The price task is a state machine. Events come from conditions that are
//  looked at once a second; the table says what each one does in each state,
//  so every combination has an answer and there are no flags to disagree.
//
//  Events are raised in the order of s_source, every second, whether or not
//  they mean anything in the current state. Where they do not, the table
//  leaves the state alone, and nothing happens.

typedef enum {
    EVENT_WIFI_DOWN = 0,        //  The network is gone
    EVENT_WIFI_UP,              //  The network is there
    EVENT_CLOCK_SET,            //  SNTP has set the clock at least once
    EVENT_DUE,                  //  The next request is due, or the date changed
    EVENT_FETCHED,              //  A request succeeded and the cache is updated
    EVENT_FAILED,               //  A request failed; the cache is untouched
    EVENT_COUNT
} day_ahead_event_t;

#define WAIT_WIFI DAY_AHEAD_WAIT_WIFI
#define WAIT_TIME DAY_AHEAD_WAIT_TIME
#define LOADING   DAY_AHEAD_LOADING
#define READY     DAY_AHEAD_READY
#define IN_ERROR  DAY_AHEAD_ERROR

static const day_ahead_state_t s_next [DAY_AHEAD_STATE_COUNT][EVENT_COUNT] = {
    /*               WIFI_DOWN  WIFI_UP    CLOCK_SET  DUE        FETCHED    FAILED    */
    [WAIT_WIFI] = { WAIT_WIFI, WAIT_TIME, WAIT_WIFI, WAIT_WIFI, WAIT_WIFI, WAIT_WIFI },
    [WAIT_TIME] = { WAIT_WIFI, WAIT_TIME, LOADING,   WAIT_TIME, WAIT_TIME, WAIT_TIME },
    [LOADING]   = { WAIT_WIFI, LOADING,   LOADING,   LOADING,   READY,     IN_ERROR  },
    [READY]     = { WAIT_WIFI, READY,     READY,     LOADING,   READY,     READY     },
    [IN_ERROR]  = { WAIT_WIFI, IN_ERROR,  IN_ERROR,  LOADING,   READY,     IN_ERROR  }
};

#undef WAIT_WIFI
#undef WAIT_TIME
#undef LOADING
#undef READY
#undef IN_ERROR

static const char *s_state_name [DAY_AHEAD_STATE_COUNT] = {
    "wait-wifi", "wait-time", "loading", "ready", "error"
};

static const char *s_event_name [EVENT_COUNT] = {
    "wifi-down", "wifi-up", "clock-set", "due", "fetched", "failed"
};

//  What the machine remembers between events. Only s_handle () writes these.
static day_ahead_state_t s_machine = DAY_AHEAD_WAIT_WIFI;
static int64_t s_due_us = 0;            //  When READY or FAILED asks again
static time_t s_handled_day = 0;        //  Start of the last date asked for
static unsigned s_failures = 0;         //  Backoff step, capped at five

//  Observed once and then remembered: esp_netif_sntp_sync_wait () reports a
//  synchronisation only once, so "has the clock ever been set" must be kept.
static bool s_sntp_started = false;
static bool s_clock_set = false;


//  --------------------------------------------------------------------------
//  The moment a request becomes due again, a fifth of spread added so that
//  devices that failed together do not return together.

static int64_t
    s_due_after (unsigned seconds)
{
    return esp_timer_get_time ()
         + (int64_t) (seconds + esp_random () % (seconds / 5 + 1)) * 1000000;
}


//  --------------------------------------------------------------------------
//  The start of today, or zero when the clock cannot say.

static time_t
    s_today_start (void)
{
    day_ahead_day_t today;
    return day_ahead_day_init (&today, time (NULL), 0) ? today.start : 0;
}


//  --------------------------------------------------------------------------
//  The only function that changes s_machine. Runs the entry action of the new
//  state, logs the transition, and tells the cache.

static void
    s_handle (day_ahead_event_t event)
{
    assert (event < EVENT_COUNT);       //  Caller's contract

    day_ahead_state_t next = s_next [s_machine][event];
    if (next == s_machine)
        return;

    ESP_LOGI (TAG, "prices: %s --%s--> %s",
              s_state_name [s_machine], s_event_name [event], s_state_name [next]);
    s_machine = next;

    switch (next) {
        case DAY_AHEAD_WAIT_TIME:
            //  Start SNTP the first time there is a network, and only once.
            if (!s_sntp_started) {
                esp_sntp_config_t config = ESP_NETIF_SNTP_DEFAULT_CONFIG ("pool.ntp.org");
                esp_err_t err = esp_netif_sntp_init (&config);
                if (err == ESP_OK)
                    s_sntp_started = true;
                else
                    ESP_LOGW (TAG, "Could not start time synchronization: %s", esp_err_to_name (err));
            }
            break;

        case DAY_AHEAD_LOADING:
            //  This date counts as asked for, whatever the answer. A failed
            //  midnight request then waits for its backoff, not for the
            //  next second.
            s_handled_day = s_today_start ();
            break;

        case DAY_AHEAD_READY:
            s_failures = 0;
            s_due_us = s_due_after (REFRESH_SECONDS);
            break;

        case DAY_AHEAD_ERROR:
            if (s_failures < 5)
                s_failures++;
            s_due_us = s_due_after (15U << s_failures);
            break;

        case DAY_AHEAD_WAIT_WIFI:
        case DAY_AHEAD_STATE_COUNT:
            break;
    }

    s_state (next);
}


//  --------------------------------------------------------------------------
//  The conditions the events come from. Each one only looks; none of them
//  changes the machine.

static bool
    s_wifi_down (void)
{
    return !wifi_prov_is_connected ();
}

static bool
    s_wifi_up (void)
{
    return wifi_prov_is_connected ();
}

static bool
    s_clock_is_set (void)
{
    //  A retained RTC can look plausible before SNTP has corrected it.
    //  Wait for actual synchronization before choosing a date or doing TLS.
    if (s_sntp_started && !s_clock_set && esp_netif_sntp_sync_wait (0) == ESP_OK) {
        s_clock_set = true;
        ESP_LOGI (TAG, "Clock synchronized; ready to request prices");
    }
    return s_clock_set && time (NULL) >= DAY_AHEAD_EARLIEST_TIME;
}

static bool
    s_request_due (void)
{
    time_t today = s_today_start ();
    return esp_timer_get_time () >= s_due_us
        || (today != 0 && today != s_handled_day);
}

static const struct {
    day_ahead_event_t event;
    bool (*holds) (void);
} s_source [] = {
    { EVENT_WIFI_DOWN, s_wifi_down    },
    { EVENT_WIFI_UP,   s_wifi_up      },
    { EVENT_CLOCK_SET, s_clock_is_set },
    { EVENT_DUE,       s_request_due  }
};

#define SOURCE_COUNT (sizeof (s_source) / sizeof (s_source [0]))


//  --------------------------------------------------------------------------
//  Do the request that LOADING stands for, and report how it went. The cache
//  is replaced only by a complete, valid answer.

static void
    s_load (void)
{
    static bool stack_reported = false;

    day_ahead_snapshot_t next = { .state = DAY_AHEAD_READY };
    esp_err_t err = s_fetch (&next);
    if (err != ESP_OK) {
        ESP_LOGW (TAG, "Prices unavailable: %s", esp_err_to_name (err));
        s_handle (EVENT_FAILED);
        return;
    }

    xSemaphoreTake (s_lock, portMAX_DELAY);
    s_cache = next;
    xSemaphoreGive (s_lock);

    if (!stack_reported) {
        //  ESP-IDF returns bytes here, not words as plain FreeRTOS does.
        ESP_LOGI (TAG, "Price task stack: %u bytes still free",
                  (unsigned) uxTaskGetStackHighWaterMark (NULL));
        stack_reported = true;
    }
    s_handle (EVENT_FETCHED);
}


//  --------------------------------------------------------------------------
//  The price task: look at every condition once a second, and do a request
//  whenever the machine has gone to LOADING.

static void
    s_task (void *argument)
{
    (void) argument;
    s_state (s_machine);
    while (true) {
        for (size_t source = 0; source < SOURCE_COUNT; source++) {
            if (s_source [source].holds ())
                s_handle (s_source [source].event);
        }
        if (s_machine == DAY_AHEAD_LOADING)
            s_load ();
        vTaskDelay (pdMS_TO_TICKS (1000));
    }
}


//  --------------------------------------------------------------------------
//  Start the price task. Calling it twice is harmless.

esp_err_t
    day_ahead_init (void)
{
    if (s_lock)
        return ESP_OK;
    s_lock = xSemaphoreCreateMutexStatic (&s_lock_storage);
    if (xTaskCreate (s_task, "day_ahead", 12288, NULL, 4, NULL) != pdPASS) {
        s_lock = NULL;
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}
