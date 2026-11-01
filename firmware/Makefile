# Host-side build: unit tests for the portable firmware modules and a syntax
# check of the application. The MCU build (STM32F405 + TinyUSB) lives in
# port/stm32f405/ and is generated from CubeMX; see firmware/README.md.
CC      ?= gcc
CFLAGS  ?= -std=c11 -Wall -Wextra -Werror -O1 -g -Iinclude
LDLIBS  ?= -lm
BUILD   := build

# Codec2 / FreeDV headers (git submodule). The real library is linked in the
# target build; host tests link the transparent stub in port/host/freedv_stub.c.
CODEC2_INC := third_party/codec2/src
CFLAGS     += -I$(CODEC2_INC)

# portable modules (everything in src/ except the application)
LIB_SRCS := $(filter-out src/app_main.c,$(wildcard src/*.c))
LIB_OBJS := $(LIB_SRCS:src/%.c=$(BUILD)/%.o)
LIB      := $(BUILD)/libmr.a

TEST_SRCS := $(wildcard tests/test_*.c)
TESTS     := $(TEST_SRCS:tests/%.c=$(BUILD)/%)

.PHONY: all test check-app clean
all: test check-app

$(BUILD):
	mkdir -p $(BUILD)

$(BUILD)/%.o: src/%.c | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(LIB): $(LIB_OBJS)
	ar rcs $@ $^

$(BUILD)/freedv_stub.o: port/host/freedv_stub.c | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/test_%: tests/test_%.c $(LIB) $(BUILD)/freedv_stub.o | $(BUILD)
	$(CC) $(CFLAGS) -Itests -o $@ $< $(LIB) $(BUILD)/freedv_stub.o $(LDLIBS)

$(BUILD)/vectors: tests/vectors.c $(LIB) | $(BUILD)
	$(CC) $(CFLAGS) -o $@ $< $(LIB) $(LDLIBS)

test: $(TESTS) $(BUILD)/vectors
	@fail=0; for t in $(TESTS); do ./$$t || fail=1; done; \
	if [ $$fail -eq 0 ]; then echo "ALL TESTS PASSED"; else echo "TESTS FAILED"; exit 1; fi

# Compile AND link the whole application against the host port stubs, so every
# symbol the application needs from the port layer is known to exist.
check-app: $(LIB) $(BUILD)/freedv_stub.o | $(BUILD)
	$(CC) $(CFLAGS) -c src/app_main.c -o $(BUILD)/app_main.o
	$(CC) $(CFLAGS) -c port/host/hal_host.c -o $(BUILD)/hal_host.o
	$(CC) $(CFLAGS) -c port/host/main_host.c -o $(BUILD)/main_host.o
	$(CC) $(CFLAGS) -o $(BUILD)/app_host $(BUILD)/main_host.o $(BUILD)/app_main.o $(BUILD)/hal_host.o $(LIB) $(BUILD)/freedv_stub.o $(LDLIBS)
	@echo "application links against the host port (build/app_host)"

clean:
	rm -rf $(BUILD)
