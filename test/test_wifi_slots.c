//  Host tests for the rules about the three remembered networks.
//  See besluit 3 of docs/PLAN-wifi-slots.md.

#include <stdio.h>
#include <string.h>

#include "inc/wifi_slots.h"

static int failures = 0;

static void check_choice (const char *what, const wifi_slot_t *slots,
                          const char *ssid, size_t expect, const char *expect_why)
{
    const char *why = NULL;
    size_t slot = wifi_slots_choose_for (slots, ssid, &why);

    if (slot != expect || strcmp (why, expect_why) != 0) {
        printf ("FAIL  %s: \"%s\" -> slot %u (%s), verwacht slot %u (%s)\n",
                what, ssid, (unsigned) slot, why, (unsigned) expect, expect_why);
        failures++;
    }
    else
        printf ("ok    %s: \"%s\" -> slot %u (%s)\n",
                what, ssid, (unsigned) slot, why);
}

static wifi_slot_t s_slot (const char *ssid, uint32_t ok_seq)
{
    wifi_slot_t slot = {0};
    snprintf (slot.ssid, sizeof (slot.ssid), "%s", ssid);
    snprintf (slot.password, sizeof (slot.password), "geheim");
    slot.ok_seq = ok_seq;
    return slot;
}

int main (void)
{
    //  Alles leeg: het eerste lege slot.
    wifi_slot_t leeg [WIFI_SLOT_COUNT] = {0};
    check_choice ("alles leeg", leeg, "Thuis", 0, "empty slot");

    //  Een gevuld slot, een nieuw netwerk: het volgende lege slot.
    wifi_slot_t een [WIFI_SLOT_COUNT] = { s_slot ("Thuis", 1) };
    check_choice ("een gevuld", een, "Kantoor", 1, "empty slot");

    //  Hetzelfde netwerk opnieuw: overschrijven, niet een tweede slot.
    check_choice ("zelfde netwerk", een, "Thuis", 0, "same network");

    //  Een gelijke naam wint van een leeg slot, ook als hij achteraan staat.
    wifi_slot_t achteraan [WIFI_SLOT_COUNT] = {0};
    achteraan [2] = s_slot ("Hotspot", 4);
    check_choice ("match achteraan", achteraan, "Hotspot", 2, "same network");

    //  Alles vol, onbekend netwerk: het slot dat het langst niet werkte.
    wifi_slot_t vol [WIFI_SLOT_COUNT] = {
        s_slot ("Thuis", 9), s_slot ("Kantoor", 3), s_slot ("Hotspot", 7)
    };
    check_choice ("alles vol", vol, "Buren", 1, "worked least recently");

    //  Een slot dat nooit werkte (ok_seq 0) gaat als eerste.
    wifi_slot_t nooit [WIFI_SLOT_COUNT] = {
        s_slot ("Thuis", 9), s_slot ("Kantoor", 3), s_slot ("Fout", 0)
    };
    check_choice ("nooit gewerkt", nooit, "Buren", 2, "worked least recently");

    //  Bij gelijkspel het laagste slotnummer, zodat de keuze voorspelbaar is.
    wifi_slot_t gelijk [WIFI_SLOT_COUNT] = {
        s_slot ("Een", 5), s_slot ("Twee", 5), s_slot ("Drie", 5)
    };
    check_choice ("gelijkspel", gelijk, "Vier", 0, "worked least recently");

    //  SSID's zijn hoofdlettergevoelig; "thuis" is een ander netwerk dan "Thuis".
    check_choice ("hoofdletters", een, "thuis", 1, "empty slot");

    //  Een leeg slot herkennen.
    if (!wifi_slots_is_empty (&leeg [0])) {
        printf ("FAIL  een leeg slot werd niet als leeg herkend\n");
        failures++;
    }
    else
        printf ("ok    leeg slot herkend\n");

    if (wifi_slots_is_empty (&een [0])) {
        printf ("FAIL  een gevuld slot werd als leeg herkend\n");
        failures++;
    }
    else
        printf ("ok    gevuld slot herkend\n");

    printf ("\n%s\n", failures ? "TESTS GEFAALD" : "alle tests geslaagd");

    return failures ? 1 : 0;
}
