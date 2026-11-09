#define _POSIX_C_SOURCE 200809L
#include "inc/day_ahead_model.h"
#undef NDEBUG
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>

static time_t utc(const char *text)
{
    time_t result;
    assert(day_ahead_parse_utc(text, &result));
    return result;
}

int main(void)
{
    day_ahead_day_t day;
    time_t ignored;
    assert(!day_ahead_parse_utc("2026-02-29T00:00:00Z", &ignored));
    assert(!day_ahead_parse_utc("2026-10-31T24:00:00Z", &ignored));
    assert(!day_ahead_parse_utc("2026-10-31T00:00:00+01:00", &ignored));
    assert(!day_ahead_parse_utc("2026-10-31T00:00:00", &ignored));
    assert(day_ahead_parse_utc("2028-02-29T00:00:00Z", &ignored));
    struct tm local;
    assert(day_ahead_localtime(utc("2026-03-29T00:59:59Z"), &local));
    assert(local.tm_hour == 1 && local.tm_min == 59 && !local.tm_isdst);
    assert(day_ahead_localtime(utc("2026-03-29T01:00:00Z"), &local));
    assert(local.tm_hour == 3 && local.tm_min == 0 && local.tm_isdst);
    assert(day_ahead_localtime(utc("2026-10-25T00:59:59Z"), &local));
    assert(local.tm_hour == 2 && local.tm_min == 59 && local.tm_isdst);
    assert(day_ahead_localtime(utc("2026-10-25T01:00:00Z"), &local));
    assert(local.tm_hour == 2 && local.tm_min == 0 && !local.tm_isdst);

    assert(day_ahead_day_init(&day, utc("2026-03-29T12:00:00Z"), 0));
    assert(day.count == 92);
    assert(day.start == utc("2026-03-28T23:00:00Z"));
    assert(day.end == utc("2026-03-29T22:00:00Z"));
    assert(day_ahead_day_init(&day, utc("2026-10-25T12:00:00Z"), 0));
    assert(day.count == 100);
    assert(day.start == utc("2026-10-24T22:00:00Z"));
    assert(day.end == utc("2026-10-25T23:00:00Z"));
    // Both occurrences of 02:00 occupy separate points.
    time_t first = utc("2026-10-25T00:00:00Z");
    time_t second = utc("2026-10-25T01:00:00Z");
    assert(day_ahead_day_put(&day, first, first + 900, 0.1f));
    assert(day_ahead_day_put(&day, second, second + 900, -0.2f));
    assert(day_ahead_current(&day, second) - day_ahead_current(&day, first) == 4);

    assert(day_ahead_day_init(&day, utc("2026-12-31T23:30:00Z"), 0));
    assert(day.start == utc("2026-12-31T23:00:00Z"));
    assert(day.count == 96);
    assert(day_ahead_day_init(&day, utc("2026-12-31T12:00:00Z"), 1));
    assert(day.start == utc("2026-12-31T23:00:00Z"));
    assert(day_ahead_current(&day, day.start) == -1);
    assert(day_ahead_cheapest(&day, day.start) == -1);
    assert(day_ahead_day_put(&day, day.start, day.start + 900, -0.15f));
    assert(day_ahead_day_put(&day, day.start + 900, day.start + 1800, -0.10f));
    assert(day_ahead_day_put(&day, day.start, day.start + 900, -0.20f));
    assert(day.available == 2); // Duplicate replaces; it does not inflate coverage.
    assert(day_ahead_current(&day, day.start + 899) == 0);
    assert(day_ahead_current(&day, day.start + 900) == 1);
    assert(day_ahead_current(&day, day.end) == -1);
    assert(day_ahead_cheapest(&day, day.start) == 0);
    assert(day_ahead_cheapest(&day, day.start + 1) == 1);
    assert(day_ahead_cheapest(&day, day.start + 901) == -1);
    assert(!day_ahead_day_put(&day, day.start - 900, day.start, 0.1f));
    assert(!day_ahead_day_put(&day, day.end, day.end + 900, 0.1f));
    assert(!day_ahead_day_put(&day, day.start + 1, day.start + 901, 0.1f));
    assert(!day_ahead_day_put(&day, day.start, day.start + 3600, 0.1f));
    assert(!day_ahead_day_put(&day, day.start, day.start + 900, NAN));
    assert(!day_ahead_day_put(&day, day.start, day.start + 900, INFINITY));
    puts("day_ahead_model: all tests passed");
    return 0;
}
