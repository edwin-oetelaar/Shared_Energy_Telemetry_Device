/*  =========================================================================
    day_ahead_json - read the EnergyZero price response

    Pure C plus cJSON, so the host tests can cover it.
    =========================================================================
*/

#ifndef DAY_AHEAD_JSON_H_INCLUDED
#define DAY_AHEAD_JSON_H_INCLUDED

#include "inc/day_ahead_model.h"

#ifdef __cplusplus
extern "C" {
#endif

//  Parse the base series into two initialized days; failure leaves partial data.
//  The caller publishes the candidate only after this returns true.
bool
    day_ahead_json_parse (const char *body, day_ahead_day_t days [2]);

#ifdef __cplusplus
}
#endif

#endif
