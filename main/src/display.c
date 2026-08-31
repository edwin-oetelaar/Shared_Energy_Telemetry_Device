/*  =========================================================================
    display - the screen of the ESP32-S3-BOX-3
    =========================================================================
*/

#include <inttypes.h>
#include <stdbool.h>

#include "esp_log.h"

#include "bsp/esp-box-3.h"
#include "lvgl.h"

#include "inc/display.h"

static const char *TAG = "[display]";

//  The bring-up image, converted from assets/energy-owl-bringup.png by
//  tools/png_to_lvgl.py and embedded by main/CMakeLists.txt. It is raw RGB565
//  in the screen's own size, so LVGL draws it without decoding or scaling.

extern const uint8_t bringup_start[] asm("_binary_energy_owl_bringup_bin_start");
extern const uint8_t bringup_end[]   asm("_binary_energy_owl_bringup_bin_end");

#define BRINGUP_WIDTH   320
#define BRINGUP_HEIGHT  240

//  Filled in at start-up rather than at compile time, because the data size
//  comes from the linker symbols above.
static lv_image_dsc_t s_bringup;

static bool s_ready = false;


//  --------------------------------------------------------------------------

bool display_is_ready(void)
{
    return s_ready;
}


//  --------------------------------------------------------------------------
//  Put the bring-up image on the active screen. LVGL is not thread safe, so
//  every call into it sits between bsp_display_lock () and unlock ().

static esp_err_t s_show_bringup(void)
{
    s_bringup.header.magic  = LV_IMAGE_HEADER_MAGIC;
    s_bringup.header.cf     = LV_COLOR_FORMAT_RGB565;
    s_bringup.header.w      = BRINGUP_WIDTH;
    s_bringup.header.h      = BRINGUP_HEIGHT;
    s_bringup.header.stride = BRINGUP_WIDTH * 2;
    s_bringup.data          = bringup_start;
    s_bringup.data_size     = (uint32_t) (bringup_end - bringup_start);

    if (s_bringup.data_size != BRINGUP_WIDTH * BRINGUP_HEIGHT * 2) {
        ESP_LOGE(TAG, "Bring-up image is %" PRIu32 " bytes, expected %d",
                 s_bringup.data_size, BRINGUP_WIDTH * BRINGUP_HEIGHT * 2);
        return ESP_ERR_INVALID_SIZE;
    }

    if (!bsp_display_lock(1000)) {
        ESP_LOGE(TAG, "LVGL did not release its lock in time");
        return ESP_ERR_TIMEOUT;
    }

    lv_obj_t *screen = lv_screen_active();
    lv_obj_set_style_bg_color(screen, lv_color_black(), LV_PART_MAIN);

    lv_obj_t *image = lv_image_create(screen);
    lv_image_set_src(image, &s_bringup);
    lv_obj_center(image);

    bsp_display_unlock();

    return ESP_OK;
}


//  --------------------------------------------------------------------------

esp_err_t display_init(void)
{
    esp_err_t err = bsp_i2c_init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "I2C bus failed to start: %s", esp_err_to_name(err));
        return err;
    }

    //  Starts the panel and the LVGL task. Returns NULL on failure.
    if (bsp_display_start() == NULL) {
        ESP_LOGE(TAG, "Display failed to start");
        return ESP_FAIL;
    }

    err = s_show_bringup();
    if (err != ESP_OK) {
        return err;
    }

    //  Backlight last, so the first thing anybody sees is the image and not a
    //  lit but empty panel.
    err = bsp_display_backlight_on();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Backlight failed to switch on: %s", esp_err_to_name(err));
        return err;
    }

    s_ready = true;
    ESP_LOGI(TAG, "Screen up, showing the bring-up image");

    return ESP_OK;
}
