/*  =========================================================================
    status_view - what the device is showing about itself

    One narrow interface between the logic and however the device presents
    itself. The logic decides which state it is in; the view decides how that
    looks. Today the view only writes to the log. On the ESP32-S3-BOX-3 it
    becomes the screen, and nothing above this line has to change for that.
    =========================================================================
*/

#ifndef STATUS_VIEW_H_INCLUDED
#define STATUS_VIEW_H_INCLUDED

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

//  Every state the device can be in, from the viewer's side. Keep this list
//  and the table in status_view.c in step; the table is indexed by this enum.

typedef enum {
    STATUS_VIEW_STARTING = 0,   //  Powered on, nothing decided yet
    STATUS_VIEW_PROVISIONING,   //  Waiting for someone to fill in the portal
    STATUS_VIEW_CONNECTING,     //  Credentials known, network not up
    STATUS_VIEW_CONNECT_FAILED, //  The network refused us, or is not there yet
    STATUS_VIEW_SURPLUS,        //  The community has energy to share
    STATUS_VIEW_DEFICIT,        //  The community has to buy energy
    STATUS_VIEW_BALANCED,       //  Supply and demand match
    STATUS_VIEW_KEYS_NEEDED,    //  Online, but nobody has given us API keys yet
    STATUS_VIEW_NO_DATA,        //  Connected or not, but no fresh telemetry
    STATUS_VIEW_REPORT,         //  What the device knows about itself
    STATUS_VIEW_SETUP_DONE,     //  Credentials just accepted; setup is finished
    STATUS_VIEW_STATE_COUNT     //  Not a state; the size of the table
} status_view_state_t;

//  Prepare the view. Call once, before status_view_show ().
esp_err_t status_view_init(void);

//  Show a state. Repeating the current state costs nothing and is silent, so
//  callers may call this as often as they like. Asserts that the state is a
//  real one, because passing something else is a programming error.
esp_err_t status_view_show(status_view_state_t state);

//  Step through the views by hand. Direction is -1 for back and +1 for
//  forward. Browsing never lasts: after a while without input the device goes
//  back to showing what the telemetry says, because that is its job.
void status_view_browse(int direction);

//  Stop browsing at once and follow the telemetry again, without waiting for
//  the timeout.
void status_view_resume_auto(void);

//  Somebody pressed the button on the report screen: open the portal so they
//  can enter API credentials, without touching the stored Wi-Fi settings.
void status_view_open_portal(void);

//  Somebody touched the screen without choosing a direction. Starts browsing
//  at the view already on screen, and puts the arrows up.
void status_view_touched(void);

//  Put a state on the screen for a while and then go back to following the
//  telemetry. For saying something that happened rather than something that
//  is: "setup finished" is news, not a condition.
void status_view_announce(status_view_state_t state, int milliseconds);

//  The name of a state, for logging and for the screen later.
const char *status_view_name(status_view_state_t state);

#ifdef __cplusplus
}
#endif

#endif
