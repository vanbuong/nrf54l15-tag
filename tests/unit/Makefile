# Host Unity tests for the Smart Tag policy core.
#
# Run from the repo root:
#
#   make -C tests/unit test
#   make -C tests/unit coverage
#   make -C tests/unit cppcheck

ROOT := $(abspath ../..)
BUILD := $(CURDIR)/build
UNITY := $(CURDIR)/unity
STUBS := $(CURDIR)/stubs

CC ?= gcc
# Force gcc: clang --coverage needs compiler-rt which is often missing.
ifeq ($(shell $(CC) --version 2>/dev/null | grep -c clang),1)
CC := gcc
endif
CFLAGS ?= -std=c11 -Wall -Wextra -Werror -g -O0
CFLAGS += --coverage
CFLAGS += -I$(STUBS) -I$(UNITY) -I$(ROOT)/src
CFLAGS += -I$(ROOT)/src/app -I$(ROOT)/src/storage
CFLAGS += -I$(ROOT)/src/motion -I$(ROOT)/src/zigbee
CFLAGS += -I$(CURDIR)
CFLAGS += -DUNITY_INCLUDE_DOUBLE=0

LDFLAGS += --coverage

POLICY_SRCS := \
	$(ROOT)/src/motion/motion_classifier.c \
	$(ROOT)/src/motion/shock_detector.c \
	$(ROOT)/src/motion/motion_manager.c \
	$(ROOT)/src/zigbee/zigbee_reporting.c \
	$(ROOT)/src/storage/flash_manager.c \
	$(ROOT)/src/storage/config_migrate.c \
	$(ROOT)/src/app/app_config.c \
	$(ROOT)/src/app/app_alarms.c

STUB_SRCS := \
	$(STUBS)/crc16.c \
	$(STUBS)/flash_mock.c \
	$(STUBS)/config_storage_stub.c \
	$(STUBS)/module_stubs.c

TEST_SRCS := \
	$(CURDIR)/test_motion_classifier.c \
	$(CURDIR)/test_shock_detector.c \
	$(CURDIR)/test_zigbee_reporting.c \
	$(CURDIR)/test_app_config.c \
	$(CURDIR)/test_flash_manager.c \
	$(CURDIR)/test_smart_tag.c \
	$(CURDIR)/test_config_migrate.c \
	$(CURDIR)/test_app_alarms.c \
	$(CURDIR)/test_motion_manager.c \
	$(CURDIR)/test_runner.c

UNITY_SRCS := $(UNITY)/unity.c

ALL_SRCS := $(POLICY_SRCS) $(STUB_SRCS) $(TEST_SRCS) $(UNITY_SRCS)
OBJS := $(addprefix $(BUILD)/,$(notdir $(ALL_SRCS:.c=.o)))

vpath %.c $(ROOT)/src/motion $(ROOT)/src/zigbee $(ROOT)/src/storage \
	$(ROOT)/src/app $(STUBS) $(CURDIR) $(UNITY)

.PHONY: all test coverage cppcheck clean

all: $(BUILD)/test_smart_tag

$(BUILD):
	mkdir -p $(BUILD)

$(BUILD)/%.o: %.c | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/test_smart_tag: $(OBJS)
	$(CC) $(LDFLAGS) $^ -o $@

test: all
	rm -f $(BUILD)/*.gcda
	$(BUILD)/test_smart_tag

coverage: test
	cd $(BUILD) && gcov -b -o . \
		motion_classifier.c shock_detector.c motion_manager.c \
		zigbee_reporting.c flash_manager.c app_config.c \
		config_migrate.c app_alarms.c >/dev/null
	python3 $(CURDIR)/check_coverage.py --min 80 \
		$(BUILD)/motion_classifier.c.gcov \
		$(BUILD)/shock_detector.c.gcov \
		$(BUILD)/motion_manager.c.gcov \
		$(BUILD)/zigbee_reporting.c.gcov \
		$(BUILD)/flash_manager.c.gcov \
		$(BUILD)/app_config.c.gcov \
		$(BUILD)/config_migrate.c.gcov \
		$(BUILD)/app_alarms.c.gcov
	@if command -v lcov >/dev/null 2>&1; then \
		lcov --capture --directory $(BUILD) --output-file $(BUILD)/coverage.info \
			--rc lcov_branch_coverage=1 --ignore-errors mismatch,gcov,source,unused; \
		lcov --extract $(BUILD)/coverage.info \
			'*/src/motion/*' '*/src/zigbee/zigbee_reporting.c' \
			'*/src/storage/flash_manager.c' '*/src/app/app_config.c' \
			'*/src/storage/config_migrate.c' '*/src/app/app_alarms.c' \
			--output-file $(BUILD)/coverage_filtered.info \
			--ignore-errors unused,mismatch; \
		genhtml $(BUILD)/coverage_filtered.info --output-directory $(ROOT)/coverage \
			--branch-coverage --ignore-errors source,unmapped; \
		echo "HTML coverage report: $(ROOT)/coverage/index.html"; \
	else \
		echo "lcov not installed; gcov text reports are in $(BUILD)/*.gcov"; \
	fi

cppcheck:
	cppcheck --error-exitcode=1 --enable=warning,style,performance \
		--suppress=missingIncludeSystem \
		--suppress=missingInclude \
		--suppress=unusedFunction \
		--inline-suppr \
		--std=c11 \
		-I$(ROOT)/src \
		$(ROOT)/src/motion \
		$(ROOT)/src/zigbee/zigbee_reporting.c \
		$(ROOT)/src/app/smart_tag.h \
		$(ROOT)/src/app/app_config.c \
		$(ROOT)/src/app/app_alarms.c \
		$(ROOT)/src/app/app_alarms.h \
		$(ROOT)/src/storage/flash_manager.c \
		$(ROOT)/src/storage/flash_manager.h \
		$(ROOT)/src/storage/config_migrate.c \
		$(ROOT)/src/storage/config_migrate.h

clean:
	rm -rf $(BUILD) $(ROOT)/coverage
