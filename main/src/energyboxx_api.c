#include <assert.h>
#include <stdio.h>
#include <inttypes.h>

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#include "esp_err.h"

#include "energyboxx_api.h"

#include "esp_http_client.h"
#include "esp_crt_bundle.h"
#include "esp_timer.h"
#include "esp_log.h"
#include <string.h>
#include "cJSON.h"

#define ENERGYBOXX_TOKEN_URL "https://energyboxx.grexx.today/oauth/access_token"

#define ENERGYBOXX_DATA_URL "https://energyboxx.grexx.today/api/v1/form/1:10173:112860/1:10310:6276618"

#define TOKEN_REFRESH_MARGIN_SECONDS (5 * 60)

#define RESPONSE_BUFFER_SIZE 4096

static char access_token[2048] = {0};
static int expires_in_seconds = 0;
static int64_t token_acquired_us = 0;

static char client_id[128] = {0};
static char client_secret[256] = {0};

static bool renew_token = true;

static const char *TAG = "[energyboxx_api]";

//  These two are read without the lock by the status LED task, which runs at
//  2 Hz and must not stall behind a ten-second HTTP request. A bool read is
//  atomic on this target; volatile keeps the compiler from caching it.

static volatile bool credentials_configured = false;
static volatile bool valid_credentials = false;

//  One lock over the credentials, the access token, and the flags that
//  describe them. Two tasks legitimately reach this module: the httpd task
//  while someone fills in the provisioning form, and the task that waits for
//  provisioning to complete. Without the lock both can be inside the same
//  strncpy and the same token buffer at once, which surfaces as "Invalid
//  Client ID or Client Secret" for credentials that are perfectly good.

static StaticSemaphore_t s_lock_storage;
static SemaphoreHandle_t s_lock = NULL;


//  --------------------------------------------------------------------------
//  Take and release the module lock. Waiting forever is intended: the holder
//  is doing an HTTP request with a ten-second timeout, so it always comes back.

static void
    s_lock_acquire (void)
{
    assert (s_lock);            //  energyboxx_api_init () was never called
    xSemaphoreTake (s_lock, portMAX_DELAY);
}

static void
    s_lock_release (void)
{
    assert (s_lock);
    xSemaphoreGive (s_lock);
}


//  --------------------------------------------------------------------------
//  Create the lock. Idempotent, so a second call is not an error.

esp_err_t energyboxx_api_init(void)
{
    if (s_lock)
        return ESP_OK;

    s_lock = xSemaphoreCreateMutexStatic (&s_lock_storage);
    assert (s_lock);            //  Static creation only fails on a null buffer

    return ESP_OK;
}

static esp_err_t http_event_handler(esp_http_client_event_t *evt)
{
    switch (evt->event_id)
    {
    case HTTP_EVENT_ON_DATA:
        if (evt->user_data != NULL)
        {
            // printf("%.*s", evt->data_len, (char *)evt->data);
            // printf("\n");
            char *buf = (char *)evt->user_data;
            size_t used = strlen(buf); //This works because its a string buffer initialized wiht 0's
            size_t remaining = RESPONSE_BUFFER_SIZE - used - 1;
            if (evt->data_len > remaining) return ESP_ERR_INVALID_SIZE;
            memcpy(buf + used, evt->data, evt->data_len);
            buf[used + evt->data_len] = '\0';
        }
        else
        {
            printf("%.*s", evt->data_len, (char *)evt->data);
        }
        printf("\n");
        break;

    case HTTP_EVENT_ON_FINISH:
        ESP_LOGI(TAG, "HTTP request finished");
        break;

    case HTTP_EVENT_DISCONNECTED:
        ESP_LOGI(TAG, "HTTP disconnected");
        break;

    default:
        break;
    }

    return ESP_OK;
}

