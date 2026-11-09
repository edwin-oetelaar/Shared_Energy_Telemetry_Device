#ifndef DAY_AHEAD_H
#define DAY_AHEAD_H

#include "esp_err.h"
#include "inc/day_ahead_model.h"

typedef enum {
    DAY_AHEAD_WAIT_WIFI, DAY_AHEAD_WAIT_TIME, DAY_AHEAD_LOADING,
    DAY_AHEAD_READY, DAY_AHEAD_ERROR
} day_ahead_state_t;

typedef struct {
    day_ahead_state_t state;
    time_t fetched_at;
    day_ahead_day_t days[2];
} day_ahead_snapshot_t;

// Own task and cache; neither Energyboxx keys nor telemetry are required.
esp_err_t day_ahead_init(void);
void day_ahead_snapshot(day_ahead_snapshot_t *out);

#endif
