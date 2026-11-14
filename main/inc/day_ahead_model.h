/*  =========================================================================
    day_ahead_model - Dutch civil days in quarter-hours, and prices per quarter

    Pure C, without ESP-IDF, so the host tests can cover it. Knows when a
    Dutch day begins and ends in UTC, also on the two days a year the clock
    changes, and stores one price per quarter-hour of that day.
    =========================================================================
*/

#ifndef DAY_AHEAD_MODEL_H_INCLUDED
#define DAY_AHEAD_MODEL_H_INCLUDED

#include <stdbool.h>
#include <stddef.h>
#include <time.h>

#ifdef __cplusplus
extern "C" {
#endif

//  A Dutch autumn clock-change day contains 100 quarter-hours, not 96.
#define DAY_AHEAD_MAX_POINTS 100
#define DAY_AHEAD_QUARTER_SECONDS 900

//  2025-01-01T00:00:00Z. A clock that reads earlier than this has not been set
//  yet, whatever it claims; no date can be chosen from it.
#define DAY_AHEAD_EARLIEST_TIME ((time_t) 1735689600)

typedef struct {
    time_t start;
    time_t end;
    size_t count;
    size_t available;
    float eur_kwh [DAY_AHEAD_MAX_POINTS];
    bool present [DAY_AHEAD_MAX_POINTS];
} day_ahead_day_t;

//  Dutch civil time without changing the process-wide TZ used by other modules.
//  The pointers in this module are the caller's contract and are asserted; the
//  times, texts and prices are data and are checked.
bool
    day_ahead_localtime (time_t utc, struct tm *local);

//  Prepare day to hold today (offset 0) or tomorrow (offset 1), as seen at now.
//  Any other offset is a programming error.
bool
    day_ahead_day_init (day_ahead_day_t *day, time_t now, int offset);

//  Read "YYYY-MM-DDTHH:MM:SSZ". Anything else is refused.
bool
    day_ahead_parse_utc (const char *text, time_t *result);

//  Store one quarter's price. Refuses a quarter outside the day, one that is
//  not exactly fifteen minutes long, and a price that is not a sane number.
bool
    day_ahead_day_put (day_ahead_day_t *day, time_t start, time_t end, float price);

//  The quarter that contains now, or -1 when there is no price for it.
int
    day_ahead_current (const day_ahead_day_t *day, time_t now);

//  The cheapest quarter that has not begun by from, or -1 when there is none.
int
    day_ahead_cheapest (const day_ahead_day_t *day, time_t from);

#ifdef __cplusplus
}
#endif

#endif
