PEG := LoginScreen.peg
PUBLISH := $(PEG)
PWN_PEG := $(DIR)/$(PEG)
PRODUCTS := $(PWN_PEG)

DOCKER_IMAGE := peg-login
DOCKER_BUILD_ARGS := \
	--build-arg DIR=$(DIR) \
	--build-arg PEG_BIN=$(PEG_BIN)

DOCKER_CHALLENGE_NAME := runpeg
DOCKER_CHALLENGE_PATH := $(PEG_BIN)/runpeg
DOCKER_CHALLENGE_ARGS := --flag-port-file /ctf/flag.txt /home/$(DOCKER_CHALLENGE_NAME)/$(PEG)
DOCKER_PORTS := 25701
DOCKER_TIMELIMIT := 30
