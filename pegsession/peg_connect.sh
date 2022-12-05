#!/bin/bash

echo "$$: Received connection from $SOCAT_PEERADDR:$SOCAT_PEERPORT at $(date -Is)" >&2

echo "Enter PEGASUS session ID:"
read -r line

if [[ ! "$line" =~ ^[0-9a-f]{16}$ ]]; then
	clean_line=$(echo "$line" | sed 's/[^ -~]//g')
	echo "$$: INVALID: $clean_line" >&2
	echo "Invalid PEGASUS session ID. Must be exactly 16 hexadecimal characters (lowercase)."
	exit 1
fi

echo "$$: VALID: $line" >&2
exec socat "unix-connect:/pegasus-sessions/peg.$line" -