static esp_err_t s_fetch_token_locked(void)
{
    int64_t elapsed_seconds = (esp_timer_get_time() - token_acquired_us) / 1000000;

    if ((expires_in_seconds > 0) && (elapsed_seconds < expires_in_seconds - TOKEN_REFRESH_MARGIN_SECONDS) && renew_token == false)
    {
        ESP_LOGI(TAG, "Token still valid for %d more seconds, skipping fetch", expires_in_seconds - (int)elapsed_seconds);
        return ESP_OK;
    }

    valid_credentials = false;
    renew_token = false;
    
    char post_data[512];

    snprintf(post_data, sizeof(post_data),
            "grant_type=client_credentials"
            "&client_id=%s"
            "&client_secret=%s",
            client_id,
            client_secret);

    char response_buffer[RESPONSE_BUFFER_SIZE] = {0};

    esp_http_client_config_t config = {
        .url = "https://energyboxx.grexx.today/oauth/access_token",
        .crt_bundle_attach = esp_crt_bundle_attach,
        .timeout_ms = 10000,
        .user_data = response_buffer,
        .event_handler = http_event_handler,
    };

    esp_http_client_handle_t client = esp_http_client_init(&config);

    if (client == NULL)
    {
        return ESP_FAIL;
    }

    esp_http_client_set_method(client, HTTP_METHOD_POST);
    esp_http_client_set_header(client, "Content-Type", "application/x-www-form-urlencoded");
    esp_http_client_set_post_field(client, post_data, strlen(post_data));

    esp_err_t err = esp_http_client_perform(client);

    if (err == ESP_OK)
    {
        // ESP_LOGI(TAG, "Token status = %d", esp_http_client_get_status_code(client));
        // ESP_LOGI(TAG, "Token response: %s", response_buffer);
        
        cJSON *root = cJSON_Parse(response_buffer);

        if (root == NULL)
        {
            ESP_LOGE(TAG, "Failed to parse JSON response");
            esp_http_client_cleanup(client);
            return ESP_FAIL;
        }

        cJSON *token_json = cJSON_GetObjectItem(root, "access_token");
        cJSON *expires_json = cJSON_GetObjectItem(root, "expires_in");

        if (!cJSON_IsString(token_json))
        {
            ESP_LOGE(TAG, "access_token missing");
            cJSON_Delete(root);
            esp_http_client_cleanup(client);
            return ESP_FAIL;
        }

        if (!cJSON_IsNumber(expires_json))
        {
            ESP_LOGE(TAG, "expires_in missing");
            cJSON_Delete(root);
            esp_http_client_cleanup(client);
            return ESP_FAIL;
        }

        strncpy(
            access_token,
            token_json->valuestring,
            sizeof(access_token) - 1
        );

        access_token[sizeof(access_token) - 1] = '\0';

        token_acquired_us = esp_timer_get_time();
        expires_in_seconds = expires_json->valueint;

        ESP_LOGI(TAG, "Stored token (%d chars)", strlen(access_token));

        ESP_LOGI(TAG, "Token expires in %d seconds", expires_in_seconds);

        cJSON_Delete(root);
        valid_credentials = true;
    }
    else
    {
        ESP_LOGE(TAG, "Token request failed: %s", esp_err_to_name(err));
    }

    esp_http_client_cleanup(client);
    return err;
}

esp_err_t energyboxx_api_fetch_token(void)
{
    s_lock_acquire ();
    esp_err_t err = s_fetch_token_locked ();
    s_lock_release ();

    return err;
}

const char *energyboxx_api_get_token(void)
{
    return access_token;
}

static esp_err_t s_get_data_locked(energyboxx_data_t* data)
{
    esp_err_t err = ESP_OK;

    if (data == NULL)
    {
        ESP_LOGE(TAG, "Null output struct pointer");
        return ESP_ERR_INVALID_ARG;
    }

    char response_buffer[RESPONSE_BUFFER_SIZE] = {0};

    esp_http_client_config_t config = {
        .url = ENERGYBOXX_DATA_URL,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .timeout_ms = 10000,
        .event_handler = http_event_handler,
        .user_data = response_buffer,
    };

    if (strlen(access_token) == 0)
    {
        ESP_LOGE(TAG, "No access token available");
        return ESP_FAIL;
    }

    esp_http_client_handle_t client = esp_http_client_init(&config);

    if (client == NULL)
    {
        ESP_LOGE(TAG, "Failed to initialize HTTP client");
        return ESP_FAIL;
    }

    char auth_header[sizeof(access_token) + 8];

    snprintf(auth_header, sizeof(auth_header), "Bearer %s", access_token);

    err = esp_http_client_set_header(client, "Authorization", auth_header);
    if(err != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to set Authorization header: %s", esp_err_to_name(err));
        esp_http_client_cleanup(client);
        return err;
    }

    err = esp_http_client_set_header(client, "Accept", "application/json");
    if(err != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to set Accept header: %s", esp_err_to_name(err));
        esp_http_client_cleanup(client);
        return err;
    }

    err = esp_http_client_perform(client);

    int status = esp_http_client_get_status_code(client);
    // ESP_LOGI(TAG, "Status = %d", status);
    // ESP_LOGI(TAG, "Response: %s", response_buffer);

    if (err != ESP_OK) {
        ESP_LOGE(TAG, "GET request failed: %s", esp_err_to_name(err));
        esp_http_client_cleanup(client);
        return err;
    }

    if (status == 401 || status == 403 || strstr(response_buffer, "\"AUTH-1000\"")) {
        ESP_LOGW(TAG, "Auth failed, token should be refreshed");
        valid_credentials = false;
        esp_http_client_cleanup(client);
        return ESP_ERR_INVALID_STATE;
    }

    if (status < 200 || status >= 300) {
        ESP_LOGE(TAG, "Unexpected HTTP status: %d", status);
        esp_http_client_cleanup(client);
        return ESP_FAIL;
    }

    if (err == ESP_OK)
    {
        ESP_LOGI(TAG, "Status = %d",
                 esp_http_client_get_status_code(client));

        ESP_LOGI(TAG, "Content length = %" PRId64,
                 esp_http_client_get_content_length(client));


        cJSON *root = cJSON_Parse(response_buffer);
        if (root == NULL)
        {
            ESP_LOGE(TAG, "Failed to parse JSON response");
            esp_http_client_cleanup(client);
            return ESP_FAIL;
        }

        cJSON *import_kw = cJSON_GetObjectItemCaseSensitive(root, "community_power_import_kw");
        if (cJSON_IsNumber(import_kw))
        {
            data->community_power_import_kw = (float)import_kw->valuedouble;
        }
        else
        {
            data->community_power_import_kw = 0.0f;
        }

        cJSON *export_kw = cJSON_GetObjectItemCaseSensitive(root, "community_power_export_kw");
        if (cJSON_IsNumber(export_kw))
        {
            data->community_power_export_kw = (float)export_kw->valuedouble;
        }
        else
        {
            data->community_power_export_kw = 0.0f;
        }
        cJSON *result_kw = cJSON_GetObjectItemCaseSensitive(root, "community_power_result_kw");
        if (cJSON_IsNumber(result_kw))
        {
            data->community_power_result_kw = (float)result_kw->valuedouble;
        }
        else
        {
            data->community_power_result_kw = 0.0f;
        }

        cJSON *import_price = cJSON_GetObjectItemCaseSensitive(root, "community_import_price_eur");
        if (cJSON_IsNumber(import_price))
        {
            data->community_import_price_eur = (float)import_price->valuedouble;
        }
        else
        {
            data->community_import_price_eur = 0.0f;
        }

        cJSON *export_price = cJSON_GetObjectItemCaseSensitive(root, "community_export_price_eur");
        if (cJSON_IsNumber(export_price))
        {
            data->community_export_price_eur = (float)export_price->valuedouble;
        }
        else
        {
            data->community_export_price_eur = 0.0f;
        }

        cJSON *shared_import_price = cJSON_GetObjectItemCaseSensitive(root, "community_shared_import_price_eur");
        if (cJSON_IsNumber(shared_import_price))
        {
            data->community_shared_import_price_eur = (float)shared_import_price->valuedouble;
        }
        else
        {
            data->community_shared_import_price_eur = 0.0f;
        }

        cJSON *shared_export_price = cJSON_GetObjectItemCaseSensitive(root, "community_shared_export_price_eur");
        if (cJSON_IsNumber(shared_export_price))
        {
            data->community_shared_export_price_eur = (float)shared_export_price->valuedouble;
        }
        else
        {
            data->community_shared_export_price_eur = 0.0f;
        }

        cJSON_Delete(root);
        esp_http_client_cleanup(client);

        return ESP_OK;
    }
    else
    {
        ESP_LOGE(TAG, "GET request failed: %s", esp_err_to_name(err));
    }

    esp_http_client_cleanup(client);

    return err;
}

