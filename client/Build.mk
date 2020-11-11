TARGET := submitpeg
PRODUCT := $(PEG_BIN)/$(TARGET)

LIBS := $(PEG_BIN)/libkjc_argparse.a

CFLAGS := -Wall -Wextra -I$(PEG_DIR)
BITS := 64
ASLR := 1
RELRO := 1
NX := 1
CANARY := 1
DEBUG := 1
