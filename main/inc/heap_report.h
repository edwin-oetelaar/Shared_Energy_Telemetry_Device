/*  =========================================================================
    heap_report - say how much memory is free, at a named moment

    Two kinds of memory matter on this board: internal RAM, which is small
    and which some things must use (task stacks that run while flash is being
    written, DMA), and 16 MB of PSRAM. For each, the log line gives what is
    free, the largest single block, and for internal RAM the lowest point
    since start-up. Free memory that is not in one piece is what makes a
    TLS connection fail after weeks: it needs one block of 16 KB.

    heap_low_* follows an operation, such as an update, and reports how far
    it pushed memory down. It uses ESP-IDF's own low-water monitor, so it also
    catches the deepest point inside a TLS handshake, where nobody can look.
    Only one operation can be followed at a time.
    =========================================================================
*/

#ifndef HEAP_REPORT_H_INCLUDED
#define HEAP_REPORT_H_INCLUDED

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    size_t internal_start;
    size_t psram_start;
} heap_low_t;

//  One line: internal free, largest, lowest ever; PSRAM free and largest.
void
    heap_report (const char *moment);

//  Begin following an operation.
void
    heap_low_start (heap_low_t *low);

//  Say how far the operation pushed memory down.
void
    heap_low_report (const heap_low_t *low, const char *moment);

#ifdef __cplusplus
}
#endif

#endif
