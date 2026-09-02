/*  =========================================================================
    status_view - log-only implementation

    Phase 1 of the move to the ESP32-S3-BOX-3 (see docs/PLAN-box3.md). The
    board runs everything it used to, and reports its state to the log. Phase 3
    replaces the body of status_view_show () with the screen.
    =========================================================================
*/

#include <assert.h>
#include <stdbool.h>
#include <stdint.h>

#include "esp_log.h"
#include "esp_timer.h"

#include <stdio.h>
#include <string.h>

#include "esp_app_desc.h"
#include "esp_mac.h"

#include "inc/energyboxx_api.h"

#include "inc/display.h"
#include "inc/status_view.h"
#include "inc/updater.h"
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
//
//  `live` marks the views that carry numbers which move while somebody is
//  looking at them: the report counts seconds, the About page counts a
//  download, and the update view counts percent. Those are painted again on
//  every pass; the rest only when the state changes.
//
//  `reading` marks the views that claim something about the energy of the
//  community: what stands there is what the device measured a moment ago.
//  Those, and only those, can carry the "voorbeeld" mark in the corner, and
//  only while somebody has browsed away from the live one. The report, the
//  About page and the bring-up owl are about the device itself and are true
//  whenever somebody looks at them, so they are never marked. This column is
//  the whole rule; there is no second place that decides.

typedef enum { PAINT_IMAGE = 0, PAINT_COLOUR, PAINT_REPORT, PAINT_ABOUT } paint_t;

