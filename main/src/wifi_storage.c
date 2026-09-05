/*  =========================================================================
    wifi_storage - the three remembered networks, in flash
    =========================================================================
*/

#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "inc/wifi_storage.h"

#include "nvs_flash.h"
#include "nvs.h"
#include "esp_log.h"

static const char *TAG = "[wifi_storage]";

#define WIFI_NAMESPACE  "wifi_creds"

//  The keys of the firmware that stored one network - v0.2.x and earlier.
//
//  They are NOT removed, and that is on purpose. A device that takes the
//  update over the air keeps the old firmware in its other slot, and the
//  update can still be rolled back: an image that fails its probation sends
//  the bootloader back to the version before it. That version looks for these
//  keys. Erasing them means a rollback lands on a device with no network at
//  all - the portal comes up and somebody has to walk over with a telephone.
//  The rollback exists to save a device, not to strand it.
//
//  So they stay, and they are kept current: whichever network last worked is
//  mirrored here, so that older firmware finds something that worked recently
//  rather than something from months ago.
//
//  This bridge can go once no device has a v0.2.x image in its other slot.
//  Until then it costs two strings in NVS and a write on a path that only
//  runs when the network a device uses changes.
#define KEY_OLD_SSID      "ssid"
#define KEY_OLD_PASSWORD  "password"

//  Per slot: "ssid0", "pw0", "seq0", and so on. NVS allows fifteen characters
//  per key, so this stays well inside the limit for any slot count that would
//  fit on the screen.
#define KEY_LAST_OK  "last_ok"

//  Which layout this namespace is on. Not which firmware wrote it: a device
//  can sit offline for months and then arrive here from any older version in
//  one jump, so the number has to describe what is stored and nothing else.
//
//  It became necessary the moment the old keys stopped being erased: their
//  presence used to mean "not migrated yet", and now it means nothing at all.
//  Without this number the copy below runs on every single start and writes
//  the bridge over slot 0 - whatever the owner had put there. Seen on the
//  bench: slot 0 held one network at boot and the bridge's network a second
//  later.
//
//  The rules for raising it are in docs/NVS.md. The short version: one step at
//  a time, each step commits its own number, add but never remove, and never
//  reuse a name.
#define KEY_LAYOUT     "layout"

//  1 = three slots plus the bridge for older firmware.
#define LAYOUT_CURRENT 1

static void s_key_for(char *key, size_t size, const char *stem, size_t slot)
{
    snprintf(key, size, "%s%u", stem, (unsigned) slot);
}


//  --------------------------------------------------------------------------
//  A string that is not there yet is not an error. NVS says
//  ESP_ERR_NVS_NOT_FOUND for an empty slot, and every caller here means "then
//  it is empty" by that.

static esp_err_t s_get_str(nvs_handle_t handle, const char *key,
                           char *value, size_t size)
{
    size_t length = size;
    esp_err_t err = nvs_get_str(handle, key, value, &length);

    if (err == ESP_ERR_NVS_NOT_FOUND) {
        value [0] = '\0';
        return ESP_OK;
    }

    return err;
}


static esp_err_t s_read_slot(nvs_handle_t handle, size_t slot, wifi_slot_t *credentials)
{
    char key [16];

    memset(credentials, 0, sizeof(*credentials));

    s_key_for(key, sizeof(key), "ssid", slot);
    esp_err_t err = s_get_str(handle, key, credentials->ssid, sizeof(credentials->ssid));

    if (err == ESP_OK) {
        s_key_for(key, sizeof(key), "pw", slot);
        err = s_get_str(handle, key, credentials->password, sizeof(credentials->password));
    }

    if (err == ESP_OK) {
        s_key_for(key, sizeof(key), "seq", slot);

        if (nvs_get_u32(handle, key, &credentials->ok_seq) != ESP_OK) {
            credentials->ok_seq = 0;    //  Never worked, or never written
        }
    }

    return err;
}


//  --------------------------------------------------------------------------
//  Move a device that was provisioned by an older firmware to the slotted
//  layout. The old pair goes into slot 0 and is marked as having worked,
//  because it is what the device was using a moment ago.
//
//  Order matters. The old keys are removed only after the new ones are
//  committed, so a power cut in the middle leaves the device with the old
//  layout and one more chance at the next start. Losing this pair means
//  somebody has to walk up to the device with a telephone.

//  Say that this device is on the slotted layout, so the copy above runs once
//  and not once per start.

