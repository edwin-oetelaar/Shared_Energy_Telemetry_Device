/*  =========================================================================
    form_text - clean up a text value typed or pasted into the portal
    =========================================================================
*/

#include <assert.h>
#include <stdint.h>
#include <string.h>

#include "inc/form_text.h"

//  What each byte is, indexed by the byte itself. Text is the zero default, so
//  only the exceptions are listed. Bytes from 0x80 up are text; they are how
//  UTF-8 looks, and refusing them is not this module's business.
//
//  Three kinds of exception, because the ends and the middle differ:
//
//      kind        at either end   in the middle
//      space       trimmed         kept
//      break       trimmed         refused  (tab, CR, LF: the pasted line end)
//      control     refused         refused

typedef enum {
    CHAR_TEXT = 0,
    CHAR_SPACE,
    CHAR_BREAK,
    CHAR_CONTROL
} char_class_t;

static const uint8_t s_char_class [256] = {
    [0x00] = CHAR_CONTROL, [0x01] = CHAR_CONTROL, [0x02] = CHAR_CONTROL,
    [0x03] = CHAR_CONTROL, [0x04] = CHAR_CONTROL, [0x05] = CHAR_CONTROL,
    [0x06] = CHAR_CONTROL, [0x07] = CHAR_CONTROL, [0x08] = CHAR_CONTROL,
    ['\t'] = CHAR_BREAK,   ['\n'] = CHAR_BREAK,   [0x0B] = CHAR_CONTROL,
    [0x0C] = CHAR_CONTROL, ['\r'] = CHAR_BREAK,   [0x0E] = CHAR_CONTROL,
    [0x0F] = CHAR_CONTROL, [0x10] = CHAR_CONTROL, [0x11] = CHAR_CONTROL,
    [0x12] = CHAR_CONTROL, [0x13] = CHAR_CONTROL, [0x14] = CHAR_CONTROL,
    [0x15] = CHAR_CONTROL, [0x16] = CHAR_CONTROL, [0x17] = CHAR_CONTROL,
    [0x18] = CHAR_CONTROL, [0x19] = CHAR_CONTROL, [0x1A] = CHAR_CONTROL,
    [0x1B] = CHAR_CONTROL, [0x1C] = CHAR_CONTROL, [0x1D] = CHAR_CONTROL,
    [0x1E] = CHAR_CONTROL, [0x1F] = CHAR_CONTROL, [' ']  = CHAR_SPACE,
    [0x7F] = CHAR_CONTROL
};

#define CLASS_OF(c) (s_char_class [(uint8_t) (c)])

//  Both kinds of white space may be trimmed from the ends.
#define TRIMMABLE(c) (CLASS_OF (c) == CHAR_SPACE || CLASS_OF (c) == CHAR_BREAK)


//  --------------------------------------------------------------------------
//  Trim both ends, then look at what is left. Nothing is refused before the
//  trim, so a key with a line break after it is accepted and cleaned, and a
//  key with one in the middle is not.

bool
    form_text_clean (char *value)
{
    assert (value);             //  Caller's contract, not client input

    const char *first = value;
    while (*first && TRIMMABLE (*first))
        first++;

    const char *end = first + strlen (first);
    while (end > first && TRIMMABLE (end [-1]))
        end--;

    size_t length = (size_t) (end - first);
    memmove (value, first, length);
    value [length] = '\0';

    for (size_t index = 0; index < length; index++) {
        char_class_t kind = (char_class_t) CLASS_OF (value [index]);
        if (kind == CHAR_BREAK || kind == CHAR_CONTROL)
            return false;       //  A line break or control character inside
    }
    return true;
}
