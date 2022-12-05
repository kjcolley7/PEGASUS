TARGET := libpegasus_server.so
PRODUCT := $(PEG_BIN)/$(TARGET)

LIBS := $(PEG_BIN)/libpegasus_ear.so

CFLAGS := -Wall -Wextra -I$(PEG_DIR)

ifeq "$(CC)" "gcc"
CFLAGS := $(CFLAGS) -Wno-format-truncation
endif

BITS := 64
ASLR := 1
RELRO := 1
NX := 1
CANARY := 1
DEBUG := 1
