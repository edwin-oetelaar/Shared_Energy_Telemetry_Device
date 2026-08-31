#include <assert.h>
#include <string.h>

#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_wifi.h"

#include "cJSON.h"

#include "inc/wifi_web.h"
#include "inc/wifi_provisioning.h"
#include "inc/energyboxx_api.h"
#include "inc/api_storage.h"
#include "inc/uri_decode.h"

static const char *TAG = "[wifi_web]";

//  A percent-encoded value is at worst three times its own length, so every
//  buffer that holds a field as it arrives is sized for the encoded form and
//  shrunk by uri_decode () afterwards. The body buffers hold the whole form:
//  the fields plus their names and separators, with room to spare.

#define WIFI_FIELD_SSID_MAX      (3 * 32 + 1)
#define WIFI_FIELD_PASSWORD_MAX  (3 * 64 + 1)
#define WIFI_BODY_MAX            384

#define API_FIELD_ID_MAX         (3 * 128)
#define API_FIELD_SECRET_MAX     (3 * 256)
#define API_BODY_MAX             1280

//  How often a socket timeout is tolerated while reading one body. Bounded on
//  purpose: the server has a single worker, so a client that dribbles bytes
//  must not be able to hold it forever.

#define BODY_RECV_TIMEOUT_RETRIES  3

static httpd_handle_t s_server = NULL;

//  --------------------------------------------------------------------------
//  Read a complete request body and terminate it. httpd_req_recv () may return
//  less than was asked for, so this keeps reading until content_len bytes have
//  arrived. A body that does not fit is refused rather than silently cut in
//  half, which is what used to turn a long password into a wrong one.

static esp_err_t
    s_receive_body (httpd_req_t *req, char *body, size_t body_size)
{
    assert (req);               //  Caller's contract, not client input
    assert (body);
    assert (body_size > 1);

    size_t expected = req->content_len;

    if (expected == 0)
        return ESP_ERR_INVALID_ARG;         //  Nothing to parse
    if (expected > body_size - 1)
        return ESP_ERR_INVALID_SIZE;        //  More than this form can hold

    size_t received = 0;
    int timeouts = 0;

    while (received < expected) {
        int chunk = httpd_req_recv (req, body + received, expected - received);

        if (chunk == HTTPD_SOCK_ERR_TIMEOUT) {
            if (++timeouts > BODY_RECV_TIMEOUT_RETRIES)
                return ESP_ERR_TIMEOUT;
            continue;
        }
        if (chunk <= 0)
            return ESP_FAIL;                //  Connection closed or broken

        received += (size_t) chunk;
    }
    body [received] = '\0';

    return ESP_OK;
}


static esp_err_t favicon_get_handler(httpd_req_t *req)
{
    httpd_resp_set_status(req, "204 No Content");
    return httpd_resp_send(req, NULL, 0);
}

static esp_err_t root_get_handler(httpd_req_t *req)
{
    wifi_prov_note_portal_activity();

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
    wifi_prov_note_portal_activity();

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

        "<button id='checkBtn' onclick='checkKeys()'>Check and save</button>"

        "<div id='status'>";

    //  The one variable part of this page. It used to disable the button when
    //  credentials were already stored, on the assumption that valid
    //  credentials meant there was nothing left to do here. That assumption
    //  broke as soon as the portal could be opened again to replace working
    //  keys: the page then refused the very thing it was opened for. It now
    //  says what it knows and leaves the button alone.
    const char *html_body =
        "</div>"
        "</div>"

        "<script>"
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
        api_ready
            ? "Er zijn al werkende sleutels opgeslagen. Nieuwe invoeren vervangt ze."
            : "Status: Ready",
        html_body
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
    wifi_prov_note_portal_activity();

    char body[WIFI_BODY_MAX] = {0};
    char ssid[WIFI_FIELD_SSID_MAX] = {0};
    char password[WIFI_FIELD_PASSWORD_MAX] = {0};

    esp_err_t err = s_receive_body(req, body, sizeof(body));
    if (err != ESP_OK) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Could not read the form data");
        return ESP_FAIL;
    }

    if (httpd_query_key_value(body, "ssid", ssid, sizeof(ssid)) != ESP_OK) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Missing ssid");
        return ESP_FAIL;
    }

    //  An open network has no password field at all, which is fine. Anything
    //  else - a truncated value in particular - is not, because a silently
    //  shortened password is exactly the failure this fix is about.
    esp_err_t password_err = httpd_query_key_value(body, "password", password, sizeof(password));
    if (password_err != ESP_OK && password_err != ESP_ERR_NOT_FOUND) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Password field could not be read");
        return ESP_FAIL;
    }

    // httpd_query_key_value does not decode percent-escapes, so the values are
    // still exactly as the browser encoded them.
    if (!uri_decode(ssid) || !uri_decode(password)) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Malformed ssid or password encoding");
        return ESP_FAIL;
    }

    // Decoded now, so these are the lengths Wi-Fi actually has to store. Too
    // long is refused here rather than quietly cut down to size later.
    if (strlen(ssid) == 0 || strlen(ssid) > 32 || strlen(password) > 63) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Network name or password is too long");
        return ESP_FAIL;
    }

    err = wifi_prov_connect(ssid, password);
    if (err != ESP_OK) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Connect failed");
        return err;
    }

    //  Deliberately no wifi_prov_note_credentials_accepted () here: this only
    //  starts a connection attempt, and the person still has to enter the API
    //  keys. Closing the portal now would strand them halfway.
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
    wifi_prov_note_portal_activity();

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

    //  Built with cJSON rather than glued together with snprintf. An SSID is
    //  chosen by whoever owns the access point, so it is untrusted input: a
    //  network named  Net","rssi":0},{"ssid":"X  used to break the structure of
    //  this list, and one containing a quote or a backslash made the whole
    //  network picker come up empty. cJSON escapes it instead.
    cJSON *networks = cJSON_CreateArray();
    if (networks == NULL) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Out of memory");
        return ESP_ERR_NO_MEM;
    }

    for (int i = 0; i < count; i++) {
        cJSON *network = cJSON_CreateObject();
        if (network == NULL) {
            break;              //  Send what we have rather than nothing
        }

        cJSON_AddStringToObject(network, "ssid", (const char *) aps[i].ssid);
        cJSON_AddNumberToObject(network, "rssi", aps[i].rssi);
        cJSON_AddItemToArray(networks, network);
    }

    char *body = cJSON_PrintUnformatted(networks);
    cJSON_Delete(networks);

    if (body == NULL) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Out of memory");
        return ESP_ERR_NO_MEM;
    }

    httpd_resp_set_type(req, "application/json");
    esp_err_t send_err = httpd_resp_sendstr(req, body);

    cJSON_free(body);

    return send_err;
}

