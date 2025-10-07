#!/bin/bash

# The /var/run/utmp file must be writeable. Otherwise, this error message will
# be printed when a session is closed:
# syslogin_perform_logout: logout() returned an error
# Therefore, /var/run/utmp will actually be a symlink to /dev/shm/utmp. The
# entrypoint script will copy from /var/run/utmp_orig to /dev/shm/utmp before
# starting sshd.
cp /var/run/utmp_orig /dev/shm/utmp

exec /usr/sbin/sshd -D -e
