# CROSS_COMPILE ?=
CFLAGS ?= -g -Wall -Werror -pthread
# CC ?= $(CROSS_COMPILE)gcc
CC ?= gcc
LDFLAGS ?= 
TARGET ?= aesdsocket

all: $(TARGET)
default: all

$(TARGET): aesdsocket.c
	rm -f $(TARGET)
	$(CC) $(CFLAGS) aesdsocket.c $(LDFLAGS) -o $(TARGET)

clean:
	rm -f $(TARGET)