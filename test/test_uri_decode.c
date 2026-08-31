#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include "inc/uri_decode.h"

static int failures = 0;

static void check_ok (const char *input, const char *expect)
{
    char buf [256];
    snprintf (buf, sizeof (buf), "%s", input);
    bool rc = uri_decode (buf);
    if (!rc || strcmp (buf, expect) != 0) {
        printf ("FAIL  \"%s\" -> rc=%d \"%s\" (verwacht \"%s\")\n", input, rc, buf, expect);
        failures++;
    }
    else
        printf ("ok    \"%s\" -> \"%s\"\n", input, buf);
}

static void check_bad (const char *input)
{
    char buf [256];
    snprintf (buf, sizeof (buf), "%s", input);
    if (uri_decode (buf)) {
        printf ("FAIL  \"%s\" werd geaccepteerd, moest afgekeurd\n", input);
        failures++;
    }
    else
        printf ("ok    \"%s\" afgekeurd\n", input);
}

int main (void)
{
    check_ok ("mijn%20wifi%202024", "mijn wifi 2024");
    check_ok ("a+b",                "a b");
    check_ok ("%2B",                "+");
    check_ok ("%25",                "%");
    check_ok ("%41%42",             "AB");
    check_ok ("%aF",                "\xAF");
    check_ok ("%e2%82%ac",          "\xe2\x82\xac");   // euroteken
    check_ok ("",                   "");
    check_ok ("geenescapes",        "geenescapes");
    check_ok ("Wachtwoord%21%40%23","Wachtwoord!@#");
    check_ok ("a%26b%3Dc",          "a&b=c");          // & en = in het wachtwoord
    check_bad ("%");
    check_bad ("%A");
    check_bad ("%ZZ");
    check_bad ("abc%");
    check_bad ("abc%2");
    check_bad ("%%20");
    printf ("\n%s\n", failures ? "TESTS GEFAALD" : "alle tests geslaagd");
    return failures != 0;
}
