/*  =========================================================================
    web_page - een ingebedde pagina uitsturen, met de gaten ingevuld

    De pagina's van het portaal staan als bestand in main/web/ en worden bij
    het bouwen ingebed. Waar iets per bezoek verschilt staat in het bestand
    een gat: {{naam}}. Deze module loopt de pagina langs, vult de gaten in en
    geeft de stukken door aan wie ze verstuurt.

    Plain C, zonder ESP-IDF, zodat de lastige kant - het vinden en invullen
    van die gaten - op een host te testen is. Dat is waar dit soort code fout
    gaat: een gat dat niet gesloten wordt, een naam die niemand kent, een
    lege waarde.

    Bevinding L5 in docs/REVIEW.md.
    =========================================================================
*/

#ifndef WEB_PAGE_H_INCLUDED
#define WEB_PAGE_H_INCLUDED

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

//  Een gat en wat erin hoort. De naam staat in de pagina tussen {{ en }}.
typedef struct {
    const char *name;
    const char *value;
} web_value_t;

//  Stuurt één stuk weg. Geeft false als het misging; dan stopt het geheel.
typedef bool (*web_write_fn) (void *context, const char *text, size_t length);

//  Loop de pagina langs en geef hem stuk voor stuk door.
//
//  Een gat waarvan de naam niet in de tabel staat, gaat er onveranderd door.
//  Zo staat een typefout zichtbaar op de pagina in plaats van te verdwijnen -
//  wie hem ziet weet meteen wat er mis is.
//
//  Een {{ zonder }} erachter is geen gat maar tekst. Dat is de veilige kant:
//  een pagina die halverwege afbreekt zou erger zijn dan twee accolades in
//  beeld.
//
//  Een leeg stuk wordt nooit doorgegeven. Bij chunked encoding betekent een
//  stuk van lengte nul "einde antwoord", en een lege waarde zou de pagina dus
//  midden in afkappen. Die val hoort hier, waar hij begrepen wordt, en niet
//  bij elke aanroeper.
bool web_page_render (const char *page, size_t length,
                      const web_value_t *values, size_t count,
                      web_write_fn write, void *context);

#ifdef __cplusplus
}
#endif

#endif
