/*  =========================================================================
    uri_decode - decode application/x-www-form-urlencoded values in place

    ESP-IDF's httpd_query_key_value() splits a form body into its separate
    fields but leaves the percent-escapes alone; it does no decoding at all.
    Everything that arrives from the provisioning portal therefore passes
    through here before it is used or stored, or a Wi-Fi password of
    "mijn wifi 2024" lands in NVS as "mijn%20wifi%202024".
    =========================================================================
*/

#ifndef URI_DECODE_H_INCLUDED
#define URI_DECODE_H_INCLUDED

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

//  Decode one percent-encoded form value in place. Turns "+" into a space and
//  "%XX" into the byte it names. The decoded value is never longer than the
//  encoded one, so it always fits the buffer it came in.
//
//  Returns true on success. Returns false when the value is malformed - a "%"
//  that is not followed by two hex digits - and leaves the buffer holding a
//  partially decoded value that the caller must discard. Malformed input is a
//  client error and never an assertion: the portal answers 400 and the device
//  keeps running.
//
//  Asserts that value is not null. That is the caller's contract, and breaking
//  it is a programming error rather than something to handle at runtime.

bool
    uri_decode (char *value);

#ifdef __cplusplus
}
#endif

#endif
