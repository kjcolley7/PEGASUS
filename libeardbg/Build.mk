TARGET := libeardbg.so
PRODUCT := $(PEG_BIN)/$(TARGET)

DBG_DIR := $(DIR)
DBG_BUILD := $(BUILD_DIR)

$(DBG_BUILD)/$(TARGET)_objs/debugger.c.o: $(LIB_DIR)/linenoise/linenoise.h

LIBS := \
	$(PEG_BIN)/libear.so \
	$(PEG_BIN)/liblinenoise.a

PUBLISH_TOP := $(PRODUCT)
