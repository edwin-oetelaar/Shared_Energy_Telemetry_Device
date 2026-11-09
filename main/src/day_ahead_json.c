#include "inc/day_ahead_json.h"
#include "cJSON.h"
#include <ctype.h>
#include <errno.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

// Only decode base entries, one object at a time. The API also returns three
// tax/markup series: building a cJSON tree for all of them wastes scarce RAM.
bool day_ahead_json_parse(const char *body, day_ahead_day_t days[2])
{
    if (!body || !days) return false;
    const char *cursor = strstr(body, "\"base\"");
    if (!cursor) return false;
    cursor += 6;
    while (isspace((unsigned char)*cursor)) cursor++;
    if (*cursor++ != ':') return false;
    while (isspace((unsigned char)*cursor)) cursor++;
    if (*cursor++ != '[') return false;
    while (true) {
        while (isspace((unsigned char)*cursor)) cursor++;
        if (*cursor == ']') return true;
        const char *end = NULL;
        cJSON *entry = cJSON_ParseWithOpts(cursor, &end, false);
        if (!entry || !cJSON_IsObject(entry) || !end || end <= cursor) {
            cJSON_Delete(entry);
            return false;
        }
        cJSON *start = cJSON_GetObjectItemCaseSensitive(entry, "start");
        cJSON *finish = cJSON_GetObjectItemCaseSensitive(entry, "end");
        cJSON *price = cJSON_GetObjectItemCaseSensitive(entry, "price");
        cJSON *value = cJSON_GetObjectItemCaseSensitive(price, "value");
        time_t a = 0, b = 0;
        char *tail = NULL;
        errno = 0;
        float number = cJSON_IsString(value) ? strtof(value->valuestring, &tail) : 0;
        bool valid = cJSON_IsString(start) && cJSON_IsString(finish) &&
                     cJSON_IsString(value) && tail != value->valuestring && tail &&
                     *tail == '\0' && errno == 0 &&
                     isfinite(number) && fabsf(number) <= 1000.0f &&
                     day_ahead_parse_utc(start->valuestring, &a) &&
                     day_ahead_parse_utc(finish->valuestring, &b) &&
                     b - a == DAY_AHEAD_QUARTER_SECONDS && a % DAY_AHEAD_QUARTER_SECONDS == 0;
        if (valid) {
            for (size_t d = 0; d < 2; d++)
                day_ahead_day_put(&days[d], a, b, number);
        }
        cJSON_Delete(entry);
        if (!valid) return false;
        cursor = end;
        while (isspace((unsigned char)*cursor)) cursor++;
        if (*cursor == ']') return true;
        if (*cursor++ != ',') return false;
        // Reject a trailing comma rather than accepting a truncated schedule.
        while (isspace((unsigned char)*cursor)) cursor++;
        if (*cursor != '{') return false;
    }
}
