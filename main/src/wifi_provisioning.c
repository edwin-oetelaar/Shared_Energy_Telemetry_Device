
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"

#include <assert.h>
#include <inttypes.h>
#include <stdlib.h>
#include <string.h>

#include "esp_system.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"

#include "inc/wifi_provisioning.h"
#include "inc/wifi_web.h"
#include "inc/dns_server.h"
#include "inc/wifi_storage.h"
#include "inc/energyboxx_api.h"


#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT      BIT1

//  Separate from WIFI_FAIL_BIT on purpose. "The fast attempts are used up" and
//  "this password is wrong" call for different answers: the first is worth
//  waiting out, the second needs somebody to type something.
#define WIFI_REJECTED_BIT  BIT2

//  How long the device waits for somebody to finish provisioning before it
//  restarts and tries the stored network again. Without this the wait is
//  unbounded: a device whose API was briefly unreachable would sit on its
//  windowsill showing a portal that nobody is looking at, forever. A restart
//  costs a few seconds and gives the saved credentials a fresh chance; if
//  nothing has changed the portal is simply back. The clock is reset by every
//  handler a person triggers, so it measures silence, not elapsed time.
//
//  esp_restart () reports ESP_RST_SW, which main.c deliberately does not count
//  towards the three-power-cycles credential wipe.

#define PROVISIONING_SILENCE_TIMEOUT_MS  (15 * 60 * 1000)

//  How often the stored API credentials are retried while the provisioning
//  portal is open. Slow on purpose: the portal is the normal way in, and this
//  retry only exists to recover from an API that was down at boot.
#define API_RETRY_WHILE_PROVISIONING_MS  30000

#define PROV_AP_SSID       "SETD_Provisioning"
#define PROV_AP_PASSWORD   ""
#define PROV_AP_CHANNEL    1
#define PROV_AP_MAX_CONN   4

static EventGroupHandle_t s_wifi_event_group;

static dns_server_handle_t s_dns_handle = NULL;

static esp_netif_t *s_sta_netif = NULL;
static esp_netif_t *s_ap_netif = NULL;

static const char *TAG = "[wifi_prov]";


/*  =========================================================================
    Two state machines, because the device is doing two independent things.

    The link is its connection to a network. The portal is the access point and
    web server it offers so somebody can set it up. They overlap freely: a
    device can be online and still have its portal open.

    These used to share one variable written from six places. That is where the
    mismatches came from: bringing the access point up overwrote the connection
    state, and closing it had to guess what to put back. The state a screen
    reads is now derived from both machines rather than stored, so it cannot
    disagree with them.
    =========================================================================
*/

typedef enum {
    LINK_IDLE = 0,      //  Nothing has been asked of the radio yet
    LINK_CONNECTING,    //  Trying, or waiting out a backoff step
    LINK_CONNECTED,     //  On a network, with an address
    LINK_FAILED,        //  Still trying, but the fast attempts are used up
    LINK_REJECTED,      //  The network refused this password
    LINK_STATE_COUNT
} link_state_t;

typedef enum {
    LINK_EV_CONNECT = 0,    //  Somebody asked for a network
    LINK_EV_GOT_IP,         //  The network gave us an address
    LINK_EV_LOST,           //  The connection dropped; another try is planned
    LINK_EV_GAVE_UP,        //  The schedule has reached its slow tail
    LINK_EV_REFUSED,        //  The password was refused, not merely unlucky
    LINK_EV_COUNT
} link_event_t;

typedef enum {
    PORTAL_CLOSED = 0,
    PORTAL_OPEN,
    PORTAL_CLOSING,     //  Accepted, but the browser still needs its answer
    PORTAL_STATE_COUNT
} portal_state_t;

typedef enum {
    PORTAL_EV_OPEN = 0,     //  Bring it up, or keep it up
    PORTAL_EV_ACCEPTED,     //  Credentials were accepted
    PORTAL_EV_DONE,         //  Somebody pressed Klaar, or start-up finished
    PORTAL_EV_SILENT,       //  Nobody has used it for a long time
    PORTAL_EV_GRACE_OVER,   //  The browser has had its moment
    PORTAL_EV_COUNT
} portal_event_t;

//  One row per state, one column per event. Every combination has an answer,
//  so no event can leave a machine somewhere undefined.

static const link_state_t s_link_next [LINK_STATE_COUNT][LINK_EV_COUNT] = {
    /*                     CONNECT          GOT_IP          LOST             GAVE_UP         REFUSED       */
    [LINK_IDLE]       = { LINK_CONNECTING, LINK_CONNECTED, LINK_IDLE,       LINK_IDLE,      LINK_REJECTED },
    [LINK_CONNECTING] = { LINK_CONNECTING, LINK_CONNECTED, LINK_CONNECTING, LINK_FAILED,    LINK_REJECTED },
    [LINK_CONNECTED]  = { LINK_CONNECTING, LINK_CONNECTED, LINK_CONNECTING, LINK_FAILED,    LINK_REJECTED },
    [LINK_FAILED]     = { LINK_CONNECTING, LINK_CONNECTED, LINK_FAILED,     LINK_FAILED,    LINK_REJECTED },
    //  Only new credentials, or the password working after all, get out of
    //  here. Another timeout on the same password changes nothing.
    [LINK_REJECTED]   = { LINK_CONNECTING, LINK_CONNECTED, LINK_REJECTED,   LINK_REJECTED,  LINK_REJECTED }
};

