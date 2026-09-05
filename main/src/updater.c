/*  =========================================================================
    updater - firmware over the air
    =========================================================================
*/

#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_app_desc.h"
#include "esp_app_format.h"
#include "esp_crt_bundle.h"
#include "esp_http_client.h"
#include "esp_https_ota.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_timer.h"
#include "nvs.h"

#include "inc/updater.h"
#include "inc/wifi_provisioning.h"

static const char *TAG = "[updater]";

//  GitHub keeps this address pointing at the newest release, so the device does
//  not need a list of versions anywhere. It answers with a redirect to the file
//  itself, which esp_https_ota follows.
#define FIRMWARE_URL \
    "https://github.com/edwin-oetelaar/Shared_Energy_Telemetry_Device" \
    "/releases/latest/download/energy-owl.bin"

//  How often to look. An hour is often enough that a fix reaches five testers
//  the same afternoon, and rare enough to be invisible.
#define CHECK_INTERVAL_MS   (60 * 60 * 1000)

//  Not straight away: a device caught in a restart loop should not spend its
//  life downloading, and the first minutes are for connecting and reporting.
#define FIRST_CHECK_DELAY_MS (5 * 60 * 1000)

//  A fresh image proves itself by fetching telemetry. When the API is down
//  that never happens, and an image that is otherwise perfect would be rolled
//  back on the next power cut. So a network that has been up this long counts
//  as proof too: whatever else is wrong, a device that is on the network can
//  still be reached with a better version.
#define PROBATION_NETWORK_MS (10 * 60 * 1000)

//  How often to look at the probation, while it lasts.
#define PROBATION_POLL_MS    (30 * 1000)

typedef enum {
    UPDATER_EV_CHECK = 0,   //  Look for something newer
    UPDATER_EV_FOUND,       //  There is something newer; fetch it
    UPDATER_EV_NOTHING,     //  Already up to date
    UPDATER_EV_INSTALLED,   //  The new image is written and verified
    UPDATER_EV_TROUBLE,     //  Anything that went wrong
    UPDATER_EV_COUNT
} updater_event_t;

//  One row per state, one column per event; every combination has an answer.
//  From FAILED a new check is allowed, so a device is never stuck on one bad
//  afternoon at the far end of somebody's wifi.

static const updater_state_t s_next [UPDATER_STATE_COUNT][UPDATER_EV_COUNT] = {
    /*                        CHECK                FOUND                 NOTHING         INSTALLED        TROUBLE          */
    [UPDATER_IDLE]        = { UPDATER_CHECKING,    UPDATER_IDLE,         UPDATER_IDLE,   UPDATER_IDLE,    UPDATER_IDLE     },
    [UPDATER_CHECKING]    = { UPDATER_CHECKING,    UPDATER_DOWNLOADING,  UPDATER_IDLE,   UPDATER_READY,   UPDATER_FAILED   },
    [UPDATER_DOWNLOADING] = { UPDATER_DOWNLOADING, UPDATER_DOWNLOADING,  UPDATER_IDLE,   UPDATER_READY,   UPDATER_FAILED   },
    [UPDATER_READY]       = { UPDATER_READY,       UPDATER_READY,        UPDATER_READY,  UPDATER_READY,   UPDATER_READY    },
    [UPDATER_FAILED]      = { UPDATER_CHECKING,    UPDATER_FAILED,       UPDATER_IDLE,   UPDATER_READY,   UPDATER_FAILED   }
};

static const char *s_state_name [UPDATER_STATE_COUNT] = {
    "idle", "checking", "downloading", "ready", "failed"
};

static const char *s_event_name [UPDATER_EV_COUNT] = {
    "check", "found", "nothing", "installed", "trouble"
};

//  Why an attempt came to nothing, in words. "ESP_FAIL" tells a tester
//  nothing, and with five of them sending in logs the answer has to be in the
//  log itself. The last row catches everything the table does not name, so the
//  table cannot be incomplete.

static const struct {
    int status;
    const char *meaning;
} s_fetch_failure [] = {
    { 404, "there is no published release yet - a draft release cannot be downloaded" },
    { 403, "GitHub refused the download" },
    { 429, "GitHub is turning us away for asking too often" },
    {   0, "GitHub gave no usable answer" }
};

