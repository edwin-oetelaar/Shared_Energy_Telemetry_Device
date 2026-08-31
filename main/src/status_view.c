/*  =========================================================================
    status_view - log-only implementation

    Phase 1 of the move to the ESP32-S3-BOX-3 (see docs/PLAN-box3.md). The
    board runs everything it used to, and reports its state to the log. Phase 3
    replaces the body of status_view_show () with the screen.
    =========================================================================
*/

#include <assert.h>

#include "esp_log.h"

#include "inc/status_view.h"

static const char *TAG = "[status_view]";

//  One row per state. The table is indexed by status_view_state_t, so a state
//  added to the enum without a row here shows up immediately as a null name
//  rather than as silently wrong behaviour.

static const struct {
    const char *name;
    const char *meaning;
} s_view [STATUS_VIEW_STATE_COUNT] = {
    [STATUS_VIEW_STARTING]     = { "starting",     "powered on"                        },
    [STATUS_VIEW_PROVISIONING] = { "provisioning", "waiting for the portal to be used" },
    [STATUS_VIEW_CONNECTING]   = { "connecting",   "credentials known, network not up" },
    [STATUS_VIEW_SURPLUS]      = { "surplus",      "energy available to share"         },
    [STATUS_VIEW_DEFICIT]      = { "deficit",      "energy has to be bought"           },
    [STATUS_VIEW_BALANCED]     = { "balanced",     "supply and demand match"           },
    [STATUS_VIEW_NO_DATA]      = { "no-data",      "no fresh telemetry"                }
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
    //  A row without a name means the enum grew and this table did not.
    for (size_t row = 0; row < STATUS_VIEW_STATE_COUNT; row++) {
        assert (s_view [row].name);
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

    return ESP_OK;
}
