PEG := gimli-hash.peg
PRODUCT := $(DIR)/$(PEG)
GIMLI_PEG := $(PRODUCT)
GIMLI_BUILD := $(BUILD_DIR)

# Just pick a bunch of random files I guess
GIMLI_TEST_FILES := $(wildcard $(PEG_DIR)/asm/*.ear)
GIMLI_TEST_TARGETS := $(patsubst $(PEG_DIR)/asm/%,check-gimli[%],$(GIMLI_TEST_FILES))

check: check-gimli

.PHONY: check-gimli
check-gimli: $(GIMLI_TEST_TARGETS) check-gimli-null

.PHONY: check-gimli-null
check-gimli-null: $(GIMLI_BUILD)/null.hash $(PRODUCT)
	$(_v)$(PEG_BIN)/runpeg --timeout=1 $(GIMLI_PEG) < /dev/null | diff $(GIMLI_BUILD)/null.hash - && echo "PASS gimli(null)" || echo "FAIL gimli(null)"

check-gimli[%]: $(PEG_DIR)/asm/% $(GIMLI_BUILD)/%.hash $(PRODUCT)
	$(_v)$(PEG_BIN)/runpeg --timeout=1 $(GIMLI_PEG) < $< | diff $(GIMLI_BUILD)/$(*F).hash - && echo "PASS gimli($*)" || echo "FAIL gimli($*)"

$(GIMLI_BUILD)/%.hash: $(PEG_DIR)/asm/% $(PEG_BIN)/gimli | $(GIMLI_BUILD)/.dir
	$(_v)$(PEG_BIN)/gimli < $< > $@

$(GIMLI_BUILD)/null.hash: $(PEG_BIN)/gimli | $(GIMLI_BUILD)/.dir
	$(_v)$(PEG_BIN)/gimli < /dev/null > $@
