#include <string.h>

#include "inc/api_storage.h"
#include "esp_log.h"
#include "nvs_flash.h"

static const char *TAG = "[api_storage]";

esp_err_t api_storage_save_credentials(const char *client_id, const char *client_secret)
{
    if (client_id == NULL || client_secret == NULL) {
       return ESP_ERR_INVALID_ARG;
    }
    
    if (strlen(client_id) == 0 || strlen(client_secret) == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    nvs_handle_t nvs_handle;
    esp_err_t err = nvs_open("api_credentials", NVS_READWRITE, &nvs_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to open NVS handle: %s", esp_err_to_name(err));
        return err;
    }

    err = nvs_set_str(nvs_handle, "client_id", client_id);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to save client_id: %s", esp_err_to_name(err));
        nvs_close(nvs_handle);
        return err;
    }

    err = nvs_set_str(nvs_handle, "client_secret", client_secret);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to save client_secret: %s", esp_err_to_name(err));
        nvs_close(nvs_handle);
        return err;
    }

    err = nvs_commit(nvs_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to commit NVS changes: %s", esp_err_to_name(err));
        nvs_close(nvs_handle);
        return err;
    }

    nvs_close(nvs_handle);
    ESP_LOGI(TAG, "API credentials saved successfully");
    return ESP_OK;
}

esp_err_t api_storage_load_credentials(char *client_id, size_t client_id_len, char *client_secret, size_t client_secret_len)
{
    if (client_id == NULL || client_secret == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    nvs_handle_t nvs_handle;
    esp_err_t err = nvs_open("api_credentials", NVS_READONLY, &nvs_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to open NVS handle: %s", esp_err_to_name(err));
        return err;
    }

    size_t required_client_id_len = 0;
    size_t required_client_secret_len = 0;

    err = nvs_get_str(nvs_handle, "client_id", NULL, &required_client_id_len);
    if (err != ESP_OK && err != ESP_ERR_NVS_NOT_FOUND) {
        ESP_LOGE(TAG, "Failed to get client_id length: %s", esp_err_to_name(err));
        nvs_close(nvs_handle);
        return err;
    }

    err = nvs_get_str(nvs_handle, "client_secret", NULL, &required_client_secret_len);
    if (err != ESP_OK && err != ESP_ERR_NVS_NOT_FOUND) {
        ESP_LOGE(TAG, "Failed to get client_secret length: %s", esp_err_to_name(err));
        nvs_close(nvs_handle);
        return err;
    }

    if (required_client_id_len > client_id_len || required_client_secret_len > client_secret_len) {
        ESP_LOGE(TAG, "Provided buffers are too small");
        nvs_close(nvs_handle);
        return ESP_ERR_NVS_INVALID_LENGTH;
    }

    err = nvs_get_str(nvs_handle, "client_id", client_id, &required_client_id_len);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Failed to read client_id: %s", esp_err_to_name(err));
        nvs_close(nvs_handle);
        return err;
    }

    err = nvs_get_str(nvs_handle, "client_secret", client_secret, &required_client_secret_len);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Failed to read client_secret: %s", esp_err_to_name(err));
        nvs_close(nvs_handle);
        return err;
    }

    nvs_close(nvs_handle);
    ESP_LOGI(TAG, "API credentials loaded successfully");
    return ESP_OK;
}

esp_err_t api_storage_clear_credentials(void)
{
    nvs_handle_t nvs_handle;
    esp_err_t err = nvs_open("api_credentials", NVS_READWRITE, &nvs_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to open NVS handle: %s", esp_err_to_name(err));
        return err;
    }

    err = nvs_erase_key(nvs_handle, "client_id");
    if (err != ESP_OK && err != ESP_ERR_NVS_NOT_FOUND) {
        ESP_LOGE(TAG, "Failed to erase client_id: %s", esp_err_to_name(err));
        nvs_close(nvs_handle);
        return err;
    }

    err = nvs_erase_key(nvs_handle, "client_secret");
    if (err != ESP_OK && err != ESP_ERR_NVS_NOT_FOUND) {
        ESP_LOGE(TAG, "Failed to erase client_secret: %s", esp_err_to_name(err));
        nvs_close(nvs_handle);
        return err;
    }

    err = nvs_commit(nvs_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to commit NVS changes: %s", esp_err_to_name(err));
        nvs_close(nvs_handle);
        return err;
    }

    nvs_close(nvs_handle);
    ESP_LOGI(TAG, "API credentials cleared successfully");
    return ESP_OK;
}