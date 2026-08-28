#  Host tests for the parts of the firmware that are plain C. Anything that
#  does not touch ESP-IDF can be compiled and run here in a second, which is
#  a great deal faster than flashing a board to find out that a decoder is
#  wrong. Run with: make -C test check

CC      ?= gcc
CFLAGS  ?= -std=c11 -Wall -Wextra -Werror -O1 -g -I../main

TESTS = test_uri_decode

all: check

check: $(TESTS)
	@for test in $(TESTS); do \
	    echo "== $$test"; \
	    ./$$test || exit 1; \
	done

test_uri_decode: test_uri_decode.c ../main/src/uri_decode.c
	$(CC) $(CFLAGS) -o $@ $^

clean:
	rm -f $(TESTS)

.PHONY: all check clean
