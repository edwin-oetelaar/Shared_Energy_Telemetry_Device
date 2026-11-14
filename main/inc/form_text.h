/*  =========================================================================
    form_text - clean up a text value typed or pasted into the portal

    A key copied from an e-mail often brings a space or a line break along at
    either end. Those are never part of a key, so they are removed. A line
    break in the middle, or any other control character anywhere, cannot be
    part of one either; such a value is refused rather than stored, because a
    stored key with a line feed in it fails at the API much later and much
    less clearly. A plain space in the middle is left alone.

    Only for values that are known never to contain spaces at the ends - the
    API keys. A Wi-Fi password may end in a space and must not come here.
    =========================================================================
*/

#ifndef FORM_TEXT_H_INCLUDED
#define FORM_TEXT_H_INCLUDED

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

//  Remove spaces, tabs, carriage returns and line feeds from both ends of
//  value, in place. Returns false when what is left still contains a tab, a
//  line break or another control character; the buffer then holds the
//  trimmed value and the caller must discard it. Garbage from a client is a false, never an assertion.
//
//  Asserts that value is not null: that is the caller's contract.

bool
    form_text_clean (char *value);

#ifdef __cplusplus
}
#endif

#endif
