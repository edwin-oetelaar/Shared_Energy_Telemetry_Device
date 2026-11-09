#ifndef DAY_AHEAD_MODEL_H
#define DAY_AHEAD_MODEL_H

#include <stdbool.h>
#include <stddef.h>
#include <time.h>

// A Dutch autumn clock-change day contains 100 quarter-hours, not 96.
#define DAY_AHEAD_MAX_POINTS 100
#define DAY_AHEAD_QUARTER_SECONDS 900

typedef struct {
    time_t start;
    time_t end;
    size_t count;
    size_t available;
    float eur_kwh[DAY_AHEAD_MAX_POINTS];
    bool present[DAY_AHEAD_MAX_POINTS];
} day_ahead_day_t;

// Dutch civil time without changing the process-wide TZ used by other modules.
bool day_ahead_localtime(time_t utc, struct tm *local);
bool day_ahead_day_init(day_ahead_day_t *day, time_t now, int offset);
bool day_ahead_parse_utc(const char *text, time_t *result);
bool day_ahead_day_put(day_ahead_day_t *day, time_t start, time_t end, float price);
int day_ahead_current(const day_ahead_day_t *day, time_t now);
int day_ahead_cheapest(const day_ahead_day_t *day, time_t from);

#endif
