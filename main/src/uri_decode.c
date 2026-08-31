/*  =========================================================================
    uri_decode - decode application/x-www-form-urlencoded values in place
    =========================================================================
*/

#include <assert.h>
#include <stdint.h>

#include "inc/uri_decode.h"

//  Hex digit value plus one, indexed by the character itself; zero means "not
//  a hex digit". The plus-one lets one table answer both questions at once -
//  is this a hex digit, and what is it worth - so the decoder needs no range
//  tests. Every one of the 256 byte values has an answer here, including the
//  ones no well-formed body will ever contain.

static const uint8_t s_hex_digit [256] = {
    ['0'] =  1, ['1'] =  2, ['2'] =  3, ['3'] =  4, ['4'] =  5,
    ['5'] =  6, ['6'] =  7, ['7'] =  8, ['8'] =  9, ['9'] = 10,
    ['a'] = 11, ['b'] = 12, ['c'] = 13, ['d'] = 14, ['e'] = 15, ['f'] = 16,
    ['A'] = 11, ['B'] = 12, ['C'] = 13, ['D'] = 14, ['E'] = 15, ['F'] = 16
};

//  A percent escape is the only construct in this grammar that spans more than
//  one character, so three states describe the whole decoder: reading literal
//  text, holding the high nibble, holding the low nibble. Ending anywhere but
//  in literal_text means the value stopped halfway through an escape.

typedef enum {
    state_literal_text = 0,
    state_high_nibble,
    state_low_nibble
} decoder_state_t;


//  --------------------------------------------------------------------------
//  Decode one percent-encoded form value in place. Returns false, leaving the
//  buffer half-decoded, when the value is malformed.

bool
    uri_decode (char *value)
{
    assert (value);             //  Caller's contract, not client input

    decoder_state_t state = state_literal_text;
    uint8_t high_nibble = 0;
    const char *read = value;
    char *write = value;

    while (*read) {
        uint8_t digit = s_hex_digit [(uint8_t) *read];

        switch (state) {
            case state_literal_text:
                if (*read == '%')
                    state = state_high_nibble;
                else
                    //  "+" is a space in this content type; the browser sends
                    //  a literal plus as "%2B", so this never eats one.
                    *write++ = (*read == '+' ? ' ' : *read);
                break;

            case state_high_nibble:
                if (digit == 0)
                    return false;
                high_nibble = (uint8_t) ((digit - 1) << 4);
                state = state_low_nibble;
                break;

            case state_low_nibble:
                if (digit == 0)
                    return false;
                *write++ = (char) (high_nibble | (digit - 1));
                state = state_literal_text;
                break;
        }
        read++;
    }
    if (state != state_literal_text)
        return false;           //  Value ended in the middle of an escape

    //  Decoding only ever shrinks a value, so the write pointer cannot have
    //  passed the read pointer and the terminator stays inside the buffer.
    assert (write <= read);

    *write = '\0';
    return true;
}
