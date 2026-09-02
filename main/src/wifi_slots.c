/*  =========================================================================
    wifi_slots - the rules about the three remembered networks
    =========================================================================
*/

#include <assert.h>
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
