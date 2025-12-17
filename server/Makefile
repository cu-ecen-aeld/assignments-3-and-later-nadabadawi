CROSS_COMPILE ?=
CFLAGS ?= -g -Wall -Werror
CC ?= $(CROSS_COMPILE)gcc
LDFLAGS ?= -lpthread -lrt
TARGET ?= aesdsocket

all: $(TARGET)
default: all

$(TARGET): aesdsocket.c
	rm -f $(TARGET)
	$(CC) aesdsocket.c -o $(TARGET)

clean:
	rm -f $(TARGET)