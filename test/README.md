# Host tests

Modules that do not depend on ESP-IDF are compiled and run on the development
machine, so a wrong decoder shows up in a second instead of after a flash
cycle.

```bash
make -C test check
```

A module qualifies when it includes nothing from ESP-IDF. `uri_decode` is the
first one; keep new pure-C logic in that shape where it is practical, and add
it here.
