/*  =========================================================================
    wifi_slots - the rules about the three remembered networks
    =========================================================================
*/

#include <assert.h>
#include <stdint.h>
#include <string.h>

#include "inc/wifi_slots.h"

bool wifi_slots_is_empty (const wifi_slot_t *slot)
{
    assert (slot);              //  Caller's contract

    return slot->ssid [0] == '\0';
}


//  --------------------------------------------------------------------------
//  Three rules, tried in order, and the first one that names a slot wins.
//  Somebody standing at the portal types a network and a password; they do not
//  type a slot number, and they should not have to. See besluit 3 of
//  docs/PLAN-wifi-slots.md.
//
//  Each rule returns a slot, or -1 for "not my case". Adding a fourth rule is
//  a row in the table below, not a branch somewhere in the middle of a
//  function.

//  The same network, entered again. A new password for a network the device
//  already knows must replace the old one rather than take a second slot,
//  otherwise the device keeps trying a password its owner has changed.
static int s_rule_same_network (const wifi_slot_t *slots, const char *ssid)
{
    for (size_t slot = 0; slot < WIFI_SLOT_COUNT; slot++) {
        if (strcmp (slots [slot].ssid, ssid) == 0) {
            return (int) slot;
        }
    }

    return -1;
}


//  A slot nobody is using yet. Filling those first means the first three
//  networks somebody enters all survive.
static int s_rule_empty_slot (const wifi_slot_t *slots, const char *ssid)
{
    (void) ssid;

    for (size_t slot = 0; slot < WIFI_SLOT_COUNT; slot++) {
        if (wifi_slots_is_empty (&slots [slot])) {
            return (int) slot;
        }
    }

    return -1;
}


//  All three full and none of them this network, so one has to go. The one
//  that has worked least recently is the one the owner is least likely to
//  come back to. A slot that never worked has ok_seq 0 and goes first.
static int s_rule_stalest (const wifi_slot_t *slots, const char *ssid)
{
    (void) ssid;

    size_t stalest = 0;

    for (size_t slot = 1; slot < WIFI_SLOT_COUNT; slot++) {
        if (slots [slot].ok_seq < slots [stalest].ok_seq) {
            stalest = slot;
        }
    }

    return (int) stalest;
}


static const struct {
    const char *why;
    int (*pick) (const wifi_slot_t *slots, const char *ssid);
} s_rules [] = {
    { "same network",            s_rule_same_network },
    { "empty slot",              s_rule_empty_slot   },
    { "worked least recently",   s_rule_stalest      }
};

#define RULE_COUNT  (sizeof (s_rules) / sizeof (s_rules [0]))


size_t wifi_slots_choose_for (const wifi_slot_t *slots, const char *ssid,
                              const char **why)
{
    assert (slots);                 //  Caller's contract
    assert (ssid);
    assert (ssid [0] != '\0');      //  The portal rejects an empty name

    for (size_t rule = 0; rule < RULE_COUNT; rule++) {
        int slot = s_rules [rule].pick (slots, ssid);

        if (slot >= 0) {
            if (why != NULL) {
                *why = s_rules [rule].why;
            }
            return (size_t) slot;
        }
    }

    //  The last rule always names a slot, so this line is unreachable. It is
    //  here so that a fourth rule added above a broken last one cannot walk
    //  off the end of the table.
    assert (false);
    return 0;
}


//  --------------------------------------------------------------------------
//  Was this network in the scan, and how strong? Returns the strength, or
//  INT8_MIN for "not seen at all". An SSID may appear more than once when a
//  network has several access points; the strongest one is the one we would
//  associate with anyway.

#define NOT_SEEN  INT8_MIN

static int8_t s_strength_of (const wifi_slot_t *slot,
                             const wifi_seen_t *seen, size_t seen_count)
{
    int8_t strongest = NOT_SEEN;

    for (size_t row = 0; row < seen_count; row++) {
        if (strcmp (seen [row].ssid, slot->ssid) == 0
        &&  seen [row].rssi > strongest) {
            strongest = seen [row].rssi;
        }
    }

    return strongest;
}


size_t wifi_slots_plan (const wifi_slot_t *slots,
                        const wifi_seen_t *seen, size_t seen_count,
                        int last_ok,
                        size_t *order)
{
    assert (slots);             //  Caller's contract
    assert (order);
    assert (seen != NULL || seen_count == 0);

    int8_t strength [WIFI_SLOT_COUNT];
    bool planned [WIFI_SLOT_COUNT] = {false};
    size_t count = 0;

    for (size_t slot = 0; slot < WIFI_SLOT_COUNT; slot++) {
        strength [slot] = wifi_slots_is_empty (&slots [slot])
                        ? NOT_SEEN
                        : s_strength_of (&slots [slot], seen, seen_count);

        //  An empty slot is never worth trying, whatever the radio saw.
        planned [slot] = wifi_slots_is_empty (&slots [slot]);
    }

    //  1. The one that worked last time, if it is there.
    if (last_ok >= 0 && last_ok < (int) WIFI_SLOT_COUNT
    &&  !planned [last_ok] && strength [last_ok] != NOT_SEEN) {
        order [count++] = (size_t) last_ok;
        planned [last_ok] = true;
    }

    //  2. The rest of what the radio saw, strongest first.
    for (;;) {
        int strongest = -1;

        for (size_t slot = 0; slot < WIFI_SLOT_COUNT; slot++) {
            if (planned [slot] || strength [slot] == NOT_SEEN) {
                continue;
            }
            if (strongest < 0 || strength [slot] > strength [strongest]) {
                strongest = (int) slot;
            }
        }

        if (strongest < 0) {
            break;
        }

        order [count++] = (size_t) strongest;
        planned [strongest] = true;
    }

    //  3. What is left: filled slots the radio did not see. Hidden networks
    //  live here, and so does everything when no scan was possible.
    for (size_t slot = 0; slot < WIFI_SLOT_COUNT; slot++) {
        if (!planned [slot]) {
            order [count++] = slot;
            planned [slot] = true;
        }
    }

    return count;
}
