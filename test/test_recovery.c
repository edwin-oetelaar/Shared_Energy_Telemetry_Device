/*  =========================================================================
    test_recovery - host tests for recovery_decide ()
    =========================================================================
*/

#define _POSIX_C_SOURCE 200809L
#undef NDEBUG
#include <assert.h>
#include <stdio.h>

#include "inc/day_ahead_model.h"
#include "inc/recovery.h"

static int s_failures = 0;

static time_t
    s_utc (const char *text)
{
    time_t result;
    assert (day_ahead_parse_utc (text, &result));
    return result;
}

static void
    s_check (const char *name, recovery_input_t input, recovery_action_t expected)
{
    const char *rule = NULL;
    recovery_action_t action = recovery_decide (&input, &rule);
    bool good = action == expected && rule != NULL;
    printf ("%s  %-44s -> %s (%s)\n", good ? "ok   " : "FOUT ", name,
            action == RECOVERY_RESTART ? "herstart" : "wacht", rule ? rule : "?");
    if (!good)
        s_failures++;
}

int
    main (void)
{
    //  November: Dutch winter time is UTC+1, so 03:00 local is 02:00 UTC.
    time_t night = s_utc ("2026-11-18T02:30:00Z");          //  03:30 CET
    time_t evening = s_utc ("2026-11-18T19:00:00Z");        //  20:00 CET

    s_check ("drie mislukt, 03:30",
             (recovery_input_t) { 3, true, night, false }, RECOVERY_RESTART);
    s_check ("twee mislukt, 03:30",
             (recovery_input_t) { 2, true, night, false }, RECOVERY_WAIT);
    s_check ("drie mislukt, 20:00",
             (recovery_input_t) { 3, true, evening, false }, RECOVERY_WAIT);
    s_check ("drie mislukt, 03:30, maar het portaal is open",
             (recovery_input_t) { 3, true, night, true }, RECOVERY_WAIT);

    //  The edges of the window: 03:00 is in, 05:00 is out, 02:59 is out.
    s_check ("rand: 03:00 precies",
             (recovery_input_t) { 3, true, s_utc ("2026-11-18T02:00:00Z"), false }, RECOVERY_RESTART);
    s_check ("rand: 04:59",
             (recovery_input_t) { 3, true, s_utc ("2026-11-18T03:59:00Z"), false }, RECOVERY_RESTART);
    s_check ("rand: 05:00 precies",
             (recovery_input_t) { 3, true, s_utc ("2026-11-18T04:00:00Z"), false }, RECOVERY_WAIT);
    s_check ("rand: 02:59",
             (recovery_input_t) { 3, true, s_utc ("2026-11-18T01:59:00Z"), false }, RECOVERY_WAIT);

    //  Summer: Dutch time is UTC+2, so 03:30 CEST is 01:30 UTC.
    s_check ("zomer: 03:30 CEST",
             (recovery_input_t) { 3, true, s_utc ("2026-07-01T01:30:00Z"), false }, RECOVERY_RESTART);
    s_check ("zomer: 02:30 CEST is nog geen nacht",
             (recovery_input_t) { 3, true, s_utc ("2026-07-01T00:30:00Z"), false }, RECOVERY_WAIT);

    //  No clock: wait for half a day of failures, then restart at any hour.
    s_check ("geen klok, elf mislukt",
             (recovery_input_t) { 11, false, 0, false }, RECOVERY_WAIT);
    s_check ("geen klok, twaalf mislukt",
             (recovery_input_t) { 12, false, 0, false }, RECOVERY_RESTART);
    s_check ("geen klok, twaalf mislukt, portaal open",
             (recovery_input_t) { 12, false, 0, true }, RECOVERY_WAIT);

    if (s_failures) {
        printf ("\n%d test(s) mislukt\n", s_failures);
        return 1;
    }
    printf ("\nalle tests geslaagd\n");
    return 0;
}
