PEG_DIR := $(DIR)
PEG_BUILD := $(BUILD_DIR)
PEG_BIN := $(PEG_DIR)/bin
LIB_DIR := $(PEG_DIR)/libraries
BOOTROM_DIR := $(PEG_DIR)/bootrom
BOOTROM := $(PEG_BIN)/boot.peg

ifdef IS_MAC
CONFIG_USE_PWNCC :=
CONFIG_NO_EXTRA_FLAGS := 1

DEFAULT_CC := clang
DEFAULT_LD := clang
CC := clang
LD := clang

PEG_BIN := $(PEG_BIN)/mac
endif #IS_MAC

PEG_SAVED_DEFAULT_BITS   := $(DEFAULT_BITS)
PEG_SAVED_DEFAULT_ASLR   := $(DEFAULT_ASLR)
PEG_SAVED_DEFAULT_RELRO  := $(DEFAULT_RELRO)
PEG_SAVED_DEFAULT_NX     := $(DEFAULT_NX)
PEG_SAVED_DEFAULT_CANARY := $(DEFAULT_CANARY)
PEG_SAVED_DEFAULT_STRIP  := $(DEFAULT_STRIP)
PEG_SAVED_DEFAULT_DEBUG  := $(DEFAULT_DEBUG)
PEG_SAVED_DEFAULT_CFLAGS := $(DEFAULT_CFLAGS)

DEFAULT_BITS := 64
DEFAULT_ASLR := 1
DEFAULT_RELRO := 1
DEFAULT_NX := 1
DEFAULT_CANARY := 1
DEFAULT_STRIP := 0
DEFAULT_DEBUG := 1
DEFAULT_CFLAGS := \
	-Wall \
	-Wextra \
	-Werror \
	-Wno-dangling-else \
	-I$(PEG_DIR) \
	-I$(LIB_DIR)


PEG_PATH := $(abspath $(PEG_BIN)):$(PATH)
PEG_SESSIONS_MOUNT_ARG := --volumes-from pegsession
PEG_SESSIONS_MOUNT_POINT := /pegasus-sessions
PEG_SESSIONS_START_DEP := docker-start[pegsession]

# Ports!
PEGSESSION_PORT := 25700
# PEG_DEBUG_PORT := 22701
# PEG_DEBUG_SSH_PORT := 22702
# PEG_AUTOREV_PORT := 22703
# PEG_CHEAT_PORT := 22704
# PEG_AUTOREV2_PORT := 22705

ifdef PYTHONPATH
PEG_PYTHONPATH := $(PYTHONPATH):$(abspath $(PEG_DIR))
else
PEG_PYTHONPATH := $(abspath $(PEG_DIR))
endif


EARASM := PYTHONPATH="$(PEG_PYTHONPATH)" python3 -m earasm
EARASMFLAGS := \
	-I $(PEG_DIR) \
	-I $(PEG_DIR)/asm \
	-I $(PEG_BUILD)

EAR_EXTRA := $(PEG_DIR)/asm/start.ear

.PHONY: check

# Rule for assembling EAR assembly code into PEGASUS binaries (in-place)
$(PEG_DIR)/%.peg: $(PEG_DIR)/%.ear
	$(_V)echo 'Assembling $*.ear'
	$(_v)$(EARASM) $(EARASMFLAGS) -o $@ $< $(EAR_EXTRA)

# Rule for assembling EAR assembly code into PEGASUS binaries (separate build dir)
$(PEG_BUILD)/%.peg: $(PEG_DIR)/%.ear
	$(_V)echo 'Assembling $*.ear'
	$(_v)mkdir -p $(@D)
	$(_v)$(EARASM) $(EARASMFLAGS) -o $@ $< $(EAR_EXTRA)


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
