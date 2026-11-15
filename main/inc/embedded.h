/*  =========================================================================
    embedded - give a file embedded with EMBED_FILES a readable C name

    The linker names an embedded file _binary_<name>_start and _end. An asm
    label on the declaration gives those symbols a name that reads well in C:

        extern const char page_start [] EMBEDDED_AS ("_binary_page_html_start");

    cppcheck 2.19 cannot parse an asm label on a declaration and stops with a
    syntax error, whatever the spelling. cppcheck defines __CPPCHECK__, so it
    sees the declaration without the label; the compiler always gets it.
    =========================================================================
*/

#ifndef EMBEDDED_H_INCLUDED
#define EMBEDDED_H_INCLUDED

#ifdef __CPPCHECK__
#   define EMBEDDED_AS(symbol)
#else
#   define EMBEDDED_AS(symbol) __asm__ (symbol)
#endif

#endif
