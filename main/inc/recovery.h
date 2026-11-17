/*  =========================================================================
    recovery - decide when a device that cannot update should restart itself

    After weeks of running, an update can keep failing until the device is
    restarted: whatever piles up in memory is gone after a restart, and the
    first check after start-up then succeeds. A customer did exactly that
    with the plug in November 2026. This module lets the device do it itself,
    at night, after a few failed attempts in a row.

    Pure C, without ESP-IDF, so the host tests can try every rule.
    =========================================================================
*/

#ifndef RECOVERY_H_INCLUDED
#define RECOVERY_H_INCLUDED

#include <stdbool.h>
#include <time.h>

#ifdef __cplusplus
extern "C" {
#endif

//  Failed update attempts in a row before a restart is considered. With a
//  check every hour, that is three hours of failing.
#define RECOVERY_FAILURE_LIMIT          3

//  Without a clock the night cannot be found. Then wait half a day of
//  failures before restarting at whatever hour it is.
#define RECOVERY_FAILURE_LIMIT_NO_CLOCK 12

//  The night window, Dutch local time: from 03:00 up to 05:00.
#define RECOVERY_NIGHT_FROM_HOUR        3
#define RECOVERY_NIGHT_UNTIL_HOUR       5

typedef struct {
    unsigned failures;          //  Update attempts that failed in a row
    bool clock_known;           //  SNTP has set the clock
    time_t now;                 //  UTC; only looked at when the clock is known
    bool busy;                  //  The portal is open, or an update is running
} recovery_input_t;

typedef enum {
    RECOVERY_WAIT = 0,
    RECOVERY_RESTART
} recovery_action_t;

//  What to do now. Sets *rule to the name of the rule that decided, for the
//  log.
recovery_action_t
    recovery_decide (const recovery_input_t *input, const char **rule);

#ifdef __cplusplus
}
#endif

#endif
