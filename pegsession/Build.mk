# This project defines a Docker image which will connect the client to any
# active PEGASUS I/O session as long as they know the session ID.
DOCKER_IMAGE := pegsession

# This project allows connecting to a PEGASUS session from any challenge.
# Upon receiving a connection, this service will ask for the PEGASUS
# session ID, which is a 64-bit securely random hex string. It will use
# this ID to look for a listening UNIX domain socket in a mounted volume
# at /pegasus-sessions.
DOCKER_PORTS := $(PEGSESSION_PORT)

# This Docker image does not use the base image for PwnableHarness
DOCKER_IMAGE_CUSTOM := 1

# The Dockerfile needs to copy the entrypoint.sh and peg_connect.sh scripts
# from the project directory, so tell it where that is.
DOCKER_BUILD_ARGS := \
	--build-arg "DIR=$(DIR)" \
	--build-arg "PORT=$(PEGSESSION_PORT)" \
	--build-arg "MOUNT_POINT=$(PEG_SESSIONS_MOUNT_POINT)"

# Automatically rebuild the Docker image whenever any of these files change.
DOCKER_BUILD_DEPS := \
	$(DIR)/entrypoint.sh \
	$(DIR)/peg_connect.sh
