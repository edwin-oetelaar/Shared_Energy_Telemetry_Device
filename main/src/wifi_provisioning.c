
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"

#include <assert.h>
#include <inttypes.h>
#include <string.h>

#include "esp_system.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"

#include "inc/wifi_provisioning.h"
#include "inc/wifi_web.h"
#include "inc/dns_server.h"
#include "inc/wifi_storage.h"
#include "inc/energyboxx_api.h"


#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT      BIT1

//  How long the device waits for somebody to finish provisioning before it
//  restarts and tries the stored network again. Without this the wait is
//  unbounded: a device whose API was briefly unreachable would sit on its
//  windowsill showing a portal that nobody is looking at, forever. A restart
//  costs a few seconds and gives the saved credentials a fresh chance; if
//  nothing has changed the portal is simply back. The clock is reset by every
//  handler a person triggers, so it measures silence, not elapsed time.
//
//  esp_restart () reports ESP_RST_SW, which main.c deliberately does not count
//  towards the three-power-cycles credential wipe.

#define PROVISIONING_SILENCE_TIMEOUT_MS  (15 * 60 * 1000)

//  How often the stored API credentials are retried while the provisioning
//  portal is open. Slow on purpose: the portal is the normal way in, and this
//  retry only exists to recover from an API that was down at boot.
#define API_RETRY_WHILE_PROVISIONING_MS  30000

#define PROV_AP_SSID       "SETD_Provisioning"
#define PROV_AP_PASSWORD   ""
#define PROV_AP_CHANNEL    1
#define PROV_AP_MAX_CONN   4

static EventGroupHandle_t s_wifi_event_group;

static dns_server_handle_t s_dns_handle = NULL;

static esp_netif_t *s_sta_netif = NULL;
static esp_netif_t *s_ap_netif = NULL;

static const char *TAG = "[wifi_prov]";

static wifi_prov_state_t s_state = WIFI_PROV_STATE_IDLE;

//  Reconnect schedule. Each row says how long to wait before the next attempt
//  and what the device reports about itself while it waits. The last row
//  repeats for as long as the network stays away: a device on a windowsill has
//  no keyboard, so giving up is not a strategy. The whole recovery policy is
//  this table - to change how patient the device is, change these numbers.

typedef struct {
    uint32_t delay_ms;
    wifi_prov_state_t state;
} retry_step_t;

static const retry_step_t s_retry_schedule [] = {
    {    500, WIFI_PROV_STATE_CONNECTING     },
    {   1000, WIFI_PROV_STATE_CONNECTING     },
    {   2000, WIFI_PROV_STATE_CONNECTING     },
    {   5000, WIFI_PROV_STATE_CONNECTING     },
    {  10000, WIFI_PROV_STATE_CONNECT_FAILED },
    {  30000, WIFI_PROV_STATE_CONNECT_FAILED },
    {  60000, WIFI_PROV_STATE_CONNECT_FAILED },
    { 300000, WIFI_PROV_STATE_CONNECT_FAILED }
};

#define RETRY_SCHEDULE_ROWS  (sizeof (s_retry_schedule) / sizeof (s_retry_schedule [0]))

static esp_timer_handle_t s_retry_timer = NULL;
static size_t s_retry_row = 0;

static TickType_t s_last_portal_activity = 0;
static bool s_portal_open = false;
static esp_timer_handle_t s_portal_timer = NULL;

//  Set once the credentials are accepted, so the browser gets its answer
//  before the access point disappears from under it.
static int64_t s_portal_close_at_us = 0;

static wifi_prov_accepted_cb_t s_accepted_cb = NULL;

static char current_ssid[33] = {0};
static char current_password[65] = {0};

static void wifi_set_state(wifi_prov_state_t state)
{
    s_state = state;
}


//  --------------------------------------------------------------------------
//  The backoff delay has passed, so ask Wi-Fi to try again. A refusal here is
//  not fatal: the disconnect event that follows schedules the next attempt.

