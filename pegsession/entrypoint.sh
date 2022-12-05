#!/bin/sh

# Non-root users can only create files and cd here
chmod 1733 /pegasus-sessions

# The "pktinfo" part provides the "SOCAT_PEERADDR" env var to the script:
# https://stackoverflow.com/a/8285112
exec /usr/bin/socat TCP4-LISTEN:$PORT,pktinfo,reuseaddr,fork EXEC:./peg_connect.sh,pty,ctty,rawer
