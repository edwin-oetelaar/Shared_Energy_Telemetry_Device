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

esp_err_t energyboxx_api_setup(const char *client_id, const char *client_secret);
esp_err_t energyboxx_api_fetch_token(void);
esp_err_t energyboxx_api_get_data(energyboxx_data_t* data);
const char *energyboxx_api_get_token(void);
bool energyboxx_api_has_credentials(void);
bool energyboxx_api_is_valid_credentials(void);
void energyboxx_data_print(const energyboxx_data_t *data);
void energyboxx_api_set_renew_token(bool renew);

#endif // ENERGYBOXX_API_H