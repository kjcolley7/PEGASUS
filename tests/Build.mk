# Assemble all .ear files by default
TEST_DIR := $(DIR)
TEST_BUILD := $(BUILD_DIR)
TEST_EAR_SRCS := $(wildcard $(TEST_DIR)/*.ear)
TEST_PEG_FILES := $(patsubst $(TEST_DIR)/%.ear,$(TEST_BUILD)/%.peg,$(TEST_EAR_SRCS))
TEST_CHECK_TARGETS := $(patsubst $(TEST_DIR)/%.ear,check-ear[%],$(TEST_EAR_SRCS))
PRODUCTS := $(TEST_PEG_FILES)


.PHONY: check check-python check-ear

check: check-python check-ear

check-python:
	$(_v)pytest --quiet $(PEG_DIR)

check-ear: $(TEST_CHECK_TARGETS)

check-ear[%]: $(TEST_BUILD)/%.peg $(TEST_DIR)/test_flag.txt | $(PEG_BIN)/runpeg
	$(_v)$(PEG_BIN)/runpeg --timeout=5 --flag-port-file=$(TEST_DIR)/test_flag.txt $< >/dev/null 2>&1 && echo "PASS $*" || echo "FAIL $* : $$?"
