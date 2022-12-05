FROM ubuntu:16.04
ARG DIR

############
# SSH Bits #
############

# Most of the environment setup is taken from PwnableHarness's base Dockerfile

# Install an SSH server so players can choose to have an enhanced
# terminal for the debugger session
RUN apt-get update \
	&& DEBIAN_FRONTEND=noninteractive apt-get install -y \
		openssh-server \
	&& rm -rf /var/lib/apt/lists/*

# Which port is exposed by this docker container
ARG PORT
ENV PORT=$PORT
EXPOSE $PORT

# Overwrite the SSH server config file and add the Port config value
COPY $DIR/sshd_config /etc/ssh/sshd_config
RUN echo "ListenAddress 0.0.0.0:$PORT" >> /etc/ssh/sshd_config

# This is needed for sshd to start, even if it is read-only
RUN mkdir /var/run/sshd

# Remove the message displayed upon connecting to the SSH server
RUN rm -f /etc/legal

# When running in a read-only filesystem (as PwnableHarness does by default),
# the sshd logs contain an error message every time a connection happens:
# sh: 1: cannot create /run/motd.dynamic.new: Read-only file system
# To fix this, we disable dynamic motd functionality in PAM:
# https://askubuntu.com/a/804205
# https://stackoverflow.com/a/5410784
RUN sed -i '/pam_motd\.so/d' /etc/pam.d/login \
	&& sed -i '/pam_motd\.so/d' /etc/pam.d/sshd

# Allow login without a password
RUN sed -i 's/pam_unix\.so/pam_unix.so nullok/g' /etc/pam.d/common-password \
	&& sed -i 's/nullok_secure/nullok/g' /etc/pam.d/common-auth

# CHALLENGE_NAME is the name of both the user and executable
ARG CHALLENGE_NAME
ENV CHALLENGE_NAME=$CHALLENGE_NAME

# Create the user this challenge runs as, without a password
RUN groupadd -g 1337 $CHALLENGE_NAME \
	&& useradd -m -s /home/$CHALLENGE_NAME/$CHALLENGE_NAME -u 1337 -g 1337 $CHALLENGE_NAME \
	&& passwd -d $CHALLENGE_NAME

# Copy the executable to the new user's home directory. It
# will be owned and only writeable by root.
WORKDIR /home/$CHALLENGE_NAME
ARG CHALLENGE_PATH
COPY $CHALLENGE_PATH ./$CHALLENGE_NAME
RUN chmod 0755 $CHALLENGE_NAME

# Go to CTF workdir and add fake flag for when the volume isn't mounted
WORKDIR /ctf
ARG FLAG_DST=flag.txt
RUN \
	echo 'fakeflag{now_try_on_the_real_challenge_server}' > "$FLAG_DST" && \
	chown "root:$CHALLENGE_NAME" "$FLAG_DST" && \
	chmod 0640 "$FLAG_DST"

# The /var/run/utmp file must be writeable. Otherwise, this error message will
# be printed when a session is closed:
# syslogin_perform_logout: logout() returned an error
# Therefore, /var/run/utmp will actually be a symlink to /dev/shm/utmp. The
# entrypoint script will copy from /var/run/utmp_orig to /dev/shm/utmp before
# starting sshd.
RUN mv /var/run/utmp /var/run/utmp_orig \
	&& ln -s /dev/shm/utmp /var/run/utmp

# Copy in entrypoint script
COPY $DIR/entrypoint.sh /entrypoint.sh


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

# Launch SSHd as the main program here
ENTRYPOINT [ \
	"/entrypoint.sh" \
]
