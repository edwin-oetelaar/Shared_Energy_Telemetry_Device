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
#include <string.h>

#include "inc/energyboxx_api.h"

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

typedef enum { PAINT_IMAGE = 0, PAINT_COLOUR, PAINT_REPORT } paint_t;

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
    [STATUS_VIEW_KEYS_NEEDED]  = { "keys-needed",  "no API credentials stored",
                                   "Sleutels nodig",    PAINT_COLOUR, 0x8A5A1A },
    [STATUS_VIEW_NO_DATA]      = { "no-data",      "no fresh telemetry",
                                   "Geen gegevens",     PAINT_COLOUR, 0x5A5A5A },
    [STATUS_VIEW_REPORT]       = { "report",       "what the device knows about itself",
                                   "Status",            PAINT_REPORT, 0x243028 }
};

//  The bring-up image is how somebody learns what this product is. A device
//  that connects quickly would otherwise show it for well under a second,
//  which reads as a flicker rather than as a logo. Hold it at least this long
//  after it first appears.
#define BRINGUP_MIN_VISIBLE_MS  1000

//  How long the device keeps showing what somebody browsed to. Short enough
//  that a stray touch does not leave a wrong reading on the wall for long, and
//  long enough to look at something on purpose.
#define BROWSE_TIMEOUT_MS  15000

//  The views worth paging through by hand. Provisioning and connecting are not
//  in the list: they say what the device is doing right now, and browsing to
//  them would be a lie.
static const status_view_state_t s_browsable [] = {
    STATUS_VIEW_REPORT,
    STATUS_VIEW_STARTING,
    STATUS_VIEW_SURPLUS,
    STATUS_VIEW_DEFICIT,
    STATUS_VIEW_BALANCED,
    STATUS_VIEW_NO_DATA
};

#define BROWSABLE_COUNT  (sizeof (s_browsable) / sizeof (s_browsable [0]))

static status_view_state_t s_current = STATUS_VIEW_STARTING;
static bool s_shown = false;
static int64_t s_bringup_since_us = 0;

//  What the telemetry last said, kept while somebody is browsing so the device
//  can go straight back to it.
static status_view_state_t s_auto_state = STATUS_VIEW_STARTING;
static bool s_browsing = false;
static size_t s_browse_row = 0;
static int64_t s_browse_until_us = 0;

//  Built when a state is drawn, so the screen never holds a pointer into
//  something that has since changed.
static char s_detail [64];
static char s_qr [96];


//  --------------------------------------------------------------------------
//  What the device knows about itself, in the words of somebody standing in
//  front of it. Every line answers a question that until now could only be
//  answered by reading a serial log.

static char s_report [256];

static void s_build_report(void)
{
    char ip [16];
    wifi_prov_ip_string(ip, sizeof(ip));

    const char *network = wifi_prov_current_ssid();
    int token_left = energyboxx_api_token_seconds_left();
    int since_data = energyboxx_api_seconds_since_data();

    char wifi_line [72];
    if (wifi_prov_get_state() == WIFI_PROV_STATE_CONNECTED && ip [0] != '\0') {
        snprintf(wifi_line, sizeof(wifi_line), "Wifi      %s\n          %s", network, ip);
    }
    else {
        snprintf(wifi_line, sizeof(wifi_line), "Wifi      geen verbinding");
    }

    char keys_line [48];
    if (!energyboxx_api_has_credentials()) {
        snprintf(keys_line, sizeof(keys_line), "Sleutels  niet ingevoerd");
    }
    else if (!energyboxx_api_is_valid_credentials()) {
        snprintf(keys_line, sizeof(keys_line), "Sleutels  afgekeurd");
    }
    else {
        snprintf(keys_line, sizeof(keys_line), "Sleutels  goed, nog %d min", token_left / 60);
    }

    char data_line [48];
    if (since_data < 0) {
        snprintf(data_line, sizeof(data_line), "Meting    nog geen");
    }
    else {
        snprintf(data_line, sizeof(data_line), "Meting    %d s geleden", since_data);
    }

    snprintf(s_report, sizeof(s_report), "%s\n%s\n%s\nPortaal   %s",
             wifi_line, keys_line, data_line,
             wifi_prov_portal_is_open() ? "open" : "dicht");
}


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

