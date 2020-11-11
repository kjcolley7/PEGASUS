TARGET := mkexeloadable
PRODUCT := $(PEG_BIN)/$(TARGET)

CFLAGS := -Wall -Wextra
BITS := 64
ASLR := 1
RELRO := 1
NX := 1
CANARY := 1
DEBUG := 1
