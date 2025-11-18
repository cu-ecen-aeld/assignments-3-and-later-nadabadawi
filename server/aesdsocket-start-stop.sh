#!/bin/bash

# make clean
# make all
# ./aesdsocket -d

case "$1" in
    start)
        echo "Starting aesdsocket..."
        start-stop-daemon -S -n aesdsocket -d -a /usr/sbin/aesdsocket
        ;;
    stop)
        echo "Stopping aesdsocket..."
        start-stop-daemon -K -n aesdsocket
        ;;
    *)
        echo "Usage: $0 {start|stop}"
    exit 1
esac
exit 0