static void
    s_retry_timer_expired (void *argument)
{
    (void) argument;

    esp_err_t rc = esp_wifi_connect ();
    if (rc != ESP_OK)
        ESP_LOGW (TAG, "Reconnect attempt refused: %s", esp_err_to_name (rc));
}


//  --------------------------------------------------------------------------
//  Arm the timer for the current row of the schedule and report the state that
//  belongs to it, then step one row down. The last row is where we stay.

static void
    s_schedule_retry (void)
{
    assert (s_retry_timer);         //  Created by wifi_prov_init ()
    assert (s_retry_row < RETRY_SCHEDULE_ROWS);

    const retry_step_t *step = &s_retry_schedule [s_retry_row];
    if (s_retry_row + 1 < RETRY_SCHEDULE_ROWS)
        s_retry_row++;

    wifi_set_state (step->state);
    if (step->state == WIFI_PROV_STATE_CONNECT_FAILED)
        xEventGroupSetBits (s_wifi_event_group, WIFI_FAIL_BIT);

    ESP_LOGW (TAG, "Reconnecting in %" PRIu32 " ms", step->delay_ms);

    esp_timer_stop (s_retry_timer);     //  Harmless when it is not running

    esp_err_t rc = esp_timer_start_once (s_retry_timer,
                                         (uint64_t) step->delay_ms * 1000);
    assert (rc == ESP_OK);              //  Only fails on bad arguments
    (void) rc;
}


//  --------------------------------------------------------------------------
//  Back to the top of the schedule. A connection that succeeded, or a fresh
//  set of credentials, starts counting again from the shortest delay.

static void
    s_reset_retry_schedule (void)
{
    s_retry_row = 0;
    if (s_retry_timer)
        esp_timer_stop (s_retry_timer);
}

static void wifi_event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data)
{
    (void) arg;

    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        ESP_LOGI(TAG, "STA started");
    }

    else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        wifi_event_sta_disconnected_t *disc = (wifi_event_sta_disconnected_t *)event_data;

        ESP_LOGW(TAG, "STA disconnected, reason=%d", disc->reason);

        xEventGroupClearBits(s_wifi_event_group, WIFI_CONNECTED_BIT);

        //  Only chase a network we have credentials for. Without an SSID the
        //  device is waiting to be provisioned, not for the router to return.
        if (current_ssid [0] != '\0')
            s_schedule_retry ();
        else
            wifi_set_state (WIFI_PROV_STATE_IDLE);
    }

    else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_AP_START) {
        ESP_LOGI(TAG, "Provisioning AP started");
        wifi_set_state(WIFI_PROV_STATE_AP_ACTIVE);
    }

    else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_AP_STACONNECTED) {
        wifi_event_ap_staconnected_t *event = (wifi_event_ap_staconnected_t *)event_data;
        ESP_LOGI(TAG, "Client connected to AP, AID=%d", event->aid);
    }

    else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_AP_STADISCONNECTED) {
        wifi_event_ap_stadisconnected_t *event = (wifi_event_ap_stadisconnected_t *)event_data;
        ESP_LOGI(TAG, "Client disconnected from AP, AID=%d", event->aid);
    }

    else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;

        ESP_LOGI(TAG, "Got IP: " IPSTR, IP2STR(&event->ip_info.ip));

        s_reset_retry_schedule ();
        wifi_set_state(WIFI_PROV_STATE_CONNECTED);

        wifi_storage_save_credentials(current_ssid, current_password);

        
        xEventGroupClearBits(s_wifi_event_group, WIFI_FAIL_BIT);
        xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
    }
}