static const portal_state_t s_portal_next [PORTAL_STATE_COUNT][PORTAL_EV_COUNT] = {
    /*                    OPEN            ACCEPTED         DONE             SILENT          GRACE_OVER    */
    [PORTAL_CLOSED]  = { PORTAL_OPEN,    PORTAL_CLOSED,   PORTAL_CLOSED,   PORTAL_CLOSED,  PORTAL_CLOSED },
    [PORTAL_OPEN]    = { PORTAL_OPEN,    PORTAL_CLOSING,  PORTAL_CLOSING,  PORTAL_CLOSED,  PORTAL_OPEN   },
    [PORTAL_CLOSING] = { PORTAL_CLOSING, PORTAL_CLOSING,  PORTAL_CLOSING,  PORTAL_CLOSED,  PORTAL_CLOSED }
};

static const char *s_link_name [LINK_STATE_COUNT] =
    { "idle", "connecting", "connected", "failed", "rejected" };
static const char *s_portal_name [PORTAL_STATE_COUNT] = { "closed", "open", "closing" };
static const char *s_link_event_name [LINK_EV_COUNT] =
    { "connect", "got-ip", "lost", "gave-up", "refused" };
static const char *s_portal_event_name [PORTAL_EV_COUNT] =
    { "open", "accepted", "done", "silent", "grace-over" };

static link_state_t s_link = LINK_IDLE;
static portal_state_t s_portal = PORTAL_CLOSED;

//  Set on entering PORTAL_CLOSING, so the browser gets its answer before the
//  access point disappears from under it.
static int64_t s_portal_close_at_us = 0;

static esp_err_t s_portal_bring_up(void);
static void s_portal_tear_down(void);


//  --------------------------------------------------------------------------
//  The only place the link state changes. Every transition is logged, so a
//  device that ends up somewhere unexpected says how it got there.

static void link_handle(link_event_t event)
{
    assert (event < LINK_EV_COUNT);         //  Caller's contract

    link_state_t next = s_link_next [s_link][event];

    if (next == s_link) {
        return;
    }

    ESP_LOGI(TAG, "link: %s --%s--> %s",
             s_link_name [s_link], s_link_event_name [event], s_link_name [next]);

    s_link = next;
}


//  --------------------------------------------------------------------------
//  The only place the portal state changes, and therefore the only place the
//  access point and web server go up or down. Tearing down is the entry action
//  of PORTAL_CLOSED, so a second copy of it cannot exist to forget something.

static void portal_handle(portal_event_t event)
{
    assert (event < PORTAL_EV_COUNT);       //  Caller's contract

    portal_state_t next = s_portal_next [s_portal][event];

    if (next == s_portal) {
        //  Asking an open portal to open again is how activity is refreshed.
        if (event == PORTAL_EV_OPEN) {
            wifi_prov_note_portal_activity();
        }
        return;
    }

    ESP_LOGI(TAG, "portal: %s --%s--> %s",
             s_portal_name [s_portal], s_portal_event_name [event], s_portal_name [next]);

    portal_state_t previous = s_portal;
    s_portal = next;

    switch (next) {
        case PORTAL_OPEN:
            if (s_portal_bring_up() != ESP_OK) {
                s_portal = previous;
            }
            break;

        case PORTAL_CLOSING:
            s_portal_close_at_us = esp_timer_get_time() + 3 * 1000 * 1000;
            break;

        case PORTAL_CLOSED:
            s_portal_tear_down();
            break;

        default:
            break;
    }
}





//  Reconnect schedule. Each row says how long to wait before the next attempt
//  and what the device reports about itself while it waits. The last row
//  repeats for as long as the network stays away: a device on a windowsill has
//  no keyboard, so giving up is not a strategy. The whole recovery policy is
//  this table - to change how patient the device is, change these numbers.

typedef struct {
    uint32_t delay_ms;
    link_event_t event;     //  What this step means for the link machine
} retry_step_t;

static const retry_step_t s_retry_schedule [] = {
    {    500, LINK_EV_LOST    },
    {   1000, LINK_EV_LOST    },
    {   2000, LINK_EV_LOST    },
    {   5000, LINK_EV_LOST    },
    {  10000, LINK_EV_GAVE_UP },
    {  30000, LINK_EV_GAVE_UP },
    {  60000, LINK_EV_GAVE_UP },
    { 300000, LINK_EV_GAVE_UP }
};

#define RETRY_SCHEDULE_ROWS  (sizeof (s_retry_schedule) / sizeof (s_retry_schedule [0]))

//  --------------------------------------------------------------------------
//  What a disconnect means for the credentials we are holding. The radio hands
//  us a number; this table turns it into one of three answers, and the answer
//  decides what happens next. Anything not named here is TEMPORARY, which is
//  the safe default: keep trying.
//
//  The numbers come from wifi_err_reason_t in esp_wifi_types_generic.h and are
//  used by name, so a renumbering in a future ESP-IDF cannot silently point a
//  row at the wrong meaning.
//
//  WIFI_REASON_AUTH_EXPIRE (2) is deliberately NOT rejection, although it shows
//  up in the log of M10 right after a refused handshake. It also happens on a
//  weak link with the right password, and calling a good network wrong is the
//  worse mistake of the two: it puts the device in front of somebody who has to
//  type. After this change the refused handshake stops the cycle before a
//  reason 2 ever follows.

typedef enum {
    DISCONNECT_TEMPORARY = 0,   //  Bad moment, bad luck; try again
    DISCONNECT_ABSENT,          //  This network is not within reach
    DISCONNECT_REJECTED         //  These credentials will not work here
} disconnect_meaning_t;

