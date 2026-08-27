#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_wifi.h"

#include "inc/wifi_web.h"
#include "inc/wifi_provisioning.h"
#include "inc/energyboxx_api.h"
#include "inc/api_storage.h"
#include "inc/uri_decode.h"

static const char *TAG = "[wifi_web]";

static httpd_handle_t s_server = NULL;

static esp_err_t favicon_get_handler(httpd_req_t *req)
{
    httpd_resp_set_status(req, "204 No Content");
    return httpd_resp_send(req, NULL, 0);
}

static esp_err_t root_get_handler(httpd_req_t *req)
{
    if (wifi_prov_is_connected()) {
        httpd_resp_set_status(req, "303 See Other");
        httpd_resp_set_hdr(req, "Location", "/api-setup");
        return httpd_resp_send(req, "Redirecting to API setup", HTTPD_RESP_USE_STRLEN);
    }

    const char *html =
        "<!DOCTYPE html>"
        "<html>"
        "<head>"
        "<meta name='viewport' content='width=device-width, initial-scale=1'>"
        "<title>SETD WiFi Setup</title>"
        "<style>"
        "body{font-family:Arial,sans-serif;margin:0;background:#f4f6f8;color:#111;}"
        ".card{max-width:420px;margin:40px auto;padding:24px;background:white;border-radius:16px;"
        "box-shadow:0 8px 30px rgba(0,0,0,.12);}"
        "h1{margin-top:0;font-size:28px;}"
        "label{display:block;margin-top:18px;font-weight:bold;}"
        "select,input,button{width:100%;box-sizing:border-box;font-size:16px;padding:12px;margin-top:8px;"
        "border-radius:10px;border:1px solid #ccc;}"
        "button{background:#111;color:white;border:none;margin-top:24px;font-weight:bold;}"
        "button:disabled{background:#999;}"
        "#status{margin-top:18px;font-weight:bold;}"
        "</style>"
        "</head>"
        "<body>"
        "<div class='card'>"
        "<h1>SETD WiFi Setup</h1>"
        "<p>Choose the WiFi network this device should connect to.</p>"

        "<label for='ssid'>Network</label>"
        "<select id='ssid'>"
        "<option>Scanning...</option>"
        "</select>"
        "<button type='button' onclick='scan()'>Refresh networks</button>"

        "<label for='password'>Password</label>"
        "<input id='password' type='password' placeholder='WiFi password'>"

        "<button id='connectBtn' onclick='connectWifi()'>Connect</button>"
        "<div id='status'>Status: Ready</div>"
        "</div>"

        "<script>"
        "async function scan(){"
        " const ssid=document.getElementById('ssid');"
        " ssid.innerHTML='<option>Scanning...</option>';"
        " try{"
        "  const r=await fetch('/scan');"
        "  const networks=await r.json();"
        "  ssid.innerHTML='';"
        "  networks.forEach(n=>{"
        "   const o=document.createElement('option');"
        "   o.value=n.ssid;"
        "   o.textContent=n.ssid+' ('+n.rssi+' dBm)';"
        "   ssid.appendChild(o);"
        "  });"
        " }catch(e){ssid.innerHTML='<option>Scan failed</option>';}"
        "}"
        ""
        "async function connectWifi(){"
        " const btn=document.getElementById('connectBtn');"
        " const status=document.getElementById('status');"
        " btn.disabled=true;"
        " status.textContent='Status: Connecting...';"
        " const ssid = document.getElementById('ssid').value;"
        " const password = document.getElementById('password').value;"
        " await fetch('/connect', {"
        "     method: 'POST',"
        "     headers: {'Content-Type': 'application/x-www-form-urlencoded'},"
        "     body: `ssid=${encodeURIComponent(ssid)}&password=${encodeURIComponent(password)}`"
        " });"
        " pollStatus();"
        "}"
        ""
        "async function pollStatus(){"
        " const btn=document.getElementById('connectBtn');"
        " const status=document.getElementById('status');"
        " const timer=setInterval(async()=>{"
        "  const r=await fetch('/status');"
        "  const s=await r.json();"
        "  status.textContent='Status: '+s.state;"
        "  if(s.state==='connected'){clearInterval(timer);window.location.href='/api-setup';return;}"
        "  if(s.state==='failed'){clearInterval(timer);status.textContent='Could not connect. Check password.';btn.disabled=false;}"
        " },1000);"
        "}"
        "scan();"
        "</script>"
        "</body>"
        "</html>";

    httpd_resp_set_type(req, "text/html");
    return httpd_resp_send(req, html, HTTPD_RESP_USE_STRLEN);
}

