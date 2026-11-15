/*  =========================================================================
    test_day_ahead_model - dates, clock changes, quarters
    =========================================================================
*/

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

//  The schedule: one case per rule, and the floor of 60 s. Times are Dutch
//  local times on a winter day, 15 November 2026 (UTC+1).

static void
    s_check_schedule (void)
{
    day_ahead_day_t today, tomorrow;
    assert (day_ahead_day_init (&today, utc ("2026-11-15T12:00:00Z"), 0));
    assert (day_ahead_day_init (&tomorrow, utc ("2026-11-15T12:00:00Z"), 1));
    const char *rule = NULL;

    //  00:23, tomorrow not in yet: ask at 13:05 plus the spread.
    day_ahead_schedule_t night = { .now = utc ("2026-11-14T23:23:00Z"), .today = today.start,
                                   .tomorrow = tomorrow.start, .spread = 214 };
    assert (day_ahead_next_request (&night, &rule) == 12 * 3600 + 42 * 60 + 214);
    assert (rule [0] == 'b');           //  before today's publication

    //  13:10, tomorrow complete: nothing until tomorrow 13:05 plus the spread.
    day_ahead_schedule_t done = { .now = utc ("2026-11-15T12:10:00Z"), .today = today.start,
                                  .tomorrow = tomorrow.start, .tomorrow_complete = true, .spread = 214 };
    assert (day_ahead_next_request (&done, &rule) == 23 * 3600 + 55 * 60 + 214);
    assert (rule [0] == 't');           //  tomorrow is complete

    //  14:00, tomorrow still missing: every half hour, with up to a fifth more.
    day_ahead_schedule_t late = { .now = utc ("2026-11-15T13:00:00Z"), .today = today.start,
                                  .tomorrow = tomorrow.start, .random = 1000 };
    long long wait = day_ahead_next_request (&late, &rule);
    assert (wait == DAY_AHEAD_REFRESH_SECONDS + 1000 % (DAY_AHEAD_REFRESH_SECONDS / 5 + 1));
    assert (rule [0] == 'w');           //  waiting for tomorrow

    //  A second before 13:05 without spread: never sooner than a minute.
    day_ahead_schedule_t edge = { .now = today.start + DAY_AHEAD_PUBLICATION_SECONDS - 1,
                                  .today = today.start, .tomorrow = tomorrow.start };
    assert (day_ahead_next_request (&edge, &rule) == 60);

    //  Spring clock change, 29 March 2026: 13:05 counted from midnight is
    //  14:05 by the clock. Late, and after the publication: harmless.
    day_ahead_day_t spring;
    assert (day_ahead_day_init (&spring, utc ("2026-03-29T10:00:00Z"), 0));
    day_ahead_schedule_t change = { .now = spring.start + 60, .today = spring.start,
                                    .tomorrow = spring.end };
    long long until = day_ahead_next_request (&change, &rule);
    struct tm at;
    assert (day_ahead_localtime (change.now + (time_t) until, &at));
    assert (at.tm_hour == 14 && at.tm_min == 5);
}

int main(void)
{
    s_check_schedule ();
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