//  The status of the last answer from GitHub, or zero when there was none.
//  --------------------------------------------------------------------------
//  Remembering which version did not stick.
//
//  A release that starts up but never finds a network is the worst kind. Its
//  probation never ends, so it is never marked good - but nothing rolls it
//  back either, because the probation only ever marks good. The device simply
//  sits there. The owner pulls the plug, the bootloader puts the previous
//  version back, that one connects, and five minutes later it installs the
//  same broken release again. Every power cut buys five working minutes.
//
//  A factory reset makes it worse: it clears the credentials, not the update
//  logic, so the owner sets everything up again and walks straight back in.
//  That is why this lives in a namespace of its own - wiping the device must
//  not wipe the memory of what went wrong.
//
//  How it works: before restarting into a new version we write down which
//  version that is. If the next start is running something else, the install
//  did not stick and that version gets a mark against it.
//
//  Not on the first failure. A perfectly good version fails its probation when
//  the router happens to be off, and blocking it for ever would be a worse
//  fault than the one we are preventing. The second failure is what counts:
//  bad luck twice in a row over the same version, with an hour in between, is
//  no longer luck.
#define UPDATER_NAMESPACE   "updater"
#define KEY_PENDING         "pending"
#define KEY_BAD_VERSION     "bad_ver"
#define KEY_BAD_COUNT       "bad_count"
#define BAD_VERSION_LIMIT   2

//  esp_https_ota_begin () frees its handle when it fails, so the status has to
//  be caught on the way past rather than asked for afterwards.
static int s_http_status = 0;

static updater_state_t s_state = UPDATER_IDLE;
static int s_percent = 0;
static bool s_on_probation = false;
static bool s_marked_good = false;
static esp_timer_handle_t s_timer = NULL;


//  --------------------------------------------------------------------------
//  Watch the answers go by, and say what went wrong when one of them is the
//  last thing that happened.

static esp_err_t s_http_event(esp_http_client_event_t *event)
{
    if (event->event_id == HTTP_EVENT_ON_HEADER
    ||  event->event_id == HTTP_EVENT_ON_FINISH) {
        s_http_status = esp_http_client_get_status_code(event->client);
    }

    return ESP_OK;
}


//  --------------------------------------------------------------------------
//  Small readers and writers for the namespace above. None of them fail the
//  update when NVS is unhappy: not being able to remember is a reason to be
//  careful, not a reason to stop.

static bool s_read_note(const char *key, char *value, size_t size)
{
    nvs_handle_t handle;

    if (nvs_open(UPDATER_NAMESPACE, NVS_READONLY, &handle) != ESP_OK) {
        return false;
    }

    size_t length = size;
    bool found = nvs_get_str(handle, key, value, &length) == ESP_OK;

    nvs_close(handle);

    return found;
}


static uint8_t s_read_count(void)
{
    nvs_handle_t handle;

    if (nvs_open(UPDATER_NAMESPACE, NVS_READONLY, &handle) != ESP_OK) {
        return 0;
    }

    uint8_t count = 0;

    if (nvs_get_u8(handle, KEY_BAD_COUNT, &count) != ESP_OK) {
        count = 0;
    }

    nvs_close(handle);

    return count;
}


//  Pass NULL to erase.

static void s_write_note(const char *key, const char *value)
{
    nvs_handle_t handle;

    if (nvs_open(UPDATER_NAMESPACE, NVS_READWRITE, &handle) != ESP_OK) {
        return;
    }

    if (value != NULL) {
        nvs_set_str(handle, key, value);
    }
    else {
        nvs_erase_key(handle, key);
    }

    nvs_commit(handle);
    nvs_close(handle);
}


static void s_write_count(uint8_t count)
{
    nvs_handle_t handle;

    if (nvs_open(UPDATER_NAMESPACE, NVS_READWRITE, &handle) != ESP_OK) {
        return;
    }

    nvs_set_u8(handle, KEY_BAD_COUNT, count);
    nvs_commit(handle);
    nvs_close(handle);
}


