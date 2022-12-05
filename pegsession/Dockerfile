FROM ubuntu:20.04

RUN apt-get update \
	&& DEBIAN_FRONTEND=noninteractive apt-get install -y \
		socat \
		net-tools \
	&& rm -rf /var/lib/apt/lists/*

ARG DIR
COPY $DIR/peg_connect.sh $DIR/entrypoint.sh ./

# This Docker container is the one that creates and owns the pegsessions volume
ARG MOUNT_POINT
VOLUME $MOUNT_POINT

# Which port is exposed by this docker container
ARG PORT
ENV PORT=$PORT
EXPOSE $PORT

# Need to use a shell to expand the PORT environment variable
ENTRYPOINT [ "./entrypoint.sh" ]
