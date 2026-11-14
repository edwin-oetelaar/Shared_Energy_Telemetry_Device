/*  =========================================================================
    test_form_text - host tests for form_text_clean ()
    =========================================================================
*/

#undef NDEBUG
#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "inc/form_text.h"

static int s_failures = 0;

//  One case: the input, whether it must be accepted, and what must be left.

static void
    s_check (const char *name, const char *input, bool accepted, const char *left)
{
    char buffer [64];
    assert (strlen (input) < sizeof (buffer));
    strcpy (buffer, input);

    bool result = form_text_clean (buffer);
    bool good = result == accepted && strcmp (buffer, left) == 0;
    printf ("%s  %s\n", good ? "ok   " : "FOUT ", name);
    if (!good) {
        printf ("      verwacht %s \"%s\", kreeg %s \"%s\"\n",
                accepted ? "goed" : "fout", left, result ? "goed" : "fout", buffer);
        s_failures++;
    }
}

int
    main (void)
{
    //  What a paste brings along: cleaned and accepted.
    s_check ("gewone sleutel",          "abc123",          true,  "abc123");
    s_check ("enter erachter (Job's bug)", "abc123\n",     true,  "abc123");
    s_check ("CR LF erachter",          "abc123\r\n",      true,  "abc123");
    s_check ("spaties eromheen",        "  abc123  ",      true,  "abc123");
    s_check ("tab ervoor",              "\tabc123",        true,  "abc123");
    s_check ("spatie in het midden",    "abc 123",         true,  "abc 123");
    s_check ("UTF-8 blijft staan",      "caf\xc3\xa9",     true,  "caf\xc3\xa9");

    //  What no key contains: refused.
    s_check ("enter in het midden",     "abc\n123",        false, "abc\n123");
    s_check ("tab in het midden",       "abc\t123",        false, "abc\t123");
    s_check ("escape erachter",         "abc123\x1b",      false, "abc123\x1b");
    s_check ("DEL in het midden",       "abc\x7f" "123",   false, "abc\x7f" "123");
    s_check ("stuurteken vooraan",      "\x01" "abc",      false, "\x01" "abc");

    //  Edges: nothing left is accepted here; the caller refuses empty keys.
    s_check ("leeg",                    "",                true,  "");
    s_check ("alleen witruimte",        " \r\n\t ",        true,  "");

    if (s_failures) {
        printf ("\n%d test(s) mislukt\n", s_failures);
        return 1;
    }
    printf ("\nalle tests geslaagd\n");
    return 0;
}
