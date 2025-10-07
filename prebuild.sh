#!/bin/bash

apt-get update
apt-get install -y --no-install-recommends \
	python3-ply \
	python3-pwntools \
	python3-pytest
