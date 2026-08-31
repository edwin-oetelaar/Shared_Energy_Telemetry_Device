/*  =========================================================================
    display - the screen of the ESP32-S3-BOX-3

    Phase 2 of docs/PLAN-box3.md: bring the screen up and show the bring-up
    image. Later phases draw the device state here; see status_view.h for the
    states they will draw.
    =========================================================================
*/

#ifndef DISPLAY_H_INCLUDED
#define DISPLAY_H_INCLUDED

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

//  Start the panel, the backlight and LVGL, and show the bring-up image.
//  Returns an error rather than aborting: a device that cannot draw can still
//  connect, fetch telemetry and be reprovisioned, and those are worth keeping.
esp_err_t display_init(void);

//  Show the bring-up image, full screen.
esp_err_t display_show_bringup(void);

//  Show a screen with a title, an optional line under it, and an optional QR
//  code. Pass NULL for detail or qr_text to leave that part out.
//
//  The colour is plain 0xRRGGBB so that callers do not have to know about
//  LVGL. The text colour is chosen from the background's brightness, so a
//  caller cannot pick a pair that nobody can read.
esp_err_t display_show_status(const char *title,
                              const char *detail,
                              const char *qr_text,
                              uint32_t background_rgb);

//  Called when somebody touches the screen anywhere, and when they press one
//  of the two arrows. The direction is -1 for back and +1 for forward.
typedef void (*display_tap_cb_t)(void);
typedef void (*display_browse_cb_t)(int direction);

//  Register who hears about touches. Both may be NULL.
void display_set_input_callbacks(display_tap_cb_t on_tap, display_browse_cb_t on_browse);

//  Show or hide the two arrows. They are hidden while the device is simply
//  reporting, so the screen stays uncluttered, and appear once somebody starts
//  browsing.
esp_err_t display_show_browse_controls(bool visible);

//  Called when somebody presses the button on the report screen.
typedef void (*display_action_cb_t)(void);
void display_set_action_callback(display_action_cb_t on_action);

//  A screen with a heading and several lines under it, for telling somebody
//  what the device knows about itself. Pass NULL for action_label to leave the
//  button off.
esp_err_t display_show_report(const char *title,
                              const char *body,
                              const char *action_label,
                              uint32_t background_rgb);

//  Mark the screen as a preview rather than a live reading. Without this a
//  view somebody browsed to looks exactly like a measurement, which is a way
//  to leave a number on the wall that was never measured.
esp_err_t display_show_preview_marker(bool visible);

//  Whether display_init () succeeded. Callers that draw should check this
//  first; drawing without a screen is not an error worth reporting every time.
bool display_is_ready(void);

#ifdef __cplusplus
}
#endif

#endif
