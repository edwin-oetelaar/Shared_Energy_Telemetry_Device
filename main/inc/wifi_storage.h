/*  =========================================================================
    wifi_storage - the three remembered networks, in flash

    Keeps what wifi_slots.h describes in NVS, and nothing more. The rules
    about which network belongs in which slot live in wifi_slots.h, where a
    host test can reach them.

    Phase 0 of docs/PLAN-wifi-slots.md.
    =========================================================================
*/

#ifndef WIFI_STORAGE_H_INCLUDED
#define WIFI_STORAGE_H_INCLUDED

#include <stdbool.h>
#include <stddef.h>

#include "esp_err.h"

#include "inc/wifi_slots.h"

//  Start NVS, and move a device that was provisioned by an older firmware to
//  the slotted layout. Call once, before anything else here.
esp_err_t wifi_storage_init(void);

//  Read one slot. An empty slot comes back as an empty ssid rather than as an
//  error: "nothing stored here" is an answer, not a failure. Asserts on a slot
//  number out of range, because that is a programming error.
esp_err_t wifi_storage_load_slot(size_t slot, wifi_slot_t *credentials);

//  Read all three at once, for the rules in wifi_slots.h.
esp_err_t wifi_storage_load_slots(wifi_slot_t *slots);

//  Write one slot. The ok_seq of the slot is left alone; a network that is
//  entered has not worked yet.
esp_err_t wifi_storage_save_slot(size_t slot, const char *ssid, const char *password);

//  Empty one slot.
esp_err_t wifi_storage_clear_slot(size_t slot);

//  Note that this slot just reached an IP address. Raises its ok_seq above
//  every other slot, so that "last used" and "stalest" stay answerable
//  without a clock. Writes nothing when the slot is already the highest,
//  because NVS wears out and a device reconnects all day.
esp_err_t wifi_storage_note_success(size_t slot);

//  The slot that last reached an IP address, or -1 when none ever did.
int wifi_storage_last_ok(void);

//  --------------------------------------------------------------------------
//  What the rest of the firmware uses today. Phase 0 keeps the behaviour it
//  had with one stored network: everything above the storage layer works on
//  slot 0. Phase 2 of the plan replaces these with a choice over the three.

esp_err_t wifi_storage_load_credentials(
    char *ssid,
    size_t ssid_len,
    char *password,
    size_t password_len
);

//  Save credentials that have just been proven to work, and mark their slot
//  as the one that worked most recently.
esp_err_t wifi_storage_save_credentials(const char *ssid, const char *password);

//  Forget every network. This is the factory reset behind three power cycles
//  and behind the reset button, so it empties all three slots and not just
//  the one in use.
esp_err_t wifi_storage_clear_credentials(void);

#endif
