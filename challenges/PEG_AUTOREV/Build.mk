TARGET := peg_autorev_checker.so
PEG_AUTOREV_CHALLENGE_PATH := $(PEG_BIN)/$(TARGET)
DOCKER_IMAGE := peg-autorev
DOCKER_CHALLENGE_NAME := $(DOCKER_IMAGE)

LIBS := \
	$(PEG_BIN)/libpegasus_ear.so

CFLAGS := -Wall -Wextra "-I$(PEG_DIR)"
BITS := 64
ASLR := 1
RELRO := 1
NX := 1
CANARY := 1
DEBUG := 1

# Defined in top-level Build.mk
DOCKER_PORTS := $(PEG_AUTOREV_PORT)

# Each level has a 30 second time limit. The challenge has 5 levels.
# That adds up to 2.5 minutes. Set the time limit to 3 minutes to give
# some leniency for slower connections.
TIMELIMIT := 180
DOCKERFILE := $(DIR)/Dockerfile
DOCKER_IMAGE_CUSTOM := 1
DOCKER_BUILD_DEPS := \
	$(PEG_BIN)/runpeg \
	$(PEG_BIN)/libpegasus_ear.so \
	$(PEG_AUTOREV_CHALLENGE_PATH) \
	$(DIR)/asm_grid.py \
	$(DIR)/grid.py \
	$(DIR)/serve.py \
	$(DIR)/block_template.ear

# TODO: remove password before making public
# PASSWORD := robotic pegasus overlords

DOCKER_BUILD_ARGS := \
	--build-arg "PEG_BIN=$(PEG_BIN)" \
	--build-arg "DIR=$(DIR)" \
	--build-arg "PEG_DIR=$(PEG_DIR)" \
	--build-arg "PORT=$(PEG_AUTOREV_PORT)" \
	--build-arg "TIMELIMIT=$(TIMELIMIT)"

# Publish the checker module so players can try out the challenge locally
PUBLISH_TOP := $(PEG_AUTOREV_CHALLENGE_PATH)


$(call make_loadable_exe,$(DIR))