static const struct {
    uint8_t reason;
    disconnect_meaning_t meaning;
    const char *what;
} s_disconnect_meaning [] = {
    { WIFI_REASON_4WAY_HANDSHAKE_TIMEOUT, DISCONNECT_REJECTED,  "the password is wrong" },
    { WIFI_REASON_HANDSHAKE_TIMEOUT,      DISCONNECT_REJECTED,  "the handshake was refused" },
    { WIFI_REASON_AUTH_FAIL,              DISCONNECT_REJECTED,  "the network refused our key" },
    { WIFI_REASON_NO_AP_FOUND,            DISCONNECT_ABSENT,    "the network is not here" }
};

#define DISCONNECT_MEANING_ROWS \
    (sizeof (s_disconnect_meaning) / sizeof (s_disconnect_meaning [0]))

static disconnect_meaning_t s_meaning_of(uint8_t reason, const char **what)
{
    for (size_t row = 0; row < DISCONNECT_MEANING_ROWS; row++) {
        if (s_disconnect_meaning [row].reason == reason) {
            *what = s_disconnect_meaning [row].what;
            return s_disconnect_meaning [row].meaning;
        }
    }

    *what = "reason unknown to us";

    return DISCONNECT_TEMPORARY;
}


static esp_timer_handle_t s_retry_timer = NULL;
static size_t s_retry_row = 0;

static TickType_t s_last_portal_activity = 0;
static esp_timer_handle_t s_portal_timer = NULL;

static wifi_prov_accepted_cb_t s_accepted_cb = NULL;

static char current_ssid[33] = {0};
static char current_password[65] = {0};

/*  =========================================================================
    Rounds

    A round is one pass along the stored networks, best first. The order comes
    from a scan, because "which of my networks am I standing in?" is one
    question the radio can answer in two seconds - and answering it by trying
    each network until one connects costs a timeout apiece.

    Inside a round nothing waits: a network that is not here, or that refuses
    the password, hands over to the next one at once. Waiting is what happens
    between rounds, and that is the reconnect schedule doing its old job on
    the round as a whole.

    Phase 2 of docs/PLAN-wifi-slots.md.
    =========================================================================
*/

//  Read once, in the task that starts the device, so that no timer or event
//  handler has to go to NVS to find out what to try next.
static wifi_slot_t s_slots [WIFI_SLOT_COUNT];
static int s_last_ok = -1;

static size_t s_plan [WIFI_SLOT_COUNT];
static size_t s_plan_count = 0;
static size_t s_plan_row = 0;
static int s_slot_in_use = -1;

//  False while the portal is driving: credentials typed by hand are tried as
//  given, not planned around.
static bool s_using_slots = false;

//  True between the start of a round and its end. A failure means something
//  different inside a round (try the next network) than outside one (the
//  network we are on has dropped).
static bool s_in_round = false;

//  How this round went, and it is the mix that decides. A network that is not
//  here says nothing about our passwords, so it neither counts for nor against
//  the verdict. A failure we cannot read does count against it: concluding
//  "your password is wrong" from a reason we do not understand would send
//  somebody looking for a problem that is not there.
static size_t s_round_refused = 0;
static bool s_round_uncertain = false;

//  The scan we asked for is ours, not the portal's "Refresh networks".
static bool s_scan_for_plan = false;

//  The next time the schedule fires, look around again instead of trying the
//  same network. Set when a round ends, and when the fast attempts on a
//  network we were connected to are used up.
static bool s_replan_next = false;

static void s_begin_round (void);
static void s_try_next_in_round (void);
static void s_plan_and_start_from_scan (void);

//  --------------------------------------------------------------------------
//  The backoff delay has passed, so ask Wi-Fi to try again. A refusal here is
//  not fatal: the disconnect event that follows schedules the next attempt.

static void
    s_retry_timer_expired (void *argument)
{
    (void) argument;

    //  A round that came up empty, or a network whose fast attempts are used
    //  up, deserves a fresh look: the device may be somewhere else now.
    if (s_using_slots && s_replan_next) {
        s_begin_round ();
        return;
    }

    esp_err_t rc = esp_wifi_connect ();
    if (rc != ESP_OK)
        ESP_LOGW (TAG, "Reconnect attempt refused: %s", esp_err_to_name (rc));
}


//  --------------------------------------------------------------------------
//  Arm the timer for the current row of the schedule and report the state that
//  belongs to it, then step one row down. The last row is where we stay.

static void
    s_schedule_retry (void)
{
    assert (s_retry_timer);         //  Created by wifi_prov_init ()
    assert (s_retry_row < RETRY_SCHEDULE_ROWS);

    const retry_step_t *step = &s_retry_schedule [s_retry_row];
    if (s_retry_row + 1 < RETRY_SCHEDULE_ROWS)
        s_retry_row++;

    link_handle (step->event);
    if (step->event == LINK_EV_GAVE_UP) {
        xEventGroupSetBits (s_wifi_event_group, WIFI_FAIL_BIT);

        //  The fast attempts on this network are spent. Whatever comes next
        //  starts with a scan, in case we are no longer where we were.
        s_replan_next = true;
    }

    ESP_LOGW (TAG, "Reconnecting in %" PRIu32 " ms", step->delay_ms);

    esp_timer_stop (s_retry_timer);     //  Harmless when it is not running

    esp_err_t rc = esp_timer_start_once (s_retry_timer,
                                         (uint64_t) step->delay_ms * 1000);
    assert (rc == ESP_OK);              //  Only fails on bad arguments
    (void) rc;
}


//  --------------------------------------------------------------------------
//  Back to the top of the schedule. A connection that succeeded, or a fresh
//  set of credentials, starts counting again from the shortest delay.

static void
    s_reset_retry_schedule (void)
{
    s_retry_row = 0;
    if (s_retry_timer)
        esp_timer_stop (s_retry_timer);
}


