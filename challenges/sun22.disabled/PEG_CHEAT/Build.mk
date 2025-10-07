PEG := $(DIR)/SiliconBridge.peg
TARGET := peg_cheat_checker.so
PRODUCTS := $(PEG)

LIBS := \
	$(PEG_BIN)/libpegasus_ear.so

CFLAGS := -Wall -Wextra -I$(PEG_DIR)
BITS := 64
ASLR := 1
RELRO := 1
NX := 1
CANARY := 1
DEBUG := 1


# Defined in top-level Build.mk
DOCKER_PORTS := $(PEG_CHEAT_PORT)

DOCKER_IMAGE := peg-cheat
DOCKER_TIMELIMIT := 30
DOCKER_CHALLENGE_NAME := $(DOCKER_IMAGE)
DOCKER_CHALLENGE_PATH := $(PEG_BIN)/$(TARGET)
DOCKER_BUILD_ARGS := \
	--build-arg "PEG_BIN=$(PEG_BIN)" \
	--build-arg "DIR=$(DIR)"
DOCKER_BUILD_DEPS := \
	$(PEG_BIN)/libpegasus_ear.so \
	$(PEG_BIN)/runpeg \
	$(PEG)

PUBLISH_TOP := $(PEG_BIN)/$(TARGET) $(PEG)

$(call make_loadable_exe,$(DIR))
