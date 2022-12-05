PEG_DIR := $(DIR)
PEG_BIN := $(PEG_DIR)/bin
PEG_BUILD := $(BUILD)/$(PEG_DIR)
PEG_PATH := $(abspath $(PEG_BIN)):$(PATH)
PEG_SESSIONS_MOUNT_ARG := --volumes-from pegsession
PEG_SESSIONS_MOUNT_POINT := /pegasus-sessions
PEG_SESSIONS_START_DEP := docker-start[pegsession]

# Ports!
PEGSESSION_PORT := 22700
PEG_DEBUG_PORT := 22701
PEG_DEBUG_SSH_PORT := 22702
PEG_AUTOREV_PORT := 22703
PEG_CHEAT_PORT := 22704
PEG_AUTOREV2_PORT := 22705

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


#####
# make_loadable_exe($1: DIR)
#
# Fix the dynamic linking problem for executable libraries
#####
define _make_loadable_exe

BINTYPE := executable

ifdef IS_LINUX

TARGET_PATCHED := $$(TARGET)
TARGET := $$(TARGET:.so=-nold.so)

PRODUCTS := $$(PRODUCTS) $$(PEG_BIN)/$$(TARGET_PATCHED)

LDFLAGS := $$(LDFLAGS) -Wl,-E
LDLIBS := $$(LDLIBS) -ldl

$$(PEG_BIN)/%.so: $$(BUILD)/$1/%-nold.so $$(PEG_BIN)/mkexeloadable
	$$(_V)echo 'Patching $$@ to be loadable with dlopen()'
	$$(_v)cp $$< $$@.tmp && $$(PEG_BIN)/mkexeloadable $$@.tmp && mv $$@.tmp $$@

else #IS_LINUX

PRODUCT := $$(PEG_BIN)/$$(TARGET)
PRODUCTS := $$(PRODUCTS) $$(PRODUCT)

endif #IS_LINUX

endef
make_loadable_exe = $(eval $(call _make_loadable_exe,$1))
#####


PEG_CHECK_FILES := $(wildcard $(PEG_DIR)/tests/*.expected)
$(foreach f,$(PEG_CHECK_FILES),$(call checkrule,$(f:.expected=),$(patsubst $(PEG_DIR)/%.expected,$(PEG_BUILD)/%.peg,$f)))


PUBLISH := \
	bin/libpegasus_ear.so \
	bin/runpeg \
	bin/submitpeg \
	docs/EAR_EAR_v2.md \
	docs/PEGASUS.md
