//  Host tests voor het invullen van de gaten in een ingebedde pagina.
//  Zie L5 in docs/REVIEW.md.

#include <stdio.h>
#include <string.h>

#include "inc/web_page.h"

static int failures = 0;

//  Een verstuurder die alles achter elkaar in een buffer zet, en telt hoe
//  vaak hij is aangeroepen. Dat tweede is nodig om te bewijzen dat er nooit
//  een leeg stuk langskomt.
typedef struct {
    char text [8192];
    int calls;
    int fail_after;     //  0 = nooit falen
} collector_t;

static bool collect (void *context, const char *text, size_t length)
{
    collector_t *into = (collector_t *) context;

    if (length == 0) {
        printf ("FAIL  er werd een leeg stuk verstuurd\n");
        failures++;
    }

    into->calls++;

    if (into->fail_after != 0 && into->calls >= into->fail_after) {
        return false;
    }

    strncat (into->text, text, length);

    return true;
}

static void check (const char *what, const char *page,
                   const web_value_t *values, size_t count, const char *expect)
{
    collector_t into = {0};

    bool ok = web_page_render (page, strlen (page), values, count, collect, &into);

    if (!ok || strcmp (into.text, expect) != 0) {
        printf ("FAIL  %s: \"%s\" (ok=%d), verwacht \"%s\"\n",
                what, into.text, ok, expect);
        failures++;
    }
    else
        printf ("ok    %s: \"%s\"\n", what, into.text);
}

//  --------------------------------------------------------------------------
//  De echte pagina met de echte namen. Dit is het enige wat stil kan breken:
//  iemand hernoemt een gat in het bestand, de C-code blijft de oude naam
//  gebruiken, en het portaal toont {{api_ready}} aan een bewoner. De build
//  merkt daar niets van, want het bestand wordt alleen ingebed.

static void check_real_page (void)
{
    const char *path = "../main/web/api-setup.html";
    FILE *file = fopen (path, "rb");

    if (file == NULL) {
        printf ("FAIL  %s is er niet\n", path);
        failures++;
        return;
    }

    static char page [8192];
    size_t length = fread (page, 1, sizeof (page) - 1, file);
    fclose (file);
    page [length] = '\0';

    //  Dezelfde namen als in api_setup_get_handler ().
    web_value_t values [] = {
        { "intro",     "INTRO-TEKST" },
        { "api_ready", "true" }
    };

    collector_t into = {0};

    if (!web_page_render (page, length, values, 2, collect, &into)) {
        printf ("FAIL  de echte pagina kon niet worden opgebouwd\n");
        failures++;
        return;
    }

    struct { const char *what; const char *needle; bool want; } expect [] = {
        { "geen gat blijft staan",        "{{",                false },
        { "de introtekst staat erin",     "INTRO-TEKST",       true  },
        { "de vlag staat erin",           "const apiReady=true;", true },
        { "de pagina is heel",            "</html>",           true  }
    };

    for (size_t row = 0; row < sizeof (expect) / sizeof (expect [0]); row++) {
        bool found = strstr (into.text, expect [row].needle) != NULL;

        if (found != expect [row].want) {
            printf ("FAIL  echte pagina: %s\n", expect [row].what);
            failures++;
        }
        else
            printf ("ok    echte pagina: %s\n", expect [row].what);
    }
}


int main (void)
{
    web_value_t twee [] = {
        { "naam",  "Edwin" },
        { "leeg",  "" }
    };

    check ("geen gaten", "hallo", twee, 2, "hallo");
    check ("gat in het midden", "a{{naam}}b", twee, 2, "aEdwinb");
    check ("gat vooraan", "{{naam}}!", twee, 2, "Edwin!");
    check ("gat achteraan", "!{{naam}}", twee, 2, "!Edwin");
    check ("twee gaten naast elkaar", "{{naam}}{{naam}}", twee, 2, "EdwinEdwin");

    //  Een onbekende naam blijft staan: zichtbaar mis is beter dan stil weg.
    check ("onbekende naam", "a{{onbekend}}b", twee, 2, "a{{onbekend}}b");

    //  Nooit gesloten: gewone tekst, en niet voorbij het einde lezen.
    check ("nooit gesloten", "a{{naam", twee, 2, "a{{naam");
    check ("alleen accolades", "{{", twee, 2, "{{");
    check ("losse accolade", "a{b}c", twee, 2, "a{b}c");

    //  Een lege waarde levert niets op, en vooral geen leeg stuk - collect ()
    //  hierboven klaagt als dat toch gebeurt.
    check ("lege waarde", "a{{leeg}}b", twee, 2, "ab");
    check ("alleen een leeg gat", "{{leeg}}", twee, 2, "");

    //  Zonder tabel is elk gat onbekend.
    check ("geen tabel", "a{{naam}}b", NULL, 0, "a{{naam}}b");

    //  Een verstuurder die faalt, stopt het geheel.
    collector_t stuk = { .fail_after = 1 };
    if (web_page_render ("a{{naam}}b", 10, twee, 2, collect, &stuk)) {
        printf ("FAIL  een mislukte verzending werd niet doorgegeven\n");
        failures++;
    }
    else
        printf ("ok    mislukte verzending stopt het geheel\n");

    check_real_page ();

    printf ("\n%s\n", failures ? "TESTS GEFAALD" : "alle tests geslaagd");

    return failures ? 1 : 0;
}
