/*  =========================================================================
    web_page - een ingebedde pagina uitsturen, met de gaten ingevuld
    =========================================================================
*/

#include <assert.h>
#include <string.h>

#include "inc/web_page.h"

#define OPEN   "{{"
#define CLOSE  "}}"


//  Niets versturen is geen versturen. Zie de opmerking bij de kop.

static bool s_write (web_write_fn write, void *context,
                     const char *text, size_t length)
{
    if (length == 0) {
        return true;
    }

    return write (context, text, length);
}


//  De waarde bij een naam van `length` tekens, of NULL als niemand hem kent.

static const char *s_value_of (const web_value_t *values, size_t count,
                               const char *name, size_t length)
{
    for (size_t row = 0; row < count; row++) {
        if (strlen (values [row].name) == length
        &&  strncmp (values [row].name, name, length) == 0) {
            return values [row].value;
        }
    }

    return NULL;
}


bool web_page_render (const char *page, size_t length,
                      const web_value_t *values, size_t count,
                      web_write_fn write, void *context)
{
    assert (page);              //  Contract van de aanroeper
    assert (write);
    assert (values != NULL || count == 0);

    size_t at = 0;              //  Waar we zijn
    size_t plain = 0;           //  Waar het huidige stuk gewone tekst begon

    while (at < length) {
        if (page [at] != OPEN [0]
        ||  at + 1 >= length
        ||  page [at + 1] != OPEN [1]) {
            at++;
            continue;
        }

        //  Een {{ gezien. Waar sluit het?
        size_t name = at + 2;
        size_t end = name;

        while (end + 1 < length
        &&    !(page [end] == CLOSE [0] && page [end + 1] == CLOSE [1])) {
            end++;
        }

        if (end + 1 >= length) {
            break;              //  Nooit gesloten: de rest is gewone tekst
        }

        const char *fill = s_value_of (values, count, &page [name], end - name);

        if (fill == NULL) {
            //  Onbekende naam: laat het gat staan zoals het is, en ga verder
            //  ná de sluitende accolades, zodat we hem niet opnieuw vinden.
            at = end + 2;
            continue;
        }

        if (!s_write (write, context, &page [plain], at - plain)
        ||  !s_write (write, context, fill, strlen (fill))) {
            return false;
        }

        at = end + 2;
        plain = at;
    }

    return s_write (write, context, &page [plain], length - plain);
}
