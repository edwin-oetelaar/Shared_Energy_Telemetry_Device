#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_log.h"
#include "esp_system.h"
#include "nvs.h"
#include "driver/gpio.h"

#include <inttypes.h>

#include "inc/api_storage.h"
#include "inc/wifi_storage.h"
#include "inc/wifi_provisioning.h"
#include "inc/wifi_web.h"
#include "inc/energyboxx_api.h"


#include "inc/status_led.h"
#include "soc/gpio_num.h"

static const char *TAG = "[main]";
energyboxx_data_t data;

#define RESET_WIFI_GPIO GPIO_NUM_17
#define RESET_HOLD_MS   3000

#define BRIGHTNESS_PERCENTAGE 10.0f // Brightness percentage for the LED rings
#define POWER_BALANCE_DEADBAND_KW 0.05f
#define API_RETRY_DELAY_MS 10000

static led_ring_t led_ring_1;
static led_ring_t led_ring_2;
static volatile bool data_connection_ok = false;


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

static void status_led_task(void *pvParameters)
{
    bool blink_on = false;

    while (true) {
        wifi_prov_state_t wifi_state = wifi_prov_get_state();

        if (wifi_state == WIFI_PROV_STATE_CONNECTED) {
            s_log_if_failed("wifi status LED", status_led_set_wifi(true));
        } else if (wifi_state == WIFI_PROV_STATE_AP_ACTIVE ||
                   wifi_state == WIFI_PROV_STATE_CONNECT_FAILED) {
            s_log_if_failed("wifi status LED", status_led_set_wifi(blink_on));
        } else {
            s_log_if_failed("wifi status LED", status_led_set_wifi(false));
        }

        bool data_ready = energyboxx_api_is_valid_credentials() && data_connection_ok;
        s_log_if_failed("data status LED", status_led_set_data(data_ready ? true : blink_on));

        blink_on = !blink_on;
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
    char client_id[128] = {0};
    char client_secret[256] = {0};

    if (api_storage_load_credentials(client_id, sizeof(client_id), client_secret, sizeof(client_secret)) != ESP_OK) {
        ESP_LOGW(TAG, "API credentials not found in storage");
        return false;
    }

    if (energyboxx_api_setup(client_id, client_secret) != ESP_OK) {
        ESP_LOGW(TAG, "Stored API credentials could not be loaded");
        return false;
    }

    if (energyboxx_api_fetch_token() != ESP_OK) {
        ESP_LOGW(TAG, "Stored API credentials are invalid");
        return false;
    }

    return true;
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
   
    while(true)
    {
        if (!wifi_prov_is_connected()) {
            data_connection_ok = false;
            s_log_if_failed("clearing the energy ring", led_ring_clear(&led_ring_2));
            vTaskDelay(pdMS_TO_TICKS(API_RETRY_DELAY_MS));
            continue;
        }

        esp_err_t err = energyboxx_api_fetch_token();
        if (err != ESP_OK)
        {
            ESP_LOGE(TAG, "Failed to fetch token: %s", esp_err_to_name(err));
            data_connection_ok = false;
            s_log_if_failed("clearing the energy ring", led_ring_clear(&led_ring_2));
            energyboxx_api_set_renew_token(true);
            vTaskDelay(pdMS_TO_TICKS(API_RETRY_DELAY_MS));
            continue;
        }

        err = energyboxx_api_get_data(&data);
        if (err != ESP_OK)
        {
            ESP_LOGE(TAG, "Failed to perform API call: %s", esp_err_to_name(err));
            data_connection_ok = false;
            s_log_if_failed("clearing the energy ring", led_ring_clear(&led_ring_2));
            ESP_LOGW(TAG, "Renewing token in 10 seconds...");
            vTaskDelay(pdMS_TO_TICKS(API_RETRY_DELAY_MS));
            energyboxx_api_set_renew_token(true);
            continue;
        }

        data_connection_ok = true;
        energyboxx_data_print(&data);
        if(data.community_power_result_kw < -POWER_BALANCE_DEADBAND_KW)
        {
            ESP_LOGW(TAG, "Community is importing power");
            led_ring_set_all(&led_ring_2, (led_rgb_t){ .r = 255, .g = 255, .b = 0 }); // Yellow
        }
        else if(data.community_power_result_kw > POWER_BALANCE_DEADBAND_KW)
        {
            ESP_LOGW(TAG, "Community is exporting power");
            led_ring_set_all(&led_ring_2, (led_rgb_t){ .r = 0, .g = 255, .b = 0 }); // Green
        }
        else
        {
            ESP_LOGW(TAG, "Community is balanced");
            led_ring_clear(&led_ring_2);
        }

        vTaskDelay(pdMS_TO_TICKS(60 * 1000)); // Wait for 60 seconds.
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

void task_reset_boot_count(void *pvParameters)
{
    vTaskDelay(pdMS_TO_TICKS(10000));

    s_store_boot_count (0);
    ESP_LOGI(TAG, "Ten seconds up, boot count reset to 0");

    vTaskDelete(NULL);
}

void app_main(void)
{
    ESP_ERROR_CHECK(wifi_storage_init());
    ESP_ERROR_CHECK(energyboxx_api_init());
    ESP_ERROR_CHECK(status_leds_init());

    //  Read the boot counter. Only a deliberate power cycle adds to it; see
    //  the reset reason table above for why a panic must not.
    nvs_handle_t nvs_handle;
    uint32_t boot_count = 0;

    esp_err_t err = nvs_open("boot_count", NVS_READWRITE, &nvs_handle);
    if (err == ESP_OK) {
        err = nvs_get_u32(nvs_handle, "boot_count", &boot_count);
        if (err == ESP_ERR_NVS_NOT_FOUND) {
            boot_count = 0;
        } else {
            s_log_if_failed("reading the boot counter", err);
        }
        nvs_close(nvs_handle);
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

    //  A ring that will not initialise leaves the device without its display,
    //  but it can still connect, fetch telemetry and be reprovisioned. Rebooting
    //  over it would only take those away too.
    s_log_if_failed("initialising LED ring 1", led_ring_init(&led_ring_1, GPIO_NUM_1));
    s_log_if_failed("initialising LED ring 2", led_ring_init(&led_ring_2, GPIO_NUM_2));
    s_log_if_failed("setting ring 1 brightness",
                    led_ring_set_brightness(&led_ring_1, BRIGHTNESS_PERCENTAGE));
    s_log_if_failed("setting ring 2 brightness",
                    led_ring_set_brightness(&led_ring_2, BRIGHTNESS_PERCENTAGE));

    ESP_ERROR_CHECK(wifi_prov_init());

    BaseType_t status_task_created = xTaskCreate(
        status_led_task,
        "status_led_task",
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
            ESP_LOGW(TAG, "Stored API credentials are missing or invalid, starting AP provisioning");
            wifi_provisioning_started = start_provisioning_portal();
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
