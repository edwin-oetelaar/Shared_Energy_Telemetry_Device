/*  =========================================================================
    display - the screen of the ESP32-S3-BOX-3
    =========================================================================
*/

#include <assert.h>
#include <inttypes.h>
#include <stdint.h>
#include <string.h>
#include <stdbool.h>

#include "esp_app_desc.h"
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

//  Built once in display_init () and reused. Rebuilding the objects on every
//  state change would churn the heap for no gain.
static lv_obj_t *s_image = NULL;
static lv_obj_t *s_version = NULL;
static lv_obj_t *s_back = NULL;
static lv_obj_t *s_forward = NULL;

static display_tap_cb_t s_on_tap = NULL;
static display_browse_cb_t s_on_browse = NULL;
static lv_obj_t *s_title = NULL;
static lv_obj_t *s_detail = NULL;
static lv_obj_t *s_qr = NULL;

static bool s_ready = false;


//  --------------------------------------------------------------------------

bool display_is_ready(void)
{
    return s_ready;
}


//  --------------------------------------------------------------------------
//  A touch on the background, rather than on one of the arrows.

static void s_screen_clicked(lv_event_t *event)
{
    (void) event;

    if (s_on_tap != NULL) {
        s_on_tap();
    }
}


//  --------------------------------------------------------------------------
//  The arrows carry their direction as their user data, so one handler serves
//  both and there is no second place to keep them in step.

static void s_arrow_clicked(lv_event_t *event)
{
    if (s_on_browse != NULL) {
        s_on_browse((int) (intptr_t) lv_event_get_user_data(event));
    }
}