//  --------------------------------------------------------------------------
//  A version we wrote down as "about to run" is not what came up. Count it
//  against that version, and say so: this is the line somebody needs when a
//  device keeps going quiet after an update.

static void s_note_version_did_not_stick(const char *pending, const char *running)
{
    char bad [32] = {0};
    uint8_t count = 1;

    if (s_read_note(KEY_BAD_VERSION, bad, sizeof(bad)) && strcmp(bad, pending) == 0) {
        count = s_read_count();
        count = count < 255 ? count + 1 : 255;
    }

    s_write_note(KEY_BAD_VERSION, pending);
    s_write_count(count);
    s_write_note(KEY_PENDING, NULL);

    ESP_LOGE(TAG, "Version %s was installed but %s came up instead; "
                  "that is %u time%s for this version",
             pending, running, (unsigned) count, count == 1 ? "" : "s");

    if (count >= BAD_VERSION_LIMIT) {
        ESP_LOGE(TAG, "Not installing %s again. A higher version still will be",
                 pending);
    }
}


//  Whether this offer is the one that keeps failing.

static bool s_is_known_bad(const char *offered)
{
    char bad [32] = {0};

    if (!s_read_note(KEY_BAD_VERSION, bad, sizeof(bad)) || strcmp(bad, offered) != 0) {
        return false;
    }

    return s_read_count() >= BAD_VERSION_LIMIT;
}


static void s_report_failure(esp_err_t err)
{
    size_t row = 0;

    while (s_fetch_failure [row].status != 0
    &&     s_fetch_failure [row].status != s_http_status) {
        row++;
    }

    ESP_LOGE(TAG, "No update: %s (HTTP %d, %s)",
             s_fetch_failure [row].meaning, s_http_status, esp_err_to_name(err));
}


//  --------------------------------------------------------------------------
//  The only place the state changes.

static void updater_handle(updater_event_t event)
{
    assert (event < UPDATER_EV_COUNT);      //  Caller's contract

    updater_state_t next = s_next [s_state][event];

    if (next == s_state) {
        return;
    }

    ESP_LOGI(TAG, "update: %s --%s--> %s",
             s_state_name [s_state], s_event_name [event], s_state_name [next]);

    s_state = next;
}


updater_state_t updater_get_state(void)
{
    return s_state;
}


int updater_progress_percent(void)
{
    return s_percent;
}


bool updater_image_on_probation(void)
{
    return s_on_probation;
}


//  --------------------------------------------------------------------------
//  Version numbers, as "v1.2.3" possibly followed by what git describe adds.
//  Returns false for anything that is not a release version, which is how a
//  build made straight from a working tree is recognised.

static bool s_parse_version(const char *text, int parts [3])
{
    if (text == NULL) {
        return false;
    }

    while (*text == 'v' || *text == 'V') {
        text++;
    }

    return sscanf(text, "%d.%d.%d", &parts [0], &parts [1], &parts [2]) == 3;
}


//  True when `offered` is a later release than `running`.

static bool s_is_newer(const char *offered, const char *running)
{
    int there [3];
    int here [3];

    if (!s_parse_version(offered, there)) {
        //  Whatever is on the server is not a release. Leave it alone: an
        //  unnumbered image cannot be compared, and installing it blindly is
        //  how a device ends up older than it started.
        ESP_LOGW(TAG, "Offered version '%s' is not a release number", offered ? offered : "");
        return false;
    }

    if (!s_parse_version(running, here)) {
        //  This device runs a build from somebody's working tree. Any real
        //  release is a step forward from that.
        ESP_LOGW(TAG, "Running '%s' is not a release number; taking the release",
                 running ? running : "");
        return true;
    }

    for (int part = 0; part < 3; part++) {
        if (there [part] != here [part]) {
            return there [part] > here [part];
        }
    }

    return false;
}


//  --------------------------------------------------------------------------
//  Ask for the image, read its description, and only keep going if it is newer.
//  The description sits at the front, so nothing is downloaded needlessly.

