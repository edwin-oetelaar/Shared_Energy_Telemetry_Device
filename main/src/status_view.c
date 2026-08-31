/*  =========================================================================
    status_view - log-only implementation

    Phase 1 of the move to the ESP32-S3-BOX-3 (see docs/PLAN-box3.md). The
    board runs everything it used to, and reports its state to the log. Phase 3
    replaces the body of status_view_show () with the screen.
    =========================================================================
*/

#include <assert.h>
#include <stdint.h>

#include "esp_log.h"

#include "inc/display.h"
#include "inc/status_view.h"

static const char *TAG = "[status_view]";

//  One row per state. The table is indexed by status_view_state_t, so a state
//  added to the enum without a row here shows up immediately as a null name
//  rather than as silently wrong behaviour.
//
//  `name` goes in the log and stays English, like the rest of the code.
//  `label` goes on the screen and is Dutch, because the people looking at this
//  device live on the Wilhelminaweg.
//
//  The bring-up image has its own row rather than a colour: while the device is
//  starting, the owl is what people see.

typedef enum { PAINT_IMAGE = 0, PAINT_COLOUR } paint_t;

static const struct {
    const char *name;
    const char *meaning;
    const char *label;
    paint_t     paint;
    uint32_t    rgb;
} s_view [STATUS_VIEW_STATE_COUNT] = {
    [STATUS_VIEW_STARTING]     = { "starting",     "powered on",
                                   "",                  PAINT_IMAGE,  0x000000 },
    [STATUS_VIEW_PROVISIONING] = { "provisioning", "waiting for the portal to be used",
                                   "Instellen",         PAINT_COLOUR, 0x1E5A8A },
    [STATUS_VIEW_CONNECTING]   = { "connecting",   "credentials known, network not up",
                                   "Verbinden",         PAINT_COLOUR, 0x334A55 },
    [STATUS_VIEW_SURPLUS]      = { "surplus",      "energy available to share",
                                   "Energie over",      PAINT_COLOUR, 0x1F9E4B },
    [STATUS_VIEW_DEFICIT]      = { "deficit",      "energy has to be bought",
                                   "Energie inkopen",   PAINT_COLOUR, 0xE0A21B },
    [STATUS_VIEW_BALANCED]     = { "balanced",     "supply and demand match",
                                   "In balans",         PAINT_COLOUR, 0x243028 },
    [STATUS_VIEW_NO_DATA]      = { "no-data",      "no fresh telemetry",
                                   "Geen gegevens",     PAINT_COLOUR, 0x5A5A5A }
};

static status_view_state_t s_current = STATUS_VIEW_STARTING;
static bool s_shown = false;


//  --------------------------------------------------------------------------

const char *status_view_name(status_view_state_t state)
{
    if (state >= STATUS_VIEW_STATE_COUNT || s_view [state].name == NULL) {
        return "unknown";
    }

    return s_view [state].name;
}


//  --------------------------------------------------------------------------

esp_err_t status_view_init(void)
{
    //  A row without a name or label means the enum grew and this table did
    //  not. Only the bring-up row may have an empty label; it shows a picture.
    for (size_t row = 0; row < STATUS_VIEW_STATE_COUNT; row++) {
        assert (s_view [row].name);
        assert (s_view [row].label);
        assert (s_view [row].paint == PAINT_IMAGE || s_view [row].label [0] != '\0');
    }

    s_current = STATUS_VIEW_STARTING;
    s_shown = false;

    return ESP_OK;
}


//  --------------------------------------------------------------------------
//  Only a change is worth saying out loud. Callers can therefore report their
//  state on every pass of their loop without filling the log.

esp_err_t status_view_show(status_view_state_t state)
{
    assert (state < STATUS_VIEW_STATE_COUNT);   //  Caller's contract

    if (s_shown && state == s_current) {
        return ESP_OK;
    }

    ESP_LOGI(TAG, "%s - %s", s_view [state].name, s_view [state].meaning);

    s_current = state;
    s_shown = true;

    //  No screen is not an error worth reporting on every state change. The
    //  device keeps working; display_init () already said so once.
    if (!display_is_ready()) {
        return ESP_OK;
    }

    esp_err_t err = s_view [state].paint == PAINT_IMAGE
                  ? display_show_bringup()
                  : display_show_status(s_view [state].label, s_view [state].rgb);

    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Could not draw '%s': %s", s_view [state].name, esp_err_to_name(err));
    }

    return err;
}