static esp_err_t s_draw(status_view_state_t state)
{
    if (s_shown && state == s_current) {
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

    esp_err_t err;

    if (s_view [state].paint == PAINT_REPORT) {
        s_build_report();
        err = display_show_report(s_view [state].label, s_report,
                                  wifi_prov_portal_is_open()
                                      ? "Portaal is open"
                                      : "Sleutels invoeren",
                                  s_view [state].rgb);
    }
    else if (s_view [state].paint == PAINT_IMAGE) {
        err = display_show_bringup();
    }
    else {
        err = display_show_status(s_view [state].label,
                                  s_detail_for(state),
                                  s_qr_for(state),
                                  s_view [state].rgb);
    }

    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Could not draw '%s': %s", s_view [state].name, esp_err_to_name(err));
    }

    return err;
}


//  --------------------------------------------------------------------------
//  Where in the browsable list a state sits. Falls back to the first row for
//  states nobody browses to, so browsing from one of those starts at the top.

static size_t s_row_of(status_view_state_t state)
{
    for (size_t row = 0; row < BROWSABLE_COUNT; row++) {
        if (s_browsable [row] == state) {
            return row;
        }
    }

    return 0;
}


static void s_start_browsing(void)
{
    if (!s_browsing) {
        s_browsing = true;
        s_browse_row = s_row_of(s_current);
        display_show_browse_controls(true);
    }

    s_browse_until_us = esp_timer_get_time() + (int64_t) BROWSE_TIMEOUT_MS * 1000;
}


void status_view_touched(void)
{
    s_start_browsing();
}


void status_view_open_portal(void)
{
    esp_err_t err = wifi_prov_open_portal();

    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Could not open the portal: %s", esp_err_to_name(err));
        return;
    }

    //  Keep the report on screen and let it refresh; it now says the portal is
    //  open, and the QR for joining lives on the provisioning screen next door.
    s_start_browsing();
}


void status_view_resume_auto(void)
{
    if (!s_browsing) {
        return;
    }

    ESP_LOGI(TAG, "Back to following the telemetry");

    s_browsing = false;
    display_show_browse_controls(false);
    display_show_preview_marker(false);

    s_draw(s_auto_state);
}


void status_view_browse(int direction)
{
    assert (direction == -1 || direction == 1);     //  Caller's contract

    s_start_browsing();

    //  Wrap around in both directions without going negative on a size_t.
    s_browse_row = (s_browse_row + BROWSABLE_COUNT + (size_t) (direction > 0 ? 1 : -1))
                 % BROWSABLE_COUNT;

    status_view_state_t state = s_browsable [s_browse_row];

    s_draw(state);

    //  Anything that is not the live state is a preview, and has to say so.
    //  The report is about the device itself, so it is never a preview.
    display_show_preview_marker(state != s_auto_state && state != STATUS_VIEW_REPORT);
}


//  --------------------------------------------------------------------------
//  What the logic wants shown. While somebody is browsing this is remembered
//  but not drawn; the moment they stop, the device catches up by itself.

esp_err_t status_view_show(status_view_state_t state)
{
    assert (state < STATUS_VIEW_STATE_COUNT);   //  Caller's contract

    s_auto_state = state;

    if (s_browsing) {
        if (esp_timer_get_time() < s_browse_until_us) {
            //  The report ages while you look at it, so it is redrawn rather
            //  than left standing with a stale "42 s geleden".
            if (s_current == STATUS_VIEW_REPORT) {
                s_build_report();
                display_show_report(s_view [STATUS_VIEW_REPORT].label, s_report,
                                    wifi_prov_portal_is_open()
                                        ? "Portaal is open"
                                        : "Sleutels invoeren",
                                    s_view [STATUS_VIEW_REPORT].rgb);
            }
            return ESP_OK;
        }

        ESP_LOGI(TAG, "Browsing timed out, following the telemetry again");
        s_browsing = false;
        display_show_browse_controls(false);
        display_show_preview_marker(false);
    }

    //  Hold the bring-up image its minimum time, but only on the automatic
    //  path: somebody who browsed to it may leave whenever they like.
    if (s_shown
    &&  s_current == STATUS_VIEW_STARTING
    &&  state != STATUS_VIEW_STARTING
    &&  s_bringup_since_us != 0
    &&  esp_timer_get_time() - s_bringup_since_us < BRINGUP_MIN_VISIBLE_MS * 1000) {
        return ESP_OK;
    }

    return s_draw(state);
}
