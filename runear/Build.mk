TARGET := runear
PRODUCT := $(PEG_BIN)/$(TARGET)

LIBS := $(PEG_BIN)/libpegasus_ear.so

CFLAGS := -Wall -Wextra -Wno-dangling-else -I$(PEG_DIR)
BITS := 64
ASLR := 1
RELRO := 1
NX := 1
CANARY := 1
DEBUG := 1
