# This project defines a Docker image which will connect the client to any
# active PEGASUS I/O session as long as they know the session ID.
DOCKER_IMAGE := peg-debug-ssh

# This project allows connecting to a PEGASUS session from any challenge.
# Upon receiving a connection, this service will ask for the PEGASUS
# session ID, which is a 64-bit securely random hex string. It will use
# this ID to look for a listening UNIX domain socket in a mounted volume
# at /pegasus-sessions.
DOCKER_PORTS := $(PEG_DEBUG_SSH_PORT)

# This Docker image does not use the base image for PwnableHarness
DOCKER_IMAGE_CUSTOM := 1

# Flag comes from the parent project folder
FLAG_FILE := $(dir $(DIR))flag.txt

# The Dockerfile needs to copy the peg_connect.sh script from the project
# directory, so tell it where that is.
DOCKER_BUILD_ARGS := \
	--build-arg "CHALLENGE_NAME=peg-debug" \
	--build-arg "CHALLENGE_PATH=$(PEG_DEBUG_CHALLENGE_PATH)" \
	--build-arg "PORT=$(PEG_DEBUG_SSH_PORT)" \
	--build-arg "DIR=$(DIR)" \
	--build-arg "PEG_BIN=$(PEG_BIN)" \
	--build-arg "PEG_DEBUG_PROG=$(PEG_DEBUG_PROG_PATH)"

# If any of these files are changed, the Docker image needs to be rebuilt
DOCKER_BUILD_DEPS := \
	$(DIR)/sshd_config \
	$(DIR)/entrypoint.sh \
	$(PEG_BIN)/libpegasus_ear.so \
	$(PEG_BIN)/runpeg \
	$(PEG_DEBUG_CHALLENGE_PATH) \
	$(PEG_DEBUG_PROG_PATH)

# Bind-mount the pegasus-sessions directory into this Docker container.
# This same directory will need to be bind-mounted into the Docker container
# for each challenge that wishes to use the PEGASUS sessions feature.
DOCKER_RUN_ARGS := $(PEG_SESSIONS_MOUNT_ARG)

# Ensure that the pegsession container is started before this one
DOCKER_START_DEPS := $(PEG_SESSIONS_START_DEP)