static void s_mark_layout(nvs_handle_t handle)
{
    if (nvs_set_u8(handle, KEY_LAYOUT, LAYOUT_CURRENT) == ESP_OK) {
        nvs_commit(handle);
    }
}


static esp_err_t s_migrate_single_network(nvs_handle_t handle)
{
    uint8_t layout = 0;

    if (nvs_get_u8(handle, KEY_LAYOUT, &layout) == ESP_OK) {
        if (layout > LAYOUT_CURRENT) {
            //  Newer firmware has been here. This is a rollback, and there is
            //  nothing to migrate downwards - we read the keys we know, which
            //  works because a layout only ever adds. Worth saying out loud:
            //  somebody reading this log is looking at a device that went
            //  backwards, and that is rarely on purpose.
            ESP_LOGW(TAG, "Storage is on layout %u and this firmware knows %u; "
                          "running on what we recognise",
                     (unsigned) layout, (unsigned) LAYOUT_CURRENT);
            return ESP_OK;
        }

        if (layout == LAYOUT_CURRENT) {
            return ESP_OK;          //  Nothing to do
        }

        //  layout < LAYOUT_CURRENT would run the steps between here and now.
        //  There is only one layout so far, so there are no steps to run yet;
        //  docs/NVS.md describes the shape the chain takes when there are.
    }

    char ssid [WIFI_SSID_SIZE] = {0};
    size_t length = sizeof(ssid);

    if (nvs_get_str(handle, KEY_OLD_SSID, ssid, &length) != ESP_OK) {
        //  Nothing from an older firmware. Mark the layout anyway: this device
        //  is on it, and the first network it stores will put something under
        //  the old keys as a bridge. Without the marker that bridge would look
        //  like something to migrate at the next start.
        s_mark_layout(handle);
        return ESP_OK;
    }

    char password [WIFI_PASSWORD_SIZE] = {0};
    length = sizeof(password);
    nvs_get_str(handle, KEY_OLD_PASSWORD, password, &length);

    ESP_LOGW(TAG, "Copying the stored network '%s' to slot 0", ssid);

    esp_err_t err = nvs_set_str(handle, "ssid0", ssid);

    if (err == ESP_OK) {
        err = nvs_set_str(handle, "pw0", password);
    }
    if (err == ESP_OK) {
        err = nvs_set_u32(handle, "seq0", 1);
    }
    if (err == ESP_OK) {
        err = nvs_set_i32(handle, KEY_LAST_OK, 0);
    }
    if (err == ESP_OK) {
        err = nvs_commit(handle);   //  The new layout is safe from here on
    }

    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Could not write slot 0: %s. The old keys stay; "
                      "the next start tries again", esp_err_to_name(err));
        return err;
    }

    //  The old keys stay where they are; see the comment above them. What was
    //  a migration is therefore a copy, and a device can go back to older
    //  firmware without losing its network.
    s_mark_layout(handle);

    ESP_LOGI(TAG, "Slot 0 now holds '%s'; the old keys stay for a rollback", ssid);

    return err;
}


esp_err_t wifi_storage_init(void)
{
    esp_err_t err = nvs_flash_init();

    if (err == ESP_ERR_NVS_NO_FREE_PAGES ||
        err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }

    if (err != ESP_OK) {
        return err;
    }

    nvs_handle_t handle;

    //  NVS_READWRITE creates the namespace when it is not there, so a device
    //  that has never been provisioned lands here with an empty one and
    //  nothing to migrate. A failure at this point is a real one.
    err = nvs_open(WIFI_NAMESPACE, NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Could not open %s: %s", WIFI_NAMESPACE, esp_err_to_name(err));
        return err;
    }

    err = s_migrate_single_network(handle);

    nvs_close(handle);

    return err;
}


//  --------------------------------------------------------------------------

esp_err_t wifi_storage_load_slot(size_t slot, wifi_slot_t *credentials)
{
    assert (slot < WIFI_SLOT_COUNT);    //  Caller's contract
    assert (credentials);

    nvs_handle_t handle;

    esp_err_t err = nvs_open(WIFI_NAMESPACE, NVS_READONLY, &handle);
    if (err != ESP_OK) {
        memset(credentials, 0, sizeof(*credentials));
        return err;
    }

    err = s_read_slot(handle, slot, credentials);

    nvs_close(handle);

    return err;
}


