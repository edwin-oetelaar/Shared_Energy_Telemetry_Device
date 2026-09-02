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

//  --------------------------------------------------------------------------
//  De volgorde waarin de slots geprobeerd worden.

static wifi_seen_t s_seen (const char *ssid, int8_t rssi)
{
    wifi_seen_t seen = {0};
    snprintf (seen.ssid, sizeof (seen.ssid), "%s", ssid);
    seen.rssi = rssi;
    return seen;
}

static void check_order (const char *what, const wifi_slot_t *slots,
                         const wifi_seen_t *seen, size_t seen_count,
                         int last_ok, const char *expect)
{
    size_t order [WIFI_SLOT_COUNT] = {0};
    size_t count = wifi_slots_plan (slots, seen, seen_count, last_ok, order);

    char got [WIFI_SLOT_COUNT * 2 + 1] = {0};
    for (size_t row = 0; row < count; row++) {
        got [row] = (char) ('0' + (int) order [row]);
    }

    if (strcmp (got, expect) != 0) {
        printf ("FAIL  %s: volgorde \"%s\", verwacht \"%s\"\n", what, got, expect);
        failures++;
    }
    else
        printf ("ok    %s: volgorde \"%s\"\n", what, got);
}

static void check_plan (void)
{
    wifi_slot_t drie [WIFI_SLOT_COUNT] = {
        s_slot ("Thuis", 4), s_slot ("Kantoor", 9), s_slot ("Hotspot", 2)
    };

    //  Geen scan mogelijk: alles op slotvolgorde, zoals vóór fase 2.
    check_order ("zonder scan", drie, NULL, 0, -1, "012");

    //  Alleen kantoor in de lucht: de andere twee komen erachteraan, want een
    //  verborgen netwerk staat niet in een scan.
    wifi_seen_t alleen_kantoor [] = { s_seen ("Kantoor", -50) };
    check_order ("een zichtbaar", drie, alleen_kantoor, 1, -1, "102");

    //  Twee zichtbaar: de sterkste eerst.
    wifi_seen_t twee [] = { s_seen ("Thuis", -80), s_seen ("Hotspot", -40) };
    check_order ("sterkste eerst", drie, twee, 2, -1, "201");

    //  Het laatst geslaagde slot wint van een sterker signaal, zolang het
    //  zichtbaar is: een apparaat dat blijft staan hoort niet van gedachten
    //  te veranderen omdat de buren harder zenden.
    check_order ("laatst geslaagde eerst", drie, twee, 2, 0, "021");

    //  Maar niet als het laatst geslaagde netwerk er niet is.
    check_order ("laatst geslaagde weg", drie, alleen_kantoor, 1, 2, "102");

    //  Een leeg slot staat nooit in de volgorde.
    wifi_slot_t een = s_slot ("Thuis", 1);
    wifi_slot_t met_leeg [WIFI_SLOT_COUNT] = {0};
    met_leeg [1] = een;
    check_order ("lege slots overgeslagen", met_leeg, twee, 2, -1, "1");

    //  Helemaal leeg: niets te proberen.
    wifi_slot_t geen [WIFI_SLOT_COUNT] = {0};
    check_order ("niets opgeslagen", geen, twee, 2, -1, "");

    //  Hetzelfde netwerk op twee accesspoints: de sterkste telt.
    wifi_seen_t dubbel [] = {
        s_seen ("Thuis", -85), s_seen ("Kantoor", -70), s_seen ("Thuis", -35)
    };
    check_order ("zelfde ssid twee keer", drie, dubbel, 3, -1, "012");

    //  Een last_ok die nergens op slaat mag niets breken.
    check_order ("last_ok buiten bereik", drie, twee, 2, 99, "201");
}


//  --------------------------------------------------------------------------
//  Een netwerk terugvinden om het te vergeten.

static void check_find_one (const char *what, const wifi_slot_t *slots,
                            const char *ssid, int expect)
{
    int slot = wifi_slots_find (slots, ssid);

    if (slot != expect) {
        printf ("FAIL  %s: \"%s\" -> %d, verwacht %d\n", what, ssid, slot, expect);
        failures++;
    }
    else
        printf ("ok    %s: \"%s\" -> %d\n", what, ssid, slot);
}

static void check_find (void)
{
    wifi_slot_t drie [WIFI_SLOT_COUNT] = {
        s_slot ("Thuis", 4), s_slot ("Kantoor", 9), s_slot ("Hotspot", 2)
    };

    check_find_one ("eerste", drie, "Thuis", 0);
    check_find_one ("laatste", drie, "Hotspot", 2);
    check_find_one ("onbekend", drie, "Buren", -1);
    check_find_one ("hoofdletters", drie, "thuis", -1);

    //  Een lege naam mag nooit een leeg slot aanwijzen: "vergeet niets" is
    //  geen verzoek, en zou anders slot 1 wissen.
    wifi_slot_t met_leeg [WIFI_SLOT_COUNT] = {0};
    met_leeg [0] = s_slot ("Thuis", 1);
    check_find_one ("lege naam", met_leeg, "", -1);
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

    check_plan ();
    check_find ();

    printf ("\n%s\n", failures ? "TESTS GEFAALD" : "alle tests geslaagd");

    return failures ? 1 : 0;
}