//  --------------------------------------------------------------------------
//  The network told us the key is wrong. Retrying it quickly cannot work: only
//  new credentials can, and those come through the portal. So the schedule
//  jumps straight to its last row instead of climbing through the fast ones,
//  which leaves the radio free for whoever is standing in the portal - the
//  second complaint in M10.
//
//  Not "never again", though. A handshake can time out on a very poor link
//  with the right password, and a device that stops trying altogether is
//  exactly the failure C2 was about. The last row is a five minute heartbeat:
//  quiet enough to stay out of the way, patient enough to heal by itself.

static void
    s_refuse_credentials (const char *what)
{
    ESP_LOGW (TAG, "The network refused these credentials: %s", what);

    link_handle (LINK_EV_REFUSED);
    xEventGroupSetBits (s_wifi_event_group, WIFI_REJECTED_BIT);

    s_retry_row = RETRY_SCHEDULE_ROWS - 1;
    s_schedule_retry ();
}

static void wifi_event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data)
{
    (void) arg;

    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        ESP_LOGI(TAG, "STA started");
    }

    else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        wifi_event_sta_disconnected_t *disc = (wifi_event_sta_disconnected_t *)event_data;

        const char *what = NULL;
        disconnect_meaning_t meaning = s_meaning_of ((uint8_t) disc->reason, &what);

        ESP_LOGW(TAG, "STA disconnected, reason=%d: %s", disc->reason, what);

        xEventGroupClearBits(s_wifi_event_group, WIFI_CONNECTED_BIT);

        //  Only chase a network we have credentials for. Without an SSID the
        //  device is waiting to be provisioned, not for the router to return.
        if (current_ssid [0] == '\0')
            link_handle (LINK_EV_LOST);
        else
        if (s_in_round) {
            //  Inside a round nothing waits. This network is not here, or will
            //  not have us; the next one might, and it is a moment away rather
            //  than the whole schedule.
            if (meaning == DISCONNECT_REJECTED)
                s_round_refused++;
            else
            if (meaning == DISCONNECT_TEMPORARY)
                s_round_uncertain = true;

            s_try_next_in_round ();
        }
        else
        if (meaning == DISCONNECT_REJECTED)
            s_refuse_credentials (what);
        else
            s_schedule_retry ();
    }

    else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_SCAN_DONE) {
        //  The portal scans too, for its list of networks. That one is somebody
        //  else's answer; only take the one we asked for.
        if (s_scan_for_plan) {
            s_scan_for_plan = false;
            s_plan_and_start_from_scan ();
        }
    }

    else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_AP_START) {
        ESP_LOGI(TAG, "Provisioning AP started");
    }

    else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_AP_STACONNECTED) {
        wifi_event_ap_staconnected_t *event = (wifi_event_ap_staconnected_t *)event_data;
        ESP_LOGI(TAG, "Client connected to AP, AID=%d", event->aid);
    }

    else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_AP_STADISCONNECTED) {
        wifi_event_ap_stadisconnected_t *event = (wifi_event_ap_stadisconnected_t *)event_data;
        ESP_LOGI(TAG, "Client disconnected from AP, AID=%d", event->aid);
    }

    else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;

        ESP_LOGI(TAG, "Got IP: " IPSTR, IP2STR(&event->ip_info.ip));

        s_reset_retry_schedule ();
        link_handle(LINK_EV_GOT_IP);

        s_in_round = false;
        s_replan_next = false;

        if (s_slot_in_use >= 0) {
            //  This slot worked. Raising it above the others is what makes
            //  "the one that worked last time" answerable at the next start.
            wifi_storage_note_success((size_t) s_slot_in_use);
            s_last_ok = s_slot_in_use;
        }
        else {
            //  Typed in the portal, and now proven. Which slot it belongs in
            //  is storage's rule, not ours; we only want to know the answer,
            //  because the screen says which network the device is using.
            size_t slot = 0;

            if (wifi_storage_save_credentials(current_ssid, current_password,
                                              &slot, NULL) == ESP_OK) {
                s_slot_in_use = (int) slot;
                s_last_ok = (int) slot;

                //  The cached copy is now behind by one network.
                wifi_storage_load_slots(s_slots);
            }
        }

        
        xEventGroupClearBits(s_wifi_event_group, WIFI_FAIL_BIT);
        xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
    }
}

esp_err_t wifi_prov_init(void)
{
    esp_err_t err;

    err = esp_netif_init();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        return err;
    }

    err = esp_event_loop_create_default();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        return err;
    }

    s_wifi_event_group = xEventGroupCreate();
    if (s_wifi_event_group == NULL) {
        ESP_LOGE(TAG, "Failed to create event group");
        return ESP_FAIL;
    }

    s_sta_netif = esp_netif_create_default_wifi_sta();
    s_ap_netif = esp_netif_create_default_wifi_ap();

    if (s_sta_netif == NULL || s_ap_netif == NULL) {
        ESP_LOGE(TAG, "Failed to create netifs");
        return ESP_FAIL;
    }

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();

    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    ESP_ERROR_CHECK(esp_event_handler_register(
        WIFI_EVENT,
        ESP_EVENT_ANY_ID,
        wifi_event_handler,
        NULL
    ));

    ESP_ERROR_CHECK(esp_event_handler_register(
        IP_EVENT,
        IP_EVENT_STA_GOT_IP,
        wifi_event_handler,
        NULL
    ));

    const esp_timer_create_args_t retry_timer_args = {
        .callback = s_retry_timer_expired,
        .name = "wifi_retry"
    };

    ESP_ERROR_CHECK(esp_timer_create(&retry_timer_args, &s_retry_timer));

    //  Say what mode we are in instead of inheriting it. With
    //  CONFIG_ESP_WIFI_NVS_ENABLED the driver keeps the mode and the access
    //  point configuration in its own NVS namespace, so without this line the
    //  device comes up in whatever mode it was left in. On any device whose
    //  portal has ever been open that means an open access point at every
    //  boot, brought up by the driver rather than by us - visible in the log
    //  as "Provisioning AP started" without our own "Starting provisioning AP"
    //  in front of it.
    //
    //  Station only is the resting state. wifi_prov_start_ap () switches to
    //  AP+station when somebody asks for the portal, and closing it comes back
    //  here.
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));

    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(TAG, "WiFi provisioning initialized");

    return ESP_OK;
}