static const struct {
    const char *name;
    const char *meaning;
    const char *label;
    paint_t     paint;
    uint32_t    rgb;
    bool        live;
    bool        reading;
} s_view [STATUS_VIEW_STATE_COUNT] = {
    [STATUS_VIEW_STARTING]     = { "starting",     "powered on",
                                   "",                  PAINT_IMAGE,  0x000000 },
    [STATUS_VIEW_PROVISIONING] = { "provisioning", "waiting for the portal to be used",
                                   "Instellen",         PAINT_COLOUR, 0x1E5A8A },
    [STATUS_VIEW_CONNECTING]   = { "connecting",   "credentials known, network not up",
                                   "Verbinden",         PAINT_COLOUR, 0x334A55 },
    [STATUS_VIEW_CONNECT_FAILED] = { "connect-failed", "the network refused us or is absent",
                                   "Geen verbinding",   PAINT_COLOUR, 0x8A3A2A },
    [STATUS_VIEW_WIFI_REJECTED] = { "wifi-rejected", "the network refused the stored password",
                                   "Wachtwoord klopt niet", PAINT_COLOUR, 0x8A3A2A },
    [STATUS_VIEW_SURPLUS]      = { "surplus",      "energy available to share",
                                   "Energie over",      PAINT_COLOUR, 0x1F9E4B, .reading = true },
    [STATUS_VIEW_DEFICIT]      = { "deficit",      "energy has to be bought",
                                   "Energie inkopen",   PAINT_COLOUR, 0xE0A21B, .reading = true },
    [STATUS_VIEW_BALANCED]     = { "balanced",     "supply and demand match",
                                   "In balans",         PAINT_COLOUR, 0x243028, .reading = true },
    [STATUS_VIEW_KEYS_NEEDED]  = { "keys-needed",  "no API credentials stored",
                                   "Sleutels nodig",    PAINT_COLOUR, 0x8A5A1A },
    [STATUS_VIEW_NO_DATA]      = { "no-data",      "no fresh telemetry",
                                   "Geen gegevens",     PAINT_COLOUR, 0x5A5A5A, .reading = true },
    [STATUS_VIEW_REPORT]       = { "report",       "what the device knows about itself",
                                   "Status",            PAINT_REPORT, 0x243028, .live = true },
    [STATUS_VIEW_ABOUT]        = { "about",        "who made this and which build it is",
                                   "Energy Owl",        PAINT_ABOUT,  0x14324A, .live = true },
    [STATUS_VIEW_SETUP_DONE]   = { "setup-done",   "credentials accepted, setup finished",
                                   "Klaar, we zijn online", PAINT_COLOUR, 0x1F7A3D },
    [STATUS_VIEW_UPDATING]     = { "updating",     "installing new firmware",
                                   "Bijwerken",         PAINT_COLOUR, 0x1E5A8A, .live = true }
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
    STATUS_VIEW_ABOUT,
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

//  Whether the corner mark is up. Remembered so the rule may be applied on
//  every pass without taking the display lock for an answer that has not
//  changed.
static bool s_preview_marked = false;

//  An announcement outranks browsing: news about what just happened matters
//  more than whatever somebody was paging through.
static bool s_announcing = false;
static int64_t s_announce_until_us = 0;

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

    //  Which of the remembered networks this is. Without it, "why is it on
    //  the wrong network?" has no answer short of a serial cable. Left off
    //  when there is only one, because "1 van 1" tells nobody anything.
    char which [32] = "";
    int slot = wifi_prov_current_slot();
    size_t stored = wifi_prov_stored_count();

    if (slot >= 0 && stored > 1) {
        snprintf(which, sizeof(which), " (%d van %u)", slot + 1, (unsigned) stored);
    }

    char wifi_line [128];
    if (wifi_prov_get_state() == WIFI_PROV_STATE_CONNECTED && ip [0] != '\0') {
        snprintf(wifi_line, sizeof(wifi_line), "Wifi      %s%s\n          %s",
                 network, which, ip);
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
//  Who made this, which build is running, and where to read more. The version
//  and the build date come from the image itself rather than from a constant
//  somebody has to remember to raise. The MAC address is there for support:
//  with several testers running different builds, "which device is this?" is
//  the first question, and this is the number their router shows too.
//
//  The ESP-IDF version is deliberately not here. It fitted only by pushing the
//  text into the arrow strip, and of the four numbers it is the one a tester
//  never has to read out: the version hash already pins the build it came from.

#define PROJECT_PAGE  "https://www.dolphinsolutions.nl/gestuurde-energie-gemeenschap/"

static char s_about [384];

//  What the updater is doing, in words a tester can read out over the phone.
//  One row per state, so the enum and the screen cannot drift apart.

static const char *s_update_words [UPDATER_STATE_COUNT] = {
    [UPDATER_IDLE]        = "niets te doen",
    [UPDATER_CHECKING]    = "zoeken",
    [UPDATER_DOWNLOADING] = "bezig",
    [UPDATER_READY]       = "klaar, herstart",
    [UPDATER_FAILED]      = "mislukt"
};

static char s_update_line [40];

static const char *s_update_status(void)
{
    updater_state_t state = updater_get_state();

    if (state == UPDATER_DOWNLOADING) {
        snprintf(s_update_line, sizeof(s_update_line), "bezig, %d%%",
                 updater_progress_percent());
    }
    else {
        snprintf(s_update_line, sizeof(s_update_line), "%s", s_update_words [state]);
    }

    return s_update_line;
}

static void s_build_about(void)
{
    const esp_app_desc_t *app = esp_app_get_description();

    uint8_t mac [6] = {0};
    esp_read_mac(mac, ESP_MAC_WIFI_STA);

    snprintf(s_about, sizeof(s_about),
             "Paddy, Job en Edwin\n"
             "(c) 2026\n"
             "Dolphin Solutions\n"
             "dolphinsolutions.nl\n"
             "Versie    %s\n"
             "Gebouwd   %s\n"
             "Apparaat  %02X%02X%02X\n"
             "Update    %s",
             app->version, app->date,
             mac [3], mac [4], mac [5],
             s_update_status());
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

        case STATUS_VIEW_UPDATING:
            snprintf(s_detail, sizeof(s_detail), "%s", s_update_status());
            return s_detail;

        case STATUS_VIEW_CONNECTING:
        case STATUS_VIEW_CONNECT_FAILED:
        case STATUS_VIEW_WIFI_REJECTED:
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
    s_preview_marked = false;   //  display_init () builds the mark hidden

    return ESP_OK;
}


//  --------------------------------------------------------------------------
//  The button under the view, and what pressing it does. Two views carry one;
//  the rest have an empty row, which means no button and nothing to press.
//  The label is asked for on every paint, because both of them change with
//  what the device is doing.

static void s_open_portal(void);
static void s_check_for_update(void);

static const char *s_portal_button(void)
{
    return wifi_prov_portal_is_open() ? "Portaal is open" : "Sleutels invoeren";
}

static const char *s_update_button(void)
{
    updater_state_t state = updater_get_state();

    if (state == UPDATER_CHECKING || state == UPDATER_DOWNLOADING) {
        return "Bezig met bijwerken";
    }

    if (state == UPDATER_READY) {
        return "Klaar, herstart nu";
    }

    return "Nu bijwerken";
}

static const struct {
    const char *(*label) (void);
    void        (*run)   (void);
} s_action [STATUS_VIEW_STATE_COUNT] = {
    [STATUS_VIEW_REPORT] = { s_portal_button, s_open_portal },
    [STATUS_VIEW_ABOUT]  = { s_update_button, s_check_for_update }
};


//  The words on the button, or nothing at all for a view without one.

static const char *s_action_label_for(status_view_state_t state)
{
    return s_action [state].label ? s_action [state].label () : NULL;
}


//  --------------------------------------------------------------------------
//  Put a view on the screen. Says nothing and decides nothing; s_draw () and
//  the browse loop decide when this is called.

static esp_err_t s_paint(status_view_state_t state)
{
    //  No screen is not an error worth reporting on every state change. The
    //  device keeps working; display_init () already said so once.
    if (!display_is_ready()) {
        return ESP_OK;
    }

    esp_err_t err;

    if (s_view [state].paint == PAINT_ABOUT) {
        s_build_about();
        err = display_show_about(s_view [state].label, s_about,
                                 PROJECT_PAGE, s_action_label_for(state),
                                 s_view [state].rgb);
    }
    else if (s_view [state].paint == PAINT_REPORT) {
        s_build_report();
        err = display_show_report(s_view [state].label, s_report,
                                  s_action_label_for(state),
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
//  The mark in the corner says one thing: what you see is not what the device
//  is measuring right now. So it goes up when a view that claims a reading is
//  not the live one, and comes down otherwise. The table says which views
//  claim a reading. The mark is decided again whenever either half moves, so a
//  reading somebody parked on becomes a preview the moment the community moves
//  on without them.

static void s_mark_preview(status_view_state_t state)
{
    bool preview = s_view [state].reading && state != s_auto_state;

    if (preview == s_preview_marked) {
        return;
    }

    if (display_show_preview_marker(preview) == ESP_OK) {
        s_preview_marked = preview;

        //  In the log as well as on the screen, so that a hardware test can
        //  say what the corner did instead of relying on somebody's memory of
        //  what they saw.
        ESP_LOGI(TAG, "Preview mark %s", preview ? "up" : "down");
    }
}


//  --------------------------------------------------------------------------
//  Only a change is worth saying out loud. Callers can therefore report their
//  state on every pass of their loop without filling the log. A live view is
//  painted again on every pass, but still only logged when it arrives.

static esp_err_t s_draw(status_view_state_t state)
{
    s_mark_preview(state);

    if (s_shown && state == s_current) {
        return s_view [state].live ? s_paint(state) : ESP_OK;
    }

    ESP_LOGI(TAG, "%s - %s", s_view [state].name, s_view [state].meaning);

    s_current = state;
    s_shown = true;

    if (state == STATUS_VIEW_STARTING && s_bringup_since_us == 0) {
        s_bringup_since_us = esp_timer_get_time();
    }

    return s_paint(state);
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


void status_view_announce(status_view_state_t state, int milliseconds)
{
    assert (state < STATUS_VIEW_STATE_COUNT);   //  Caller's contract
    assert (milliseconds > 0);

    s_browsing = false;
    display_show_browse_controls(false);

    s_announcing = true;
    s_announce_until_us = esp_timer_get_time() + (int64_t) milliseconds * 1000;

    s_draw(state);
}


static void s_open_portal(void)
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


static void s_check_for_update(void)
{
    updater_check_now();

    //  Stay on the About page: it carries the update line, and whoever pressed
    //  the button wants to see what happens next.
    s_start_browsing();
}


void status_view_action(void)
{
    if (s_action [s_current].run == NULL) {
        return;                 //  This view has no button; nothing to do
    }

    ESP_LOGI(TAG, "Button pressed on '%s'", s_view [s_current].name);

    s_action [s_current].run ();
}


void status_view_resume_auto(void)
{
    if (!s_browsing) {
        return;
    }

    ESP_LOGI(TAG, "Back to following the telemetry");

    s_browsing = false;
    display_show_browse_controls(false);

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
}


//  --------------------------------------------------------------------------
//  What the logic wants shown. While somebody is browsing this is remembered
//  but not drawn; the moment they stop, the device catches up by itself.

esp_err_t status_view_show(status_view_state_t state)
{
    assert (state < STATUS_VIEW_STATE_COUNT);   //  Caller's contract

    s_auto_state = state;

    if (s_announcing) {
        if (esp_timer_get_time() < s_announce_until_us) {
            return ESP_OK;
        }

        s_announcing = false;
    }

    if (s_browsing) {
        if (esp_timer_get_time() < s_browse_until_us) {
            //  The telemetry may have moved while somebody is looking at
            //  something else, so the mark is decided again here, whether or
            //  not this view repaints.
            s_mark_preview(s_current);

            //  A live view ages while somebody looks at it, so it is painted
            //  again rather than left standing with a stale "42 s geleden".
            if (s_view [s_current].live) {
                s_paint(s_current);
            }
            return ESP_OK;
        }

        ESP_LOGI(TAG, "Browsing timed out, following the telemetry again");
        s_browsing = false;
        display_show_browse_controls(false);
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
