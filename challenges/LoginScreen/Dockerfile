FROM c0deh4cker/pwnableharness:base-24.04-v2.1

ARG PEG_BIN
COPY $PEG_BIN/libear.so /usr/lib/libear.so
COPY $PEG_BIN/libeardbg.so /usr/lib/libeardbg.so

ARG DIR
COPY $DIR/LoginScreen.peg /home/runpeg/LoginScreen.peg
