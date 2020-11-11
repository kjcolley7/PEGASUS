TARGET := libpegasus_ear.so
PRODUCT := $(PEG_BIN)/$(TARGET)

EAR_DIR := $(DIR)

SRCS := $(patsubst $(EAR_DIR)/%,%,$(wildcard $(EAR_DIR)/*.c)) linenoise/linenoise.c

$(BUILD)/$(EAR_DIR)/$(TARGET)_objs/debugger.c.o: $(EAR_DIR)/linenoise/linenoise.h

# Git submodules must be pulled before compiling this project's sources
$(EAR_DIR)/linenoise/linenoise.c $(EAR_DIR)/linenoise/linenoise.h:
	$(_V)echo 'Pulling git submodules...'
	$(_v)cd $(EAR_DIR) && git submodule update --init --recursive

CFLAGS := -Wall -Wextra -I$(PEG_DIR)
BITS := 64
ASLR := 1
RELRO := 1
NX := 1
CANARY := 1
DEBUG := 1
