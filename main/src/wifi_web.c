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
#include "inc/wifi_storage.h"

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

static esp_err_t s_send_wifi_page(httpd_req_t *req)
{
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
        ".slots{margin-top:12px;padding:12px;background:#eef1f4;border-radius:10px;"
        "font-size:14px;line-height:1.4;}"
        ".slots .row{display:flex;align-items:center;gap:8px;margin-top:6px;}"
        ".slots .row span{flex:1;}"
        ".slots .use{color:#1f7a3d;font-weight:bold;}"
        ".slots button{width:auto;margin-top:0;padding:6px 12px;font-size:13px;"
        "background:#8a3a2a;border-radius:8px;}"
        "</style>"
        "</head>"
        "<body>"
        "<div class='card'>"
        "<h1>SETD WiFi Setup</h1>"
        "<p>Choose the WiFi network this device should connect to.</p>"

        "<div id='slots' class='slots'></div>"

        "<label for='ssid'>Network</label>"
        "<select id='ssid' onchange='slots()'>"
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
        " slots();"
        "}"
        ""

        //  The device decides where a network goes and what it remembers; this
        //  only shows the answers. Every network name goes in through
        //  textContent and never through innerHTML: an SSID is chosen by
        //  whoever owns the access point, and one with a tag in it must not be
        //  able to write this page.
        "async function slots(){"
        " const sel=document.getElementById('ssid');"
        " const box=document.getElementById('slots');"
        " const q=sel.value?('?ssid='+encodeURIComponent(sel.value)):'';"
        " try{"
        "  const r=await fetch('/networks'+q);"
        "  const d=await r.json();"
        "  const named=d.stored.filter(s=>s.length>0);"
        "  box.textContent='';"
        "  const head=document.createElement('div');"
        "  head.textContent=named.length"
        "   ?('This device remembers '+named.length+' of '+d.stored.length+' networks:')"
        "   :'This device remembers no networks yet.';"
        "  box.appendChild(head);"
        "  d.stored.forEach((s,i)=>{"
        "   if(!s)return;"
        "   const row=document.createElement('div');"
        "   row.className='row';"
        "   const name=document.createElement('span');"
        "   name.textContent=s;"
        "   row.appendChild(name);"
        "   if(i===d.in_use){"
        "    const now=document.createElement('span');"
        "    now.className='use';"
        "    now.textContent='in use now';"
        "    row.appendChild(now);"
        "   }"
        "   const btn=document.createElement('button');"
        "   btn.type='button';"
        "   btn.textContent='Forget';"
        "   btn.onclick=()=>forget(s,i===d.in_use);"
        "   row.appendChild(btn);"
        "   box.appendChild(row);"
        "  });"
        "  if(d.why!==undefined){"
        "   const note=document.createElement('div');"
        "   note.textContent='What you enter now goes in place '+(d.target+1)+' ('+d.why+').';"
        "   box.appendChild(note);"
        "  }"
        " }catch(e){box.textContent='';}"
        "}"
        ""

        "async function forget(ssid,inUse){"
        " let ask='Forget '+ssid+'?';"
        " if(inUse){ask+=String.fromCharCode(10,10)"
        "  +'The device is using this network right now. It stays connected until it restarts,'"
        "  +' and after that it will not come back to this one.';}"
        " if(!confirm(ask))return;"
        " await fetch('/forget',{"
        "  method:'POST',"
        "  headers:{'Content-Type':'application/x-www-form-urlencoded'},"
        "  body:'ssid='+encodeURIComponent(ssid)"
        " });"
        " slots();"
        "}"
        ""
        "async function connectWifi(){"
        " const btn=document.getElementById('connectBtn');"
        " const status=document.getElementById('status');"
        " btn.disabled=true;"
        " status.textContent='Status: Connecting...';"
        " const ssid = document.getElementById('ssid').value;"
        " const password = document.getElementById('password').value;"
        //  The answer was thrown away here. When the device could not even
        //  start - it says so, with a 500 - the page went on polling a state
        //  that was never going to change, and left its button disabled.
        " let r;"
        " try{"
        "  r=await fetch('/connect', {"
        "      method: 'POST',"
        "      headers: {'Content-Type': 'application/x-www-form-urlencoded'},"
        "      body: `ssid=${encodeURIComponent(ssid)}&password=${encodeURIComponent(password)}`"
        "  });"
        " }catch(e){"
        "  status.textContent='The device did not answer. Try again.';"
        "  btn.disabled=false;"
        "  return;"
        " }"
        " if(!r.ok){"
        "  status.textContent='The device could not start connecting. Try again.';"
        "  btn.disabled=false;"
        "  return;"
        " }"
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


//  --------------------------------------------------------------------------
//  Somebody who arrives at the door and has no network yet is taken through
//  the steps in order, and the second step is where they should end up once
//  the first one is done. Somebody who opened the portal on purpose from the
//  device's own screen has usually come for the networks, and the device is
//  online, so the redirect would send them straight past the page they came
//  for. Hence /wifi, which always shows it, linked from the API page.

static esp_err_t root_get_handler(httpd_req_t *req)
{
    wifi_prov_note_portal_activity();

    if (wifi_prov_is_connected()) {
        httpd_resp_set_status(req, "303 See Other");
        httpd_resp_set_hdr(req, "Location", "/api-setup");
        return httpd_resp_send(req, "Redirecting to API setup", HTTPD_RESP_USE_STRLEN);
    }

    return s_send_wifi_page(req);
}


static esp_err_t wifi_get_handler(httpd_req_t *req)
{
    wifi_prov_note_portal_activity();

    return s_send_wifi_page(req);
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
        ".slots{margin-top:12px;padding:12px;background:#eef1f4;border-radius:10px;"
        "font-size:14px;line-height:1.4;}"
        ".slots .row{display:flex;align-items:center;gap:8px;margin-top:6px;}"
        ".slots .row span{flex:1;}"
        ".slots .use{color:#1f7a3d;font-weight:bold;}"
        ".slots button{width:auto;margin-top:0;padding:6px 12px;font-size:13px;"
        "background:#8a3a2a;border-radius:8px;}"
        "</style></head><body>"
        "<div class='card'>"
        "<h1>SETD API Setup</h1>"
        "<p>Enter your API credentials.</p>"

        "<label for='clientId'>Client ID</label>"
        "<input id='clientId' type='text' autocomplete='off' placeholder='Client ID'>"

        "<label for='clientSecret'>Client Secret</label>"
        "<input id='clientSecret' type='password' autocomplete='off' placeholder='Client Secret'>"

        "<button id='checkBtn' onclick='act()'>Controleren en opslaan</button>"

        "<div id='status'>";

    //  Two variable parts. The first is what the status line says on arrival,
    //  the second tells the script whether there are working credentials, which
    //  decides what the button means when the fields are left empty.
    const char *html_body =
        "</div>"
        "<p><a href='/wifi'>Networks this device remembers</a></p>"
        "</div>"

        "<script>"
        "const apiReady=";

    const char *html_tail =
        ";"
        "const id=document.getElementById('clientId');"
        "const sec=document.getElementById('clientSecret');"
        "const btn=document.getElementById('checkBtn');"
        "const status=document.getElementById('status');"
        "let mode='save';"

        //  The button says what it will do, and does what it says. Empty
        //  fields with working credentials means "leave things as they are";
        //  filled fields mean "replace them". Half filled is neither, so the
        //  button is off rather than failing on a blank secret.
        "function refresh(){"
        " const a=id.value.trim()!=='';"
        " const b=sec.value.trim()!=='';"
        " if(a&&b){mode='save';btn.textContent='Controleren en opslaan';btn.disabled=false;}"
        " else if(!a&&!b&&apiReady){mode='done';btn.textContent='Klaar';btn.disabled=false;}"
        " else {mode='save';btn.textContent='Controleren en opslaan';btn.disabled=true;}"
        "}"
        "id.addEventListener('input',refresh);"
        "sec.addEventListener('input',refresh);"
        "refresh();"

        "async function act(){"
        " if(mode==='done'){return finish();}"
        " return checkKeys();"
        "}"

        "async function finish(){"
        " btn.disabled=true;"
        " status.textContent='Afronden...';"
        " try{"
        "  await fetch('/done',{method:'POST'});"
        "  status.textContent='Klaar. Het apparaat gaat verder; u kunt deze pagina sluiten.';"
        " }catch(e){"
        "  status.textContent='Klaar. Het apparaat gaat verder; u kunt deze pagina sluiten.';"
        " }"
        "}"

        "async function checkKeys(){"
        " status.textContent='Sleutels controleren...';"
        " btn.disabled=true;"
        " try{"
        "  const r=await fetch('/api-check',{"
        "   method:'POST',"
        "   headers:{'Content-Type':'application/x-www-form-urlencoded'},"
        "   body:`client_id=${encodeURIComponent(id.value.trim())}"
        "&client_secret=${encodeURIComponent(sec.value.trim())}`"
        "  });"
        "  const j=await r.json();"
        "  if(j.ok){status.textContent='Sleutels opgeslagen. Het apparaat gaat verder.';return;}"
        "  status.textContent=j.message||'Sleutels afgekeurd';"
        "  refresh();"
        " }catch(e){status.textContent='Controle mislukt';refresh();}"
        "}"
        "</script></body></html>";

    const char *piece [] = {
        html_head,
        api_ready
            ? "De opgeslagen sleutels werken. Laat de velden leeg en druk op Klaar, "
              "of vul nieuwe in om ze te vervangen."
            : "Vul de Energyboxx client ID en client secret in.",
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
        //  The page has one branch for "this did not work", and its message -
        //  "Could not connect. Check password." - is exactly right here.
        //  Without this line a refused password left the portal polling a
        //  state it did not know, and the page waited for ever.
        case WIFI_PROV_STATE_REJECTED: return "failed";
        default: return "unknown";
    }
}

static esp_err_t status_get_handler(httpd_req_t *req)
{
    char json[64];

    //  The link, not the merged view. This page is waiting to hear how its own
    //  connection attempt went, and "the portal is open" - which stays true the
    //  whole time somebody is using it - is not an answer to that.
    snprintf(json, sizeof(json),"{\"state\":\"%s\"}", state_to_string(wifi_prov_link_state()));

    httpd_resp_set_type(req, "application/json");
    return httpd_resp_send(req, json, HTTPD_RESP_USE_STRLEN);
}

//  --------------------------------------------------------------------------
//  What the device remembers, and - when asked about a network - which slot
//  that network would take. Somebody about to type a third network can see
//  what it would replace before they do it.
//
//  The rule lives in wifi_slots_choose_for (). It is not repeated in the page,
//  because a rule in two places is a rule that will disagree with itself.

static esp_err_t networks_get_handler(httpd_req_t *req)
{
    wifi_prov_note_portal_activity();

    wifi_slot_t slots [WIFI_SLOT_COUNT];

    if (wifi_storage_load_slots(slots) != ESP_OK) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR,
                            "Could not read the stored networks");
        return ESP_FAIL;
    }

    cJSON *root = cJSON_CreateObject();
    cJSON *stored = cJSON_CreateArray();

    if (root == NULL || stored == NULL) {
        cJSON_Delete(root);
        cJSON_Delete(stored);
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Out of memory");
        return ESP_ERR_NO_MEM;
    }

    //  cJSON for the same reason as the network list: an SSID is chosen by
    //  whoever owns the access point, and a quote in one must not be able to
    //  break this answer apart.
    for (size_t slot = 0; slot < WIFI_SLOT_COUNT; slot++) {
        cJSON_AddItemToArray(stored, cJSON_CreateString(slots [slot].ssid));
    }

    cJSON_AddItemToObject(root, "stored", stored);
    cJSON_AddNumberToObject(root, "in_use", (double) wifi_prov_current_slot());

    //  ?ssid=... asks where that network would go. The name arrives
    //  percent-encoded, like every other field the portal sends.
    char query [176] = {0};
    char ssid [WIFI_SSID_SIZE] = {0};

    if (httpd_req_get_url_query_str(req, query, sizeof(query)) == ESP_OK
    &&  httpd_query_key_value(query, "ssid", ssid, sizeof(ssid)) == ESP_OK
    &&  uri_decode(ssid)
    &&  ssid [0] != '\0') {
        const char *why = NULL;
        size_t target = wifi_slots_choose_for(slots, ssid, &why);

        cJSON_AddNumberToObject(root, "target", (double) target);
        cJSON_AddStringToObject(root, "why", why);
    }

    char *text = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);

    if (text == NULL) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Out of memory");
        return ESP_ERR_NO_MEM;
    }

    httpd_resp_set_type(req, "application/json");
    esp_err_t err = httpd_resp_sendstr(req, text);

    cJSON_free(text);

    return err;
}


