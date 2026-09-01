/*  =========================================================================
    updater - firmware over the air

    Fetches a newer build from GitHub Releases and installs it in the spare
    OTA slot. A fresh image is on probation until the device has connected and
    fetched telemetry once; if it never does, the bootloader falls back to the
    firmware that was there before.

    Follows the pattern of wifi_provisioning: one state machine, one function
    that may change the state, every transition logged.
    =========================================================================
*/

#ifndef UPDATER_H_INCLUDED
#define UPDATER_H_INCLUDED

#include <stdbool.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    UPDATER_IDLE = 0,       //  Nothing going on
    UPDATER_CHECKING,       //  Asking whether there is something newer
    UPDATER_DOWNLOADING,    //  Writing a new image into the spare slot
    UPDATER_READY,          //  Installed; the device restarts into it
    UPDATER_FAILED,         //  The last attempt did not finish
    UPDATER_STATE_COUNT
} updater_state_t;

//  Start the updater. Arms the hourly check; the first one happens a few
//  minutes after start-up, so a device that keeps rebooting does not spend its
//  life downloading.
esp_err_t updater_init(void);

//  Look for a newer build now. Safe to call at any time; it is ignored while a
//  check or a download is already running.
void updater_check_now(void);

//  Where the updater is, and how far a download has come, for the screen.
updater_state_t updater_get_state(void);
int updater_progress_percent(void);

//  Tell the updater the device is doing its job. The first time this is called
//  after a fresh image has started, that image is marked as good and the
//  rollback is cancelled.
void updater_note_device_working(void);

//  Whether the running image is still on probation.
bool updater_image_on_probation(void);

#ifdef __cplusplus
}
#endif

#endif