static esp_err_t api_setup_get_handler(httpd_req_t *req)
{
    const bool api_ready = energyboxx_api_has_credentials() && energyboxx_api_is_valid_credentials();

    //  The page goes out in pieces instead of through snprintf, because the
    //  CSS below contains "width:100%" and printf reads that as a conversion
    //  specification. The two variable pieces are the button's disabled
    //  attribute and the flag the script reads.

    const char *html_head =
        "<!DOCTYPE html><html><head>"
        "<meta name='viewport' content='width=device-width, initial-scale=1'>"
        "<title>SETD API Setup</title>"
        "<style>"
        "body{font-family:Arial,sans-serif;margin:0;background:#f4f6f8;color:#111;}"
        ".card{max-width:420px;margin:40px auto;padding:24px;background:white;border-radius:16px;"
        "box-shadow:0 8px 30px rgba(0,0,0,.12);}"
        "label{display:block;margin-top:18px;font-weight:bold;}"
        "input,button{width:100%;box-sizing:border-box;font-size:16px;padding:12px;margin-top:8px;"
        "border-radius:10px;border:1px solid #ccc;}"
        "button{background:#111;color:white;border:none;margin-top:24px;font-weight:bold;}"
        "button:disabled{background:#999;}"
        "#status{margin-top:18px;font-weight:bold;}"
        "</style></head><body>"
        "<div class='card'>"
        "<h1>SETD API Setup</h1>"
        "<p>Enter your API credentials.</p>"

        "<label for='clientId'>Client ID</label>"
        "<input id='clientId' type='text' placeholder='Client ID'>"

        "<label for='clientSecret'>Client Secret</label>"
        "<input id='clientSecret' placeholder='Client Secret'>"

        "<button id='checkBtn' onclick='checkKeys()' ";

    const char *html_body =
        ">Check and save</button>"

        "<div id='status'>Status: Ready</div>"
        "</div>"

        "<script>"
        "const apiReady=";

    const char *html_tail =
        ";"
        "if(apiReady){document.getElementById('checkBtn').disabled=true;}"
        "async function checkKeys(){"
        " const status=document.getElementById('status');"
        " const btn=document.getElementById('checkBtn');"
        " status.textContent='Status: Checking keys...';"
        " btn.disabled=true;"
        " const clientId=document.getElementById('clientId').value;"
        " const clientSecret=document.getElementById('clientSecret').value;"
        " try{"
        "  const r=await fetch('/api-check',{"
        "   method:'POST',"
        "   headers:{'Content-Type':'application/x-www-form-urlencoded'},"
        "   body:`client_id=${encodeURIComponent(clientId)}&client_secret=${encodeURIComponent(clientSecret)}`"
        "  });"
        "  const j=await r.json();"
        "  if(j.ok){btn.disabled=true;status.textContent='Status: Validation successful, you can close this page!';return;}"
        "  status.textContent=j.message||'Status: Invalid keys';"
        "  btn.disabled=false;"
        " }catch(e){status.textContent='Status: Check failed';btn.disabled=false;}"
        "}"
        "</script></body></html>";

    const char *piece [] = {
        html_head,
        api_ready ? "disabled" : "",
        html_body,
        api_ready ? "true" : "false",
        html_tail
    };

    httpd_resp_set_type(req, "text/html");

    for (size_t index = 0; index < sizeof (piece) / sizeof (piece [0]); index++) {
        //  A zero-length chunk is how chunked encoding says "end of response",
        //  so an empty piece has to be skipped rather than sent.
        if (piece [index][0] == '\0')
            continue;

        esp_err_t err = httpd_resp_send_chunk(req, piece [index], HTTPD_RESP_USE_STRLEN);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Failed to send API setup page: %s", esp_err_to_name(err));
            return err;
        }
    }

    return httpd_resp_send_chunk(req, NULL, 0);
}

static const char *state_to_string(wifi_prov_state_t state)
{
    switch (state) {
        case WIFI_PROV_STATE_IDLE: return "idle";
        case WIFI_PROV_STATE_AP_ACTIVE: return "ready";
        case WIFI_PROV_STATE_CONNECTING: return "connecting";
        case WIFI_PROV_STATE_CONNECTED: return "connected";
        case WIFI_PROV_STATE_CONNECT_FAILED: return "failed";
        default: return "unknown";
    }
}

static esp_err_t status_get_handler(httpd_req_t *req)
{
    char json[64];

    snprintf(json, sizeof(json),"{\"state\":\"%s\"}", state_to_string(wifi_prov_get_state()));

    httpd_resp_set_type(req, "application/json");
    return httpd_resp_send(req, json, HTTPD_RESP_USE_STRLEN);
}

