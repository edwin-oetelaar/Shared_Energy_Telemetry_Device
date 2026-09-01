#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_log.h"
#include "esp_random.h"
#include "esp_system.h"
#include "nvs.h"
#include "driver/gpio.h"

#include <inttypes.h>

#include "inc/api_storage.h"
#include "inc/wifi_storage.h"
#include "inc/wifi_provisioning.h"
#include "inc/wifi_web.h"
#include "inc/energyboxx_api.h"


#include "inc/display.h"
#include "inc/input.h"
#include "inc/status_view.h"
#include "inc/updater.h"
#include "soc/gpio_num.h"

static const char *TAG = "[main]";
energyboxx_data_t data;

#define RESET_WIFI_GPIO GPIO_NUM_17
#define RESET_HOLD_MS   3000

#define POWER_BALANCE_DEADBAND_KW 0.05f

//  How long to wait before asking the API again after a failed round. The last
//  row repeats, so a long outage settles at one attempt every five minutes
//  instead of six per minute per device - with a few hundred devices that
//  difference is the one between polling and a self-inflicted flood.

static const uint32_t s_api_retry_delay_ms [] = {
    10000, 20000, 40000, 80000, 160000, 300000
};

#define API_RETRY_ROWS  (sizeof (s_api_retry_delay_ms) / sizeof (s_api_retry_delay_ms [0]))

#define TELEMETRY_INTERVAL_MS   (60 * 1000)
#define WIFI_WAIT_POLL_MS       10000

static volatile bool data_connection_ok = false;

//  What the telemetry task last worked out about the community. The view task
//  combines it with the Wi-Fi state to decide what the device shows.
static volatile status_view_state_t energy_state = STATUS_VIEW_NO_DATA;
static size_t api_retry_row = 0;


//  --------------------------------------------------------------------------
//  Report a failure and carry on. ESP_ERROR_CHECK is an abort, so it belongs
//  only on initialisation the device genuinely cannot run without. A status
//  LED that will not light or a counter that will not store is not that: the
//  device is more useful running with one broken part than rebooting forever.
//  A reboot loop here is especially expensive, because three boots in ten
//  seconds erase the customer's credentials.

static void
    s_log_if_failed (const char *what, esp_err_t err)
{
    if (err != ESP_OK)
        ESP_LOGE (TAG, "%s failed: %s", what, esp_err_to_name (err));
}


//  --------------------------------------------------------------------------
//  Which resets count towards the three-boots-in-ten-seconds credential wipe.
//  Only a deliberate power cycle by the person holding the device does. A
//  panic, a watchdog bite or a brownout is the firmware failing, and counting
//  those means a bug early in app_main erases the customer's configuration all
//  by itself - the device forgets who it is because of our mistake, not their
//  request. The last row is the catch-all, so the table is never incomplete.

static const struct {
    esp_reset_reason_t reason;
    bool counts;
} s_reset_reason [] = {
    { ESP_RST_POWERON,  true  },
    { ESP_RST_EXT,      true  },
    { ESP_RST_PANIC,    false },
    { ESP_RST_INT_WDT,  false },
    { ESP_RST_TASK_WDT, false },
    { ESP_RST_WDT,      false },
    { ESP_RST_BROWNOUT, false },
    { ESP_RST_SW,       false },

    //  Seen on the XIAO ESP32-S3: the host toggling the reset line over the
    //  native USB port reports ESP_RST_USB. That is a developer flashing or
    //  resetting the board, not somebody pulling a plug, so it does not count.
    //  JTAG is the same class of event.
    { ESP_RST_USB,      false },
    { ESP_RST_JTAG,     false },

    //  Firmware failing, like a panic or a watchdog bite.
    { ESP_RST_CPU_LOCKUP, false },

    //  A power problem, but not a deliberate power cycle.
    { ESP_RST_PWR_GLITCH, false },

    { ESP_RST_UNKNOWN,  false }
};

#define RESET_REASON_ROWS  (sizeof (s_reset_reason) / sizeof (s_reset_reason [0]))

