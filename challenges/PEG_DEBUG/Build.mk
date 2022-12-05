TARGET := peg_debug_checker.so
PEG_DEBUG_PROG := debugme.peg
PEG_DEBUG_PROG_PATH := $(DIR)/$(PEG_DEBUG_PROG)
PEG_DEBUG_CHALLENGE_PATH := $(PEG_BIN)/$(TARGET)
PRODUCTS := $(PEG_DEBUG_CHALLENGE_PATH) $(PEG_DEBUG_PROG_PATH)
DOCKER_IMAGE := peg-debug
DOCKER_CHALLENGE_NAME := $(DOCKER_IMAGE)

LIBS := \
	$(PEG_BIN)/libpegasus_ear.so

CFLAGS := -Wall -Wextra "-I$(PEG_DIR)" \
	"-DPEG_DEBUG_PROG=\"/home/$(DOCKER_CHALLENGE_NAME)/$(PEG_DEBUG_PROG)\"" \
	"-DPEGSESSION_PORT=$(PEGSESSION_PORT)" \
	"-DPEG_SESSIONS_MOUNT_POINT=\"$(PEG_SESSIONS_MOUNT_POINT)\""
BITS := 64
ASLR := 1
RELRO := 1
NX := 1
CANARY := 1
DEBUG := 1

# The listed port is for the debugger. Connecting to the challenge's I/O requires players
# to connect to the pegsession service, which is running on port 22700.
DOCKER_PORTS := $(PEG_DEBUG_PORT)

# As we expect people to use the interactive debugger, give them a generous 10-minute time limit
DOCKER_TIMELIMIT := 600
DOCKER_CHALLENGE_PATH := $(PEG_DEBUG_CHALLENGE_PATH)
DOCKERFILE := $(DIR)/Dockerfile
DOCKER_BUILD_ARGS := \
	--build-arg "PEG_BIN=$(PEG_BIN)" \
	--build-arg "PEG_DEBUG_PROG=$(PEG_DEBUG_PROG_PATH)"
DOCKER_BUILD_DEPS := \
	$(PEG_BIN)/libpegasus_ear.so \
	$(PEG_BIN)/runpeg \
	$(PEG_DEBUG_CHALLENGE_PATH) \
	$(PEG_DEBUG_PROG_PATH)
DOCKER_RUN_ARGS := $(PEG_SESSIONS_MOUNT_ARG)
DOCKER_START_DEPS := $(PEG_SESSIONS_START_DEP)

# Don't actually publish any binaries for this challenge, make them use the debugger
# PUBLISH_TOP := $(PEG_BIN)/$(TARGET)

$(call make_loadable_exe,$(DIR))
