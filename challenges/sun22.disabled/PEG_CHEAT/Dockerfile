FROM c0deh4cker/pwnableharness:2.0b1

ARG PEG_BIN
COPY $PEG_BIN/libpegasus_ear.so /usr/local/lib/
COPY $PEG_BIN/runpeg /usr/local/bin/

WORKDIR /home/peg-cheat
ARG DIR
COPY $DIR/SiliconBridge.peg ./