static bool
    s_reset_counts_as_user_request (void)
{
    esp_reset_reason_t reason = esp_reset_reason ();

    size_t row = 0;
    while (row < RESET_REASON_ROWS - 1 && s_reset_reason [row].reason != reason)
        row++;

    ESP_LOGI (TAG, "Reset reason %d, counts as user request: %s",
              (int) reason, s_reset_reason [row].counts ? "yes" : "no");

    return s_reset_reason [row].counts;
}

//  --------------------------------------------------------------------------
//  Work out what the device should be showing, and tell the view. Wi-Fi comes
//  first: without a network the energy state says nothing about the community,
//  only about how stale our last answer is.

static status_view_state_t s_state_to_show(void)
{
    //  An update takes over the screen. It ends in a restart, and somebody
    //  standing in front of the device should know that before the screen goes
    //  dark rather than after.
    if (updater_get_state() != UPDATER_IDLE && updater_get_state() != UPDATER_FAILED) {
        return STATUS_VIEW_UPDATING;
    }

    wifi_prov_state_t wifi_state = wifi_prov_get_state();

    if (wifi_state == WIFI_PROV_STATE_AP_ACTIVE) {
        return STATUS_VIEW_PROVISIONING;
    }

    //  Idle means Wi-Fi has been initialised but nothing has been asked of it
    //  yet. That is still starting up, not connecting to something.
    if (wifi_state == WIFI_PROV_STATE_IDLE) {
        return STATUS_VIEW_STARTING;
    }

    if (wifi_state == WIFI_PROV_STATE_CONNECT_FAILED) {
        return STATUS_VIEW_CONNECT_FAILED;
    }

    if (wifi_state != WIFI_PROV_STATE_CONNECTED) {
        return STATUS_VIEW_CONNECTING;
    }

    //  No keys at all is a different problem from no data, and it needs a
    //  different answer from the person looking at the screen.
    if (!energyboxx_api_has_credentials()) {
        return STATUS_VIEW_KEYS_NEEDED;
    }

    if (!energyboxx_api_is_valid_credentials() || !data_connection_ok) {
        return STATUS_VIEW_NO_DATA;
    }

    return energy_state;
}

//  --------------------------------------------------------------------------
//  The portal accepted new credentials. Say so on the screen: somebody who
//  just typed them in is standing right there, and the browser message alone
//  leaves them wondering whether the device noticed.

static void s_credentials_accepted(void)
{
    status_view_announce(STATUS_VIEW_SETUP_DONE, 6000);
}


static void status_view_task(void *pvParameters)
{
    (void) pvParameters;

    while (true) {
        s_log_if_failed("showing the status", status_view_show(s_state_to_show()));
        vTaskDelay(pdMS_TO_TICKS(500));
    }
}

static bool reset_button_held_on_boot(void)
{
    gpio_config_t io_conf = {
        .pin_bit_mask = 1ULL << RESET_WIFI_GPIO,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };

    gpio_config(&io_conf);

    if (gpio_get_level(RESET_WIFI_GPIO) != 0) {
        return false;
    }

    int elapsed = 0;

    while (elapsed < RESET_HOLD_MS) {
        if (gpio_get_level(RESET_WIFI_GPIO) != 0) {
            return false;
        }

        vTaskDelay(pdMS_TO_TICKS(100));
        elapsed += 100;
    }

    return true;
}

static bool validate_stored_api_credentials(void)
{
    return energyboxx_api_load_stored_credentials() == ESP_OK;
}


//  --------------------------------------------------------------------------
//  Spread a delay by up to a fifth, so devices that failed at the same moment -
//  a street coming back after a power cut, an API that was down for everyone -
//  drift apart instead of returning in lockstep.

static uint32_t
    s_with_jitter (uint32_t delay_ms)
{
    return delay_ms + (uint32_t) (esp_random () % (delay_ms / 5 + 1));
}


//  --------------------------------------------------------------------------
//  Wait out the current backoff row, then step one row down. The last row is
//  where a long outage settles.

static void
    s_api_backoff_wait (void)
{
    uint32_t delay_ms = s_with_jitter (s_api_retry_delay_ms [api_retry_row]);

    if (api_retry_row + 1 < API_RETRY_ROWS)
        api_retry_row++;

    ESP_LOGW (TAG, "Next API attempt in %" PRIu32 " ms", delay_ms);
    vTaskDelay (pdMS_TO_TICKS (delay_ms));
}


