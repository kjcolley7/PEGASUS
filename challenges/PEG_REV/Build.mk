TARGET := peg_rev_checker-nold.so
BINTYPE := executable

TARGET_PATCHED := $(TARGET:-nold.so=.so)

LIBS := \
	$(PEG_BIN)/libpegasus_ear.so

LDFLAGS := -Wl,-E
LDLIBS := -ldl
CFLAGS := -Wall -Wextra -I$(PEG_DIR)
BITS := 64
ASLR := 1
RELRO := 1
NX := 1
CANARY := 1
DEBUG := 1


DOCKER_IMAGE := peg-rev
DOCKER_PORTS := 10001
DOCKER_TIMELIMIT := 30
DOCKER_CHALLENGE_NAME := $(DOCKER_IMAGE)
DOCKER_CHALLENGE_PATH := $(BUILD_DIR)/$(TARGET_PATCHED)
DOCKERFILE := $(DIR)/Dockerfile
DOCKER_BUILD_ARGS := \
	--build-arg "PEG_BIN=$(PEG_BIN)" \
	--build-arg "DIR=$(DIR)"
DOCKER_BUILD_DEPS := $(DIR)/submissions/.docker_dir $(PEG_BIN)/runpeg $(DIR)/LicenseChecker.peg
DOCKER_RUN_ARGS := -v $(abspath $(DIR)/submissions):/peg

PUBLISH_BUILD := $(TARGET_PATCHED)
PUBLISH := LicenseChecker.peg
