# Host tests

Modules that do not depend on ESP-IDF are compiled and run on the development
machine, so a wrong decoder shows up in a second instead of after a flash
cycle.

```bash
make -C test check
```

A module qualifies when it includes nothing from ESP-IDF. Tests cover URL decoding,
Wi-Fi slot selection, page templates, and day-ahead pricing date/quarter selection
(including Dutch summer/winter clock changes, missing slots, and negative prices).

After a firmware configure has fetched the managed cJSON dependency, also run:

```bash
make -C test check-json
```

This checks the EnergyZero parser against the API's string-valued prices,
separate price series, and malformed/truncated entries. CI runs it after the
firmware build. Override `CJSON_DIR` if cJSON is stored elsewhere.