//  --------------------------------------------------------------------------
//  Watches an on-demand portal. It closes on its own for two reasons: the
//  credentials were accepted, or nobody has touched it for a while. Without
//  this an open access point would stay up until the next power cut.

static void s_portal_watchdog(void *argument)
{
    (void) argument;

    if (s_portal == PORTAL_CLOSING
    &&  s_portal_close_at_us != 0
    &&  esp_timer_get_time() >= s_portal_close_at_us) {
        portal_handle(PORTAL_EV_GRACE_OVER);
        return;
    }

    if (s_portal == PORTAL_OPEN
    &&  xTaskGetTickCount() - s_last_portal_activity
    >   pdMS_TO_TICKS(PROVISIONING_SILENCE_TIMEOUT_MS)) {
        ESP_LOGW(TAG, "Nobody used the portal for %d minutes",
                 PROVISIONING_SILENCE_TIMEOUT_MS / 60000);
        portal_handle(PORTAL_EV_SILENT);
    }
}


void wifi_prov_set_credentials_accepted_cb(wifi_prov_accepted_cb_t callback)
{
    s_accepted_cb = callback;
}


void wifi_prov_note_credentials_accepted(void)
{
    ESP_LOGI(TAG, "Credentials accepted");

    if (s_accepted_cb != NULL) {
        s_accepted_cb();
    }

    portal_handle(PORTAL_EV_ACCEPTED);
}


bool wifi_prov_portal_is_open(void)
{
    return s_portal != PORTAL_CLOSED;
}


//  The only way a portal is taken down. It used to refuse when the portal had
//  not been opened from the screen, which left the portal that runs at start-up
//  to tear itself down by hand - and that copy forgot to put the reported state
//  back, so the screen kept showing "Instellen" with a QR code for an access
//  point that was no longer there.
//
//  Safe to call when there is nothing to close: every step below checks.

static void s_portal_tear_down(void)
{
    s_portal_close_at_us = 0;

    if (s_portal_timer != NULL) {
        esp_timer_stop(s_portal_timer);
    }

    wifi_web_stop();

    if (s_dns_handle != NULL) {
        stop_dns_server(s_dns_handle);
        s_dns_handle = NULL;
    }

    //  Back to station only. The device keeps the network it was already on.
    //  What a screen reports is derived from the two machines, so nothing has
    //  to be put back by hand here; that guessing is what M12 was.
    esp_err_t err = esp_wifi_set_mode(WIFI_MODE_STA);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Could not return to station mode: %s", esp_err_to_name(err));
    }
}


esp_err_t wifi_prov_close_portal(void)
{
    portal_handle(PORTAL_EV_DONE);

    return ESP_OK;
}


static esp_err_t s_portal_bring_up(void)
{
    esp_err_t err = wifi_prov_start_ap();
    if (err != ESP_OK) {
        return err;
    }

    err = wifi_web_start();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Portal did not start: %s", esp_err_to_name(err));
        return err;
    }

    if (s_portal_timer == NULL) {
        const esp_timer_create_args_t args = {
            .callback = s_portal_watchdog,
            .name = "portal_watchdog"
        };

        err = esp_timer_create(&args, &s_portal_timer);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Portal watchdog missing: %s", esp_err_to_name(err));
            return err;
        }
    }

    s_portal_close_at_us = 0;
    wifi_prov_note_portal_activity();
    esp_timer_start_periodic(s_portal_timer, 5 * 1000 * 1000);

    return ESP_OK;
}


esp_err_t wifi_prov_open_portal(void)
{
    portal_handle(PORTAL_EV_OPEN);

    return s_portal == PORTAL_CLOSED ? ESP_FAIL : ESP_OK;
}


esp_err_t wifi_prov_start_ap(void)
{
    wifi_config_t ap_config = {
        .ap = {
            .ssid = PROV_AP_SSID,
            .ssid_len = strlen(PROV_AP_SSID),
            .channel = PROV_AP_CHANNEL,
            .password = PROV_AP_PASSWORD,
            .max_connection = PROV_AP_MAX_CONN,
            .authmode = WIFI_AUTH_OPEN,
        },
    };

    ESP_LOGI(TAG, "Starting provisioning AP: %s", PROV_AP_SSID);

    //  No ESP_ERROR_CHECK here. This used to run once at start-up, where an
    //  abort was survivable; since the portal can be opened again from the
    //  screen it runs while somebody is using the device, and a failure has to
    //  be reported rather than take the whole thing down.
    esp_err_t err = esp_wifi_set_mode(WIFI_MODE_APSTA);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Could not switch to AP+station mode: %s", esp_err_to_name(err));
        return err;
    }

    err = esp_wifi_set_config(WIFI_IF_AP, &ap_config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Could not configure the access point: %s", esp_err_to_name(err));
        return err;
    }

    // Start the DNS server that will redirect all queries to the softAP IP
    dns_server_config_t dns_config = DNS_SERVER_CONFIG_SINGLE("*" /* all A queries */, "WIFI_AP_DEF" /* softAP netif ID */);
    s_dns_handle = start_dns_server(&dns_config);
    if (s_dns_handle == NULL) {
        //  The portal still works on its own address; only the redirect that
        //  opens it by itself is missing. Worth saying out loud rather than
        //  leaving somebody to wonder why nothing pops up.
        ESP_LOGE(TAG, "DNS redirect did not start; the portal is only at 192.168.4.1");
    }

    return ESP_OK;
}