//  --------------------------------------------------------------------------
//  Bring up the provisioning access point and its portal. Returns false when
//  either refuses to start; the caller then does not wait for a portal that is
//  not there, and the reconnect schedule keeps trying the saved network.

static bool start_provisioning_portal(void)
{
    esp_err_t err = wifi_prov_start_ap();
    if (err != ESP_OK) {
        s_log_if_failed("starting the provisioning access point", err);
        return false;
    }

    err = wifi_web_start();
    if (err != ESP_OK) {
        s_log_if_failed("starting the provisioning portal", err);
        return false;
    }

    return true;
}

static void energyboxx_task(void *pvParameters)
{
    (void) pvParameters;
   
    while(true)
    {
        if (!wifi_prov_is_connected()) {
            //  Nothing was asked of the API, so the backoff row stays put; this
            //  is just waiting for the radio to come back.
            data_connection_ok = false;
            vTaskDelay(pdMS_TO_TICKS(WIFI_WAIT_POLL_MS));
            continue;
        }

        esp_err_t err = energyboxx_api_fetch_token();
        if (err != ESP_OK)
        {
            ESP_LOGE(TAG, "Failed to fetch token: %s", esp_err_to_name(err));
            data_connection_ok = false;
            energyboxx_api_set_renew_token(true);
            s_api_backoff_wait();
            continue;
        }

        err = energyboxx_api_get_data(&data);
        if (err != ESP_OK)
        {
            ESP_LOGE(TAG, "Failed to perform API call: %s", esp_err_to_name(err));
            data_connection_ok = false;
            energyboxx_api_set_renew_token(true);
            s_api_backoff_wait();
            continue;
        }

        data_connection_ok = true;
        api_retry_row = 0;              //  A good round starts the backoff over

        //  Network up, credentials accepted, telemetry in hand: whatever is
        //  running does its job. If this image is on probation, that ends here.
        //  Anything less than a full round is not proof: a device that boots
        //  and shows a screen can still be unable to reach the API.
        updater_note_device_working();
        energyboxx_data_print(&data);
        if(data.community_power_result_kw < -POWER_BALANCE_DEADBAND_KW)
        {
            ESP_LOGW(TAG, "Community is importing power");
            energy_state = STATUS_VIEW_DEFICIT;
        }
        else if(data.community_power_result_kw > POWER_BALANCE_DEADBAND_KW)
        {
            ESP_LOGW(TAG, "Community is exporting power");
            energy_state = STATUS_VIEW_SURPLUS;
        }
        else
        {
            ESP_LOGW(TAG, "Community is balanced");
            energy_state = STATUS_VIEW_BALANCED;
        }

        //  Jittered as well: without it a fleet that booted together keeps
        //  asking together, once a minute, forever.
        vTaskDelay(pdMS_TO_TICKS(s_with_jitter(TELEMETRY_INTERVAL_MS)));
    }

    vTaskDelete(NULL);
}

//  --------------------------------------------------------------------------
//  Store the boot counter. A failure here costs the rapid-boot reset feature,
//  not the device, so it is reported and not fatal.

static void
    s_store_boot_count (uint32_t value)
{
    nvs_handle_t handle;

    esp_err_t err = nvs_open ("boot_count", NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        s_log_if_failed ("opening the boot counter", err);
        return;
    }

    s_log_if_failed ("writing the boot counter", nvs_set_u32 (handle, "boot_count", value));
    s_log_if_failed ("committing the boot counter", nvs_commit (handle));

    nvs_close (handle);
}

static void task_reset_boot_count(void *pvParameters)
{
    (void) pvParameters;

    vTaskDelay(pdMS_TO_TICKS(10000));

    s_store_boot_count (0);
    ESP_LOGI(TAG, "Ten seconds up, boot count reset to 0");

    vTaskDelete(NULL);
}

//  Called by the ESP-IDF startup code; declared here so the compiler sees a
//  prototype before the definition.
void app_main(void);