static esp_err_t connect_post_handler(httpd_req_t *req)
{
    char body[160] = {0};

    int received = httpd_req_recv(req, body, sizeof(body) - 1);
    if (received <= 0) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "No body");
        return ESP_FAIL;
    }

    body[received] = '\0';

    char ssid[33] = {0};
    char password[65] = {0};

    if (httpd_query_key_value(body, "ssid", ssid, sizeof(ssid)) != ESP_OK) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Missing ssid");
        return ESP_FAIL;
    }

    httpd_query_key_value(body, "password", password, sizeof(password));

    // httpd_query_key_value does not decode percent-escapes, so the values are
    // still exactly as the browser encoded them.
    if (!uri_decode(ssid) || !uri_decode(password)) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Malformed ssid or password encoding");
        return ESP_FAIL;
    }

    esp_err_t err = wifi_prov_connect(ssid, password);
    if (err != ESP_OK) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Connect failed");
        return err;
    }

    httpd_resp_set_type(req, "application/json");
    return httpd_resp_sendstr(req, "{\"ok\":true}");
}

//  How a refused scan is reported to the browser. The last row is the
//  catch-all for anything not listed above it, so this table is never
//  incomplete; add a row to give a specific failure its own answer.

static const struct {
    esp_err_t code;
    const char *status;
    const char *message;
} s_scan_error [] = {
    { ESP_ERR_WIFI_STATE,       "503 Service Unavailable",
      "{\"error\":\"Scan busy, try again\"}"   },
    { ESP_ERR_WIFI_NOT_STARTED, "503 Service Unavailable",
      "{\"error\":\"Radio not ready yet\"}"    },
    { ESP_ERR_WIFI_TIMEOUT,     "504 Gateway Timeout",
      "{\"error\":\"Scan timed out\"}"         },
    { ESP_FAIL,                 "500 Internal Server Error",
      "{\"error\":\"Scan failed\"}"            }
};

#define SCAN_ERROR_ROWS  (sizeof (s_scan_error) / sizeof (s_scan_error [0]))

static esp_err_t scan_get_handler(httpd_req_t *req)
{
    wifi_ap_record_t aps[20];
    uint16_t count = 20;

    esp_err_t err = wifi_prov_scan(aps, &count);
    if (err != ESP_OK) {
        size_t row = 0;
        while (row < SCAN_ERROR_ROWS - 1 && s_scan_error [row].code != err)
            row++;

        httpd_resp_set_status(req, s_scan_error [row].status);
        httpd_resp_set_type(req, "application/json");
        httpd_resp_sendstr(req, s_scan_error [row].message);
        return ESP_OK;
    }

    httpd_resp_set_type(req, "application/json");

    httpd_resp_sendstr_chunk(req, "[");

    for (int i = 0; i < count; i++) {
        char item[128];

        snprintf(item, sizeof(item),
                 "%s{\"ssid\":\"%s\",\"rssi\":%d}",
                 i == 0 ? "" : ",",
                 (char *)aps[i].ssid,
                 aps[i].rssi);

        httpd_resp_sendstr_chunk(req, item);
    }

    httpd_resp_sendstr_chunk(req, "]");
    httpd_resp_sendstr_chunk(req, NULL);

    return ESP_OK;
}

// HTTP Error (404) Handler - Redirects all requests to the root page
esp_err_t http_404_error_handler(httpd_req_t *req, httpd_err_code_t err)
{
    // Set status
    httpd_resp_set_status(req, "303 See Other");
    // Redirect to the "/" root directory
    httpd_resp_set_hdr(req, "Location", "/");
    // iOS requires content in the response to detect a captive portal, simply redirecting is not sufficient.
    httpd_resp_send(req, "Redirect to the captive portal", HTTPD_RESP_USE_STRLEN);

    ESP_LOGI(TAG, "Redirecting to root");
    return ESP_OK;
}

static esp_err_t parse_api_credentials(httpd_req_t *req,
                                       char *client_id,
                                       size_t client_id_len,
                                       char *client_secret,
                                       size_t client_secret_len)
{
    char body[512] = {0};

    int received = httpd_req_recv(req, body, sizeof(body) - 1);
    if (received <= 0) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "No body");
        return ESP_FAIL;
    }

    body[received] = '\0';

    if (httpd_query_key_value(body, "client_id", client_id, client_id_len) != ESP_OK ||
        httpd_query_key_value(body, "client_secret", client_secret, client_secret_len) != ESP_OK) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Missing credentials");
        return ESP_FAIL;
    }

    return ESP_OK;
}

