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

//  Whether display_init () succeeded. Callers that draw should check this
//  first; drawing without a screen is not an error worth reporting every time.
bool display_is_ready(void);

#ifdef __cplusplus
}
#endif

#endif