void app_main(void)
{
    ESP_ERROR_CHECK(wifi_storage_init());
    ESP_ERROR_CHECK(energyboxx_api_init());
    ESP_ERROR_CHECK(status_view_init());

    //  Early on purpose: the sooner the screen lights up, the sooner somebody
    //  can see that the device is alive. A screen that will not start is
    //  reported and does not stop the rest.
    s_log_if_failed("starting the display", display_init());

    //  After the display: the button on the panel only exists once the screen
    //  has started. A finger and a button do the same thing, so both are wired
    //  to the same two functions in status_view.
    display_set_input_callbacks(status_view_touched, status_view_browse);
    display_set_action_callback(status_view_action);
    wifi_prov_set_credentials_accepted_cb(s_credentials_accepted);
    s_log_if_failed("starting the buttons", input_init());

    //  Read the boot counter. Only a deliberate power cycle adds to it; see
    //  the reset reason table above for why a panic must not.
    nvs_handle_t handle;
    uint32_t boot_count = 0;

    esp_err_t err = nvs_open("boot_count", NVS_READWRITE, &handle);
    if (err == ESP_OK) {
        err = nvs_get_u32(handle, "boot_count", &boot_count);
        if (err == ESP_ERR_NVS_NOT_FOUND) {
            boot_count = 0;
        } else {
            s_log_if_failed("reading the boot counter", err);
        }
        nvs_close(handle);
    } else {
        s_log_if_failed("opening the boot counter", err);
    }

    if (s_reset_counts_as_user_request()) {
        boot_count++;
        ESP_LOGW(TAG, "Boot count: %" PRIu32, boot_count);
        s_store_boot_count(boot_count);
    } else {
        ESP_LOGW(TAG, "Boot count stays at %" PRIu32 ", this reset was not a power cycle",
                 boot_count);
    }

    if(boot_count >= 3) {
        ESP_LOGW(TAG, "Three power cycles in ten seconds, clearing credentials");
        s_log_if_failed("clearing WiFi credentials", wifi_storage_clear_credentials());
        s_log_if_failed("clearing API credentials", api_storage_clear_credentials());
        s_store_boot_count(0);
    }

    //Start a timer that resets the boot count after 10 seconds
    //This is to prevent the device from getting stuck in a boot loop if it fails to connect to WiFi
    xTaskCreate(
        task_reset_boot_count,
        "boot_count_reset_task",
        2048,
        NULL,
        5,
        NULL
    );

    if (reset_button_held_on_boot()) {
        ESP_LOGW(TAG, "WiFi reset button held, clearing credentials");
        s_log_if_failed("clearing WiFi credentials", wifi_storage_clear_credentials());
        s_log_if_failed("clearing API credentials", api_storage_clear_credentials());
    }

    ESP_ERROR_CHECK(wifi_prov_init());

    //  After the network is set up, before anything waits: this only arms a
    //  timer, and the first check is minutes away.
    s_log_if_failed("arming the update checks", updater_init());

    BaseType_t status_task_created = xTaskCreate(
        status_view_task,
        "status_view_task",
        2048,
        NULL,
        5,
        NULL
    );
    ESP_ERROR_CHECK(status_task_created == pdPASS ? ESP_OK : ESP_FAIL);

    char ssid[33] = {0};
    char password[65] = {0};

    bool wifi_provisioning_started = false;

    if (wifi_storage_load_credentials(ssid, sizeof(ssid), password, sizeof(password)) == ESP_OK) {
        s_log_if_failed("connecting to the saved network", wifi_prov_connect(ssid, password));

        if (!wifi_prov_wait_for_connection_timeout(pdMS_TO_TICKS(30000))) {
            ESP_LOGW(TAG, "Saved WiFi failed, starting provisioning");
            wifi_provisioning_started = start_provisioning_portal();
        } else if (!validate_stored_api_credentials()) {
            //  Wi-Fi works; only the API credentials are missing or refused.
            //  Do not sit in the portal waiting for somebody who may not be
            //  home: say so on the screen and carry on. Whoever walks past can
            //  open the portal from there, without filling in Wi-Fi again.
            ESP_LOGW(TAG, "API credentials missing or invalid; the screen will ask for them");
        }
    } else {
        wifi_provisioning_started = start_provisioning_portal();
    }

    if (wifi_provisioning_started) {
        wifi_prov_wait_until_completed();
    }

    xTaskCreate(
        energyboxx_task,
        "energyboxx_task",
        16384,
        NULL,
        5,
        NULL
    );
}