// HTTP Error (404) Handler - Redirects all requests to the root page
static esp_err_t http_404_error_handler(httpd_req_t *req, httpd_err_code_t err)
{
    (void) err;

    // Set status
    httpd_resp_set_status(req, "303 See Other");
    // Redirect to the "/" root directory
    httpd_resp_set_hdr(req, "Location", "/");
    // iOS requires content in the response to detect a captive portal, simply redirecting is not sufficient.
    httpd_resp_send(req, "Redirect to the captive portal", HTTPD_RESP_USE_STRLEN);

    ESP_LOGI(TAG, "Redirecting to root");
    return ESP_OK;
}

//  --------------------------------------------------------------------------
//  Pull the two credential fields out of the body and decode them. Returns an
//  error code and nothing else: the caller owns the response, because it
//  answers in JSON and two responses on one request corrupt the exchange.

static esp_err_t parse_api_credentials(httpd_req_t *req,
                                       char *client_id,
                                       size_t client_id_len,
                                       char *client_secret,
                                       size_t client_secret_len)
{
    assert (req);               //  Caller's contract, not client input
    assert (client_id);
    assert (client_secret);

    char body[API_BODY_MAX] = {0};
    char id_field[API_FIELD_ID_MAX] = {0};
    char secret_field[API_FIELD_SECRET_MAX] = {0};

    esp_err_t err = s_receive_body(req, body, sizeof(body));
    if (err != ESP_OK) {
        return err;
    }

    if (httpd_query_key_value(body, "client_id", id_field, sizeof(id_field)) != ESP_OK ||
        httpd_query_key_value(body, "client_secret", secret_field, sizeof(secret_field)) != ESP_OK) {
        return ESP_ERR_INVALID_ARG;
    }

    if (!uri_decode(id_field) || !uri_decode(secret_field)) {
        return ESP_ERR_INVALID_ARG;
    }

    size_t id_length = strlen(id_field);
    size_t secret_length = strlen(secret_field);

    if (id_length == 0 || id_length >= client_id_len ||
        secret_length == 0 || secret_length >= client_secret_len) {
        return ESP_ERR_INVALID_SIZE;
    }

    memcpy(client_id, id_field, id_length + 1);
    memcpy(client_secret, secret_field, secret_length + 1);

    return ESP_OK;
}

static esp_err_t api_check_post_handler(httpd_req_t *req)
{
    wifi_prov_note_portal_activity();

    char client_id[128] = {0};
    char client_secret[256] = {0};

    if (parse_api_credentials(req, client_id, sizeof(client_id), client_secret, sizeof(client_secret)) != ESP_OK) {
        httpd_resp_set_type(req, "application/json");
        return httpd_resp_sendstr(req,
            "{\"ok\":false,\"message\":\"Invalid request\"}");
    }

    esp_err_t err = energyboxx_api_setup(client_id, client_secret);
    if (err != ESP_OK) {
        httpd_resp_set_type(req, "application/json");
        return httpd_resp_sendstr(req,
            "{\"ok\":false,\"message\":\"Setup failed\"}");
    }

    err = energyboxx_api_fetch_token();
    if (err != ESP_OK) {
        //  Refused. Put back whatever was working before, so one mistyped
        //  character does not leave the device without credentials until
        //  somebody power-cycles it.
        energyboxx_api_restore_previous();

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

    wifi_prov_note_credentials_accepted();

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