//  --------------------------------------------------------------------------
//  Forget one remembered network. Named by its ssid rather than by a slot
//  number: the number on a page that was drawn a minute ago can point at
//  something else by the time somebody presses the button, and a wrong number
//  here wipes the wrong network.

static esp_err_t forget_post_handler(httpd_req_t *req)
{
    wifi_prov_note_portal_activity();

    char body [WIFI_BODY_MAX] = {0};
    char ssid [WIFI_FIELD_SSID_MAX] = {0};

    if (s_receive_body(req, body, sizeof(body)) != ESP_OK) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Could not read the form data");
        return ESP_FAIL;
    }

    if (httpd_query_key_value(body, "ssid", ssid, sizeof(ssid)) != ESP_OK
    ||  !uri_decode(ssid)) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Missing or malformed ssid");
        return ESP_FAIL;
    }

    esp_err_t err = wifi_prov_forget(ssid);

    if (err == ESP_ERR_NOT_FOUND) {
        //  Not a fault. The page may be showing a list that has since changed,
        //  and the outcome the person wanted - this network is not stored - is
        //  the outcome they have.
        httpd_resp_set_type(req, "application/json");
        return httpd_resp_sendstr(req, "{\"ok\":true,\"known\":false}");
    }

    if (err != ESP_OK) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Could not forget it");
        return err;
    }

    httpd_resp_set_type(req, "application/json");
    return httpd_resp_sendstr(req, "{\"ok\":true,\"known\":true}");
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