esp_err_t wifi_prov_init(void)
{
    esp_err_t err;

    err = esp_netif_init();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        return err;
    }

    err = esp_event_loop_create_default();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        return err;
    }

    s_wifi_event_group = xEventGroupCreate();
    if (s_wifi_event_group == NULL) {
        ESP_LOGE(TAG, "Failed to create event group");
        return ESP_FAIL;
    }

    s_sta_netif = esp_netif_create_default_wifi_sta();
    s_ap_netif = esp_netif_create_default_wifi_ap();

    if (s_sta_netif == NULL || s_ap_netif == NULL) {
        ESP_LOGE(TAG, "Failed to create netifs");
        return ESP_FAIL;
    }

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();

    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    ESP_ERROR_CHECK(esp_event_handler_register(
        WIFI_EVENT,
        ESP_EVENT_ANY_ID,
        wifi_event_handler,
        NULL
    ));

    ESP_ERROR_CHECK(esp_event_handler_register(
        IP_EVENT,
        IP_EVENT_STA_GOT_IP,
        wifi_event_handler,
        NULL
    ));

    const esp_timer_create_args_t retry_timer_args = {
        .callback = s_retry_timer_expired,
        .name = "wifi_retry"
    };

    ESP_ERROR_CHECK(esp_timer_create(&retry_timer_args, &s_retry_timer));

    ESP_ERROR_CHECK(esp_wifi_start());

    wifi_set_state(WIFI_PROV_STATE_IDLE);

    ESP_LOGI(TAG, "WiFi provisioning initialized");

    return ESP_OK;
}

//  --------------------------------------------------------------------------
//  Watches an on-demand portal. It closes on its own for two reasons: the
//  credentials were accepted, or nobody has touched it for a while. Without
//  this an open access point would stay up until the next power cut.

static void s_portal_watchdog(void *argument)
{
    (void) argument;

    if (!s_portal_open) {
        return;
    }

    if (s_portal_close_at_us != 0) {
        if (esp_timer_get_time() >= s_portal_close_at_us) {
            ESP_LOGI(TAG, "Credentials accepted, closing the portal");
            wifi_prov_close_portal();
        }
        return;
    }

    if (xTaskGetTickCount() - s_last_portal_activity
    >   pdMS_TO_TICKS(PROVISIONING_SILENCE_TIMEOUT_MS)) {
        ESP_LOGW(TAG, "Nobody used the portal for %d minutes, closing it",
                 PROVISIONING_SILENCE_TIMEOUT_MS / 60000);
        wifi_prov_close_portal();
    }
}


void wifi_prov_set_credentials_accepted_cb(wifi_prov_accepted_cb_t callback)
{
    s_accepted_cb = callback;
}


void wifi_prov_note_credentials_accepted(void)
{
    ESP_LOGI(TAG, "Credentials accepted");

    if (s_accepted_cb != NULL) {
        s_accepted_cb();
    }

    if (!s_portal_open) {
        return;
    }

    //  Three seconds so the browser can show the person that it worked before
    //  the access point they are on disappears.
    s_portal_close_at_us = esp_timer_get_time() + 3 * 1000 * 1000;
}


bool wifi_prov_portal_is_open(void)
{
    return s_portal_open;
}


esp_err_t wifi_prov_close_portal(void)
{
    if (!s_portal_open) {
        return ESP_OK;
    }

    s_portal_open = false;
    s_portal_close_at_us = 0;

    if (s_portal_timer != NULL) {
        esp_timer_stop(s_portal_timer);
    }

    wifi_web_stop();

    if (s_dns_handle != NULL) {
        stop_dns_server(s_dns_handle);
        s_dns_handle = NULL;
    }

    //  Back to station only. The device keeps the network it was already on.
    esp_err_t err = esp_wifi_set_mode(WIFI_MODE_STA);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Could not return to station mode: %s", esp_err_to_name(err));
    }

    //  wifi_prov_start_ap () reported the device as being in provisioning.
    //  Put that back, or the screen keeps saying "Instellen" while the device
    //  is online and fetching telemetry.
    wifi_set_state(wifi_prov_is_connected()
                       ? WIFI_PROV_STATE_CONNECTED
                       : WIFI_PROV_STATE_IDLE);

    ESP_LOGI(TAG, "Portal closed");

    return err;
}


