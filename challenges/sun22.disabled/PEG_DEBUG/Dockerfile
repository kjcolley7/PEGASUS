FROM c0deh4cker/pwnableharness:2.0b1

##################
# Challenge Bits #
##################

# This challenge needs the core PEGASUS library and the runpeg binary
ARG PEG_BIN
COPY $PEG_BIN/libpegasus_ear.so /usr/local/lib/
COPY $PEG_BIN/runpeg /usr/local/bin/

# It also needs the PEGASUS program to run
ARG PEG_DEBUG_PROG
COPY $PEG_DEBUG_PROG /home/$CHALLENGE_NAME/
