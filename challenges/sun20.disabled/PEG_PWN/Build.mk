TARGET := peg_pwn_checker.so
PRODUCTS := $(DIR)/bof.peg

LIBS := \
	$(PEG_BIN)/libpegasus_ear.so

CFLAGS := -Wall -Wextra -I$(PEG_DIR)
BITS := 64
ASLR := 1
RELRO := 1
NX := 1
CANARY := 1
DEBUG := 1


DOCKER_IMAGE := peg-pwn
DOCKER_PORTS := 10004
DOCKER_TIMELIMIT := 30
DOCKER_CHALLENGE_NAME := $(DOCKER_IMAGE)
DOCKER_CHALLENGE_PATH := $(PEG_BIN)/$(TARGET)
DOCKERFILE := $(DIR)/Dockerfile
DOCKER_BUILD_ARGS := \
	--build-arg "PEG_BIN=$(PEG_BIN)" \
	--build-arg "DIR=$(DIR)"
DOCKER_BUILD_DEPS := $(PEG_BIN)/runpeg $(DIR)/bof.peg
DOCKER_RUN_ARGS := -v $(abspath $(DIR)/submissions):/peg

PUBLISH_TOP := $(PEG_BIN)/$(TARGET)
PUBLISH := bof.peg

$(call make_loadable_exe,$(DIR))