esp_err_t wifi_storage_load_slots(wifi_slot_t *slots)
{
    assert (slots);                     //  Caller's contract

    nvs_handle_t handle;

    esp_err_t err = nvs_open(WIFI_NAMESPACE, NVS_READONLY, &handle);
    if (err != ESP_OK) {
        memset(slots, 0, WIFI_SLOT_COUNT * sizeof(*slots));
        return err;
    }

    for (size_t slot = 0; slot < WIFI_SLOT_COUNT && err == ESP_OK; slot++) {
        err = s_read_slot(handle, slot, &slots [slot]);
    }

    nvs_close(handle);

    return err;
}


esp_err_t wifi_storage_save_slot(size_t slot, const char *ssid, const char *password)
{
    assert (slot < WIFI_SLOT_COUNT);    //  Caller's contract
    assert (ssid);

    nvs_handle_t handle;
    char key [16];

    esp_err_t err = nvs_open(WIFI_NAMESPACE, NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        return err;
    }

    s_key_for(key, sizeof(key), "ssid", slot);
    err = nvs_set_str(handle, key, ssid);

    if (err == ESP_OK) {
        s_key_for(key, sizeof(key), "pw", slot);
        err = nvs_set_str(handle, key, password ? password : "");
    }

    if (err == ESP_OK) {
        err = nvs_commit(handle);
    }

    nvs_close(handle);

    if (err == ESP_OK) {
        ESP_LOGI(TAG, "Saved '%s' in slot %u", ssid, (unsigned) slot);
    }

    return err;
}


esp_err_t wifi_storage_clear_slot(size_t slot)
{
    assert (slot < WIFI_SLOT_COUNT);    //  Caller's contract

    nvs_handle_t handle;
    char key [16];

    esp_err_t err = nvs_open(WIFI_NAMESPACE, NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        return err;
    }

    //  Read the name before erasing it: if the bridge for older firmware
    //  points at this network, it has to go too. Somebody who forgets a
    //  network means forgotten, not "forgotten unless you roll back".
    char going [WIFI_SSID_SIZE] = {0};
    char bridged [WIFI_SSID_SIZE] = {0};

    s_key_for(key, sizeof(key), "ssid", slot);
    s_get_str(handle, key, going, sizeof(going));

    if (going [0] != '\0'
    &&  s_get_str(handle, KEY_OLD_SSID, bridged, sizeof(bridged)) == ESP_OK
    &&  strcmp(going, bridged) == 0) {
        nvs_erase_key(handle, KEY_OLD_SSID);
        nvs_erase_key(handle, KEY_OLD_PASSWORD);
    }

    nvs_erase_key(handle, key);
    s_key_for(key, sizeof(key), "pw", slot);
    nvs_erase_key(handle, key);
    s_key_for(key, sizeof(key), "seq", slot);
    nvs_erase_key(handle, key);

    //  Otherwise "the one that worked last time" points at an empty slot. The
    //  planner survives that, but only because it checks; leaving a stale
    //  answer in flash for a later reader to trip over is not a favour.
    int32_t last_ok = -1;

    if (nvs_get_i32(handle, KEY_LAST_OK, &last_ok) == ESP_OK
    &&  last_ok == (int32_t) slot) {
        nvs_erase_key(handle, KEY_LAST_OK);
    }

    err = nvs_commit(handle);

    nvs_close(handle);

    ESP_LOGI(TAG, "Cleared slot %u", (unsigned) slot);

    return err;
}


//  --------------------------------------------------------------------------
//  Keep the bridge for older firmware pointing at a network that works. Writes
//  only when the value would change: this runs on every reconnect, and NVS
//  wears out.

static void s_mirror_for_older_firmware(const wifi_slot_t *slot)
{
    nvs_handle_t handle;

    if (nvs_open(WIFI_NAMESPACE, NVS_READWRITE, &handle) != ESP_OK) {
        return;                 //  Not worth failing a good connection over
    }

    char ssid [WIFI_SSID_SIZE] = {0};

    if (s_get_str(handle, KEY_OLD_SSID, ssid, sizeof(ssid)) == ESP_OK
    &&  strcmp(ssid, slot->ssid) == 0) {
        nvs_close(handle);      //  Already this network
        return;
    }

    esp_err_t err = nvs_set_str(handle, KEY_OLD_SSID, slot->ssid);

    if (err == ESP_OK) {
        err = nvs_set_str(handle, KEY_OLD_PASSWORD, slot->password);
    }
    if (err == ESP_OK) {
        err = nvs_commit(handle);
    }

    nvs_close(handle);

    if (err == ESP_OK) {
        ESP_LOGI(TAG, "Older firmware would now find '%s'", slot->ssid);
    }
    else {
        ESP_LOGW(TAG, "Could not update the rollback keys: %s", esp_err_to_name(err));
    }
}


