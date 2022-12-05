#!/bin/bash

SCRIPT_DIR=$( cd -- "$( dirname -- "${BASH_SOURCE[0]}" )" &> /dev/null && pwd )

apt-get update
apt-get install -y python3-pip
pip install -r "$SCRIPT_DIR/requirements.txt"
