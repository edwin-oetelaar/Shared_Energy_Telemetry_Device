/*  =========================================================================
    day_ahead_view - the price screen: a day's prices as a graph

    Draws in the top 180 pixels; the strip below belongs to the browse arrows
    and the action button, as on every other screen.
    =========================================================================
*/

#ifndef DAY_AHEAD_VIEW_H_INCLUDED
#define DAY_AHEAD_VIEW_H_INCLUDED

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

//  Draw the price screen from the current cache.
esp_err_t
    day_ahead_view_show (void);

//  Take the price screen away, so another view can use the screen.
void
    day_ahead_view_hide (void);

//  Switch between today and tomorrow.
void
    day_ahead_view_toggle (void);

//  The text for the action button: the day the button switches to.
const char *
    day_ahead_view_action_label (void);

#ifdef __cplusplus
}
#endif

#endif
