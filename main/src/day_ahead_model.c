/*  =========================================================================
    day_ahead_model - Dutch civil days in quarter-hours, and prices per quarter
    =========================================================================
*/

#include "inc/day_ahead_model.h"

#include <assert.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>


//  --------------------------------------------------------------------------
//  Days since 1970-01-01 for a civil date, without the C library and its
//  time zone. The days-from-civil algorithm, valid for any Gregorian date.

static int64_t
    s_days (int year, int month, int day)
{
    year -= month <= 2;
    int era = year / 400;
    unsigned yoe = (unsigned) (year - era * 400);
    unsigned shifted = (unsigned) (month + (month > 2 ? -3 : 9));
    unsigned doy = (153 * shifted + 2) / 5 + (unsigned) day - 1;
    unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return (int64_t) era * 146097 + doe - 719468;
}


//  --------------------------------------------------------------------------
//  Break a UTC time into its parts, on whichever platform this is built for.

static bool
    s_gmtime (time_t utc, struct tm *result)
{
#ifdef _WIN32
    return gmtime_s (result, &utc) == 0;
#else
    return gmtime_r (&utc, result) != NULL;
#endif
}


//  --------------------------------------------------------------------------
//  The Dutch offset from UTC at a moment, in seconds: summer time or winter
//  time. Falls back to winter time if the date cannot be broken down.

static int
    s_offset (time_t utc)
{
    struct tm date;
    if (!s_gmtime (utc, &date))
        return 3600;
    int year = date.tm_year + 1900;
    int64_t march = s_days (year, 3, 31);
    int64_t october = s_days (year, 10, 31);
    //  EU clock changes occur at 01:00 UTC on the last Sunday of March/October.
    time_t summer_start = (time_t) ((march - (march + 4) % 7) * 86400 + 3600);
    time_t summer_end = (time_t) ((october - (october + 4) % 7) * 86400 + 3600);
    return utc >= summer_start && utc < summer_end ? 7200 : 3600;
}


//  --------------------------------------------------------------------------
//  Dutch civil time for a UTC moment.

bool
    day_ahead_localtime (time_t utc, struct tm *local)
{
    assert (local);             //  Caller's contract
    int offset = s_offset (utc);
    if (!s_gmtime (utc + offset, local))
        return false;
    local->tm_isdst = offset == 7200;
    return true;
}


//  --------------------------------------------------------------------------
//  Set a day up empty, from local midnight to the next local midnight. The
//  offset is read an hour before each midnight: the clock never changes
//  within two hours of midnight, so that is the offset midnight itself has.

bool
    day_ahead_day_init (day_ahead_day_t *day, time_t now, int offset)
{
    assert (day);               //  Caller's contract
    assert (offset == 0 || offset == 1);
    struct tm date;
    if (!day_ahead_localtime (now, &date))
        return false;
    memset (day, 0, sizeof (*day));
    time_t midnight = (time_t) ((s_days (date.tm_year + 1900, date.tm_mon + 1, date.tm_mday) + offset) * 86400);
    day->start = midnight - s_offset (midnight - 3600);
    midnight += 86400;
    day->end = midnight - s_offset (midnight - 3600);
    if (day->start == (time_t) -1 || day->end <= day->start)
        return false;
    day->count = (size_t) ((day->end - day->start) / DAY_AHEAD_QUARTER_SECONDS);
    return day->count > 0 && day->count <= DAY_AHEAD_MAX_POINTS;
}


//  --------------------------------------------------------------------------
//  Read a UTC timestamp in exactly the form the API uses. The text comes from
//  outside, so every field is checked, the calendar included.

bool
    day_ahead_parse_utc (const char *text, time_t *result)
{
    assert (text);              //  Caller's contract: checked to be a string
    assert (result);
    int year, month, day, hour, minute, second, consumed = 0;
    if (sscanf (text, "%4d-%2d-%2dT%2d:%2d:%2dZ%n", &year, &month, &day,
                &hour, &minute, &second, &consumed) != 6
    ||  consumed != 20 || text [consumed]
    ||  year < 2020 || year > 2100 || month < 1 || month > 12 || day < 1
    ||  hour < 0 || hour > 23 || minute < 0 || minute > 59 || second < 0 || second > 59)
        return false;
    static const int lengths [] = {31,28,31,30,31,30,31,31,30,31,30,31};
    bool leap = year % 4 == 0 && (year % 100 != 0 || year % 400 == 0);
    if (day > lengths [month - 1] + (month == 2 && leap))
        return false;

    int64_t days = s_days (year, month, day);
    *result = (time_t) (days * 86400 + hour * 3600 + minute * 60 + second);
    return true;
}


//  --------------------------------------------------------------------------
//  Store one quarter's price in its place in the day.

bool
    day_ahead_day_put (day_ahead_day_t *day, time_t start, time_t end, float price)
{
    assert (day);               //  Caller's contract
    if (!isfinite (price) || fabsf (price) > 1000.0f || start < day->start
    ||  start >= day->end || end - start != DAY_AHEAD_QUARTER_SECONDS
    ||  (start - day->start) % DAY_AHEAD_QUARTER_SECONDS != 0)
        return false;
    size_t index = (size_t) ((start - day->start) / DAY_AHEAD_QUARTER_SECONDS);
    if (index >= day->count)
        return false;
    if (!day->present [index])
        day->available++;
    day->present [index] = true;
    day->eur_kwh [index] = price;
    return true;
}


//  --------------------------------------------------------------------------
//  The quarter now falls in, if the day has a price for it.

int
    day_ahead_current (const day_ahead_day_t *day, time_t now)
{
    assert (day);               //  Caller's contract
    if (now < day->start || now >= day->end)
        return -1;
    size_t index = (size_t) ((now - day->start) / DAY_AHEAD_QUARTER_SECONDS);
    return index < day->count && day->present [index] ? (int) index : -1;
}


//  --------------------------------------------------------------------------
//  The cheapest complete quarter still to come.

int
    day_ahead_cheapest (const day_ahead_day_t *day, time_t from)
{
    assert (day);               //  Caller's contract
    int best = -1;
    for (size_t i = 0; i < day->count; i++) {
        //  A slot that has already begun is not a complete upcoming quarter.
        if (!day->present [i] || day->start + (time_t) i * DAY_AHEAD_QUARTER_SECONDS < from)
            continue;
        if (best < 0 || day->eur_kwh [i] < day->eur_kwh [best])
            best = (int) i;
    }
    return best;
}
