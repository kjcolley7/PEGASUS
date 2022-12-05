TARGET := peg_brute_checker.so
PRODUCTS := $(DIR)/brute.peg

LDLIBS := -ldl
LIBS := \
	$(PEG_BIN)/libpegasus_ear.so

CFLAGS := -Wall -Wextra -I$(PEG_DIR)
BITS := 64
ASLR := 1
RELRO := 1
NX := 1
CANARY := 1
DEBUG := 1


DOCKER_IMAGE := peg-brute
DOCKER_PORTS := 10003
DOCKER_TIMELIMIT := 5
DOCKER_CHALLENGE_NAME := $(DOCKER_IMAGE)
DOCKER_CHALLENGE_PATH := $(PEG_BIN)/$(TARGET)
DOCKERFILE := $(PEG_DIR)/Dockerfile
DOCKER_BUILD_ARGS := --build-arg "PEG_BIN=$(PEG_BIN)"
DOCKER_BUILD_DEPS := $(DIR)/submissions/.docker_dir $(PEG_BIN)/libpegasus_server.so
DOCKER_RUN_ARGS := -v $(abspath $(DIR)/submissions):/peg

PUBLISH_TOP := $(PEG_BIN)/$(TARGET)

$(call make_loadable_exe,$(DIR))
