TARGETS := libkjc_argparse.a liblinenoise.a
PRODUCTS := $(addprefix $(PEG_BIN)/,$(TARGETS))

target := libkjc_argparse.a
$(target)_CFLAGS := -Wno-unused-function -DNDEBUG
$(target)_SRCS := $(LIB_DIR)/kjc_argparse/kjc_argparse.c
$(target)_OBJS := $(patsubst $(LIB_DIR)/%,$(BUILD_DIR)/$(target)_objs/%.o,$($(target)_SRCS))

target := liblinenoise.a
$(target)_SRCS := $(LIB_DIR)/linenoise/linenoise.c
$(target)_OBJS := $(patsubst $(LIB_DIR)/%,$(BUILD_DIR)/$(target)_objs/%.o,$($(target)_SRCS))

# Ensure each of the targets are built into the PEG_BIN directory
$(foreach target,$(TARGETS),$(eval $(target)_PRODUCT := $(PEG_BIN)/$(target)))

# Rule to ensure the submodule is cloned
$(LIB_DIR)/kjc_argparse/kjc_argparse.c \
$(LIB_DIR)/kjc_argparse/kjc_argparse.h \
$(LIB_DIR)/linenoise/linenoise.c \
$(LIB_DIR)/linenoise/linenoise.h:
	$(_V)echo 'Pulling git submodules...'
	$(_v)cd $(PEG_DIR) && git submodule update --init --recursive