esp_err_t energyboxx_api_get_data(energyboxx_data_t* data)
{
    s_lock_acquire ();
    esp_err_t err = s_get_data_locked (data);
    s_lock_release ();

    return err;
}

void energyboxx_data_print(const energyboxx_data_t *data)
{
    if (data == NULL)
    {
        ESP_LOGE(TAG, "energyboxx_data_print: null data pointer");
        return;
    }

    ESP_LOGI(TAG, "Energyboxx data:");
    ESP_LOGI(TAG, "  community_power_import_kw       = %.6f",
             data->community_power_import_kw);
    ESP_LOGI(TAG, "  community_power_export_kw       = %.6f",
             data->community_power_export_kw);
    ESP_LOGI(TAG, "  community_power_result_kw       = %.6f",
             data->community_power_result_kw);
    ESP_LOGI(TAG, "  community_export_price_eur      = %.6f",
             data->community_export_price_eur);
    ESP_LOGI(TAG, "  community_import_price_eur      = %.6f",
             data->community_import_price_eur);
    ESP_LOGI(TAG, "  community_shared_import_price_eur = %.6f",
             data->community_shared_import_price_eur);
    ESP_LOGI(TAG, "  community_shared_export_price_eur = %.6f",
             data->community_shared_export_price_eur);
}

void energyboxx_api_set_renew_token(bool renew){
    
    renew_token = renew;
}

bool energyboxx_api_is_valid_credentials(void){
    return valid_credentials;
}


esp_err_t energyboxx_api_setup(const char *c_id, const char *c_secret){
    
    if (c_id == NULL || c_secret == NULL) {
        ESP_LOGE(TAG, "Client ID or Client Secret is NULL");
        return ESP_ERR_INVALID_ARG;
    }

    s_lock_acquire ();

    strncpy(client_id, c_id, sizeof(client_id) - 1);
    client_id[sizeof(client_id) - 1] = '\0';

    strncpy(client_secret, c_secret, sizeof(client_secret) - 1);
    client_secret[sizeof(client_secret) - 1] = '\0';

    credentials_configured = true;
    valid_credentials = false;

    s_lock_release ();

    ESP_LOGI(TAG, "Energyboxx API setup completed with Client ID and Client Secret");

    return ESP_OK;
}

bool energyboxx_api_has_credentials(void)
{
    return credentials_configured;
}
