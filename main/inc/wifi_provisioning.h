#ifndef WIFI_PROVISIONING_H
#define WIFI_PROVISIONING_H

#include "esp_err.h"
#include <stdbool.h>
#include "esp_wifi_types.h"
#include "portmacro.h"

typedef enum {
    WIFI_PROV_STATE_IDLE = 0,
    WIFI_PROV_STATE_AP_ACTIVE,
    WIFI_PROV_STATE_CONNECTING,
    WIFI_PROV_STATE_CONNECTED,
    WIFI_PROV_STATE_CONNECT_FAILED,
    WIFI_PROV_STATE_REJECTED,       //  The network refused the password
} wifi_prov_state_t;

esp_err_t wifi_prov_init(void);

esp_err_t wifi_prov_start_ap(void);
esp_err_t wifi_prov_connect(const char *ssid, const char *password);

//  Connect using the networks the device remembers. Scans, puts the stored
//  networks in the order worth trying, and works through them: a network that
//  is not here hands over to the next one at once instead of costing a
//  timeout. Returns ESP_ERR_NOT_FOUND when no network is stored, which is
//  the case for a device nobody has set up yet.
esp_err_t wifi_prov_connect_stored(void);

//  What the connection itself is doing, with an open portal left out of it.
//  The screen wants wifi_prov_get_state (); the page inside the portal wants
//  this one, because it is asking about the connection it just started.
wifi_prov_state_t wifi_prov_link_state(void);

//  Which remembered network is in use, counting from zero, or -1 when the
//  device is not on one of them.
int wifi_prov_current_slot(void);

//  How many of the slots hold a network.
size_t wifi_prov_stored_count(void);
esp_err_t wifi_prov_scan(wifi_ap_record_t *records, uint16_t *count);
wifi_prov_state_t wifi_prov_get_state(void);
bool wifi_prov_is_connected(void);
void wifi_prov_wait_until_completed(void);

//  Tell the provisioning wait that somebody is actually using the portal. Any
//  handler that a person triggers calls this; without it the wait eventually
//  gives up and restarts the device.
void wifi_prov_note_portal_activity(void);

//  The name of the access point the device offers while provisioning. The
//  screen shows it, so it must come from the same place the radio uses.
const char *wifi_prov_ap_ssid(void);

//  The network the device is trying to join, or an empty string when there is
//  none. Never NULL.
const char *wifi_prov_current_ssid(void);

//  The address the device has on the network it joined, as text. Writes an
//  empty string when there is no address yet. Never fails.
void wifi_prov_ip_string(char *text, size_t length);

//  Open the provisioning portal while the device stays on its own network, so
//  somebody can enter new API credentials without filling in the Wi-Fi details
//  again. Closes itself once the credentials are accepted, or after the same
//  silence timeout that guards provisioning at start-up.
esp_err_t wifi_prov_open_portal(void);

//  Whether the portal is open right now.
bool wifi_prov_portal_is_open(void);

//  Shut the portal down and go back to station only.
esp_err_t wifi_prov_close_portal(void);

//  The portal accepted new credentials. Called by the web handler at the
//  moment it happens, because that moment is far too short to notice by
//  looking every few seconds - the credentials are invalid for well under a
//  second while they are replaced.
void wifi_prov_note_credentials_accepted(void);

//  Who to tell when that happens, so the screen can say so. May be NULL.
typedef void (*wifi_prov_accepted_cb_t)(void);
void wifi_prov_set_credentials_accepted_cb(wifi_prov_accepted_cb_t callback);
bool wifi_prov_wait_for_connection_timeout(TickType_t timeout);

#endif // WIFI_PROVISIONING_H