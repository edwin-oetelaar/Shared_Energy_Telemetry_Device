#  Host tests for the parts of the firmware that are plain C. Anything that
#  does not touch ESP-IDF can be compiled and run here in a second, which is
#  a great deal faster than flashing a board to find out that a decoder is
#  wrong. Run with: make -C test check

CC      ?= gcc
CFLAGS  ?= -std=c11 -Wall -Wextra -Werror -O1 -g -I../main

TESTS = test_uri_decode test_wifi_slots test_web_page test_day_ahead_model
CJSON_DIR ?= ../managed_components/espressif__cjson/cJSON

all: check

check: $(TESTS)
	@for test in $(TESTS); do \
	    echo "== $$test"; \
	    ./$$test || exit 1; \
	done

test_uri_decode: test_uri_decode.c ../main/src/uri_decode.c
	$(CC) $(CFLAGS) -o $@ $^

test_wifi_slots: test_wifi_slots.c ../main/src/wifi_slots.c
	$(CC) $(CFLAGS) -o $@ $^

test_web_page: test_web_page.c ../main/src/web_page.c
	$(CC) $(CFLAGS) -o $@ $^

test_day_ahead_model: test_day_ahead_model.c ../main/src/day_ahead_model.c
	$(CC) $(CFLAGS) -D_POSIX_C_SOURCE=200809L -o $@ $^ -lm

# Run after idf.py has fetched managed components; no system cJSON required.
check-json: test_day_ahead_json
	./test_day_ahead_json

test_day_ahead_json: test_day_ahead_json.c ../main/src/day_ahead_json.c ../main/src/day_ahead_model.c $(CJSON_DIR)/cJSON.c
	$(CC) $(CFLAGS) -D_POSIX_C_SOURCE=200809L -I$(CJSON_DIR) -o $@ $^ -lm

clean:
	rm -f $(TESTS) test_day_ahead_json

.PHONY: all check check-json clean
