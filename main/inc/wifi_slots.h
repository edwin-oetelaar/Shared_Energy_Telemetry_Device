/*  =========================================================================
    wifi_slots - the three networks the device remembers, and the rules
    about them

    Plain C on purpose: no ESP-IDF, no NVS. What lives here is policy, which
    can be compiled and tested on a host in a second. wifi_storage.h puts the
    slots in flash; this file decides what belongs in which one.

    Phase 0 of docs/PLAN-wifi-slots.md.
    =========================================================================
*/

#ifndef WIFI_SLOTS_H_INCLUDED
#define WIFI_SLOTS_H_INCLUDED

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

//  How many networks the device remembers. Three covers home, work and a
//  phone hotspot, which is what people actually named. Raising it is this one
//  number plus room on the status screen; see besluit 1 of the plan.
#define WIFI_SLOT_COUNT  3

//  The lengths come from the 802.11 standard by way of wifi_config_t: an SSID
//  is at most 32 bytes, a WPA2 passphrase at most 64. Both carry a closing
//  zero here, because these are C strings and not the fixed fields of a frame.
#define WIFI_SSID_SIZE      33
#define WIFI_PASSWORD_SIZE  65

//  One remembered network. An empty ssid means an empty slot; there is no
//  separate "in use" flag to keep in step with it.
//
//  `ok_seq` counts up: every connection that reaches an IP address gives its
//  slot a number one higher than any other slot has. Zero means this slot has
//  never worked. That is enough to answer "which of these did I last use?"
//  and "which has been dead the longest?" without carrying a clock, which the
//  device does not have before it is online.
typedef struct {
    char ssid [WIFI_SSID_SIZE];
    char password [WIFI_PASSWORD_SIZE];
    uint32_t ok_seq;
} wifi_slot_t;

//  One network the radio saw during a scan. Enough of wifi_ap_record_t to
//  choose by, and no ESP-IDF in the header.
typedef struct {
    char ssid [WIFI_SSID_SIZE];
    int8_t rssi;
} wifi_seen_t;

//  Whether this slot holds a network.
bool wifi_slots_is_empty (const wifi_slot_t *slot);

//  Put the slots worth trying in the order to try them, best first, and say
//  how many there are. Empty slots are left out, so a device with one network
//  gets a plan of one.
//
//  The order, and why:
//
//    1. The slot that last worked, if the radio can see it. A device that
//       stays where it is should not change its mind because a neighbour's
//       access point is louder.
//    2. The other slots the radio can see, strongest first. This is the whole
//       point of scanning: being at the office is one scan, not three
//       connection timeouts.
//    3. The filled slots the radio did not see, in slot order. A hidden
//       network never appears in a scan, so it has to be tried blind or it
//       could never be used at all.
//
//  Pass seen_count 0 for "no scan was possible"; the plan is then every filled
//  slot in slot order, which is what the device did before it could scan.
//
//  `order` must have room for WIFI_SLOT_COUNT entries. `last_ok` is the slot
//  that last reached an IP address, or -1 for none.
size_t wifi_slots_plan (const wifi_slot_t *slots,
                        const wifi_seen_t *seen, size_t seen_count,
                        int last_ok,
                        size_t *order);

//  The slot that new credentials for `ssid` belong in. Never fails: with
//  three slots and three rules there is always an answer, and the caller
//  should not have to handle a "nowhere" that cannot happen.
//
//  `why` is filled in with the rule that decided, for the log and the portal.
//  Pass NULL if you do not want it. The text is a literal and outlives the
//  call.
//
//  Asserts on a NULL slots array or an empty ssid, because both are the
//  calling code getting it wrong rather than somebody typing something odd in
//  the portal. The portal rejects an empty network name before it gets here.
size_t wifi_slots_choose_for (const wifi_slot_t *slots, const char *ssid,
                              const char **why);

#ifdef __cplusplus
}
#endif

#endif
