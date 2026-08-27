#ifndef API_STORAGE_H
#define API_STORAGE_H

#include <stddef.h>

#include "esp_err.h"

esp_err_t api_storage_save_credentials(const char *client_id, const char *client_secret);
esp_err_t api_storage_load_credentials(char *client_id, size_t client_id_len, char *client_secret, size_t client_secret_len);
esp_err_t api_storage_clear_credentials(void);

#endif // API_STORAGE_H