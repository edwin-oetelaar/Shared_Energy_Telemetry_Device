/*  =========================================================================
    heap_report - say how much memory is free, at a named moment
    =========================================================================
*/

#include <assert.h>

#include "esp_heap_caps.h"
#include "esp_log.h"

#include "inc/heap_report.h"

static const char *TAG = "[heap]";

#define INTERNAL (MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT)
#define PSRAM    (MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT)


//  --------------------------------------------------------------------------

void
    heap_report (const char *moment)
{
    assert (moment);            //  Caller's contract

    ESP_LOGI (TAG, "%s: internal %u free, %u largest, %u lowest ever | psram %u free, %u largest",
              moment,
              (unsigned) heap_caps_get_free_size (INTERNAL),
              (unsigned) heap_caps_get_largest_free_block (INTERNAL),
              (unsigned) heap_caps_get_minimum_free_size (INTERNAL),
              (unsigned) heap_caps_get_free_size (PSRAM),
              (unsigned) heap_caps_get_largest_free_block (PSRAM));
}


//  --------------------------------------------------------------------------

void
    heap_low_start (heap_low_t *low)
{
    assert (low);               //  Caller's contract

    low->internal_start = heap_caps_get_free_size (INTERNAL);
    low->psram_start = heap_caps_get_free_size (PSRAM);

    //  A monitor left running by an operation that never reported is stopped
    //  first; the new operation is the one that matters.
    if (heap_caps_monitor_local_minimum_free_size_start () != ESP_OK) {
        heap_caps_monitor_local_minimum_free_size_stop ();
        heap_caps_monitor_local_minimum_free_size_start ();
    }
}


//  --------------------------------------------------------------------------

void
    heap_low_report (const heap_low_t *low, const char *moment)
{
    assert (low);               //  Caller's contract
    assert (moment);

    size_t internal_low = heap_caps_get_minimum_free_size (INTERNAL);
    size_t psram_low = heap_caps_get_minimum_free_size (PSRAM);
    heap_caps_monitor_local_minimum_free_size_stop ();

    ESP_LOGI (TAG, "%s: lowest internal %u free (%u used by it), lowest psram %u free (%u used by it)",
              moment,
              (unsigned) internal_low,
              (unsigned) (low->internal_start > internal_low ? low->internal_start - internal_low : 0),
              (unsigned) psram_low,
              (unsigned) (low->psram_start > psram_low ? low->psram_start - psram_low : 0));
}
