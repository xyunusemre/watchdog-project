CC       ?= gcc
CPPFLAGS := -D_POSIX_C_SOURCE=200809L -Iinclude
CFLAGS   ?= -std=c11 -Wall -Wextra -Wpedantic -O2

BUILD_DIR := build
SRCS      := src/main.c src/signal_manager.c src/process_manager.c src/logger.c
OBJS      := $(SRCS:src/%.c=$(BUILD_DIR)/%.o)
DEPS      := $(OBJS:.o=.d)

.PHONY: all clean test

all: watchdog worker

watchdog: $(OBJS)
	$(CC) $(CFLAGS) -o $@ $^

worker: workers/worker.c
	$(CC) $(CPPFLAGS) $(CFLAGS) -o $@ $<

$(BUILD_DIR)/%.o: src/%.c | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -MMD -MP -c -o $@ $<

$(BUILD_DIR):
	mkdir -p $@

test: all
	bash tests/test_scenarios.sh

clean:
	rm -rf $(BUILD_DIR) watchdog worker watchdog.log tests/output

-include $(DEPS)
