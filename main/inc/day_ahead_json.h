#ifndef DAY_AHEAD_JSON_H
#define DAY_AHEAD_JSON_H
#include "inc/day_ahead_model.h"
// Parse the base series into two initialized days; failure leaves partial data.
// The caller publishes the candidate only after this returns true.
bool day_ahead_json_parse(const char *body, day_ahead_day_t days[2]);
#endif
