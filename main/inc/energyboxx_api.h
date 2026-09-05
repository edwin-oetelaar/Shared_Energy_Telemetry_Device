#ifndef ENERGYBOXX_API_H
#define ENERGYBOXX_API_H

#include <stdbool.h>

#include "esp_err.h"

typedef struct
{
    float community_power_import_kw;
    float community_power_export_kw;
    float community_power_result_kw;

    float community_export_price_eur;
    float community_import_price_eur;

    float community_shared_import_price_eur;
    float community_shared_export_price_eur;
} energyboxx_data_t;

//  Creates the lock that guards the credentials and the token. Call once from
//  app_main before any other function in this module; calling it twice is
//  harmless.
esp_err_t energyboxx_api_init(void);

esp_err_t energyboxx_api_setup(const char *client_id, const char *client_secret);

//  Put back the credentials that were in use before the last setup () call, and
//  forget the ones that replaced them. For when somebody types the wrong keys:
//  a refused attempt should not leave a working device without a token.
esp_err_t energyboxx_api_restore_previous(void);

//  Read the credentials from NVS and try them. Returns ESP_OK when the device
//  is ready to fetch telemetry. Anyone who has just got onto a network should
//  call this before asking a person to type keys that are already stored.
esp_err_t energyboxx_api_load_stored_credentials(void);
esp_err_t energyboxx_api_fetch_token(void);
esp_err_t energyboxx_api_get_data(energyboxx_data_t* data);
const char *energyboxx_api_get_token(void);
bool energyboxx_api_has_credentials(void);

//  How long the current token is still good for, in seconds. Zero when there
//  is no token, or when it has expired.
int energyboxx_api_token_seconds_left(void);

//  How long ago the last successful telemetry arrived, in seconds. Negative
//  when none has ever arrived, so callers can tell "never" from "just now".
int energyboxx_api_seconds_since_data(void);

//  The last measurement that arrived, without asking the network for a new
//  one. The screen redraws several times a second and must never turn that
//  into an HTTP request; energyboxx_api_get_data () fetches, this one
//  remembers.
//
//  Returns false when nothing has ever arrived, and leaves `out` untouched.
bool energyboxx_api_last_data(energyboxx_data_t *out);
bool energyboxx_api_is_valid_credentials(void);
void energyboxx_data_print(const energyboxx_data_t *data);
void energyboxx_api_set_renew_token(bool renew);

#endif // ENERGYBOXX_API_H