static esp_err_t api_check_post_handler(httpd_req_t *req)
{
    char client_id[128] = {0};
    char client_secret[256] = {0};

    if (parse_api_credentials(req, client_id, sizeof(client_id), client_secret, sizeof(client_secret)) != ESP_OK) {
        httpd_resp_set_type(req, "application/json");
        return httpd_resp_sendstr(req,
            "{\"ok\":false,\"message\":\"Invalid request\"}");
    }

    if (!uri_decode(client_id) || !uri_decode(client_secret)) {
        httpd_resp_set_type(req, "application/json");
        return httpd_resp_sendstr(req,
            "{\"ok\":false,\"message\":\"Malformed credential encoding\"}");
    }

    esp_err_t err = energyboxx_api_setup(client_id, client_secret);
    if (err != ESP_OK) {
        httpd_resp_set_type(req, "application/json");
        return httpd_resp_sendstr(req,
            "{\"ok\":false,\"message\":\"Setup failed\"}");
    }

    err = energyboxx_api_fetch_token();
    if (err != ESP_OK) {
        httpd_resp_set_type(req, "application/json");
        return httpd_resp_sendstr(req,
            "{\"ok\":false,\"message\":\"Invalid Client ID or Client Secret\"}");
    }

    // If we reach here, the credentials are valid store them in flash
    err = api_storage_save_credentials(client_id, client_secret);
    if (err != ESP_OK) {
        httpd_resp_set_type(req, "application/json");
        return httpd_resp_sendstr(req,
            "{\"ok\":false,\"message\":\"Failed to save credentials\"}");
    }

    httpd_resp_set_type(req, "application/json");
    return httpd_resp_sendstr(req, "{\"ok\":true}");
}

esp_err_t wifi_web_start(void)
{
    if (s_server != NULL) {
        return ESP_OK;
    }

    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port = 80;
    config.stack_size = 16384;

    ESP_LOGI(TAG, "Starting HTTP server");

    esp_err_t err = httpd_start(&s_server, &config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start HTTP server: %s", esp_err_to_name(err));
        return err;
    }

    httpd_uri_t root_uri = {
        .uri = "/",
        .method = HTTP_GET,
        .handler = root_get_handler,
        .user_ctx = NULL,
    };

    httpd_uri_t favicon_uri = {
        .uri = "/favicon.ico",
        .method = HTTP_GET,
        .handler = favicon_get_handler,
        .user_ctx = NULL,
    };

    httpd_uri_t status_uri = {
        .uri = "/status",
        .method = HTTP_GET,
        .handler = status_get_handler,
        .user_ctx = NULL,
    };

    httpd_uri_t connect_uri = {
        .uri = "/connect",
        .method = HTTP_POST,
        .handler = connect_post_handler,
        .user_ctx = NULL,
    };

    httpd_uri_t scan_uri = {
        .uri = "/scan",
        .method = HTTP_GET,
        .handler = scan_get_handler,
    };

    httpd_uri_t api_setup_uri = {
        .uri = "/api-setup",
        .method = HTTP_GET,
        .handler = api_setup_get_handler,
        .user_ctx = NULL,
    };

    httpd_uri_t api_check_uri = {
        .uri = "/api-check",
        .method = HTTP_POST,
        .handler = api_check_post_handler,
        .user_ctx = NULL,
    };

    err = httpd_register_uri_handler(s_server, &favicon_uri);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to register favicon URI handler: %s", esp_err_to_name(err));
        return err;
    }

    err = httpd_register_uri_handler(s_server, &root_uri);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to register root URI handler: %s", esp_err_to_name(err));
        return err;
    }

    err = httpd_register_err_handler(s_server, HTTPD_404_NOT_FOUND, http_404_error_handler);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to register 404 error handler: %s", esp_err_to_name(err));
        return err;
    }

    err = httpd_register_uri_handler(s_server, &status_uri);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to register status URI handler: %s", esp_err_to_name(err));
        return err;
    }

    err = httpd_register_uri_handler(s_server, &connect_uri);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to register connect URI handler: %s", esp_err_to_name(err));
        return err;
    }

    err = httpd_register_uri_handler(s_server, &scan_uri);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to register scan URI handler: %s", esp_err_to_name(err));
        return err;
    }

    err = httpd_register_uri_handler(s_server, &api_setup_uri);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to register API setup URI handler: %s", esp_err_to_name(err));
        return err;
    }

    err = httpd_register_uri_handler(s_server, &api_check_uri);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to register API check URI handler: %s", esp_err_to_name(err));
        return err;
    }

    return ESP_OK;
}

esp_err_t wifi_web_stop(void)
{
    if (s_server == NULL) {
        return ESP_OK;
    }

    esp_err_t err = httpd_stop(s_server);
    s_server = NULL;
    return err;
}