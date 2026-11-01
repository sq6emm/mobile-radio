# Host-side build: unit tests for the portable firmware modules and a syntax
# check of the application. The MCU build (STM32F405 + TinyUSB) lives in
# port/stm32f405/ and is generated from CubeMX; see firmware/README.md.
CC      ?= gcc
CFLAGS  ?= -std=c11 -Wall -Wextra -Werror -O1 -g -Iinclude
BUILD   := build

MODULES := mrp pmr446 sa818 pd_policy ptt_guard
TESTS   := $(MODULES:%=$(BUILD)/test_%)

.PHONY: all test check-app clean
all: test check-app

$(BUILD):
	mkdir -p $(BUILD)

$(BUILD)/test_mrp: tests/test_mrp.c src/mrp.c | $(BUILD)
	$(CC) $(CFLAGS) -Itests -o $@ $^
$(BUILD)/test_pmr446: tests/test_pmr446.c src/pmr446.c | $(BUILD)
	$(CC) $(CFLAGS) -Itests -o $@ $^
$(BUILD)/test_sa818: tests/test_sa818.c src/sa818.c src/pmr446.c | $(BUILD)
	$(CC) $(CFLAGS) -Itests -o $@ $^
$(BUILD)/test_pd_policy: tests/test_pd_policy.c src/pd_policy.c | $(BUILD)
	$(CC) $(CFLAGS) -Itests -o $@ $^
$(BUILD)/test_ptt_guard: tests/test_ptt_guard.c src/ptt_guard.c | $(BUILD)
	$(CC) $(CFLAGS) -Itests -o $@ $^

test: $(TESTS)
	@fail=0; for t in $(TESTS); do ./$$t || fail=1; done; \
	if [ $$fail -eq 0 ]; then echo "ALL TESTS PASSED"; else echo "TESTS FAILED"; exit 1; fi

# Compile the application against the port API without a target toolchain.
check-app: | $(BUILD)
	$(CC) $(CFLAGS) -fsyntax-only src/app_main.c
	$(CC) $(CFLAGS) -c port/host/hal_host.c -o $(BUILD)/hal_host.o
	@echo "app_main.c compiles against hal.h"

clean:
	rm -rf $(BUILD)
