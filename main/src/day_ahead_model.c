#include "inc/day_ahead_model.h"

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static int64_t s_days(int year, int month, int day)
{
    year -= month <= 2;
    int era = year / 400;
    unsigned yoe = (unsigned)(year - era * 400);
    unsigned shifted = (unsigned)(month + (month > 2 ? -3 : 9));
    unsigned doy = (153 * shifted + 2) / 5 + (unsigned)day - 1;
    unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return (int64_t)era * 146097 + doe - 719468;
}

static bool s_gmtime(time_t utc, struct tm *result)
{
#ifdef _WIN32
    return gmtime_s(result, &utc) == 0;
#else
    return gmtime_r(&utc, result) != NULL;
#endif
}

static int s_offset(time_t utc)
{
    struct tm date;
    if (!s_gmtime(utc, &date)) return 3600;
    int year = date.tm_year + 1900;
    int64_t march = s_days(year, 3, 31);
    int64_t october = s_days(year, 10, 31);
    // EU clock changes occur at 01:00 UTC on the last Sunday of March/October.
    time_t summer_start = (time_t)((march - (march + 4) % 7) * 86400 + 3600);
    time_t summer_end = (time_t)((october - (october + 4) % 7) * 86400 + 3600);
    return utc >= summer_start && utc < summer_end ? 7200 : 3600;
}

bool day_ahead_localtime(time_t utc, struct tm *local)
{
    if (!local) return false;
    int offset = s_offset(utc);
    if (!s_gmtime(utc + offset, local)) return false;
    local->tm_isdst = offset == 7200;
    return true;
}

bool day_ahead_day_init(day_ahead_day_t *day, time_t now, int offset)
{
    struct tm date;
    if (!day || !day_ahead_localtime(now, &date) || offset < 0 || offset > 1) return false;
    memset(day, 0, sizeof(*day));
    time_t midnight = (time_t)((s_days(date.tm_year + 1900, date.tm_mon + 1, date.tm_mday) + offset) * 86400);
    day->start = midnight - s_offset(midnight - 3600);
    midnight += 86400;
    day->end = midnight - s_offset(midnight - 3600);
    if (day->start == (time_t)-1 || day->end <= day->start) return false;
    day->count = (size_t)((day->end - day->start) / DAY_AHEAD_QUARTER_SECONDS);
    return day->count > 0 && day->count <= DAY_AHEAD_MAX_POINTS;
}

bool day_ahead_parse_utc(const char *text, time_t *result)
{
    int year, month, day, hour, minute, second, consumed = 0;
    if (!text || !result ||
        sscanf(text, "%4d-%2d-%2dT%2d:%2d:%2dZ%n", &year, &month, &day,
               &hour, &minute, &second, &consumed) != 6 || consumed != 20 || text[consumed] ||
        year < 2020 || year > 2100 || month < 1 || month > 12 || day < 1 ||
        hour < 0 || hour > 23 || minute < 0 || minute > 59 || second < 0 || second > 59)
        return false;
    static const int lengths[] = {31,28,31,30,31,30,31,31,30,31,30,31};
    bool leap = year % 4 == 0 && (year % 100 != 0 || year % 400 == 0);
    if (day > lengths[month - 1] + (month == 2 && leap)) return false;

    int64_t days = s_days(year, month, day);
    *result = (time_t)(days * 86400 + hour * 3600 + minute * 60 + second);
    return true;
}

bool day_ahead_day_put(day_ahead_day_t *day, time_t start, time_t end, float price)
{
    if (!day || !isfinite(price) || fabsf(price) > 1000.0f || start < day->start ||
        start >= day->end || end - start != DAY_AHEAD_QUARTER_SECONDS ||
        (start - day->start) % DAY_AHEAD_QUARTER_SECONDS != 0) return false;
    size_t index = (size_t)((start - day->start) / DAY_AHEAD_QUARTER_SECONDS);
    if (index >= day->count) return false;
    if (!day->present[index]) day->available++;
    day->present[index] = true;
    day->eur_kwh[index] = price;
    return true;
}

int day_ahead_current(const day_ahead_day_t *day, time_t now)
{
    if (!day || now < day->start || now >= day->end) return -1;
    size_t index = (size_t)((now - day->start) / DAY_AHEAD_QUARTER_SECONDS);
    return index < day->count && day->present[index] ? (int)index : -1;
}

int day_ahead_cheapest(const day_ahead_day_t *day, time_t from)
{
    int best = -1;
    if (!day) return best;
    for (size_t i = 0; i < day->count; i++) {
        // A slot that has already begun is not a complete upcoming quarter.
        if (!day->present[i] || day->start + (time_t)i * DAY_AHEAD_QUARTER_SECONDS < from)
            continue;
        if (best < 0 || day->eur_kwh[i] < day->eur_kwh[best]) best = (int)i;
    }
    return best;
}