static esp_err_t s_connect_to(const char *ssid, const char *password)
{
    if (ssid == NULL || ssid[0] == '\0') {
        return ESP_ERR_INVALID_ARG;
    }

    wifi_config_t sta_config = {0};

    snprintf((char *)sta_config.sta.ssid, sizeof(sta_config.sta.ssid), "%s", ssid);

    if (password != NULL) {
        snprintf((char *)sta_config.sta.password, sizeof(sta_config.sta.password), "%s", password);
    }

    s_reset_retry_schedule ();
    link_handle(LINK_EV_CONNECT);

    xEventGroupClearBits(s_wifi_event_group,
                         WIFI_CONNECTED_BIT | WIFI_FAIL_BIT | WIFI_REJECTED_BIT);

    ESP_LOGI(TAG, "Connecting to SSID: %s", ssid);

    esp_err_t config_err = esp_wifi_set_config(WIFI_IF_STA, &sta_config);
    if (config_err != ESP_OK) {
        ESP_LOGE(TAG, "Could not set the station config: %s", esp_err_to_name(config_err));
        return config_err;
    }

    current_ssid[0] = '\0';
    current_password[0] = '\0';
    strncpy(current_ssid, ssid, sizeof(current_ssid) - 1);
    if (password != NULL) {
        strncpy(current_password, password, sizeof(current_password) - 1);
    }

    return esp_wifi_connect();
}


//  Credentials somebody typed. They are tried as given: no plan, no scan, no
//  moving on to another network when they turn out to be wrong, because the
//  person who typed them is standing there waiting to hear about it.

esp_err_t wifi_prov_connect(const char *ssid, const char *password)
{
    s_using_slots = false;
    s_in_round = false;
    s_slot_in_use = -1;

    return s_connect_to(ssid, password);
}


//  --------------------------------------------------------------------------
//  Try the next network in this round's plan. When the plan runs out the round
//  is over, and waiting begins.

static void s_end_round(void)
{
    s_in_round = false;
    s_replan_next = true;       //  Look around again before the next attempt

    //  Something refused our key, and nothing failed in a way we could not
    //  read. More patience cannot help with that: only somebody typing a
    //  password can. Say so, and step back to the quiet end of the schedule to
    //  leave the radio free for the portal.
    if (s_round_refused > 0 && !s_round_uncertain) {
        ESP_LOGW(TAG, "%u of %u stored networks refused our key%s",
                 (unsigned) s_round_refused, (unsigned) s_plan_count,
                 s_round_refused < s_plan_count ? "; the rest were not here" : "");

        link_handle(LINK_EV_REFUSED);
        xEventGroupSetBits(s_wifi_event_group, WIFI_REJECTED_BIT);
        s_retry_row = RETRY_SCHEDULE_ROWS - 1;
    }

    s_schedule_retry();
}


static void s_try_next_in_round(void)
{
    if (s_plan_row >= s_plan_count) {
        s_end_round();
        return;
    }

    size_t slot = s_plan [s_plan_row++];

    ESP_LOGI(TAG, "Round: slot %u, network %u of %u",
             (unsigned) slot, (unsigned) s_plan_row, (unsigned) s_plan_count);

    s_slot_in_use = (int) slot;

    esp_err_t err = s_connect_to(s_slots [slot].ssid, s_slots [slot].password);

    //  A refusal here is this network's turn wasted, not the round's. The
    //  disconnect that would have moved us on is never going to arrive, so
    //  move on from here.
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Could not start on slot %u: %s",
                 (unsigned) slot, esp_err_to_name(err));
        s_round_uncertain = true;
        s_try_next_in_round();
    }
}


//  Plan a round and start it. The plan comes from a scan when one is possible;
//  without one it is every stored network in slot order, which is what the
//  device did before it could scan.

static void s_plan_and_start(const wifi_seen_t *seen, size_t seen_count)
{
    s_plan_count = wifi_slots_plan(s_slots, seen, seen_count, s_last_ok, s_plan);
    s_plan_row = 0;
    s_round_refused = 0;
    s_round_uncertain = false;
    s_in_round = true;

    if (s_plan_count == 0) {
        ESP_LOGW(TAG, "No stored networks to try");
        s_in_round = false;
        return;
    }

    //  Say what was decided and on what grounds. A device that picks the
    //  "wrong" network is otherwise a mystery, and the answer is nearly always
    //  in which networks the radio could see and how loud they were.
    for (size_t row = 0; row < s_plan_count; row++) {
        size_t slot = s_plan [row];
        int8_t rssi = 0;
        bool visible = false;

        for (size_t seen_row = 0; seen_row < seen_count; seen_row++) {
            if (strcmp(seen [seen_row].ssid, s_slots [slot].ssid) == 0
            &&  (!visible || seen [seen_row].rssi > rssi)) {
                rssi = seen [seen_row].rssi;
                visible = true;
            }
        }

        //  Three different things, and saying the wrong one sends somebody
        //  looking in the wrong place: seen and how loud, scanned for and not
        //  found, or never scanned for at all.
        const char *how = seen == NULL ? "no scan was needed"
                        : visible      ? NULL
                                       : "not in the scan";

        if (how == NULL) {
            ESP_LOGI(TAG, "Plan %u: slot %u '%s', %d dBm%s",
                     (unsigned) (row + 1), (unsigned) slot, s_slots [slot].ssid,
                     rssi, (int) slot == s_last_ok ? ", worked last time" : "");
        }
        else {
            ESP_LOGI(TAG, "Plan %u: slot %u '%s', %s",
                     (unsigned) (row + 1), (unsigned) slot, s_slots [slot].ssid, how);
        }
    }

    s_try_next_in_round();
}


