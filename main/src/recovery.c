/*  =========================================================================
    recovery - decide when a device that cannot update should restart itself
    =========================================================================
*/

#include <assert.h>
#include <stddef.h>

#include "inc/day_ahead_model.h"
#include "inc/recovery.h"


//  --------------------------------------------------------------------------
//  The rules, as conditions. Each one only looks.

static bool
    s_busy (const recovery_input_t *input)
{
    return input->busy;
}

static bool
    s_too_few_failures (const recovery_input_t *input)
{
    return input->failures < RECOVERY_FAILURE_LIMIT;
}

static bool
    s_no_clock_and_long_enough (const recovery_input_t *input)
{
    return !input->clock_known && input->failures >= RECOVERY_FAILURE_LIMIT_NO_CLOCK;
}

static bool
    s_no_clock (const recovery_input_t *input)
{
    return !input->clock_known;
}

static bool
    s_night (const recovery_input_t *input)
{
    struct tm local;
    if (!day_ahead_localtime (input->now, &local))
        return false;
    return local.tm_hour >= RECOVERY_NIGHT_FROM_HOUR
        && local.tm_hour < RECOVERY_NIGHT_UNTIL_HOUR;
}

static bool
    s_always (const recovery_input_t *input)
{
    (void) input;
    return true;
}


//  The first row that applies decides; the last row always applies. Nobody
//  is in front of the device at night, and a restart takes a few seconds, so
//  the night is when a restart costs nothing.

static const struct {
    const char *name;
    bool (*applies) (const recovery_input_t *input);
    recovery_action_t action;
} s_rules [] = {
    { "busy",                            s_busy,                     RECOVERY_WAIT    },
    { "not enough failures yet",         s_too_few_failures,         RECOVERY_WAIT    },
    { "no clock, failing for half a day", s_no_clock_and_long_enough, RECOVERY_RESTART },
    { "no clock yet",                    s_no_clock,                 RECOVERY_WAIT    },
    { "night",                           s_night,                    RECOVERY_RESTART },
    { "waiting for the night",           s_always,                   RECOVERY_WAIT    }
};

#define RULE_COUNT (sizeof (s_rules) / sizeof (s_rules [0]))


//  --------------------------------------------------------------------------

recovery_action_t
    recovery_decide (const recovery_input_t *input, const char **rule)
{
    assert (input);             //  Caller's contract
    assert (rule);

    size_t row = 0;
    while (!s_rules [row].applies (input))
        row++;
    assert (row < RULE_COUNT);  //  The last row always applies

    *rule = s_rules [row].name;
    return s_rules [row].action;
}
