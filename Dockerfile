FROM c0deh4cker/pwnableharness

ARG PEG_BIN
COPY $PEG_BIN/libear.so ./
COPY $PEG_BIN/libpegasus_server.so ./
