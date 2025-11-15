CROSS_COMPILE ?=
CFLAGS ?= -Wall -Werror
CC = $(CROSS_COMPILE)gcc

all: aesdsocket
default: all

aesdsocket: aesdsocket.c
	rm -f aesdsocket
	$(CC) aesdsocket.c -o aesdsocket

clean:
	rm -f aesdsocket