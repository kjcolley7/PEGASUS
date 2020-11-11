PEG_DIR := $(DIR)
PEG_BIN := $(PEG_DIR)/bin
PEG_BUILD := $(BUILD)/$(PEG_DIR)
PEG_PATH := $(abspath $(PEG_BIN)):$(PATH)

ifdef PYTHONPATH
PEG_PYTHONPATH := $(PYTHONPATH):$(abspath $(PEG_DIR))
else
PEG_PYTHONPATH := $(abspath $(PEG_DIR))
endif

TARGET := libkjc_argparse.a
PRODUCT := $(PEG_BIN)/$(TARGET)

SRCS := kjc_argparse/kjc_argparse.c

CFLAGS := -Wall -Wextra -Wno-dangling-else -I$(PEG_DIR)
BITS := 64
ASLR := 1
RELRO := 1
NX := 1
CANARY := 1
DEBUG := 1

# Rule to ensure the submodule is cloned
$(PEG_DIR)/kjc_argparse/kjc_argparse.c $(PEG_DIR)/kjc_argparse/kjc_argparse.h:
	$(_V)echo 'Pulling git submodules...'
	$(_v)cd $(PEG_DIR) && git submodule update --init --recursive


EARASM := PYTHONPATH="$(PEG_PYTHONPATH)" python3 -m earasm

.PHONY: check check-python check-ear

check: check-python check-ear

check-python:
	$(_v)pytest $(PEG_DIR)

# Rule for assembling EAR assembly code into PEGASUS binaries
$(PEG_DIR)/%.peg: $(PEG_DIR)/%.ear
	$(_V)echo 'Assembling $<'
	$(_v)$(EARASM) $< $@

#####
# checkrule($1: path to input peg/tests/<test>.ear w/o extension, $2: path to the output .peg file)
#####
define _checkrule

$2: $1.ear
	@echo 'Assembling $$<'
	@mkdir -p $$(@D)
	$$(_v)$$(EARASM) $$< $$@

check-ear:: $2 $$(PEG_BIN)/runpeg
	$$(_v)$$(PEG_BIN)/runpeg $2 > $1.actual && diff -w $1.expected $1.actual && echo "PASS $$(basename $1)" || echo "FAIL $$(basename $1)"

clean::
	$(_v)rm -f $2

endef #_checkrule
checkrule = $(eval $(call _checkrule,$1,$2))
#####


PEG_CHECK_FILES := $(wildcard $(PEG_DIR)/tests/*.expected)
$(foreach f,$(PEG_CHECK_FILES),$(call checkrule,$(f:.expected=),$(patsubst $(PEG_DIR)/%.expected,$(PEG_BUILD)/%.peg,$f)))


PUBLISH := \
	bin/libpegasus_ear.so \
	bin/runpeg \
	bin/submitpeg \
	docs/EAR_EAR.md \
	docs/PEGASUS.md