esp_err_t wifi_storage_note_success(size_t slot)
{
    assert (slot < WIFI_SLOT_COUNT);    //  Caller's contract

    wifi_slot_t slots [WIFI_SLOT_COUNT];

    esp_err_t err = wifi_storage_load_slots(slots);
    if (err != ESP_OK) {
        return err;
    }

    //  Before the early return below: a slot can be the most recent one
    //  already - straight after the copy from an older layout, for instance -
    //  and the bridge still has to point at it.
    s_mirror_for_older_firmware(&slots [slot]);

    uint32_t highest = 0;

    for (size_t row = 0; row < WIFI_SLOT_COUNT; row++) {
        if (slots [row].ok_seq > highest) {
            highest = slots [row].ok_seq;
        }
    }

    //  Already the most recent one. A device that reconnects all day would
    //  otherwise write to flash every time.
    if (slots [slot].ok_seq == highest && highest > 0 && wifi_storage_last_ok() == (int) slot) {
        return ESP_OK;
    }

    nvs_handle_t handle;
    char key [16];

    err = nvs_open(WIFI_NAMESPACE, NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        return err;
    }

    s_key_for(key, sizeof(key), "seq", slot);
    err = nvs_set_u32(handle, key, highest + 1);

    if (err == ESP_OK) {
        err = nvs_set_i32(handle, KEY_LAST_OK, (int32_t) slot);
    }
    if (err == ESP_OK) {
        err = nvs_commit(handle);
    }

    nvs_close(handle);

    return err;
}


int wifi_storage_last_ok(void)
{
    nvs_handle_t handle;

    if (nvs_open(WIFI_NAMESPACE, NVS_READONLY, &handle) != ESP_OK) {
        return -1;
    }

    int32_t slot = -1;

    if (nvs_get_i32(handle, KEY_LAST_OK, &slot) != ESP_OK) {
        slot = -1;
    }

    nvs_close(handle);

    if (slot < 0 || slot >= WIFI_SLOT_COUNT) {
        return -1;                      //  Never set, or written by something else
    }

    return (int) slot;
}


//  --------------------------------------------------------------------------
//  Credentials somebody typed in the portal. Reading is done per slot now;
//  only the writing side still assumes slot 0, until phase 3 of
//  docs/PLAN-wifi-slots.md asks wifi_slots_choose_for () where they belong.

esp_err_t wifi_storage_save_credentials(const char *ssid, const char *password,
                                        size_t *slot, const char **why)
{
    assert (ssid);                      //  Caller's contract

    wifi_slot_t slots [WIFI_SLOT_COUNT];

    esp_err_t err = wifi_storage_load_slots(slots);
    if (err != ESP_OK) {
        return err;
    }

    const char *reason = NULL;
    size_t chosen = wifi_slots_choose_for(slots, ssid, &reason);

    ESP_LOGI(TAG, "'%s' goes in slot %u (%s)", ssid, (unsigned) chosen, reason);

    err = wifi_storage_save_slot(chosen, ssid, password);

    //  This is only ever called once the network has handed out an IP
    //  address, so the slot has earned its place at the top.
    if (err == ESP_OK) {
        err = wifi_storage_note_success(chosen);
    }

    if (err == ESP_OK) {
        if (slot != NULL) {
            *slot = chosen;
        }
        if (why != NULL) {
            *why = reason;
        }
    }

    return err;
}


esp_err_t wifi_storage_clear_credentials(void)
{
    esp_err_t err = ESP_OK;

    for (size_t slot = 0; slot < WIFI_SLOT_COUNT; slot++) {
        esp_err_t slot_err = wifi_storage_clear_slot(slot);

        if (slot_err != ESP_OK) {
            err = slot_err;             //  Keep going; report the last failure
        }
    }

    nvs_handle_t handle;

    if (nvs_open(WIFI_NAMESPACE, NVS_READWRITE, &handle) == ESP_OK) {
        nvs_erase_key(handle, KEY_LAST_OK);

        //  And the bridge for older firmware. Somebody who wipes a device
        //  means all of it; leaving a working network behind for one firmware
        //  version and not the other is the kind of surprise that costs an
        //  afternoon.
        nvs_erase_key(handle, KEY_OLD_SSID);
        nvs_erase_key(handle, KEY_OLD_PASSWORD);

        nvs_commit(handle);
        nvs_close(handle);
    }

    ESP_LOGI(TAG, "Cleared WiFi credentials");

    return err;
}