static void s_run_update(void)
{
    esp_http_client_config_t http = {
        .url = FIRMWARE_URL,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .timeout_ms = 20000,
        .keep_alive_enable = true,
        .event_handler = s_http_event,
        //  GitHub answers with a redirect whose headers do not fit the default
        //  512 bytes, and the redirect target carries a long signed query.
        .buffer_size = 2048,
        .buffer_size_tx = 2048,
    };

    esp_https_ota_config_t config = {
        .http_config = &http,
        //  GitHub answers with a redirect to the file itself.
        .http_client_init_cb = NULL,
    };

    esp_https_ota_handle_t handle = NULL;

    s_http_status = 0;

    esp_err_t err = esp_https_ota_begin(&config, &handle);
    if (err != ESP_OK || handle == NULL) {
        s_report_failure(err);
        updater_handle(UPDATER_EV_TROUBLE);
        return;
    }

    esp_app_desc_t offered;
    err = esp_https_ota_get_img_desc(handle, &offered);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Update has no description: %s", esp_err_to_name(err));
        esp_https_ota_abort(handle);
        updater_handle(UPDATER_EV_TROUBLE);
        return;
    }

    const esp_app_desc_t *running = esp_app_get_description();

    if (s_is_known_bad(offered.version)) {
        ESP_LOGW(TAG, "Skipping %s: it has failed to come up %d times. "
                      "Publish a higher version to get past this",
                 offered.version, BAD_VERSION_LIMIT);
        esp_https_ota_abort(handle);
        updater_handle(UPDATER_EV_NOTHING);
        return;
    }

    if (!s_is_newer(offered.version, running->version)) {
        ESP_LOGI(TAG, "Running %s, offered %s: nothing to do",
                 running->version, offered.version);
        esp_https_ota_abort(handle);
        updater_handle(UPDATER_EV_NOTHING);
        return;
    }

    ESP_LOGW(TAG, "Updating from %s to %s", running->version, offered.version);
    updater_handle(UPDATER_EV_FOUND);

    int total = esp_https_ota_get_image_size(handle);

    while ((err = esp_https_ota_perform(handle)) == ESP_ERR_HTTPS_OTA_IN_PROGRESS) {
        if (total > 0) {
            s_percent = (esp_https_ota_get_image_len_read(handle) * 100) / total;
        }
    }

    if (err != ESP_OK || !esp_https_ota_is_complete_data_received(handle)) {
        ESP_LOGE(TAG, "Update did not arrive whole: %s (HTTP %d, %d of %d bytes)",
                 esp_err_to_name(err), s_http_status,
                 esp_https_ota_get_image_len_read(handle), total);
        esp_https_ota_abort(handle);
        updater_handle(UPDATER_EV_TROUBLE);
        return;
    }

    err = esp_https_ota_finish(handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Update refused: %s", esp_err_to_name(err));
        updater_handle(UPDATER_EV_TROUBLE);
        return;
    }

    s_percent = 100;
    updater_handle(UPDATER_EV_INSTALLED);

    //  Written down before the restart, so the next start can tell whether
    //  this version actually came up. Cleared again the moment it proves
    //  itself; see updater_note_device_working ().
    s_write_note(KEY_PENDING, offered.version);

    //  A moment so the screen can say what happened before it goes dark.
    ESP_LOGW(TAG, "Restarting into %s", offered.version);
    vTaskDelay(pdMS_TO_TICKS(4000));

    esp_restart();
}


static void s_update_task(void *arguments)
{
    (void) arguments;

    s_run_update();

    vTaskDelete(NULL);
}


void updater_check_now(void)
{
    if (s_state == UPDATER_CHECKING || s_state == UPDATER_DOWNLOADING
    ||  s_state == UPDATER_READY) {
        ESP_LOGI(TAG, "Already busy with an update");
        return;
    }

    if (!wifi_prov_is_connected()) {
        ESP_LOGI(TAG, "No network, not checking for updates");
        return;
    }

    updater_handle(UPDATER_EV_CHECK);

    //  Its own task: downloading takes minutes and must not hold up whatever
    //  called this, which may be a button on the screen.
    if (xTaskCreate(s_update_task, "updater", 8192, NULL, 4, NULL) != pdPASS) {
        ESP_LOGE(TAG, "No room for the update task");
        updater_handle(UPDATER_EV_TROUBLE);
    }
}