//  How many slots hold a network.

static size_t s_filled_slots(void)
{
    size_t filled = 0;

    for (size_t slot = 0; slot < WIFI_SLOT_COUNT; slot++) {
        if (!wifi_slots_is_empty(&s_slots [slot])) {
            filled++;
        }
    }

    return filled;
}


//  What the radio saw, reduced to the little the planner needs: the networks
//  that are ours. A block of flats can put fifty access points in a scan, and
//  none of the other forty-seven change anything here. Keeping only ours also
//  means a crowded place cannot push our network past the end of a buffer.
//
//  The records come off the heap. A scan can return dozens of them, and the
//  event task's stack is not the place for that.

#define SCAN_READ_MAX  40
#define SEEN_MAX       (WIFI_SLOT_COUNT * 4)

static bool s_is_one_of_ours(const char *ssid)
{
    for (size_t slot = 0; slot < WIFI_SLOT_COUNT; slot++) {
        if (!wifi_slots_is_empty(&s_slots [slot])
        &&  strcmp(s_slots [slot].ssid, ssid) == 0) {
            return true;
        }
    }

    return false;
}


static void s_plan_and_start_from_scan(void)
{
    uint16_t found = 0;

    if (esp_wifi_scan_get_ap_num(&found) != ESP_OK || found == 0) {
        ESP_LOGW(TAG, "The scan saw nothing; trying the networks in order");
        esp_wifi_clear_ap_list();
        s_plan_and_start(NULL, 0);
        return;
    }

    uint16_t wanted = found > SCAN_READ_MAX ? SCAN_READ_MAX : found;
    wifi_ap_record_t *records = calloc(wanted, sizeof(*records));

    if (records == NULL) {
        ESP_LOGW(TAG, "No room to read the scan; trying the networks in order");
        esp_wifi_clear_ap_list();
        s_plan_and_start(NULL, 0);
        return;
    }

    //  Static rather than on the stack, for the same reason as the records.
    static wifi_seen_t s_seen [SEEN_MAX];
    size_t seen_count = 0;

    if (esp_wifi_scan_get_ap_records(&wanted, records) == ESP_OK) {
        for (uint16_t row = 0; row < wanted && seen_count < SEEN_MAX; row++) {
            const char *ssid = (const char *) records [row].ssid;

            if (!s_is_one_of_ours(ssid)) {
                continue;
            }

            snprintf(s_seen [seen_count].ssid, sizeof(s_seen [seen_count].ssid),
                     "%s", ssid);
            s_seen [seen_count].rssi = records [row].rssi;
            seen_count++;
        }
    }

    free(records);

    ESP_LOGI(TAG, "Scan saw %u network%s, %u of them ours",
             (unsigned) found, found == 1 ? "" : "s", (unsigned) seen_count);

    s_plan_and_start(s_seen, seen_count);
}


static void s_begin_round(void)
{
    s_replan_next = false;

    //  With one network there is nothing to choose between, and a scan would
    //  be two seconds of delay for an answer we already have. This is the
    //  common case: most devices stand in one place and know one network.
    if (s_filled_slots() <= 1) {
        s_plan_and_start(NULL, 0);
        return;
    }

    //  Not while somebody is standing in the portal: a scan takes the radio
    //  away from the access point and drops their browser.
    if (s_portal != PORTAL_CLOSED) {
        ESP_LOGI(TAG, "Portal is open, planning without a scan");
        s_plan_and_start(NULL, 0);
        return;
    }

    wifi_scan_config_t scan_config = { .show_hidden = false };

    //  Asked for, not waited for: the answer arrives as WIFI_EVENT_SCAN_DONE.
    //  A blocking scan here would hold up the timer task, or the event task,
    //  for as long as the radio takes to walk the channels.
    esp_err_t err = esp_wifi_scan_start(&scan_config, false);

    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Could not scan (%s); trying the networks in order",
                 esp_err_to_name(err));
        s_plan_and_start(NULL, 0);
        return;
    }

    s_scan_for_plan = true;
}


//  --------------------------------------------------------------------------
//  Connect using what the device remembers. The slots are read here, in the
//  task that starts the device, so that nothing further along has to.

esp_err_t wifi_prov_connect_stored(void)
{
    esp_err_t err = wifi_storage_load_slots(s_slots);
    if (err != ESP_OK) {
        return err;
    }

    s_last_ok = wifi_storage_last_ok();

    size_t filled = 0;

    for (size_t slot = 0; slot < WIFI_SLOT_COUNT; slot++) {
        if (!wifi_slots_is_empty(&s_slots [slot])) {
            filled++;
        }
    }

    if (filled == 0) {
        return ESP_ERR_NOT_FOUND;   //  Nothing stored: this is a new device
    }

    ESP_LOGI(TAG, "%u stored network%s, last success in slot %d",
             (unsigned) filled, filled == 1 ? "" : "s", s_last_ok);

    s_using_slots = true;

    xEventGroupClearBits(s_wifi_event_group,
                         WIFI_CONNECTED_BIT | WIFI_FAIL_BIT | WIFI_REJECTED_BIT);

    s_reset_retry_schedule();
    s_begin_round();

    return ESP_OK;
}

