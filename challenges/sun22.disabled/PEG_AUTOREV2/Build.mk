# Checker module is built by PEG_AUTOREV
PEG_AUTOREV2_CHALLENGE_PATH := $(PEG_BIN)/peg_autorev_checker.so

DOCKER_IMAGE := peg-autorev2
DOCKER_CHALLENGE_NAME := peg-autorev
DOCKER_CHALLENGE_PATH := $(PEG_AUTOREV2_CHALLENGE_PATH)

# Defined in top-level Build.mk
DOCKER_PORTS := $(PEG_AUTOREV2_PORT)

# Each level has a 30 second time limit. The challenge has 5 levels.
# That adds up to 2.5 minutes. Set the time limit to 3 minutes to give
# some leniency for slower connections.
TIMELIMIT := 180
DOCKERFILE := $(DIR)/Dockerfile
DOCKER_IMAGE_CUSTOM := 1
DOCKER_BUILD_DEPS := \
	$(PEG_BIN)/runpeg \
	$(PEG_BIN)/libpegasus_ear.so \
	$(DIR)/asm_grid.py \
	$(DIR)/grid.py \
	$(DIR)/serve.py \
	$(DIR)/block_template.ear

DOCKER_BUILD_ARGS := \
	--build-arg "PEG_BIN=$(PEG_BIN)" \
	--build-arg "DIR=$(DIR)" \
	--build-arg "PEG_DIR=$(PEG_DIR)" \
	--build-arg "PORT=$(PEG_AUTOREV2_PORT)" \
	--build-arg "TIMELIMIT=$(TIMELIMIT)"

# Publish the checker module so players can try out the challenge locally
PUBLISH_TOP := $(PEG_AUTOREV2_CHALLENGE_PATH)
