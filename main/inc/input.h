/*  =========================================================================
    input - the three buttons on the ESP32-S3-BOX-3

    The touch screen is handled by LVGL through the BSP; this covers only the
    physical buttons. Both end up calling the same functions in status_view, so
    a finger and a button do the same thing.
    =========================================================================
*/

#ifndef INPUT_H_INCLUDED
#define INPUT_H_INCLUDED

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

//  Claim the buttons and wire them up. Call after display_init (), because the
//  button on the panel is only available once the display has started.
//  Returns an error rather than aborting: a device whose buttons do not work
//  still shows the community state, and that is what it is for.
esp_err_t input_init(void);

#ifdef __cplusplus
}
#endif

#endif