esp_err_t wifi_prov_open_portal(void)
{
    if (s_portal_open) {
        wifi_prov_note_portal_activity();
        return ESP_OK;
    }

    esp_err_t err = wifi_prov_start_ap();
    if (err != ESP_OK) {
        return err;
    }

    err = wifi_web_start();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Portal did not start: %s", esp_err_to_name(err));
        return err;
    }

    if (s_portal_timer == NULL) {
        const esp_timer_create_args_t args = {
            .callback = s_portal_watchdog,
            .name = "portal_watchdog"
        };

        err = esp_timer_create(&args, &s_portal_timer);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Portal watchdog missing: %s", esp_err_to_name(err));
            return err;
        }
    }

    s_portal_open = true;
    s_portal_close_at_us = 0;
    wifi_prov_note_portal_activity();
    esp_timer_start_periodic(s_portal_timer, 5 * 1000 * 1000);

    ESP_LOGI(TAG, "Portal open on request");

    return ESP_OK;
}


esp_err_t wifi_prov_start_ap(void)
{
    wifi_config_t ap_config = {
        .ap = {
            .ssid = PROV_AP_SSID,
            .ssid_len = strlen(PROV_AP_SSID),
            .channel = PROV_AP_CHANNEL,
            .password = PROV_AP_PASSWORD,
            .max_connection = PROV_AP_MAX_CONN,
            .authmode = WIFI_AUTH_OPEN,
        },
    };

    ESP_LOGI(TAG, "Starting provisioning AP: %s", PROV_AP_SSID);

    //  No ESP_ERROR_CHECK here. This used to run once at start-up, where an
    //  abort was survivable; since the portal can be opened again from the
    //  screen it runs while somebody is using the device, and a failure has to
    //  be reported rather than take the whole thing down.
    esp_err_t err = esp_wifi_set_mode(WIFI_MODE_APSTA);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Could not switch to AP+station mode: %s", esp_err_to_name(err));
        return err;
    }

    err = esp_wifi_set_config(WIFI_IF_AP, &ap_config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Could not configure the access point: %s", esp_err_to_name(err));
        return err;
    }

    wifi_set_state(WIFI_PROV_STATE_AP_ACTIVE);
    wifi_prov_note_portal_activity();

    // Start the DNS server that will redirect all queries to the softAP IP
    dns_server_config_t dns_config = DNS_SERVER_CONFIG_SINGLE("*" /* all A queries */, "WIFI_AP_DEF" /* softAP netif ID */);
    s_dns_handle = start_dns_server(&dns_config);
    if (s_dns_handle == NULL) {
        //  The portal still works on its own address; only the redirect that
        //  opens it by itself is missing. Worth saying out loud rather than
        //  leaving somebody to wonder why nothing pops up.
        ESP_LOGE(TAG, "DNS redirect did not start; the portal is only at 192.168.4.1");
    }

    return ESP_OK;
}

esp_err_t wifi_prov_connect(const char *ssid, const char *password)
{
    if (ssid == NULL || ssid[0] == '\0') {
        return ESP_ERR_INVALID_ARG;
    }

    wifi_config_t sta_config = {0};

    snprintf((char *)sta_config.sta.ssid, sizeof(sta_config.sta.ssid), "%s", ssid);

    if (password != NULL) {
        snprintf((char *)sta_config.sta.password, sizeof(sta_config.sta.password), "%s", password);
    }

    s_reset_retry_schedule ();
    wifi_set_state(WIFI_PROV_STATE_CONNECTING);

    xEventGroupClearBits(s_wifi_event_group, WIFI_CONNECTED_BIT | WIFI_FAIL_BIT);

    ESP_LOGI(TAG, "Connecting to SSID: %s", ssid);

    esp_err_t config_err = esp_wifi_set_config(WIFI_IF_STA, &sta_config);
    if (config_err != ESP_OK) {
        ESP_LOGE(TAG, "Could not set the station config: %s", esp_err_to_name(config_err));
        return config_err;
    }

    current_ssid[0] = '\0';
    current_password[0] = '\0';
    strncpy(current_ssid, ssid, sizeof(current_ssid) - 1);
    if (password != NULL) {
        strncpy(current_password, password, sizeof(current_password) - 1);
    }

    return esp_wifi_connect();
}

