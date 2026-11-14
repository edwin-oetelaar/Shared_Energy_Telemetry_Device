/*  =========================================================================
    day_ahead - fetch Dutch day-ahead electricity prices

    Has its own task and cache; neither Energyboxx keys nor telemetry are
    required, only Wi-Fi and a clock that has been set by SNTP.
    =========================================================================
*/

#ifndef DAY_AHEAD_H_INCLUDED
#define DAY_AHEAD_H_INCLUDED

#include "esp_err.h"
#include "inc/day_ahead_model.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    DAY_AHEAD_WAIT_WIFI, DAY_AHEAD_WAIT_TIME, DAY_AHEAD_LOADING,
    DAY_AHEAD_READY, DAY_AHEAD_ERROR
} day_ahead_state_t;

typedef struct {
    day_ahead_state_t state;
    time_t fetched_at;
    day_ahead_day_t days [2];
} day_ahead_snapshot_t;

//  Own task and cache; neither Energyboxx keys nor telemetry are required.
esp_err_t
    day_ahead_init (void);

//  Copy the cache, under its lock, so the caller can read it at leisure.
void
    day_ahead_snapshot (day_ahead_snapshot_t *out);

#ifdef __cplusplus
}
#endif

#endif
