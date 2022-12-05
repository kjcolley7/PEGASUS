# Need python 3.8 for os.memfd_create, which is available in ubuntu 20.04
FROM ubuntu:20.04

# Add unprivileged user account
WORKDIR /challenge
RUN groupadd -g 1337 pegasus \
	&& useradd -d /challenge -u 1337 -g 1337 -s /bin/false pegasus

# Install python3
RUN apt-get update \
	&& DEBIAN_FRONTEND=noninteractive apt-get install -y --no-install-recommends \
		python3 \
		python3-pip \
		socat \
	&& rm -rf /var/lib/apt/lists/*

# Copy in core PEGASUS files
ARG PEG_BIN
COPY $PEG_BIN/libpegasus_ear.so /usr/local/lib
COPY $PEG_BIN/runpeg /usr/local/bin

# Tell Python not to write .pyc files or buffer stdout/stderr
ENV PYTHONDONTWRITEBYTECODE 1
ENV PYTHONUNBUFFERED 1

# Prepare python requirements for the EAR assembler
ARG PEG_DIR
COPY $PEG_DIR/requirements.txt ./
RUN pip3 install --no-cache-dir -r requirements.txt

# Copy in EAR assembler and setup PYTHONPATH
COPY $PEG_DIR/earasm ./earasm
ENV PYTHONPATH=/challenge

# Copy in challenge files
ARG DIR
COPY $DIR/asm_grid.py \
	$DIR/grid.py \
	$DIR/serve.py \
	$DIR/block_template.ear \
	$PEG_BIN/peg_autorev_checker.so \
	./

# Silence warning about unused build variable
ARG FLAG_DST

# Add these build arguments to the environment for serve.py to use
ARG TIMELIMIT
ENV TIMELIMIT=$TIMELIMIT

# Command is to run socat as the listener, which in turn launches the
# Python serve script to do the actual challenge portion
ARG PORT
ENV PORT=$PORT
CMD exec socat -d -d tcp-listen:$PORT,reuseaddr,fork exec:./serve.py,pty,ctty,rawer
