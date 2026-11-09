#include "inc/day_ahead_json.h"
#undef NDEBUG
#include <assert.h>
#include <math.h>
#include <stdio.h>

static day_ahead_day_t days[2];

static void reset(void)
{
    time_t now;
    assert(day_ahead_parse_utc("2026-10-31T12:00:00Z", &now));
    assert(day_ahead_day_init(&days[0], now, 0));
    assert(day_ahead_day_init(&days[1], now, 1));
}

int main(void)
{
    reset();
    // Match the live EnergyZero schema, including string-valued prices.
    assert(day_ahead_json_parse("{\"base\":["
        "{\"start\":\"2026-10-30T23:00:00Z\",\"end\":\"2026-10-30T23:15:00Z\",\"price\":{\"value\":\"-0.12345\"}},"
        "{\"start\":\"2026-10-31T23:00:00Z\",\"end\":\"2026-10-31T23:15:00Z\",\"price\":{\"value\":\"0\"}}],"
        "\"all_in_with_vat\":[{\"price\":{\"value\":\"99\"}}]}", days));
    assert(days[0].available == 1 && days[1].available == 1);
    assert(fabsf(days[0].eur_kwh[0] + 0.12345f) < 0.000001f);
    assert(days[1].eur_kwh[0] == 0);
    reset();
    assert(day_ahead_json_parse("{\"base\":[]}", days));
    assert(days[0].available == 0 && days[1].available == 0);
    assert(!day_ahead_json_parse("{}", days));
    assert(!day_ahead_json_parse("{\"base\":null}", days));
    assert(!day_ahead_json_parse("{\"base\":[{}]}", days));
    assert(!day_ahead_json_parse("{\"base\":[", days));
    assert(!day_ahead_json_parse("{\"base\":[null]}", days));
    assert(!day_ahead_json_parse("{\"base\":[{\"start\":\"2026-10-30T23:00:00Z\","
        "\"end\":\"2026-10-30T23:15:00Z\",\"price\":{\"value\":\"oops\"}}]}", days));
    assert(!day_ahead_json_parse("{\"base\":[{\"start\":\"2026-10-30T23:00:00Z\","
        "\"end\":\"2026-10-30T23:15:00Z\",\"price\":{\"value\":\"0.12\"}},]}", days));
    puts("day_ahead_json: all tests passed");
    return 0;
}