wifi_prov_state_t wifi_prov_get_state(void)
{
    return s_state;
}

esp_err_t wifi_prov_scan(wifi_ap_record_t *records, uint16_t *count)
{
    assert (records);           //  Caller's contract, not client input
    assert (count);

    wifi_scan_config_t scan_config = {
        .show_hidden = false,
    };

    //  A scan can legitimately be refused - one is already running, or the
    //  reconnect timer just asked for a connection. That is a busy signal for
    //  whoever clicked "Refresh networks", not a reason to reboot the device.
    esp_err_t err = esp_wifi_scan_start(&scan_config, true);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Scan refused: %s", esp_err_to_name(err));
        return err;
    }

    return esp_wifi_scan_get_ap_records(count, records);
}

bool wifi_prov_is_connected(void)
{
    if (s_wifi_event_group == NULL) {
        return false;
    }

    EventBits_t bits = xEventGroupGetBits(s_wifi_event_group);
    return (bits & WIFI_CONNECTED_BIT) != 0;
}

const char *wifi_prov_ap_ssid(void)
{
    return PROV_AP_SSID;
}

const char *wifi_prov_current_ssid(void)
{
    return current_ssid;
}

void wifi_prov_ip_string(char *text, size_t length)
{
    assert (text);              //  Caller's contract
    assert (length > 0);

    text [0] = '\0';

    esp_netif_ip_info_t info;

    if (s_sta_netif == NULL
    ||  esp_netif_get_ip_info(s_sta_netif, &info) != ESP_OK
    ||  info.ip.addr == 0) {
        return;
    }

    snprintf(text, length, IPSTR, IP2STR(&info.ip));
}

void wifi_prov_note_portal_activity(void)
{
    s_last_portal_activity = xTaskGetTickCount ();
}

void wifi_prov_wait_until_completed(void)
{
    xEventGroupWaitBits(
        s_wifi_event_group,
        WIFI_CONNECTED_BIT,
        pdFALSE,
        pdTRUE,
        portMAX_DELAY);

    //  Wait until the API has been set up and validated. The portal is what
    //  normally does that: /api-check calls setup and fetch_token in the httpd
    //  task and records the result. Retrying here as well is only for the case
    //  where the stored credentials are fine but the API was unreachable at
    //  boot - so it happens on a slow tick, not once a second, and it goes
    //  through the same lock as the portal.

    TickType_t next_retry = xTaskGetTickCount ();
    wifi_prov_note_portal_activity ();

    while (energyboxx_api_is_valid_credentials() == false) {
        if (energyboxx_api_has_credentials()
        &&  (int32_t) (xTaskGetTickCount () - next_retry) >= 0) {
            energyboxx_api_fetch_token();
            next_retry = xTaskGetTickCount ()
                       + pdMS_TO_TICKS (API_RETRY_WHILE_PROVISIONING_MS);
        }

        if (xTaskGetTickCount () - s_last_portal_activity
        >   pdMS_TO_TICKS (PROVISIONING_SILENCE_TIMEOUT_MS)) {
            ESP_LOGW (TAG, "Nobody used the portal for %d minutes, restarting to "
                           "try the saved network again",
                      PROVISIONING_SILENCE_TIMEOUT_MS / 60000);
            esp_restart ();
        }

        vTaskDelay(pdMS_TO_TICKS(1000));
    }

    vTaskDelay(pdMS_TO_TICKS(3000));

    wifi_web_stop();
    if(s_dns_handle != NULL) {
        stop_dns_server(s_dns_handle);
        s_dns_handle = NULL;
    }

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
}

bool wifi_prov_wait_for_connection_timeout(TickType_t timeout)
{
    EventBits_t bits = xEventGroupWaitBits(
        s_wifi_event_group,
        WIFI_CONNECTED_BIT,
        pdFALSE,
        pdTRUE,
        timeout
    );

    return (bits & WIFI_CONNECTED_BIT) != 0;
}
