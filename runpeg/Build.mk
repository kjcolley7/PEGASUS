TARGET := runpeg
PRODUCT := $(PEG_BIN)/$(TARGET)

LIBS := $(PEG_BIN)/libpegasus_ear.so $(PEG_BIN)/libkjc_argparse.a

$(DIR)/runpeg.c: $(PEG_DIR)/kjc_argparse/kjc_argparse.h

LDLIBS := -ldl
CFLAGS := -Wall -Wextra -Wno-dangling-else -I$(PEG_DIR)
BITS := 64
ASLR := 1
RELRO := 1
NX := 1
CANARY := 1
DEBUG := 1
