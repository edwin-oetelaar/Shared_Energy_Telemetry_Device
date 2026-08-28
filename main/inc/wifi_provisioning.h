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
} wifi_prov_state_t;

esp_err_t wifi_prov_init(void);

esp_err_t wifi_prov_start_ap(void);
esp_err_t wifi_prov_connect(const char *ssid, const char *password);
esp_err_t wifi_prov_scan(wifi_ap_record_t *records, uint16_t *count);
wifi_prov_state_t wifi_prov_get_state(void);
bool wifi_prov_is_connected(void);
void wifi_prov_wait_until_completed(void);

//  Tell the provisioning wait that somebody is actually using the portal. Any
//  handler that a person triggers calls this; without it the wait eventually
//  gives up and restarts the device.
void wifi_prov_note_portal_activity(void);
bool wifi_prov_wait_for_connection_timeout(TickType_t timeout);

#endif // WIFI_PROVISIONING_H