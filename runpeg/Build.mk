TARGET := runpeg
PRODUCT := $(PEG_BIN)/$(TARGET)

RUNPEG_DIR := $(DIR)

LIBS := \
	$(PEG_BIN)/libear.so \
	$(PEG_BIN)/libeardbg.so \
	$(PEG_BIN)/libkjc_argparse.a

SRCS := runpeg.c bootrom.c

$(RUNPEG_DIR)/runpeg.c: $(PEG_DIR)/kjc_argparse/kjc_argparse.h

$(RUNPEG_DIR)/bootrom.c: $(BOOTROM)
	$(_v)xxd -i -C -n BOOTROM $< $@

LDLIBS := -ldl

PUBLISH_TOP := $(PRODUCT)
