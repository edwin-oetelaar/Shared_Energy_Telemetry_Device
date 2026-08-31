/*  =========================================================================
    input - the three buttons on the ESP32-S3-BOX-3
    =========================================================================
*/

#include "esp_log.h"

#include "bsp/esp-box-3.h"
#include "iot_button.h"

#include "inc/input.h"
#include "inc/status_view.h"

static const char *TAG = "[input]";

//  What each button does. One row per button, in the order the BSP hands them
//  over, so the mapping is one table rather than a chain of comparisons.
//
//  The two side buttons page back and forward. The panel button goes back to
//  following the telemetry at once, without waiting for the browse timeout.

typedef enum { ACTION_BACK = -1, ACTION_FORWARD = 1, ACTION_RESUME = 0 } action_t;

static const struct {
    const char *name;
    action_t    action;
} s_button [BSP_BUTTON_NUM] = {
    [BSP_BUTTON_CONFIG] = { "config", ACTION_BACK    },
    [BSP_BUTTON_MUTE]   = { "mute",   ACTION_FORWARD },
    [BSP_BUTTON_MAIN]   = { "main",   ACTION_RESUME  }
};


//  --------------------------------------------------------------------------
//  The action travels as the callback's user data, so one handler serves all
//  three buttons.

static void s_button_clicked(void *handle, void *user_data)
{
    (void) handle;

    action_t action = (action_t) (intptr_t) user_data;

    if (action == ACTION_RESUME) {
        status_view_resume_auto();
        return;
    }

    status_view_browse((int) action);
}


//  --------------------------------------------------------------------------

esp_err_t input_init(void)
{
    button_handle_t buttons [BSP_BUTTON_NUM] = {0};
    int count = 0;

    esp_err_t err = bsp_iot_button_create(buttons, &count, BSP_BUTTON_NUM);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Could not claim the buttons: %s", esp_err_to_name(err));
        return err;
    }

    for (int index = 0; index < count && index < BSP_BUTTON_NUM; index++) {
        if (buttons [index] == NULL) {
            continue;
        }

        err = iot_button_register_cb(buttons [index], BUTTON_SINGLE_CLICK, NULL,
                                     s_button_clicked,
                                     (void *) (intptr_t) s_button [index].action);
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "Button '%s' has no handler: %s",
                     s_button [index].name, esp_err_to_name(err));
        }
    }

    ESP_LOGI(TAG, "%d buttons ready", count);

    return ESP_OK;
}