//  --------------------------------------------------------------------------
//  "Klaar": somebody looked, changed nothing, and wants the device to carry on.
//  Closing the portal here means they do not have to wait out the silence
//  timeout with their phone on an access point that has no internet.

static esp_err_t done_post_handler(httpd_req_t *req)
{
    wifi_prov_note_portal_activity();

    ESP_LOGI(TAG, "Portal finished by the visitor");

    httpd_resp_set_type(req, "application/json");
    esp_err_t err = httpd_resp_sendstr(req, "{\"ok\":true}");

    //  After the answer is on its way, so the browser still gets it.
    wifi_prov_close_portal();

    return err;
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

    //  The page will not submit empty fields, but a request can come from
    //  anywhere. An empty key is never a valid one, so it is refused here
    //  before it can push working credentials out of the way.
    if (client_id [0] == '\0' || client_secret [0] == '\0') {
        httpd_resp_set_type(req, "application/json");
        return httpd_resp_sendstr(req,
            "{\"ok\":false,\"message\":\"Vul beide velden in\"}");
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

    //  The default is eight, and this portal serves nine addresses. The ninth
    //  did not fail loudly at the door: the server started, the pages that had
    //  registered worked, and only "done" and "api-check" were quietly
    //  missing - so setup could be started but not finished. Counted out here
    //  with room to spare, because the failure is invisible until somebody is
    //  standing in front of it.
    config.max_uri_handlers = 16;

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

    httpd_uri_t wifi_uri = {
        .uri = "/wifi",
        .method = HTTP_GET,
        .handler = wifi_get_handler,
        .user_ctx = NULL
    };

    httpd_uri_t forget_uri = {
        .uri = "/forget",
        .method = HTTP_POST,
        .handler = forget_post_handler,
        .user_ctx = NULL
    };

    httpd_uri_t networks_uri = {
        .uri = "/networks",
        .method = HTTP_GET,
        .handler = networks_get_handler,
        .user_ctx = NULL
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

    httpd_uri_t done_uri = {
        .uri = "/done",
        .method = HTTP_POST,
        .handler = done_post_handler,
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

    err = httpd_register_uri_handler(s_server, &networks_uri);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to register networks URI handler: %s", esp_err_to_name(err));
        return err;
    }

    err = httpd_register_uri_handler(s_server, &forget_uri);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to register forget URI handler: %s", esp_err_to_name(err));
        return err;
    }

    err = httpd_register_uri_handler(s_server, &wifi_uri);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to register wifi URI handler: %s", esp_err_to_name(err));
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

    err = httpd_register_uri_handler(s_server, &done_uri);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to register done URI handler: %s", esp_err_to_name(err));
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