//  Derived, never stored. An open portal is what somebody standing in front of
//  the device needs to see; underneath it the link keeps its own state, which
//  comes back by itself the moment the portal closes.

wifi_prov_state_t wifi_prov_get_state(void)
{
    if (s_portal != PORTAL_CLOSED) {
        return WIFI_PROV_STATE_AP_ACTIVE;
    }

    switch (s_link) {
        case LINK_CONNECTED:  return WIFI_PROV_STATE_CONNECTED;
        case LINK_CONNECTING: return WIFI_PROV_STATE_CONNECTING;
        case LINK_FAILED:     return WIFI_PROV_STATE_CONNECT_FAILED;
        case LINK_REJECTED:   return WIFI_PROV_STATE_REJECTED;
        default:              return WIFI_PROV_STATE_IDLE;
    }
}

esp_err_t wifi_prov_scan(wifi_ap_record_t *records, uint16_t *count)
{
    assert (records);           //  Caller's contract, not client input
    assert (count);

    wifi_scan_config_t scan_config = {
        .show_hidden = false,
    };

    //  A scan can legitimately be refused - one is already running, or the
    //  reconnect timer just asked for a connection. That is a busy signal for
    //  whoever clicked "Refresh networks", not a reason to reboot the device.
    esp_err_t err = esp_wifi_scan_start(&scan_config, true);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Scan refused: %s", esp_err_to_name(err));
        return err;
    }

    return esp_wifi_scan_get_ap_records(count, records);
}

bool wifi_prov_is_connected(void)
{
    if (s_wifi_event_group == NULL) {
        return false;
    }

    EventBits_t bits = xEventGroupGetBits(s_wifi_event_group);
    return (bits & WIFI_CONNECTED_BIT) != 0;
}

const char *wifi_prov_ap_ssid(void)
{
    return PROV_AP_SSID;
}

//  --------------------------------------------------------------------------
//  Which of the remembered networks is in use, and how many there are. Both
//  answer from what is already in memory: the screen asks on every paint, and
//  that is no reason to go to flash.

int wifi_prov_current_slot(void)
{
    return s_slot_in_use;
}


size_t wifi_prov_stored_count(void)
{
    return s_filled_slots();
}


const char *wifi_prov_current_ssid(void)
{
    return current_ssid;
}

void wifi_prov_ip_string(char *text, size_t length)
{
    assert (text);              //  Caller's contract
    assert (length > 0);

    text [0] = '\0';

    esp_netif_ip_info_t info;

    if (s_sta_netif == NULL
    ||  esp_netif_get_ip_info(s_sta_netif, &info) != ESP_OK
    ||  info.ip.addr == 0) {
        return;
    }

    snprintf(text, length, IPSTR, IP2STR(&info.ip));
}

void wifi_prov_note_portal_activity(void)
{
    s_last_portal_activity = xTaskGetTickCount ();
}

void wifi_prov_wait_until_completed(void)
{
    xEventGroupWaitBits(
        s_wifi_event_group,
        WIFI_CONNECTED_BIT,
        pdFALSE,
        pdTRUE,
        portMAX_DELAY);

    //  Wait until the API has been set up and validated. The portal is what
    //  normally does that: /api-check calls setup and fetch_token in the httpd
    //  task and records the result. Retrying here as well is only for the case
    //  where the stored credentials are fine but the API was unreachable at
    //  boot - so it happens on a slow tick, not once a second, and it goes
    //  through the same lock as the portal.

    //  On this path nobody has read NVS yet: main.c only does that when the
    //  device connects with credentials it already had. Somebody who moves the
    //  device to another router therefore got asked for API keys again, while
    //  perfectly good ones were sitting in flash. Those keys are long, hard to
    //  type and usually not to hand, so try them before asking.
    if (energyboxx_api_load_stored_credentials() == ESP_OK) {
        ESP_LOGI (TAG, "Stored API credentials still work, no need to ask again");
        wifi_prov_note_credentials_accepted();
    }

    TickType_t next_retry = xTaskGetTickCount ();
    wifi_prov_note_portal_activity ();

    while (energyboxx_api_is_valid_credentials() == false) {
        if (energyboxx_api_has_credentials()
        &&  (int32_t) (xTaskGetTickCount () - next_retry) >= 0) {
            energyboxx_api_fetch_token();
            next_retry = xTaskGetTickCount ()
                       + pdMS_TO_TICKS (API_RETRY_WHILE_PROVISIONING_MS);
        }

        if (xTaskGetTickCount () - s_last_portal_activity
        >   pdMS_TO_TICKS (PROVISIONING_SILENCE_TIMEOUT_MS)) {
            ESP_LOGW (TAG, "Nobody used the portal for %d minutes, restarting to "
                           "try the saved network again",
                      PROVISIONING_SILENCE_TIMEOUT_MS / 60000);
            esp_restart ();
        }

        vTaskDelay(pdMS_TO_TICKS(1000));
    }

    //  Three seconds so a browser that just got "done" can show it.
    vTaskDelay(pdMS_TO_TICKS(3000));

    wifi_prov_close_portal();
}

//  Waits for the connection, and stops waiting early once the network has
//  refused the password. Sitting out the full timeout for credentials that
//  cannot work only delays the portal, which is the one thing that can help.

bool wifi_prov_wait_for_connection_timeout(TickType_t timeout)
{
    EventBits_t bits = xEventGroupWaitBits(
        s_wifi_event_group,
        WIFI_CONNECTED_BIT | WIFI_REJECTED_BIT,
        pdFALSE,
        pdFALSE,        //  Either bit ends the wait, not both
        timeout
    );

    return (bits & WIFI_CONNECTED_BIT) != 0;
}
