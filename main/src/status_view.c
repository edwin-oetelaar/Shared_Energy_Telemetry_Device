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
#include "esp_timer.h"

#include <stdio.h>

#include "inc/display.h"
#include "inc/status_view.h"
#include "inc/wifi_provisioning.h"

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
    [STATUS_VIEW_CONNECT_FAILED] = { "connect-failed", "the network refused us or is absent",
                                   "Geen verbinding",   PAINT_COLOUR, 0x8A3A2A },
    [STATUS_VIEW_SURPLUS]      = { "surplus",      "energy available to share",
                                   "Energie over",      PAINT_COLOUR, 0x1F9E4B },
    [STATUS_VIEW_DEFICIT]      = { "deficit",      "energy has to be bought",
                                   "Energie inkopen",   PAINT_COLOUR, 0xE0A21B },
    [STATUS_VIEW_BALANCED]     = { "balanced",     "supply and demand match",
                                   "In balans",         PAINT_COLOUR, 0x243028 },
    [STATUS_VIEW_NO_DATA]      = { "no-data",      "no fresh telemetry",
                                   "Geen gegevens",     PAINT_COLOUR, 0x5A5A5A }
};

//  The bring-up image is how somebody learns what this product is. A device
//  that connects quickly would otherwise show it for well under a second,
//  which reads as a flicker rather than as a logo. Hold it at least this long
//  after it first appears.
#define BRINGUP_MIN_VISIBLE_MS  1000

static status_view_state_t s_current = STATUS_VIEW_STARTING;
static bool s_shown = false;
static int64_t s_bringup_since_us = 0;

//  Built when a state is drawn, so the screen never holds a pointer into
//  something that has since changed.
static char s_detail [64];
static char s_qr [96];


//  --------------------------------------------------------------------------
//  The line under the title. Only two states have something worth saying
//  there, and both of them name a network.

static const char *s_detail_for(status_view_state_t state)
{
    switch (state) {
        case STATUS_VIEW_PROVISIONING:
            snprintf(s_detail, sizeof(s_detail), "Wifi: %s", wifi_prov_ap_ssid());
            return s_detail;

        case STATUS_VIEW_CONNECTING:
        case STATUS_VIEW_CONNECT_FAILED:
            if (wifi_prov_current_ssid() [0] == '\0') {
                return NULL;
            }
            snprintf(s_detail, sizeof(s_detail), "%s", wifi_prov_current_ssid());
            return s_detail;

        default:
            return NULL;
    }
}


//  --------------------------------------------------------------------------
//  A QR code only helps in one place: joining the device's own access point.
//  The WIFI: form is what phone cameras understand; T:nopass says the network
//  is open, which is what wifi_prov_start_ap () sets up.

static const char *s_qr_for(status_view_state_t state)
{
    if (state != STATUS_VIEW_PROVISIONING) {
        return NULL;
    }

    snprintf(s_qr, sizeof(s_qr), "WIFI:S:%s;T:nopass;;", wifi_prov_ap_ssid());

    return s_qr;
}


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
    s_bringup_since_us = 0;

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

    //  Refuse to leave the bring-up image too soon. The caller polls, so the
    //  state it wants is simply shown on one of its next passes; nothing is
    //  lost by saying "not yet".
    if (s_shown
    &&  s_current == STATUS_VIEW_STARTING
    &&  s_bringup_since_us != 0
    &&  esp_timer_get_time() - s_bringup_since_us < BRINGUP_MIN_VISIBLE_MS * 1000) {
        return ESP_OK;
    }

    ESP_LOGI(TAG, "%s - %s", s_view [state].name, s_view [state].meaning);

    s_current = state;
    s_shown = true;

    if (state == STATUS_VIEW_STARTING && s_bringup_since_us == 0) {
        s_bringup_since_us = esp_timer_get_time();
    }

    //  No screen is not an error worth reporting on every state change. The
    //  device keeps working; display_init () already said so once.
    if (!display_is_ready()) {
        return ESP_OK;
    }

    esp_err_t err = s_view [state].paint == PAINT_IMAGE
                  ? display_show_bringup()
                  : display_show_status(s_view [state].label,
                                        s_detail_for(state),
                                        s_qr_for(state),
                                        s_view [state].rgb);

    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Could not draw '%s': %s", s_view [state].name, esp_err_to_name(err));
    }

    return err;
}