static lv_obj_t *s_make_arrow(lv_obj_t *parent, const char *symbol,
                              lv_align_t align, int32_t x, int32_t y, int direction)
{
    lv_obj_t *button = lv_button_create(parent);

    lv_obj_set_size(button, 56, 44);
    lv_obj_align(button, align, x, y);
    lv_obj_set_style_bg_color(button, lv_color_hex(0x101410), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(button, LV_OPA_60, LV_PART_MAIN);
    lv_obj_set_style_radius(button, 6, LV_PART_MAIN);
    lv_obj_add_event_cb(button, s_arrow_clicked, LV_EVENT_CLICKED, (void *) (intptr_t) direction);
    lv_obj_add_flag(button, LV_OBJ_FLAG_HIDDEN);

    lv_obj_t *label = lv_label_create(button);
    lv_label_set_text(label, symbol);
    lv_obj_set_style_text_color(label, lv_color_hex(0xF2F5F0), LV_PART_MAIN);
    lv_obj_center(label);

    return button;
}


//  --------------------------------------------------------------------------

void display_set_input_callbacks(display_tap_cb_t on_tap, display_browse_cb_t on_browse)
{
    s_on_tap = on_tap;
    s_on_browse = on_browse;
}


//  --------------------------------------------------------------------------

esp_err_t display_show_browse_controls(bool visible)
{
    if (!s_ready) {
        return ESP_ERR_INVALID_STATE;
    }

    if (!bsp_display_lock(1000)) {
        return ESP_ERR_TIMEOUT;
    }

    if (visible) {
        lv_obj_remove_flag(s_back, LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_flag(s_forward, LV_OBJ_FLAG_HIDDEN);
    }
    else {
        lv_obj_add_flag(s_back, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(s_forward, LV_OBJ_FLAG_HIDDEN);
    }

    bsp_display_unlock();

    return ESP_OK;
}


//  --------------------------------------------------------------------------
//  Build the two things the screen can show: the image and one line of text.
//  Only one of them is visible at a time. LVGL is not thread safe, so every
//  call into it sits between bsp_display_lock () and unlock ().

static esp_err_t s_build_screen(void)
{
    s_bringup.header.magic  = LV_IMAGE_HEADER_MAGIC;
    s_bringup.header.cf     = LV_COLOR_FORMAT_RGB565;
    s_bringup.header.w      = BRINGUP_WIDTH;
    s_bringup.header.h      = BRINGUP_HEIGHT;
    s_bringup.header.stride = BRINGUP_WIDTH * 2;
    s_bringup.data          = bringup_start;
    //  The linker places these two symbols around one embedded file, so the
    //  subtraction is its length. cppcheck cannot see that and takes them for
    //  pointers into unrelated objects.
    //  cppcheck-suppress comparePointers
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
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, LV_PART_MAIN);

    s_image = lv_image_create(screen);
    lv_image_set_src(s_image, &s_bringup);
    lv_obj_center(s_image);

    s_title = lv_label_create(screen);
    lv_obj_set_style_text_font(s_title, &lv_font_montserrat_28, LV_PART_MAIN);
    lv_label_set_long_mode(s_title, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(s_title, BRINGUP_WIDTH - 32);
    lv_obj_set_style_text_align(s_title, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_label_set_text(s_title, "");
    lv_obj_add_flag(s_title, LV_OBJ_FLAG_HIDDEN);

    s_detail = lv_label_create(screen);
    lv_label_set_long_mode(s_detail, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(s_detail, BRINGUP_WIDTH - 32);
    lv_obj_set_style_text_align(s_detail, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_label_set_text(s_detail, "");
    lv_obj_add_flag(s_detail, LV_OBJ_FLAG_HIDDEN);

    s_qr = lv_qrcode_create(screen);
    lv_qrcode_set_size(s_qr, 116);
    lv_obj_add_flag(s_qr, LV_OBJ_FLAG_HIDDEN);

    //  The firmware version, over the bring-up image. Drawn at run time rather
    //  than baked into the picture, because the version changes with every
    //  build and the picture does not. It sits on a dark chip so that it stays
    //  readable whatever the artwork does underneath it.
    const esp_app_desc_t *app = esp_app_get_description();

    s_version = lv_label_create(screen);
    lv_label_set_text(s_version, app->version);
    lv_obj_set_style_text_color(s_version, lv_color_hex(0xF2F5F0), LV_PART_MAIN);
    lv_obj_set_style_bg_color(s_version, lv_color_hex(0x101410), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(s_version, LV_OPA_70, LV_PART_MAIN);
    lv_obj_set_style_pad_hor(s_version, 6, LV_PART_MAIN);
    lv_obj_set_style_pad_ver(s_version, 3, LV_PART_MAIN);
    lv_obj_set_style_radius(s_version, 4, LV_PART_MAIN);
    lv_obj_align(s_version, LV_ALIGN_BOTTOM_RIGHT, -6, -6);
    lv_obj_add_flag(s_version, LV_OBJ_FLAG_HIDDEN);

    ESP_LOGI(TAG, "Firmware version on screen: %s", app->version);

    //  A touch anywhere counts. The arrows sit on top of that and stop the
    //  event from also reading as a tap on the background.
    lv_obj_add_flag(screen, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(screen, s_screen_clicked, LV_EVENT_CLICKED, NULL);

    s_back = s_make_arrow(screen, LV_SYMBOL_LEFT, LV_ALIGN_BOTTOM_LEFT, 8, -8, -1);
    s_forward = s_make_arrow(screen, LV_SYMBOL_RIGHT, LV_ALIGN_BOTTOM_RIGHT, -8, -8, +1);

    bsp_display_unlock();

    return ESP_OK;
}


//  --------------------------------------------------------------------------

esp_err_t display_show_bringup(void)
{
    if (!s_ready) {
        return ESP_ERR_INVALID_STATE;
    }

    if (!bsp_display_lock(1000)) {
        return ESP_ERR_TIMEOUT;
    }

    lv_obj_set_style_bg_color(lv_screen_active(), lv_color_black(), LV_PART_MAIN);
    lv_obj_remove_flag(s_image, LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(s_version, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(s_title, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(s_detail, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(s_qr, LV_OBJ_FLAG_HIDDEN);

    bsp_display_unlock();

    return ESP_OK;
}


//  --------------------------------------------------------------------------
//  Dark text on a light background and light text on a dark one. Deciding this
//  here means a caller cannot pick a combination that nobody can read.

static lv_color_t s_readable_text_colour(uint32_t background_rgb)
{
    uint32_t red   = (background_rgb >> 16) & 0xFF;
    uint32_t green = (background_rgb >> 8) & 0xFF;
    uint32_t blue  = background_rgb & 0xFF;

    //  Rough perceived brightness: green counts most, blue least.
    uint32_t brightness = (red * 2 + green * 5 + blue) / 8;

    return brightness > 128 ? lv_color_hex(0x101410) : lv_color_hex(0xF2F5F0);
}


esp_err_t display_show_status(const char *title,
                              const char *detail,
                              const char *qr_text,
                              uint32_t background_rgb)
{
    assert (title);             //  Caller's contract; detail and qr_text may be NULL

    if (!s_ready) {
        return ESP_ERR_INVALID_STATE;
    }

    if (!bsp_display_lock(1000)) {
        return ESP_ERR_TIMEOUT;
    }

    lv_color_t ink = s_readable_text_colour(background_rgb);

    lv_obj_add_flag(s_image, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(s_version, LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_style_bg_color(lv_screen_active(), lv_color_hex(background_rgb), LV_PART_MAIN);

    lv_obj_set_style_text_color(s_title, ink, LV_PART_MAIN);
    lv_label_set_text(s_title, title);
    lv_obj_remove_flag(s_title, LV_OBJ_FLAG_HIDDEN);

    if (detail != NULL && detail [0] != '\0') {
        lv_obj_set_style_text_color(s_detail, ink, LV_PART_MAIN);
        lv_label_set_text(s_detail, detail);
        lv_obj_remove_flag(s_detail, LV_OBJ_FLAG_HIDDEN);
    }
    else {
        lv_obj_add_flag(s_detail, LV_OBJ_FLAG_HIDDEN);
    }

    if (qr_text != NULL && qr_text [0] != '\0') {
        //  A QR needs a light background of its own to stay scannable, whatever
        //  colour the screen is.
        lv_qrcode_set_dark_color(s_qr, lv_color_hex(0x101410));
        lv_qrcode_set_light_color(s_qr, lv_color_hex(0xFFFFFF));
        lv_qrcode_update(s_qr, qr_text, strlen(qr_text));
        lv_obj_set_style_border_width(s_qr, 4, LV_PART_MAIN);
        lv_obj_set_style_border_color(s_qr, lv_color_hex(0xFFFFFF), LV_PART_MAIN);
        lv_obj_remove_flag(s_qr, LV_OBJ_FLAG_HIDDEN);
    }
    else {
        lv_obj_add_flag(s_qr, LV_OBJ_FLAG_HIDDEN);
    }

    //  Lay the visible parts out from the top down, so a screen without a QR
    //  or without a detail line still looks deliberate rather than lopsided.
    bool has_qr = qr_text != NULL && qr_text [0] != '\0';
    bool has_detail = detail != NULL && detail [0] != '\0';

    if (has_qr) {
        lv_obj_align(s_title, LV_ALIGN_TOP_MID, 0, 12);
        lv_obj_align(s_qr, LV_ALIGN_TOP_MID, 0, 56);
        lv_obj_align(s_detail, LV_ALIGN_BOTTOM_MID, 0, -14);
    }
    else if (has_detail) {
        lv_obj_align(s_title, LV_ALIGN_CENTER, 0, -18);
        lv_obj_align(s_detail, LV_ALIGN_CENTER, 0, 24);
    }
    else {
        lv_obj_center(s_title);
    }

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

    err = s_build_screen();
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