static void s_timer_fired(void *arguments)
{
    (void) arguments;

    updater_check_now();

    //  One shot each time, rather than a periodic timer, so a check that took
    //  twenty minutes does not queue up the ones it ran through.
    esp_timer_start_once(s_timer, (uint64_t) CHECK_INTERVAL_MS * 1000);
}


//  --------------------------------------------------------------------------
//  Watch over the probation of a fresh image. Ends the moment the image is
//  marked good, whichever of the two proofs came first, and the task with it.

static void s_probation_task(void *arguments)
{
    (void) arguments;

    int64_t connected_since_us = 0;

    while (s_on_probation) {
        if (!wifi_prov_is_connected()) {
            connected_since_us = 0;
        }
        else {
            if (connected_since_us == 0) {
                connected_since_us = esp_timer_get_time();
            }

            int64_t connected_ms = (esp_timer_get_time() - connected_since_us) / 1000;

            if (connected_ms >= PROBATION_NETWORK_MS) {
                ESP_LOGW(TAG, "No telemetry yet, but the network has been up for "
                              "%d minutes; this image can still be replaced remotely",
                         PROBATION_NETWORK_MS / 60000);
                updater_note_device_working();
            }
        }

        vTaskDelay(pdMS_TO_TICKS(PROBATION_POLL_MS));
    }

    vTaskDelete(NULL);
}


//  --------------------------------------------------------------------------
//  A fresh image is on probation. Marking it good is what stops the bootloader
//  putting the previous firmware back on the next restart, and that only
//  happens once the device has shown it can do its work.

void updater_note_device_working(void)
{
    if (!s_on_probation || s_marked_good) {
        return;
    }

    esp_err_t err = esp_ota_mark_app_valid_cancel_rollback();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Could not confirm this image: %s", esp_err_to_name(err));
        return;
    }

    s_marked_good = true;
    s_on_probation = false;

    //  This version did come up and it does work. Nothing left to suspect: the
    //  note about what we were installing goes, and so does any mark against
    //  this version from an earlier attempt that failed for its own reasons.
    s_write_note(KEY_PENDING, NULL);
    s_write_note(KEY_BAD_VERSION, NULL);
    s_write_count(0);

    ESP_LOGW(TAG, "This image works and is now permanent");
}


esp_err_t updater_init(void)
{
    //  Did the last thing we installed actually come up? This runs before
    //  anything else, because the answer decides whether the next check may
    //  offer that version again.
    char pending [32] = {0};
    const esp_app_desc_t *app = esp_app_get_description();

    if (s_read_note(KEY_PENDING, pending, sizeof(pending)) && pending [0] != '\0') {
        if (strcmp(pending, app->version) != 0) {
            s_note_version_did_not_stick(pending, app->version);
        }
        //  Running what we wrote down: leave the note. It is cleared when the
        //  image proves itself, and stays if the device restarts before that -
        //  which is exactly the case we want to catch.
    }

    const esp_partition_t *running = esp_ota_get_running_partition();
    esp_ota_img_states_t state;

    if (esp_ota_get_state_partition(running, &state) == ESP_OK
    &&  state == ESP_OTA_IMG_PENDING_VERIFY) {
        s_on_probation = true;
        ESP_LOGW(TAG, "This image is on probation until the device does its work");

        if (xTaskCreate(s_probation_task, "probation", 3072, NULL, 4, NULL) != pdPASS) {
            //  Without the watcher the only proof left is a telemetry round.
            //  That is the stricter of the two, so nothing unsafe happens; say
            //  so and carry on.
            ESP_LOGE(TAG, "No room for the probation task");
        }
    }

    const esp_timer_create_args_t args = {
        .callback = s_timer_fired,
        .name = "update_check"
    };

    esp_err_t err = esp_timer_create(&args, &s_timer);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "No update timer: %s", esp_err_to_name(err));
        return err;
    }

    //  First check after a delay, then every hour.
    esp_timer_start_once(s_timer, (uint64_t) FIRST_CHECK_DELAY_MS * 1000);

    ESP_LOGI(TAG, "Update checks armed, every %d minutes", CHECK_INTERVAL_MS / 60000);

    return ESP_OK;
}
